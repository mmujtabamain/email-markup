# Deferred templates and engine macros

## Status and version boundary

This is a proposal for the next minor language version after Email Markup 1.1.
It is not accepted by the current grammar or required by `PLAN.md`. Before
implementation, assign the feature a release version, add its grammar to
`grammar/email-markup.ebnf`, and update the negative conformance tests that
currently reject `.emt`, `@Engine`, `@[…]`, and `@Name[…]`.

The existing execution model remains the default:

```text
.em source + JSON object -> emc -> final HTML
```

Selecting an engine opts one compilation into a second, explicit output kind:

```text
.em source + trusted build data + .emt definition -> emc -> engine template
```

The output kind is determined by successful engine selection, not by scanning
the document for square brackets. A square-bracket call without an engine is an
error. Selecting an engine is valid even when the document uses no engine
macros; its output is still reported as `engine-template`.

The first implementation should ship one reference engine definition and its
conformance suite. Django is the practical first target. Jinja2, Liquid,
Handlebars, Mustache, and ERB remain candidates, but none should be advertised
until it passes its own capability, whitespace, escaping, and runtime-render
fixtures.

---

## Language boundary

Compiler expressions and deferred engine text use visibly different forms:

| Form | Owner | Meaning |
| --- | --- | --- |
| `@{ expr }` | Email Markup | Evaluate immediately |
| `@Name(…)` | Email Markup | Component or compiler construct |
| `@[payload]` | selected `.emt` | Bodyless bare engine macro |
| `@Name[…]` | selected `.emt` | Named engine macro |

Square brackets defer only their payload. An authored `@{expr}` island inside a
payload is still evaluated once by Email Markup, and `@@` emits a literal `@`.
Text produced by an interpolation is never scanned again.

```email-markup
@For[seq: @{queryset}, var: item]
  @Quote @[item.text] @/Quote
@/For
```

Here `queryset` is resolved in the call-site compile scope. To pass the literal
characters `@{queryset}` to the engine, write `@@{queryset}`.

Body arity remains syntactic, as it is for component calls:

```email-markup
@Now[];

@If[condition]
  <p>Shown later.</p>
  @Slot(else)
    <p>Fallback.</p>
  @/Slot
@/If
```

- `@Name[…];` is bodyless.
- `@Name[…] … @/Name` has a body.
- `@[…]` is always bodyless and takes no semicolon.
- Declaration metadata validates the parsed form; it never decides where a
  source body ends.
- Named bodies use the existing `@Slot(name) … @/Slot` fill syntax. `@Else`
  remains exclusive to compiler-owned `@If(…)` and is not repurposed.

A selected engine may declare a macro named `If` or `For` without ambiguity:
parentheses select the compiler construct and brackets select the engine macro.
Names that collide with ordinary components are likewise distinguished by the
opening delimiter.

---

## Engine selection and resolution

A `.emt` file defines one engine. It may contain comments,
`@DefineTemplate`, and at most one `@DefineBareTemplate` at top level. Engine
definitions cannot include files or select another engine in the first
implementation; each packaged engine is one auditable file.

An entry `.em` file may contain one top-level, bodyless selection:

```email-markup
@Engine("${EMAIL_MARKUP_LIB}/engines/django.emt");
```

Selection sources are considered in this order:

1. `--engine <file>`;
2. entry-document `@Engine("…");`;
3. the `engine` value in `em.json`.

The first available source selects the engine. If another source is also set,
all specified paths must resolve to the same canonical file or compilation
fails with the conflicting locations. `@Engine` in an import, include, shell,
or component definition is an error.

Engine paths use the existing path-variable expansion, canonicalisation,
allowed-root checks, regular-file checks, source-size limit, dependency graph,
and resolver abstraction. They do not receive a separate filesystem escape
hatch. Only `.emt` is accepted as an engine definition.

Add `engine` to `em.json`, `--engine` to `compile`, `check`, `lint`, and `build`,
and an optional in-memory engine `{path, source}` to the compile protocol. The
machine-readable result must include `output_kind: "final-html" |
"engine-template"` and, for template output, the selected canonical engine
identity. Do not infer the engine from an output filename.

---

## Shared declaration annotations

Rich annotations are a language feature, not an engine-template special case.
`@Props` in ordinary `.em` components and `@Params` in `.emt` macros must use
one declaration parser, one constraint model, and the same diagnostics:

```text
name [?] [ : type [ (minimum .. maximum) ] [comparison bound] ] [= default]
```

Ordinary component props require `: type`. Raw macro parameters may omit it as
shorthand for `: raw`; this is a context rule applied after the shared parser.

For example, all of these are valid main-language component props:

```email-markup
@Props
  size: int(1..100) = 20
  opacity?: decimal(0.0..1.0)
  retries: int >= 0
  alias?: name
  label: string(1..80)
@/Props
```

This formalizes and extends the annotation syntax already used by built-ins such
as `height: int(4..120) = 16`. It is a prerequisite shared-language change, not
work to duplicate later inside the `.emt` parser.

| Type | Ordinary component prop | Raw macro parameter | Meaning |
| --- | --- | --- | --- |
| `string` | yes | no | JSON string; a range measures Unicode scalar values |
| `int` | yes | yes | JSON integer / raw signed-integer spelling |
| `decimal` | yes | yes | JSON non-integer number / raw decimal spelling |
| `number` | yes | yes | either numeric form |
| `bool` | yes | yes | JSON boolean / raw `true` or `false` |
| `name` | yes | yes | string matching `[A-Za-z_][A-Za-z0-9_]*` |
| `url`, `email`, `color` | yes | no | existing validated string types |
| `raw` | no | yes | unparsed engine-source text |

A numeric range and a numeric comparison may both be present and are both
enforced. Ranges on `string` measure length; ranges on `url`, `email`, `color`,
`bool`, `name`, and `raw` are errors. Comparisons are numeric-only. Bounds must
be representable by the declared type, the minimum cannot exceed the maximum,
and contradictory constraints are definition errors.

For ordinary props, defaults remain Email Markup expressions and are validated
against the declaration when used; literal defaults should also be rejected at
definition time when they are provably invalid. For raw macro parameters,
defaults remain literal engine text as specified below. A default makes a value
non-required whether or not `?` is written; `?` without a default permits
absence. Unknown types or constraints invalid for a type are errors at the
declaration, not at a later call.

Implementation must replace the current regex-and-`double` declaration parsing
with a small shared parser and typed constraint representation. Preserve source
ranges for the name, type, each bound, and default. Integer validation must not
lose precision; decimal constraint comparison must be deterministic and
locale-independent. LSP completion, hover, signature help, formatting, and the
language reference must expose the same annotation grammar in `.em` and `.emt`.

---

## Raw macro parameters

Macro parameters carry engine source, not Email Markup runtime values. They are
inserted without HTML escaping, quote decoding, or engine-language parsing.
They are suitable only for author-controlled engine expressions. Recipient or
other untrusted data must not be interpolated into a raw payload.

Declarations use the existing line-table style:

```emt
@Params
  condition
  limit: int(1..100) = 20
  alias?: name
@/Params
```

The validator surface is deliberately lexical:

| Declaration | Accepted final spelling |
| --- | --- |
| `value` or `value: raw` | any text, including empty text |
| `value: int` | `[+-]?[0-9]+` |
| `value: decimal` | `[+-]?[0-9]+\.[0-9]+` |
| `value: number` | either `int` or `decimal` spelling |
| `value: bool` | exactly `true` or `false` |
| `value: name` | `[A-Za-z_][A-Za-z0-9_]*` |

`int`, `decimal`, and `number` use the shared range and comparison annotations.
Validation and comparison are locale-independent and must not silently round
through binary floating point. A validator checks the completed text; it does
not convert the parameter into a general Email Markup value.

`?` makes a parameter optional. `= text` supplies literal default text after
trimming the declaration’s outer layout; a default counts as present. For an
absent optional parameter without a default, `param.<name>` is `false` and an
attempt to expand `@{name}` is an error. For a supplied or defaulted parameter,
`param.<name>` is `true`. This avoids silently converting absence to an empty
engine expression. Defaults cannot contain compiler interpolation.

Inside a macro `@Template`, a parameter reference must be an interpolation
island whose complete expression is its declared name, such as `@{condition}`.
It cannot use member access, operators, or other Email Markup expression forms.
Presence checks are available only as `param.<name>` in compiler `@If(…)`.

Binding depends only on the declared parameter count:

| Declaration | Call binding |
| --- | --- |
| zero parameters | bracket payload must be empty |
| exactly one parameter | the entire payload binds positionally |
| two or more parameters | named `name: value` entries separated by framing commas |

For a one-parameter macro, `@[a:b]` binds the value `a:b`; the colon is not
reinterpreted. For multiple parameters, every entry must contain a name and a
colon. Unknown, duplicate, and missing required names are errors. Declaration
order does not affect named binding.

`@DefineBareTemplate` may declare zero or one parameter and may not declare
slots. A one-parameter bare macro is the normal `@[payload]` case; a
zero-parameter definition accepts only `@[]`. Named templates may declare any
bounded number of parameters.

---

## Payload framing and interpolation

The payload scanner understands Email Markup framing, interpolation islands,
and escapes. It deliberately does not understand an engine’s strings, regular
expressions, delimiters, or comments.

1. Outside `@{…}`, the first unescaped `]` closes the payload.
2. Inside `@{…}`, the Email Markup expression parser owns its balanced
   delimiters and strings; `]` and commas there cannot close or split the call.
3. Outside interpolation, `\]`, `\,`, and `\\` decode to `]`, `,`, and `\`.
   A backslash before any other character is preserved. A trailing backslash is
   an error.
4. `@@` decodes to one literal `@`, including `@@{` for literal `@{`.
5. A zero- or one-parameter payload is never comma-split.
6. A multi-parameter payload splits on unescaped commas outside interpolation,
   then each entry splits on its first colon.
7. Layout around an entry, name, and outer value is trimmed. Interior text is
   byte-preserved.

Processing is single-pass: scan and split first while retaining source ranges;
decode framing escapes and `@@`; evaluate only interpolation islands authored
in the original payload; emit their scalar spelling without HTML escaping; then
validate the completed parameter text. Strings emit their contents, booleans
emit `true` or `false`, and numbers use locale-independent canonical spelling.
`null`, arrays, and objects cannot be inserted.

Because the scanner is engine-agnostic, an engine expression containing a
framing comma or closing bracket must escape it even when that character would
be protected by engine-specific quotes. Formatter and diagnostics must preserve
and explain these escapes.

---

## Declarations, templates, and slots

```emt
@DefineBareTemplate
  @Params
    value
  @/Params
  @Template
    {{ @{value} }}
  @/Template
@/DefineBareTemplate

@DefineTemplate(name: "If")
  @Params
    condition
  @/Params
  @Slots
    default: required
    else: optional
  @/Slots
  @Template
    {% if @{condition} %}
      @Slot(default);
      @If(slot.else)
        {% else %}
        @Slot(else);
      @/If
    {% endif %}
  @/Template
@/DefineTemplate

@DefineTemplate(name: "For")
  @Params
    seq
    var: name
  @/Params
  @Slots
    default: required
  @/Slots
  @Template
    {% for @{var} in @{seq} %}
      @Slot(default);
    {% endfor %}
  @/Template
@/DefineTemplate
```

`@DefineBareTemplate` declares `@[…]`; `@DefineTemplate(name: "Name")`
declares `@Name[…]`. Names are capitalized and must not duplicate another
definition in the same engine file.

Slots use the existing declaration and fill model. A declared `default` slot
requires the body form; no `default` slot requires the bodyless `;` form. Named
slot fills must be direct children of the macro call, every fill must be
declared, and a named slot can be filled once. Non-slot children fill `default`.
Required slots must be present. Every declared slot must be referenced by the
template, and every template slot reference must be declared.

Within a macro template, ordinary Email Markup compiler control flow is allowed
only over definition-time facts: `slot.<name>` and `param.<name>`. General
compile data, component props, loops, includes, component calls, nested deferred
macro calls, and engine selection are forbidden in `.emt`. This keeps engine
definitions deterministic and prevents an engine file from becoming a second
general component library.

Template literal text is raw engine output. Parameter substitutions are raw.
Slot content remains ordinary Email Markup content: it is compiled in its
call-site scope, retains HTML escaping for its own ordinary `@{…}` expressions,
and is inserted with its provenance intact.

---

## Compilation pipeline and safety

Deferred constructs must remain typed nodes until final serialization. Do not
expand them early into undifferentiated HTML strings.

1. Resolve and validate the selected `.emt`; build its macro registry.
2. Parse `.em` files, including typed deferred-call and raw-payload nodes.
3. Evaluate ordinary Email Markup expressions, control flow, components, and
   macro slot content.
4. Apply the shell, styles, CSS inlining, media collection, and Email Markup
   lint while deferred nodes are represented by collision-proof opaque markers.
5. Serialize markers through the selected macro templates as the final
   provenance-aware transformation.

Markers must not be user-spellable, survive every intermediate transform, and
carry the macro call, parameter-island, definition, and slot expansion ranges.
If an existing transform cannot preserve them, that transform must be made
marker-aware rather than mutating `GeneratedHtml::html` directly.

Pre-serialization lint can validate the surrounding HTML but cannot prove that
arbitrary runtime engine values or branches produce safe, balanced HTML. The
result must not be described as final HTML, and final-HTML-only guarantees such
as a runtime unsubscribe link must be reported as deferred or unverifiable when
they cross a marker. Each shipped engine therefore needs fixtures that compile
the template, render representative runtime values with the real engine, and
run the normal final-HTML lint on those rendered results.

Raw parameters are trusted source code. Compiler diagnostics and JSON protocol
responses must never echo recipient values. Preview must treat engine-template
output as code: no engine execution in the language server or VS Code extension,
no remote fetch by default, the existing restrictive CSP, and a visible
“template not rendered” state.

The current compilation limits remain in force. Add bounded counts and byte
limits for engine definitions, macro definitions, parameters per macro, raw
payload size, deferred calls, marker count, and final serialized template size.
Cancellation checks are required during engine loading, payload scanning, macro
validation, marker serialization, and engine conformance tooling.

---

## Implementation sequence

Implement this as one versioned feature with independently reviewable stages:

1. **Shared annotations:** replace the current prop-declaration regex and
   floating-point constraint fields with a shared declaration parser and typed
   constraints. Add `decimal` and `name` to ordinary props; apply range,
   comparison, optional, and default semantics consistently; update the grammar,
   language reference, formatter, LSP, and component tests. This stage is useful
   independently and lands before deferred syntax.
2. **Deferred contract and model:** update the normative grammar, configuration
   schema, compile protocol, `CompilationRequest`, result output kind, AST,
   limits, and stable diagnostic-code allocation. Keep all Email Markup 1 inputs
   and output byte behavior unchanged when no engine is selected.
3. **Engine loader:** add `.emt` resolution, selection conflict diagnostics,
   definition parsing, declaration validation, a macro registry, and dependency
   tracking. Add the single packaged Django definition only after the loader’s
   negative tests pass.
4. **Calls and framing:** add bracket lexing, syntactic body arity, raw binding,
   escape decoding, one-pass interpolation, validators, presence flags, and
   precise recovery/source ranges.
5. **Rendering and provenance:** add opaque deferred nodes through shell, CSS,
   lint, and final macro serialization. Test source maps across call sites,
   parameter islands, definition text, slot bodies, and transformed HTML.
6. **CLI and protocol:** add `--engine`, config support, virtual `.emt` input,
   output-kind metadata, deterministic dependency reporting, and atomic output.
7. **LSP and editor:** add `.emt` registration, diagnostics, completion, hover,
   definition, references, semantic tokens, folds, formatting, dependency
   refresh, and highlighting that distinguishes raw payload, authored
   interpolation islands, and escapes.
8. **Integration:** add Django capability and whitespace fixtures, render them
   with the real Django engine in a separate conformance lane, then migrate one
   product path behind an explicit opt-in. Do not broaden to another engine
   until the reference integration is stable.

Release gates must cover:

- unchanged Email Markup 1 golden output with no engine selected;
- shared `.em`/`.emt` annotation parsing, invalid type/constraint combinations,
  exact integer boundaries, deterministic decimal boundaries, Unicode string
  lengths, defaults, and optional values;
- selection precedence, canonical-path agreement, allowed-root rejection, and
  virtual compile-protocol input;
- zero/one/many-parameter binding, absent/defaulted parameters, validators,
  numeric boundary behavior, every framing escape, malformed input, Unicode,
  and non-recursive interpolation;
- bodyless/body syntax, default and named slots, duplicate fills, missing
  required slots, macro/component name collisions, and compiler/deferred
  `If`/`For` disambiguation;
- cancellation and every new size/count limit;
- provenance before and after shell insertion, CSS inlining, marker
  serialization, and mapped diagnostics;
- formatter idempotence plus TextMate and semantic-token scopes for `.em` and
  `.emt`;
- secure non-executing preview behavior;
- reference-engine compile, runtime render, whitespace, escaping, and
  final-HTML lint fixtures on every supported platform.

## Explicit non-goals for the first implementation

- detecting injection or validating arbitrary engine-language expressions;
- executing an external template engine in `emc`, the LSP, or preview;
- engine-definition includes, inheritance, or package downloads;
- automatic engine detection from syntax or output extension;
- positional binding for multi-parameter macros;
- nested deferred calls inside `.emt` definitions;
- claiming cross-engine capabilities from syntax similarity alone.
