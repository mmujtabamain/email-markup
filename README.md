# Email Markup

Email Markup (EM) is a typed, component-based language for HTML email. Email Markup 1 compiles one
`.em` document plus one JSON object into complete, CSS-inlined, linted HTML.
The compiler never leaves merge syntax for another template engine.

```email-markup
@Heading A faster website for @{business.name} @/Heading

@For(opportunity in opportunities)
  @Paragraph @{opportunity} @/Paragraph
@/For

@Button(url: call_url) See the recommendations @/Button
```

```bash
emc compile message.em --data-file recipient.json -o message.html
```

The repository contains the C++23 `email-markup-core` compiler library, `emc`,
`email-markup-lsp`, fifteen email components defined in Email Markup, neutral
examples, conformance tests, and a self-contained VS Code extension.

## Build from source

Requirements are CMake 3.25+, Ninja, Git, pkg-config, and a current C++23 compiler.
Dependencies come from a setup-managed clone at `external/vcpkg`. On the first
run, setup clones vcpkg's default branch and writes its exact commit to
`external/vcpkg.version`. Later runs fetch that pinned commit, so every clone uses
the same snapshot. Setup replaces its generated checkout when the pin changes and
removes the clone's Git metadata after download.

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
cmake --install build/release --prefix ./stage/email-markup
```

## Use `emc`

```bash
# One document, with equivalent JSON transports
emc compile message.em --data-json '{"business":{"name":"Northstar"}}' -o message.html
emc compile message.em --data-file recipient.json -o message.html
printf '%s' "$RECIPIENT_JSON" | emc compile message.em --data-stdin -o message.html

# Validate, lint, format, or build a project tree
emc check message.em --data-file recipient.json
emc lint message.em --role content --data-file recipient.json
emc fmt message.em
emc fmt message.em --write
emc build .

# Automation-friendly diagnostics and the installed config schema
emc check message.em --json
emc schema
```

`emc` finds the nearest `em.json`. The repository’s config demonstrates all
supported keys: include roots, definition imports, a development JSON fixture,
the final shell, and the build output directory. `${EMAIL_MARKUP_LIB}` resolves
to the installed standard library. Explicit `-I`, `--import`, `--shell`, and
data options override or extend project configuration.

Writes are atomic. A failed compile never replaces the previous HTML output.
Diagnostics do not print JSON values.

Embedding applications can compile entirely virtual source trees without
temporary files through the versioned `emc compile --request-stdin` JSON
protocol. See [docs/COMPILE_PROTOCOL.md](docs/COMPILE_PROTOCOL.md).

## Standard library and HTML/CSS

`lib/builtins.em` defines `Paragraph`, `Heading`, `Bullets`, `Numbered`, `Item`,
`Callout`, `Quote`, `Button`, `Image`, `Divider`, `Spacer`, `Panel`, `Columns`,
`Unsubscribe`, and `Shell` in Email Markup itself. Raw HTML remains valid source.

Ordinary class selectors declared in `<style>` blocks are inlined into matching
HTML elements during compilation. Existing inline declarations take precedence.
Validated `@Media` rules remain in the final shell; non-media style blocks are
removed before deliverability lint.

The `examples/` directory contains ten unbranded, single-feature examples with
JSON fixtures and checked-in final-HTML output. Run `./examples/compile.sh` (or
`examples\compile.bat` on Windows) to regenerate all ten.

## VS Code

Build `extensions/vscode` after the C++ build has staged `email-markup-lsp`:

```bash
cd extensions/vscode
npm ci
npm test
npm run package
```

`.em` files use an `@` file icon. The grammar layers Email Markup scopes over VS Code’s
HTML grammar and embeds CSS in `<style>`, while the extension adds HTML and CSS
completion, hover, document symbols, linked-tag editing, CSS colors, and local
class-name completion. `email-markup-lsp` provides Email Markup diagnostics, component completion,
navigation, formatting, and final-HTML preview.

In untrusted workspaces only grammar highlighting is enabled. Preview scripts and
local resource roots are disabled, and remote images stay inert until the user
explicitly enables them for the current preview.

## Reference and release evidence

- `EMAIL-MARKUP.md` — implemented Email Markup 1 language and tooling reference
- `grammar/email-markup.ebnf` — normative grammar
- `schema/em.schema.json` — project configuration schema
- `docs/PERFORMANCE.md` — latency, memory, and limits
- `docs/LANGUAGE_REVIEW.md` — recorded Email Markup 1 syntax and optimization decisions
- `docs/RELEASE.md` — release contents and verification
- `templates.md` — future deferred-templating proposal, not Email Markup 1

Email Markup is licensed under the MIT License.
