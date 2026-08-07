# Email Markup — first standalone compiler, CLI, language server, and editor client

## Context

Email Markup currently exists inside `growth-console`: a Python implementation under
`backend/campaigns/vel/`, native component renderers in
`campaigns/emailfmt.py`, a deliverability linter in `emaillint.py`, and Monaco
editor support in `frontend/src/lib/vel/`.

This repository becomes the language's standalone home. The C++
compiler is the single source of semantic truth, the LSP links it directly, and
the VS Code extension remains a thin protocol client. The Python implementation
is a makeshift prototype and read-only correctness oracle, not a released Email Markup 1.
The standalone compiler defines the first versioned release.

This plan does not modify, migrate, reformat, or repair `growth-console`.

---

## First-release scope: JSON in, final HTML out

The first release has one execution model:

```text
.em source + one JSON object -> emc -> complete HTML
```

The CLI receives the actual data for the render. Email Markup resolves every `@{…}`
expression, executes compiler `@If` and `@For`, expands components, applies the
shell and styles, inlines CSS, lints the result, and writes final HTML. Nothing
is intentionally left for another template engine to evaluate.

The direct form is:

```bash
emc compile message.em \
  --data-json '{"business":{"name":"Northstar"}}' \
  -o message.html
```

For safer automation and larger payloads, the same command also accepts JSON on
standard input or from a file:

```bash
printf '%s' "$LEAD_JSON" | emc compile message.em --data-stdin -o message.html
emc compile message.em --data-file lead.json -o message.html
```

`--data-json`, `--data-stdin`, and `--data-file` are mutually exclusive. Exactly
one data source is required when the document references compile data. All three
feed the same JSON parser and produce identical output. The JSON root must be an
object; malformed input and missing referenced paths are compile errors.

For the initial product integration, the caller invokes `emc` with the JSON for
one recipient/render and consumes the resulting HTML. It does not ask Django,
Jinja, or another engine to fill the output afterward. A future batch or
long-lived compiler process may reduce process-launch overhead without changing
this JSON-to-final-HTML contract.

Only `.em` source files participate in this release. There is no `.emt` file,
`@Engine`, `@[…]`, `@Name[…]`, engine capability matrix, or deferred-template
output in the initial grammar, CLI, LSP, extension, package, or release gates.

The deferred templating and macro design is preserved separately in
[templates.md](templates.md). It is a future extension, not an unfinished part
of this plan. Adding it later requires an explicit versioned language proposal
and its own implementation and migration plan.

---

## Language surface in this release

### Compiler-owned forms

| Form | Meaning |
| --- | --- |
| `@{ expr }` | Evaluate a compile-time expression and emit its scalar text |
| `@Name( … ) … @/Name` | Component or compiler construct with a body |
| `@Name( … );` | Component or compiler construct without a body |
| `@/Name` | Close tag |
| `@* … *@` | Block comment |
| `@// …` | Line comment |
| `@@` | Literal `@` |

`@Component(p1: v1, …);` needs no separate syntax. The trailing semicolon makes
it void; without one it takes a body and closes with `@/Component`.

`@If(expr)` and `@For(name in expr)` are compiler constructs. They are evaluated
while building this recipient's HTML; neither survives in the output.

### Expressions

```text
expr    := or
or      := and ("or" and)*
and     := cmp ("and" cmp)*
cmp     := add (("=="|"!="|"<"|"<="|">"|">=") add)*
add     := mul (("+"|"-") mul)*
mul     := unary (("*"|"/"|"%") unary)*
unary   := ("not"|"-")? primary
primary := number | string | bool | null | path | "(" expr ")"
```

Resolution is innermost-first: loop variable, declared prop, then the CLI JSON
object. Missing paths are errors rather than empty strings.

`and` and `or` short-circuit. Comparisons require compatible scalar types.
Division or modulo by zero and integer overflow are errors. Integers promote to
decimal numbers only when required; strings never enter arithmetic through
coercion. Arrays and objects are valid JSON values for lookup and iteration but
cannot be interpolated directly as text or used as arithmetic operands.

Text interpolation is deterministic: strings emit their contents, booleans emit
`true` or `false`, and numbers use locale-independent canonical spelling. `null`,
arrays, and objects must be handled explicitly rather than silently stringified.

### Composition

`@Include("file.em");` composes Email Markup files. Resolution checks, in order:

1. the including file's directory;
2. each `-I` directory in command-line order;
3. failure with a diagnostic listing every attempted path.

`${EMAIL_MARKUP_LIB}` is a compiler-defined path variable available in include
paths, search directories, and project configuration.

After canonicalisation and symlink resolution, a target must remain under an
allowed root: the project root, an explicit `-I` directory, or
`${EMAIL_MARKUP_LIB}`. Non-regular files, oversized files, unsupported extensions,
and paths outside those roots are errors.

Includes are parsed as separate documents, not textually spliced. Include-once
is keyed by canonical absolute path. The first depth-first encounter determines
content insertion and definition order; later encounters are no-ops. Cycles are
errors with the full loop. The entry document's definitions apply last so it can
deliberately override library definitions. A collision between two included
files is a warning.

### Components, styles, and built-ins

There are no native component renderers in the standalone compiler. The fifteen
built-ins are ordinary `@DefineComponent` declarations with `@Props`, `@Slots`,
and `@Template`, stored in `lib/builtins.em`. Their email-hardened markup moves
out of C++ and into Email Markup.

Component prop types remain the existing Email Markup types. Wrong types, missing
required props, unknown props, undeclared slots, and invalid self-closing/body
forms are compile errors. Nothing is silently coerced.

`@Template` inside a component definition is the component's static Email Markup body;
it is expanded completely by `emc`. It is not the deferred external-engine
templating functionality postponed to `templates.md`.

Themes, style bundles, and complete email wrappers remain ordinary project
files. The compiler performs the existing three-layer style cascade, media
validation, CSS inlining, and email-client hardening without embedding
company-specific values in C++.

Email Markup source normalisation ports the prototype's `_normalise` and `_trim`
behaviour for ordinary text and component-template indentation. There is no
textual finished-HTML minifier. Gmail clipping is linted from the measured output
byte count; any later `--minify` must be a tree-aware, evidence-driven feature.

### Release status

The implementation inside `growth-console` is not Email Markup 1. Until the standalone
compiler reaches 1.0, syntax may change with its examples and libraries in the
same repository. After 1.0, a breaking language change requires an explicit
version switch or deprecation cycle.

The future templating proposal does not reserve current syntax merely by being
documented. Before implementation, `templates.md` must be reviewed against the
then-current language and assigned a version boundary.

---

## Repository layout

```text
email-markup/
├── .gitmodules                  external/vcpkg
├── external/vcpkg/             shared C++ dependency baseline
├── CMakeLists.txt               root project
├── CMakePresets.json
├── vcpkg.json                   fmt, nlohmann-json, Catch2
├── setup.sh / setup.bat
├── run.sh / run.bat
├── packages/
│   ├── email-markup-core/                language, renderer, CSS and lint library
│   ├── email-markup-cli/                 emc
│   ├── email-markup-lsp/                 email-markup-lsp
│   └── email-markup-tests/               Catch2 suites and golden fixtures
├── lib/
│   └── builtins.em             fifteen built-in components in Email Markup
├── examples/                    final-HTML `.em` examples, neutral theme, and JSON fixtures
├── extensions/vscode/           thin LSP client and secure HTML preview
├── grammar/email-markup.ebnf             normative grammar and recovery boundaries
├── schema/em.schema.json       project configuration schema
├── syntax/lexical.json          source for editor lexical configuration
├── templates.md                 deferred future template/macro proposal
├── .github/workflows/           platform, sanitizer, package and extension gates
├── EMAIL-MARKUP.md                       implemented language reference
└── README.md
```

There is intentionally no `lib/engines/` or `.emt` editor contribution in this
release.

---

## Phase 0 — repository surgery and build skeleton

The user has explicitly authorised the exact Git and filesystem mutations in
this phase. The executing agent may request approval again if the runtime
requires it. If its governing Git policy still forbids writes, it must stop and
ask the user to run the Git command rather than work around that policy.

Before removing nested repository metadata, record the nested compiler's current
commit and confirm whether its history needs preserving. Then perform the
authorised operations:

```bash
rm -rf compiler/.git compiler/build compiler/external
git submodule add --depth 1 https://github.com/microsoft/vcpkg.git external/vcpkg
```

Move the template build files to the repository root and rewrite them:

- `vcpkg.json` uses manifest mode with `fmt`, `nlohmann-json`, and `catch2`; the
  vcpkg submodule commit is the dependency baseline.
- Root `CMakeLists.txt` lists sources explicitly and adds each package as a
  subdirectory.
- Dev builds copy `lib/` next to the binaries.
- `setup.sh`/`run.sh` and their `.bat` siblings remain behaviorally aligned.
- User-facing Bash follows the repository's Bash 3.2 and safety conventions.

The support floor is C++23, CMake 4.0, Ninja, current Apple Clang on macOS,
current MSVC on Windows, and current Clang or GCC on Linux. CI records the exact
versions used for the first release.

**Checkpoint:** `./setup.sh && ./run.sh build` produces stub `emc`, `email-markup-lsp`,
and test binaries.

---

## Phase 1 — `packages/email-markup-core`

Port the Python implementation structurally, folding in the explicit language
changes above. The core has no ambient filesystem or process state: source,
includes, JSON data, configuration, role, and limits enter through its request
and resolver; final HTML and diagnostics return in its result.

### Ownership, recovery, and provenance

These are first-commit constraints rather than later optimisations:

- An immutable `DocumentSnapshot` owns an address-stable `SourceManager`, AST,
  dependency graph, JSON data, and diagnostics. An edit creates a replacement
  snapshot. In-flight requests retain the snapshot they began with.
- Parsing returns an error-tolerant tree plus collected diagnostics. Parameter
  lists recover at commas or their delimiter, nested bodies at matching closes
  or sibling constructs, and top level at the next recognised sigil.
- Rendering returns `GeneratedHtml`: final HTML plus ordered output segments
  carrying source ranges and expansion stacks. HTML and CSS findings map back to
  the location an author can change.

Provenance rules are explicit:

- syntax, binding, type, and data errors point to the causing token or JSON path;
- markup controlled by a component argument points to the argument, with its
  definition as related information;
- static component or shell markup points to its definition, with the call site
  related;
- cross-segment HTML errors point to the opener and relate the conflicting
  closer/expansion chain;
- CSS inlining preserves both declaration and target-element origins.

### Modules

| C++ header | Responsibility |
| --- | --- |
| `source.hpp` | snapshots, canonical paths, UTF-8 buffers, line indexes, position conversion |
| `diagnostic.hpp` | stable codes, ranges, severity, related locations |
| `provenance.hpp` | `GeneratedHtml` source-map segments and expansion stacks |
| `lexer.hpp` | Email Markup sigils, comments, strings, source ranges |
| `ast.hpp` | `std::variant` node model |
| `expr.hpp` | expression parsing and evaluation |
| `types.hpp` | component prop parsing and validation |
| `parser.hpp` | error-recovering recursive descent |
| `include.hpp` | allowed-root resolution, include-once, cycles, dependency graph |
| `data.hpp` | JSON object ownership, path resolution, deterministic scalar emission |
| `css.hpp` | declaration parsing, merge, root styles, class inlining |
| `styles.hpp` | bundles, tokens, cascade, media validation |
| `registry.hpp` | component, style, and token definitions |
| `render.hpp` | bounded component expansion and final HTML generation |
| `lint.hpp` | mapped email-HTML findings and compliance rules |

The public boundary is small and re-entrant:

```cpp
CompilationResult compile(
    const CompilationRequest& request,
    FileResolver& files,
    CancellationToken cancellation);
```

`CompilationResult` owns its snapshot, final HTML, dependency graph, and
diagnostics. There is no global registry or process state. CLI and LSP call the
same API. A later `growth-console` adoption can use a binding or `emc --json`
without changing language semantics.

### Limits and cancellation

Safe defaults bound source and JSON sizes, include count/depth, component
expansion depth, compiler-loop iterations, AST nodes, generated HTML bytes, and
diagnostic count. Limits may be configured downward. Cancellation is checked
between files, loop iterations, component expansions, CSS passes, and lint
tokens. Exceeding a limit emits one stable diagnostic instead of exhausting
memory or freezing an editor.

### Lint roles

The project configuration identifies one `shell.em`. Shell lint permits its
generated media `<style>` block and requires a visible unsubscribe target.
Ordinary content files may omit the wrapper while being edited, but a full
compile always applies the configured shell before final lint.

The linter ports forbidden-tag, unsupported-CSS, media, structural, and Gmail
102,400-byte checks. Scrape or network work never occurs in the compiler.

**Checkpoint:** lexer, parser, expression, include, data, recovery, provenance,
limits, render, CSS, and lint suites pass under supported sanitizers.

---

## Phase 2 — built-ins, themes, shells, and examples in Email Markup

Create `lib/builtins.em` from the fifteen Python renderers using
`@DefineComponent` and `@Template`. Supported inputs preserve prototype markup
except for every intentional change listed in a checked-in manifest and covered
by a golden fixture.

Known intentional changes include:

- `@Bullets` and `@Item` become ordinary nested components; the native-item
  special case disappears.
- `@Columns` uses compiler arithmetic for its calculated gap.
- `@Spacer` validates its 4–120 height range and errors instead of silently
  clamping.
- `@Shell`'s Python-only wrapper escape hatch is removed; another wrapper is
  another `.em` file.
- Conditional logo, tagline, attribution, address, and link markup use compiler
  `@If` and slots.

Keep reusable neutral theme and shell fixtures under `examples/_shared/`. C++
contains mechanisms, never company constants or a component name list.

Every example has at least one JSON fixture and checked-in final HTML golden.
Examples demonstrate direct JSON interpolation, compile-time conditions and
loops, components, includes, styles, shell wrapping, and failure diagnostics.

**Checkpoint:** `emc` can compile `examples/solution_first.em` with its JSON
fixture into complete HTML, and shell lint validates unsubscribe and
media requirements.

---

## Phase 3 — `packages/email-markup-cli` (`emc`)

```text
emc build [dir]
emc compile <file> -o <file>
emc check <file>
emc lint <file> [--role content|shell]
emc fmt <file> [--write]
emc schema
emc --version

  -I <dir>            add an allowed include/search directory
  --import <file>     pre-import a `.em` definition file
  --data-json <json>  use this JSON object directly
  --data-stdin        read one JSON object from standard input
  --data-file <file>  read one JSON object from a file
  --shell <file>      select the final HTML wrapper
  --json              emit diagnostics/results as JSON
```

The three data flags are mutually exclusive. Command-line data overrides the
project's optional development fixture; conflicting command-line sources are an
error rather than precedence magic. Production automation should prefer stdin
so recipient data does not appear in process listings or shell history.

`em.json` contains only current-release concepts:

```json
{
  "$schema": "./schema/em.schema.json",
  "include": ["${EMAIL_MARKUP_LIB}", "theme", "components"],
  "imports": ["${EMAIL_MARKUP_LIB}/builtins.em", "theme/styles.em"],
  "data": "fixtures/development.json",
  "shell": "theme/shell.em",
  "out": "build"
}
```

The config's `data` entry is development/build convenience, never hidden
recipient state. An explicit CLI data source replaces it for that invocation.

Human diagnostics include file, range, severity, source excerpt, caret, and
expansion notes. `--json` uses stable codes, start/end ranges, related locations,
and a documented exit-code contract. JSON diagnostics must never echo the full
recipient payload or secret values.

Outputs are atomic. Duplicate output paths are errors, and failed builds leave
previous files untouched. `cmake --install` creates a relocatable prefix with
`emc`, `email-markup-lsp`, `lib/`, schema, documentation, and licenses.

**Checkpoint:** direct, stdin, and file JSON modes produce byte-identical final
HTML; an installed prefix performs the same build without the source tree.

---

## Phase 4 — conformance and platform gates

Tests are written with Phases 1–3; this phase completes the release matrix.

Port Python suites that test the retained language rather than Django or
database behavior. Add suites for includes, expressions, JSON types and missing
paths, compiler loops/conditions, definitions, styles, shell application,
parser recovery, limits, cancellation, and every CLI data transport.

Golden tests cover every built-in and example. Provenance tests cover entry
source, includes, component definitions, arguments, shell markup, JSON paths,
and post-inline CSS. Parser and expression entry points receive fuzz/property
tests with arbitrary Unicode and malformed input under ASan and UBSan.

Read-only comparison against `growth-console` is limited to behavior deliberately
retained by the standalone language. It never edits that repository and never
claims compatibility for removed prototype features. Intentional output changes
live in an explicit manifest and dedicated goldens.

CI runs macOS/Apple Clang, Windows/MSVC, and Linux/Clang or GCC in Debug and
Release, with ASan/UBSan where supported, installed-prefix smoke tests, and the
VS Code extension build. Dependency and compiler versions are printed in every
job.

**Checkpoint:** `./run.sh test` is green on the platform matrix with no
unexplained golden or retained-subset parity differences.

---

## Phase 5 — `packages/email-markup-lsp`

Target the current LSP 3.18 specification over stdio and link `email-markup-core`
directly. `Content-Length` counts UTF-8 bytes. Initialization negotiates position
encoding; UTF-16 is supported and used as the compatibility fallback.

### Synchronisation and project model

- Incremental edits apply through tested position converters to immutable,
  versioned snapshots.
- Requests retain their starting snapshot, honor `$/cancelRequest`, and suppress
  diagnostics or responses made stale by a newer document version.
- Each workspace folder discovers the nearest ancestor `em.json` without
  crossing its root. Loose files receive an explicit default project.
- Project context contains JSON fixture metadata, imports, shell, and allowed
  include roots. It contains no engine or `.emt` state.
- Include, data-fixture, shell, brand, and config changes invalidate only
  dependent snapshots. Removed diagnostics publish an empty set.
- Open documents are released after `didClose` and completion of in-flight work.

### Features and gates

The recovered AST and token stream are the only caret-context implementation;
the Monaco client's single-line `enclosingCall` heuristic is not ported.

The MVP advertises diagnostics with provenance, completion, hover, definition,
document symbols, folding, signature help, and formatting shared with `emc
fmt`. Completion covers components, props, bundles, slots, tokens, compile-data
keys, and include paths.

References, rename, workspace symbols, semantic tokens, selection ranges,
document highlights, linked editing, inlay hints, and code actions are enabled
only after each has request/response, malformed-document, Unicode-position,
cancellation, and stale-version tests.

`email-markup/preview` is requested only while a preview panel is visible. It returns the
document version, final compiled HTML, and mapped diagnostics. The editor sends
the selected development JSON fixture or an explicit unsaved preview data
object; preview data is never persisted implicitly. Older responses are ignored.

Full-file reparsing is the first implementation. Release budgets record p50/p95
edit-to-diagnostics latency and peak memory on representative and worst-allowed
documents; measurements decide whether incremental parsing is necessary.

**Checkpoint:** a protocol harness covers initialization, Unicode positions,
open/change/close, every advertised MVP feature, dependency invalidation,
data-fixture changes, cancellation, stale suppression, and final-HTML preview.

---

## Phase 6 — `extensions/vscode`

Build one `.em` language contribution and a thin `vscode-languageclient` host
for the bundled `email-markup-lsp`. TextMate grammar and language configuration are
generated from `syntax/lexical.json` and tested against the lexer corpus;
semantic tokens take over after server startup.

The extension declares Workspace Trust. In an untrusted workspace it does not
launch a workspace-configured executable, read workspace-selected external
paths, load JSON data, or render preview HTML. Grammar-only coloring remains.

Preview scripts are disabled, `localResourceRoots` is empty, and CSP begins with
`default-src 'none'`. Remote images are placeholders by default so opening an
email cannot fire a tracking pixel; a visible user action may load them for the
current preview. This affects preview transport, never compiler output.

Platform-specific VSIX packages bundle matching servers for macOS arm64/x64,
Windows x64, and Linux x64/arm64. Client/server protocol versions are checked at
startup. CLI/server archives, VSIX packages, checksums, schemas, libraries, and
licenses come from one release tag.

**Checkpoint:** extension-host tests cover activation, bundled-server launch,
Unicode edits, diagnostics, completion, final-HTML preview, CSP, JSON-data
handling, and untrusted workspaces. The user owns final visual and behavioral
verification.

---

## Phase 7 — audit, optimise, and document

- Profile edit-to-diagnostics and `emc build` before changing allocation or
  parsing strategies.
- Arena-allocate ASTs per snapshot and intern repeated names only when measured
  memory data justifies it; snapshot ownership and address stability do not
  change.
- Review the implemented language before 1.0, including the `@Props` table
  grammar, definition naming, errors, and formatting.
- Add an optional tree-aware minifier only if Gmail-clipping evidence warrants
  it.
- Rewrite `EMAIL-MARKUP.md` and README examples to match the implemented JSON-to-final-HTML
  compiler exactly.
- Review `templates.md` only as a future proposal. Do not allow its deferred
  syntax to leak into current compiler, CLI, LSP, or editor capabilities.

---

## Verification

Release gates, in order:

1. User-authorised repository surgery completes, nested history disposition is
   recorded, and stub binaries build on every CI platform.
2. Core grammar, recovery, source ownership, includes, JSON evaluation,
   components, rendering, CSS, lint, limits, cancellation, and provenance pass
   Catch2, fuzz, and supported sanitizer suites.
3. Direct, stdin, and file JSON inputs produce identical final HTML and never
   expose values in diagnostics.
4. Every built-in, theme, shell, and example passes final-HTML golden tests;
   intentional prototype differences are recorded.
5. CLI commands, exit codes, atomic outputs, JSON diagnostics, configuration
   schema, and installed-prefix behavior pass contract tests.
6. LSP tests pass Unicode edits, malformed trees, multi-root projects, dependency
   and JSON-fixture changes, cancellation, and stale-result suppression.
7. Recorded latency and memory budgets pass on representative and maximum-sized
   documents.
8. Platform VSIX packages pass trusted/untrusted activation, bundled-server,
   data-handling, and secured-preview tests.
9. Release archives and checksums pass clean-machine smoke tests from one version.
10. A negative conformance gate proves `.emt`, `@Engine`, `@[…]`, and
    `@Name[…]` are not advertised or silently accepted in this release.
11. The user performs final visual and behavioral verification of syntax color,
    completion, diagnostics, and rendered email output.

---

## Out of scope

- Deferred template-engine output and macros. Their future proposal lives in
  [templates.md](templates.md).
- `.emt` files, `@Engine`, square-bracket macro calls, and engine-specific
  capability or packaging work.
- Definition versioning and database pinning.
- Automatic migration of prototype sources in `growth-console`.
- The block-tree migration in `vel/serialize.py` and `convert_to_vel`.
- Any edit, migration, reformat, or repair inside `growth-console`.
- Full CSS property/value validation through an embedded CSS language service.
- Network access, discovery, recipient lookup, or campaign execution from the
  compiler.
