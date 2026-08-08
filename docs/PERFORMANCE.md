# Performance budgets

Email Markup measures the released CLI and persistent language server before considering
parser or allocation changes. Run the benchmark after a release build:

```bash
cmake --preset release
cmake --build --preset release
python3 tools/benchmark.py --iterations 30
```

## Email Markup 1 budgets

| Workload | Budget |
| --- | ---: |
| Representative `emc compile`, p95 | 100 ms |
| Representative `emc compile --request-stdin`, p95 | 100 ms |
| Representative `emc build`, p95 | 100 ms |
| Persistent edit-to-diagnostics, p95 | 50 ms |
| 900 KB near-limit `emc compile`, p95 | 1,500 ms |
| Peak child RSS across the benchmark | 64 MiB |

The representative input is `examples/09-css-inlining/message.em` with the
standard library and JSON fixture. The maximum workload generates a 900,000-byte
source beneath the project root, below the 1 MiB source limit. CLI timing includes
process launch and atomic output. LSP timing keeps one server alive and measures
a full-document change through the matching versioned diagnostic notification.

## 2026-08-08 Email Markup 1.1 baseline

Measured on macOS arm64 with Apple Clang 21.0.0, the CMake release preset, and 30
representative iterations:

| Workload | p50 | p95 |
| --- | ---: | ---: |
| Representative file `emc compile` (589-byte source) | 8.538 ms | 10.410 ms |
| Representative in-memory request compile | 8.857 ms | 9.653 ms |
| Representative one-file `emc build` | 8.612 ms | 10.041 ms |
| Persistent edit-to-diagnostics | 3.231 ms | 3.771 ms |
| 900 KB near-limit compile (10 iterations) | 1,402.572 ms | 1,437.482 ms |

Observed peak child RSS was 13.59 MiB. The in-memory request result is recorded
alongside the file path rather than treated as evidence that either transport is
inherently faster; on this run their distributions overlap. All measurements
passed the Email Markup 1 budgets.

## 2026-08-06 baseline

Measured on macOS arm64 with Apple Clang 21.0.0, the CMake release preset, and 30
representative iterations:

| Workload | p50 | p95 |
| --- | ---: | ---: |
| Representative `emc compile` (1,135-byte source) | 9.545 ms | 17.879 ms |
| Representative one-file `emc build` | 9.553 ms | 11.670 ms |
| Persistent edit-to-diagnostics | 6.379 ms | 6.588 ms |
| 900 KB near-limit compile (10 iterations) | 849.748 ms | 858.746 ms |

Observed peak child RSS was 12.12 MiB. All measurements passed their Email Markup 1
budgets. These figures are a reproducible local baseline, not a claim that every
platform has identical timings; release CI supplies platform build and test
coverage.

The data does not justify arena replacement or identifier interning for Email Markup 1.
Snapshot ownership and address stability therefore remain simple and explicit.
There is likewise no output minifier: clipping is measured from final bytes and
linted, while a minifier would need independent email-client evidence.
