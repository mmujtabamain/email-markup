from __future__ import annotations

import argparse
import hashlib
import os
import re
import shutil
import subprocess
import zipfile
from pathlib import Path


REPOSITORY_ROOT = Path(__file__).resolve().parents[1]


def release_version() -> str:
    version = (REPOSITORY_ROOT / "VERSION").read_text(encoding="utf-8").strip()
    if re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", version) is None:
        raise RuntimeError("VERSION must contain a semantic version in X.Y.Z form")
    return version


def npm_executable(platform: str = os.name) -> str:
    return "npm.cmd" if platform == "nt" else "npm"


def checksum(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def write_checksum(path: Path) -> None:
    expected = checksum(path)
    checksum_path = path.with_name(f"{path.name}.sha256")
    checksum_path.write_text(f"{expected}  {path.name}\n", encoding="utf-8")
    if checksum(path) != expected:
        raise RuntimeError(f"checksum verification failed for {path}")


def archive_tree(source: Path, destination: Path) -> None:
    with zipfile.ZipFile(destination, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        for path in sorted(source.rglob("*")):
            if path.is_file():
                archive.write(path, path.relative_to(source).as_posix())


def main() -> int:
    parser = argparse.ArgumentParser(description="Package one Email Markup release runner.")
    parser.add_argument("--install", type=Path, required=True)
    parser.add_argument("--platform", required=True)
    parser.add_argument("--extension", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    arguments = parser.parse_args()
    version = release_version()

    install = arguments.install.resolve()
    extension = arguments.extension.resolve()
    output = arguments.output.resolve()
    executable_suffix = ".exe" if arguments.platform.startswith("win32-") else ""
    emc = install / "bin" / f"emc{executable_suffix}"
    lsp = install / "bin" / f"email-markup-lsp{executable_suffix}"
    reported = subprocess.run(
        [str(emc), "--version"], check=True, capture_output=True, text=True
    ).stdout.strip()
    if reported != f"emc {version}":
        raise RuntimeError(f"expected emc {version}, got {reported!r}")

    output.mkdir(parents=True, exist_ok=True)
    runtime_archive = output / f"email-markup-{arguments.platform}.zip"
    archive_tree(install, runtime_archive)
    write_checksum(runtime_archive)

    server = extension / "server" / arguments.platform
    if server.exists():
        shutil.rmtree(server)
    server.mkdir(parents=True)
    shutil.copy2(lsp, server / lsp.name)
    shutil.copytree(install / "share" / "email-markup" / "lib", server / "lib")
    subprocess.run([npm_executable(), "run", "package"], cwd=extension, check=True)
    built = next(extension.glob(f"email-markup-language-{version}.vsix"))
    vsix = output / f"email-markup-language-{arguments.platform}.vsix"
    shutil.move(built, vsix)
    write_checksum(vsix)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
