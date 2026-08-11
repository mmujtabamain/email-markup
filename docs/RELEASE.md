# Email Markup release guide

Email Markup releases are produced by `.github/workflows/release.yml` after every push
to `main`. Do not hand-assemble platform packages.

The root `VERSION` file is the only release-version source. Each accepted release uses
the canonical `v<version>` tag. If that tag already exists, the workflow fails before
building and reports that the duplicate version was not published; the commit remains
on `main`.

## Release contents

The release matrix builds and tests:

- macOS x64 and arm64;
- Windows x64;
- Linux x64 and arm64.

Each platform publishes an installed compiler/server ZIP, a matching self-contained
VSIX, and SHA-256 files for both. The install tree contains `emc`, `email-markup-lsp`, the
core and portable browser libraries and headers, the Email Markup standard
library, grammar, syntax metadata, config and browser-protocol schemas,
documentation, and license. The VSIX
bundles the server matching its platform and architecture.

## Local release verification

```bash
cmake --preset release
cmake --build --preset release
ctest --preset release --output-on-failure
cmake --install build/release --prefix /tmp/email-markup-stage

cd extensions/vscode
npm ci
npm test
npm run package

cd ../../packages/email-markup-browser
npm test
```

When the Emscripten SDK and WASM dependency prefix are available, also run
`npm run build:wasm` and `npm run package`. The native release matrix validates
the portable browser library and installed protocol boundary; publishing the
web package additionally requires its actual `.mjs`/`.wasm` package gate.

Also run the sanitizer preset and performance benchmark documented in
`docs/PERFORMANCE.md`. The installed-prefix CTest contract compiles a fixture
using only the staged tree, which catches missing runtime assets.

For a downloaded artifact, verify its adjacent checksum before extraction:

```bash
sha256sum --check email-markup-linux-x64.zip.sha256
sha256sum --check email-markup-language-linux-x64.vsix.sha256
```

Use `shasum -a 256 -c` on macOS if GNU `sha256sum` is unavailable.

## Version and protocol

Change only the root `VERSION` file when preparing a release. CMake,
`email_markup::version()`, the release packager, and VS Code packaging all read that
value. The checked-in VS Code manifest uses `0.0.0` as a development sentinel; the
packaging command injects the release version into the VSIX without modifying tracked
files. `external/vcpkg.version` pins the setup-managed dependency snapshot to the
peeled `2026.07.29` tag commit. The extension/server protocol and stdin compilation
protocol and browser worker protocol have their own integer versions. The extension client checks the server
protocol at startup; embedding applications must check the compilation response
protocol and compiler version.

Push commits and merge commits directly to `main` as usual. Every push starts the
release workflow. A successful release consumes the current version, so update
`VERSION` before the next push that should publish.

The legacy SHA-suffixed 1.1.0 release tag has already been migrated to canonical
`v1.1.0`. All releases use canonical tags from that version onward.

Before pushing a new version to `main`, require a clean tree, all local gates, and
review of the intentional-differences manifest. After the release workflow passes,
install the matching VSIX on each supported editor platform and perform the user-owned
final visual and behavioral checks: the `@` file icon, Email Markup/HTML/CSS syntax
colors, HTML and CSS completion, Email Markup diagnostics, component completion,
secured preview, and representative rendered email output.

No release job pushes source branches, and every uploaded file is checksum-verified
before publication.
