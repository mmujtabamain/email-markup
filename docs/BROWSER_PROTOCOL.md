# Browser and Web Worker compiler surface

`email-markup-browser` exposes the portable compiler core to a dedicated Web
Worker for custom Monaco authoring. It is assistance, not a publication or
delivery authority. The native server-side `emc` must reload the durable draft
and repeat compilation before verified preview, test send, publication, or
campaign delivery.

The protocol identifier is `email-markup.browser`; its current integer version
is `1`. The normative envelope schema is
`schema/browser-protocol-v1.schema.json`. Requests and responses are plain JSON,
carry a caller-supplied string or integer `id`, and use UTF-16 line/character
positions so they map directly to Monaco.

Supported methods are:

- `capabilities`: version, trust flags, methods, position encoding, and limits;
- `analyze`: deterministic diagnostics, symbols, dependencies, and safe preview;
- `format`: the same compiler formatter used by native `emc`;
- `complete`: context-aware keyword, component, type, and data-path items;
- `hover`: component/deferred declaration documentation;
- `signature`: component prop signature and active parameter.

Every `analyze` result contains `authoritative: false`. Final-HTML preview is
compiler output suitable only for a sandboxed/CSP-constrained live preview.
Deferred output uses `kind: "target-source"`, `rendered: false`, and
`executes_target: false`; neither the portable library nor the worker executes
Django. The worker has no compiler filesystem or network access. All sources,
imports, shell, engine, and sample data arrive as bounded virtual files.

## Resource and trust limits

- One JSON request: 1 MiB.
- One source: 1 MiB; complete virtual workspace source: 2 MiB.
- Virtual files: 256.
- Compiler diagnostics: 100; generated output: 2 MiB.
- Initial WASM memory: 32 MiB; maximum memory: 256 MiB with bounded growth.
- Remote image fetching and target-engine execution are disabled.

Browser diagnostics, preview HTML, EMIR, target source, dependencies, and hashes
are untrusted hints. A host must not use them as proof that persisted source is
valid. Do not interpolate target source into the page; show it as escaped text.
Render live HTML only in a sandboxed frame with scripts and local resource roots
disabled.

## Build and package

The ordinary native build always compiles and tests the portable
`email_markup::browser` library. The web package is pinned to Emscripten
`4.0.23`: emsdk commit
`c0bb220cb6e6f4e0fabb6f6db9efd53390ef5e56`, compiler commit
`7a5d93b50f6a3a35e85a0d2fc9e667b8498e6aed`, and releases payload
`aaa43392544d695232b70eda706d751f18980c2a`. These values also live in the
package metadata, and `build:wasm` rejects a different compiler.

Keep emsdk outside the repository. For an isolated local setup:

```bash
EMSDK_ROOT="$(mktemp -d /tmp/email-markup-emsdk.XXXXXX)"
git clone --depth 1 --branch 4.0.23 https://github.com/emscripten-core/emsdk.git "$EMSDK_ROOT/emsdk"
test "$(git -C "$EMSDK_ROOT/emsdk" rev-parse HEAD)" = "c0bb220cb6e6f4e0fabb6f6db9efd53390ef5e56"
"$EMSDK_ROOT/emsdk/emsdk" install 4.0.23
"$EMSDK_ROOT/emsdk/emsdk" activate 4.0.23
source "$EMSDK_ROOT/emsdk/emsdk_env.sh"
```

The compiler depends only on the `nlohmann_json` headers. `build:wasm` finds
their CMake package in a native release/debug vcpkg tree. In a different build
layout, set `EMAIL_MARKUP_WASM_NLOHMANN_DIR` to the directory containing
`nlohmann_jsonConfig.cmake`. Then run the complete artifact gate:

```bash
npm --prefix packages/email-markup-browser run verify:wasm
```

The package step emits the ES-module worker, `.mjs` loader, `.wasm` binary,
TypeScript declarations, protocol schema, language syntax, and packaged Email
Markup library into `packages/email-markup-browser/dist/`. `manifest.json`
records the exact toolchain and SHA-256/byte size of every executable protocol
asset. The gate loads the real packaged `.wasm` in Node, invokes both stable C
exports, and inspects its imports. The Emscripten build uses the browser-only
CMake path, disables the virtual filesystem, exports only the version and
single-request C boundary, and does not link platform, CURL, CLI, LSP process,
or native runtime code.
