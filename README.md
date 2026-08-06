# ELL

ELL is a standalone email layout language. Its first release compiles one
`.ell` document and one JSON object into complete, linted HTML.

The implementation is currently at the build-skeleton stage. See
[PLAN.md](PLAN.md) for the phased delivery plan and [ELL.md](ELL.md) for the
language reference under development.

## Prerequisites

- CMake 4.0 or newer
- Ninja
- A C++23 compiler
- Git

## Build

On macOS and Linux:

```bash
./setup.sh
./run.sh build
./run.sh test
```

On Windows, run `setup.bat`, `run.bat build`, and `run.bat test` from a Visual
Studio developer shell.
