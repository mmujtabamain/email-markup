# ELL 1 language reference

ELL is a UTF-8, typed, component-based language for producing final HTML email.
Its complete execution contract is:

```text
.ell source + one JSON object -> compiler -> complete HTML
```

Expressions, loops, conditions, components, tokens, styles, includes, and the
shell are resolved by the compiler. Output contains no ELL directives.

## Lexical forms

ELL recognizes `@` only in these forms:

| Form | Meaning |
| --- | --- |
| `@{ expression }` | evaluate and HTML-escape a scalar |
| `@Name(...) ... @/Name` | component or compiler construct with a body |
| `@Name(...);` | bodyless component or construct |
| `@* ... *@` | block comment |
| `@// ...` | line comment |
| `@@` | literal `@` |

Names following `@` are capitalized. An ordinary address such as
`hello@example.org` is plain text. Raw HTML is ordinary source and passes into
the rendered document before CSS inlining and final lint.

The parser owns arity syntactically: a semicolon ends a bodyless call; otherwise
the matching close form is required. It recovers at directive and line
boundaries so tooling can report multiple errors from an incomplete document.

## Values and expressions

Literals are strings, integers, decimal numbers, booleans, and `null`. Paths such
as `business.name` read nested properties from the active scope. Arrays and
objects may be traversed or iterated but cannot be emitted directly.

Operators, from lowest to highest precedence, are:

1. `or`
2. `and`
3. `==`, `!=`, `<`, `<=`, `>`, `>=`
4. `+`, `-`
5. `*`, `/`, `%`
6. unary `not` and `-`

Parentheses override precedence. `and` and `or` short-circuit. Arithmetic has no
string coercion; division by zero, modulo by zero, overflow, incompatible
comparisons, and missing paths are errors. Integers promote to decimals only
when required. Scalar interpolation uses locale-independent spelling and escapes
HTML. `null`, arrays, and objects must be handled rather than silently printed.

Path lookup is innermost-first: loop binding, component prop, then the input JSON
object. Token values are available below `token`.

## Control flow

```ell
@If(business.review_count > 0)
  <p>@{business.rating} stars</p>
@Else
  <p>No public reviews yet.</p>
@/If

@For(opportunity in opportunities)
  @Paragraph @{opportunity} @/Paragraph
@/For
```

Conditions and loops execute during compilation. A loop’s binding is local to
its body. The default request limit is 10,000 total loop iterations.

## Components

Calls use named props and either a body or a semicolon:

```ell
@Button(url: call_url) Book a call @/Button
@Image(src: image_url, alt: "Preview");
```

Definitions are ELL source:

```ell
@DefineComponent(name: "Card")
  @Props
    title: string
    accent?: color = "#7357d8"
  @/Props
  @Slots
    default: required
  @/Slots
  @Template
    <section style="border-left:4px solid @{accent}">
      <h2>@{title}</h2>
      @Slot(default);
    </section>
  @/Template
@/DefineComponent
```

Supported prop types are `string`, `int`, `number`, `bool`, `url`, `email`, and
`color`. A declaration may be optional with `?`, carry a default with `=`, and
use a range such as `int(4..120)`. Calls reject missing required props, unknown
props, invalid values, undeclared slots, and the wrong body form.

Slots are declared as `required` or `optional`. A plain call body fills the
`default` slot. Named fills use `@Slot(name) ... @/Slot`; templates emit them with
`@Slot(name);`. Filled content evaluates in the call-site scope.

The installed standard library defines fifteen components in ordinary ELL:
`Paragraph`, `Heading`, `Bullets`, `Numbered`, `Item`, `Callout`, `Quote`,
`Button`, `Image`, `Divider`, `Spacer`, `Panel`, `Columns`, `Unsubscribe`, and
`Shell`.

## Tokens, named styles, CSS, and media

```ell
@DefineToken(name: "accent", value: "#7357d8");

@DefineStyle(name: "card")
  padding: 20px; color: @{token.accent};
@/DefineStyle

@Media("(max-width: 600px)")
  .stack-column { display: block !important; width: 100% !important; }
@/Media
```

A `style: "card"` argument applies a named declaration bundle. The three-layer
cascade is component defaults, named style, then explicit inline declarations.
For raw HTML, simple `.class { declarations }` rules in `<style>` blocks are
inlined into each matching class. An element’s existing inline property wins.
Non-media style blocks are then removed. Validated media rules are collected into
the shell because they cannot be represented as inline declarations.

The Example assets split palette/company tokens (`brand.ell`), named styles
(`styles.ell`), and the complete wrapper (`shell.ell`). Brand values are data,
not C++ constants.

## Includes and projects

```ell
@Include("components/card.ell");
```

Resolution tries the including document’s directory and then each `-I` directory
in order. `${ELL_LIB}` and `${EMAIL_MARKUP_BRAND}` are available in config and paths.
Canonical targets must remain under the project, explicit include roots, or
installed asset roots. Symlink escapes, non-regular files, unsupported
extensions, oversized sources, include cycles, and excess depth are errors.

Includes are parsed as separate documents. They are include-once by canonical
path, with first depth-first insertion order. The entry document applies last.
A collision between included definitions is diagnosed.

The nearest `ell.json` supports:

```json
{
  "$schema": "./schema/ell.schema.json",
  "include": ["${ELL_LIB}", "${EMAIL_MARKUP_BRAND}", "components"],
  "imports": ["${ELL_LIB}/builtins.ell", "${EMAIL_MARKUP_BRAND}/brand.ell"],
  "data": "examples/recipient.json",
  "shell": "${EMAIL_MARKUP_BRAND}/shell.ell",
  "out": "build"
}
```

`data` is a development fixture. Production callers should provide the one JSON
object using `--data-file`, `--data-stdin`, or `--data-json`. The transports are
mutually exclusive and byte-identical after parsing. The JSON root must be an
object and is limited to 1 MiB by default.

## Compilation pipeline and limits

Compilation performs lexing and recovery, include/import loading, definition and
type validation, JSON expression evaluation, component/control-flow expansion,
shell application, style cascade, class CSS inlining, media insertion,
deliverability lint, and provenance collection. Output and diagnostic writes are
deterministic. CLI output replacement is atomic.

Default safety limits are 1 MiB per source, 1 MiB JSON, 128 includes, include
depth 32, expansion depth 64, 10,000 loop iterations, 200,000 AST nodes, 2 MiB
HTML, and 100 diagnostics. Compilation accepts a cancellation token. Generated
segments retain source provenance for editor features.

Final lint rejects dangerous email elements, external stylesheets, invalid
content style blocks, and unbalanced markup. It reports missing image alt text,
insecure images, poorly supported CSS, Gmail clipping risk, and missing final
unsubscribe behavior at the appropriate severity.

## CLI and exit behavior

`ellc compile`, `check`, `lint`, `fmt`, `build`, `schema`, and `--version` are the
public commands. Exit code 0 is success, 1 is compilation failure, and 2 is usage
or I/O failure. `--json` returns structured diagnostics with codes, severity,
source ranges, related locations, and JSON paths while excluding recipient
values.

`ellc build` recursively compiles `.ell` entry files, skipping output, Git,
dependency, library, brand, and component-definition trees. It preserves
relative paths under the configured output directory and rejects collisions.

## Language server and VS Code

`ell-lsp` speaks LSP over stdio with UTF-16 positions and incremental document
updates. It supplies diagnostics, directive and component completion, hover,
definitions, symbols, folds, signatures, formatting, watched-dependency refresh,
multi-root config discovery, cancellation, and a versioned final-HTML preview
request. Stale versions are never published.

The VS Code grammar is HTML-first: ELL scopes are injected into HTML and CSS, and
`<style>` embeds CSS. The extension also uses HTML/CSS language services for
completion, hover, symbols, linked tags, CSS property/value help and local class
completion. The ELL language server remains the semantic source of truth.

Only grammar highlighting runs in an untrusted workspace. The server, filesystem
data, and preview require trust. Preview CSP disables scripts and local resource
roots; remote images require a deliberate per-preview action.

## Version boundary

ELL 1 supports only `.ell`. `.ellt`, `@Engine`, `@[...]`, and `@Name[...]` are
neither advertised nor accepted. `templates.md` records a future deferred-output
proposal; it is not an incomplete feature or reserved syntax in this version.
