# ELL release guide

ELL releases are produced from a single `v*` tag by
`.github/workflows/release.yml`. Do not hand-assemble platform packages.

## Release contents

The release matrix builds and tests:

- macOS x64 and arm64;
- Windows x64;
- Linux x64 and arm64.

Each platform publishes an installed compiler/server ZIP, a matching self-contained
VSIX, and SHA-256 files for both. The install tree contains `ellc`, `ell-lsp`, the
core library and headers, the ELL standard library, Example brand and shell,
grammar, syntax metadata, config schema, documentation, and license. The VSIX
bundles the server matching its platform and architecture.

## Local release verification

```bash
cmake --preset release
cmake --build --preset release
ctest --preset release --output-on-failure
cmake --install build/release --prefix /tmp/ell-stage

cd extensions/vscode
npm ci
npm test
npm run package
```

Also run the sanitizer preset and performance benchmark documented in
`docs/PERFORMANCE.md`. The installed-prefix CTest contract compiles a fixture
using only the staged tree, which catches missing runtime assets.

For a downloaded artifact, verify its adjacent checksum before extraction:

```bash
sha256sum --check ell-linux-x64.zip.sha256
sha256sum --check ell-language-linux-x64.vsix.sha256
```

Use `shasum -a 256 -c` on macOS if GNU `sha256sum` is unavailable.

## Version and protocol

CMake, `ell::version()`, the vcpkg manifest, and the VS Code package must share
the release version. The extension/server protocol has its own integer version;
the client checks it at startup and refuses a mismatched bundled or overridden
server.

Before tagging, require a clean tree, all local gates, and review of the
intentional-differences manifest. After the release workflow passes, install the
matching VSIX on each supported editor platform and perform the user-owned final
visual and behavioral checks: the `@` file icon, ELL/HTML/CSS syntax colors,
HTML and CSS completion, ELL diagnostics, component completion, secured preview,
and representative rendered email output.

No release job pushes source branches. Tag publication is the only trigger that
creates the GitHub release, and every uploaded file is checksum-verified first.
