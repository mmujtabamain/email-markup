from __future__ import annotations

import argparse
import json
import resource
import statistics
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from typing import BinaryIO, Sequence


def percentile(samples: Sequence[float], quantile: float) -> float:
    ordered = sorted(samples)
    index = min(len(ordered) - 1, max(0, int((len(ordered) - 1) * quantile + 0.5)))
    return ordered[index]


def send_message(stream: BinaryIO, payload: dict[str, object]) -> None:
    body = json.dumps(payload, separators=(",", ":")).encode()
    stream.write(f"Content-Length: {len(body)}\r\n\r\n".encode() + body)
    stream.flush()


def read_message(stream: BinaryIO) -> dict[str, object]:
    length = 0
    while True:
        line = stream.readline()
        if not line:
            raise RuntimeError("ell-lsp closed its output unexpectedly")
        if line in (b"\r\n", b"\n"):
            break
        key, value = line.decode().split(":", 1)
        if key.lower() == "content-length":
            length = int(value.strip())
    return json.loads(stream.read(length))


def wait_for(stream: BinaryIO, method: str | None = None, identifier: int | None = None) -> dict[str, object]:
    while True:
        message = read_message(stream)
        if method is not None and message.get("method") == method:
            return message
        if identifier is not None and message.get("id") == identifier:
            return message


def command_latency(binary: Path, source: Path, iterations: int) -> list[float]:
    samples: list[float] = []
    with tempfile.TemporaryDirectory(prefix="ell-benchmark-") as directory:
        output = Path(directory) / "result.html"
        for _ in range(iterations):
            started = time.perf_counter()
            subprocess.run(
                [str(binary), "compile", str(source), "-o", str(output)],
                cwd=source.parent,
                check=True,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )
            samples.append((time.perf_counter() - started) * 1000)
    return samples


def build_latency(binary: Path, root: Path, iterations: int) -> list[float]:
    samples: list[float] = []
    with tempfile.TemporaryDirectory(prefix="ell-build-benchmark-") as directory:
        project = Path(directory)
        (project / "message.ell").write_text(
            (root / "examples/solution_first.ell").read_text(encoding="utf-8"), encoding="utf-8"
        )
        (project / "data.json").write_text(
            (root / "examples/solution_first.json").read_text(encoding="utf-8"), encoding="utf-8"
        )
        (project / "ell.json").write_text(json.dumps({
            "include": [str(root / "lib"), str(root / "brand/example")],
            "imports": [
                str(root / "lib/builtins.ell"),
                str(root / "brand/example/brand.ell"),
                str(root / "brand/example/styles.ell"),
            ],
            "data": "data.json",
            "shell": str(root / "brand/example/shell.ell"),
            "out": "out",
        }), encoding="utf-8")
        for _ in range(iterations):
            started = time.perf_counter()
            subprocess.run(
                [str(binary), "build", str(project)],
                check=True,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )
            samples.append((time.perf_counter() - started) * 1000)
    return samples


def diagnostic_latency(server: Path, root: Path, source: Path, iterations: int) -> list[float]:
    process = subprocess.Popen(
        [str(server)], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL
    )
    if process.stdin is None or process.stdout is None:
        raise RuntimeError("ell-lsp pipes are unavailable")
    send_message(process.stdin, {
        "jsonrpc": "2.0", "id": 1, "method": "initialize",
        "params": {"rootUri": root.as_uri(), "capabilities": {}},
    })
    wait_for(process.stdout, identifier=1)
    send_message(process.stdin, {"jsonrpc": "2.0", "method": "initialized", "params": {}})
    uri = source.as_uri()
    original = source.read_text(encoding="utf-8")
    send_message(process.stdin, {
        "jsonrpc": "2.0", "method": "textDocument/didOpen",
        "params": {"textDocument": {"uri": uri, "languageId": "ell", "version": 1, "text": original}},
    })
    wait_for(process.stdout, method="textDocument/publishDiagnostics")
    samples: list[float] = []
    for index in range(iterations):
        version = index + 2
        text = original + (" " if index % 2 == 0 else "  ")
        started = time.perf_counter()
        send_message(process.stdin, {
            "jsonrpc": "2.0", "method": "textDocument/didChange",
            "params": {"textDocument": {"uri": uri, "version": version}, "contentChanges": [{"text": text}]},
        })
        while True:
            message = wait_for(process.stdout, method="textDocument/publishDiagnostics")
            params = message.get("params")
            if isinstance(params, dict) and params.get("version") == version:
                break
        samples.append((time.perf_counter() - started) * 1000)
    send_message(process.stdin, {"jsonrpc": "2.0", "id": 2, "method": "shutdown", "params": None})
    wait_for(process.stdout, identifier=2)
    send_message(process.stdin, {"jsonrpc": "2.0", "method": "exit", "params": None})
    process.wait(timeout=5)
    return samples


def peak_mebibytes() -> float:
    value = resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss
    return value / (1024 * 1024) if sys.platform == "darwin" else value / 1024


def summary(samples: Sequence[float]) -> dict[str, float]:
    return {
        "p50_ms": round(statistics.median(samples), 3),
        "p95_ms": round(percentile(samples, 0.95), 3),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="Measure ELL CLI and persistent-LSP latency.")
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--build", type=Path)
    parser.add_argument("--iterations", type=int, default=30)
    arguments = parser.parse_args()
    root = arguments.root.resolve()
    build = (arguments.build or root / "build/release/bin").resolve()
    if arguments.iterations < 5:
        parser.error("--iterations must be at least 5")
    source = root / "examples/component_gallery/gallery.ell"
    cli = command_latency(build / "ellc", source, arguments.iterations)
    project_build = build_latency(build / "ellc", root, arguments.iterations)
    benchmark_root = root / "build"
    benchmark_root.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="ell-maximum-", dir=benchmark_root) as directory:
        maximum_source = Path(directory) / "maximum.ell"
        maximum_source.write_text("x" * 900_000, encoding="utf-8")
        maximum_cli = command_latency(build / "ellc", maximum_source, max(5, arguments.iterations // 3))
    diagnostics = diagnostic_latency(build / "ell-lsp", root, source, arguments.iterations)
    print(json.dumps({
        "iterations": arguments.iterations,
        "ellc_compile": summary(cli),
        "ellc_build": summary(project_build),
        "ellc_compile_900kb": summary(maximum_cli),
        "edit_to_diagnostics": summary(diagnostics),
        "peak_child_rss_mib": round(peak_mebibytes(), 2),
        "source_bytes": source.stat().st_size,
    }, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
