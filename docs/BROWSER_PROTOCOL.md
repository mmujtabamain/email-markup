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
`email_markup::browser` library. To produce the worker assets, activate the
Emscripten SDK and make a WASM-compatible `nlohmann_json` CMake package
available through `EMAIL_MARKUP_WASM_PREFIX`, then run:

```bash
npm --prefix packages/email-markup-browser run build:wasm
npm --prefix packages/email-markup-browser run package
```

The package step emits the ES-module worker, `.mjs` loader, `.wasm` binary,
TypeScript declarations, protocol schema, language syntax, and packaged Email
Markup library into `packages/email-markup-browser/dist/`. The Emscripten build
uses the browser-only CMake path, disables the virtual filesystem, exports only
the version and single-request C boundary, and does not link platform, CURL,
CLI, LSP process, or native runtime code.
