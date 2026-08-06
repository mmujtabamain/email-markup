# Performance budgets

ELL measures the released CLI and persistent language server before considering
parser or allocation changes. Run the benchmark after a release build:

```bash
cmake --preset release
cmake --build --preset release
python3 tools/benchmark.py --iterations 30
```

## ELL 1 budgets

| Workload | Budget |
| --- | ---: |
| Representative `ellc compile`, p95 | 100 ms |
| Persistent edit-to-diagnostics, p95 | 50 ms |
| 900 KB near-limit `ellc compile`, p95 | 1,500 ms |
| Peak child RSS across the benchmark | 64 MiB |

The representative input is `examples/component_gallery/gallery.ell` with the
standard library and JSON fixture. The maximum workload generates a 900,000-byte
source beneath the project root, below the 1 MiB source limit. CLI timing includes
process launch and atomic output. LSP timing keeps one server alive and measures
a full-document change through the matching versioned diagnostic notification.

## 2026-08-06 baseline

Measured on macOS arm64 with Apple Clang 21.0.0, the CMake release preset, and 30
representative iterations:

| Workload | p50 | p95 |
| --- | ---: | ---: |
| Representative `ellc compile` (1,135-byte source) | 9.836 ms | 14.021 ms |
| Persistent edit-to-diagnostics | 6.793 ms | 7.029 ms |
| 900 KB near-limit compile (10 iterations) | 873.706 ms | 1,065.453 ms |

Observed peak child RSS was 13.2 MiB. All measurements passed their ELL 1
budgets. These figures are a reproducible local baseline, not a claim that every
platform has identical timings; release CI supplies platform build and test
coverage.

The data does not justify arena replacement or identifier interning for ELL 1.
Snapshot ownership and address stability therefore remain simple and explicit.
There is likewise no output minifier: clipping is measured from final bytes and
linted, while a minifier would need independent email-client evidence.
