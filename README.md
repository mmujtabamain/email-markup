# ELL

ELL is a typed, component-based language for HTML email. ELL 1 compiles one
`.ell` document plus one JSON object into complete, CSS-inlined, linted HTML.
The compiler never leaves merge syntax for another template engine.

```ell
@Heading A faster website for @{business.name} @/Heading

@For(opportunity in opportunities)
  @Paragraph @{opportunity} @/Paragraph
@/For

@Button(url: call_url) See the recommendations @/Button
```

```bash
ellc compile message.ell --data-file recipient.json -o message.html
```

The repository contains the C++23 compiler library, `ellc`, `ell-lsp`, fifteen
ELL-defined email components, the Example brand and shell, conformance tests, and
a self-contained VS Code extension.

## Build from source

Requirements are CMake 4+, Ninja, Git, and a current C++23 compiler. Dependencies
are pinned through the `external/vcpkg` submodule.

```bash
./setup.sh
./run.sh build
./run.sh test
```

On Windows, use `setup.bat`, `run.bat build`, and `run.bat test` from a Visual
Studio developer shell. A release install can be created with:

```bash
cmake --preset release
cmake --build --preset release
cmake --install build/release --prefix ./stage/ell
```

## Use `ellc`

```bash
# One document, with equivalent JSON transports
ellc compile message.ell --data-json '{"business":{"name":"Northstar"}}' -o message.html
ellc compile message.ell --data-file recipient.json -o message.html
printf '%s' "$RECIPIENT_JSON" | ellc compile message.ell --data-stdin -o message.html

# Validate, lint, format, or build a project tree
ellc check message.ell --data-file recipient.json
ellc lint message.ell --role content --data-file recipient.json
ellc fmt message.ell
ellc fmt message.ell --write
ellc build .

# Automation-friendly diagnostics and the installed config schema
ellc check message.ell --json
ellc schema
```

`ellc` finds the nearest `ell.json`. The repository’s config demonstrates all
supported keys: include roots, definition imports, a development JSON fixture,
the final shell, and the build output directory. `${ELL_LIB}` and
`${EMAIL_MARKUP_BRAND}` resolve to the installed assets. Explicit `-I`, `--import`,
`--shell`, and data options override or extend project configuration.

Writes are atomic. A failed compile never replaces the previous HTML output.
Diagnostics do not print JSON values.

## Standard library and HTML/CSS

`lib/builtins.ell` defines `Paragraph`, `Heading`, `Bullets`, `Numbered`, `Item`,
`Callout`, `Quote`, `Button`, `Image`, `Divider`, `Spacer`, `Panel`, `Columns`,
`Unsubscribe`, and `Shell` in ELL itself. Raw HTML remains valid source.

Ordinary class selectors declared in `<style>` blocks are inlined into matching
HTML elements during compilation. Existing inline declarations take precedence.
Validated `@Media` rules remain in the final shell; non-media style blocks are
removed before deliverability lint.

The `examples/` directory contains ten unbranded, single-feature examples with
JSON fixtures and checked-in final-HTML output. Run `./examples/compile.sh` (or
`examples\compile.bat` on Windows) to regenerate all ten.

## VS Code

Build `extensions/vscode` after the C++ build has staged `ell-lsp`:

```bash
cd extensions/vscode
npm ci
npm test
npm run package
```

`.ell` files use an `@` file icon. The grammar layers ELL scopes over VS Code’s
HTML grammar and embeds CSS in `<style>`, while the extension adds HTML and CSS
completion, hover, document symbols, linked-tag editing, CSS colors, and local
class-name completion. `ell-lsp` provides ELL diagnostics, component completion,
navigation, formatting, and final-HTML preview.

In untrusted workspaces only grammar highlighting is enabled. Preview scripts and
local resource roots are disabled, and remote images stay inert until the user
explicitly enables them for the current preview.

## Reference and release evidence

- `ELL.md` — implemented ELL 1 language and tooling reference
- `grammar/ell.ebnf` — normative grammar
- `schema/ell.schema.json` — project configuration schema
- `docs/PERFORMANCE.md` — latency, memory, and limits
- `docs/LANGUAGE_REVIEW.md` — recorded ELL 1 syntax and optimization decisions
- `docs/RELEASE.md` — release contents and verification
- `templates.md` — future deferred-templating proposal, not ELL 1

ELL is licensed under the MIT License.
