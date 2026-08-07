# Deferred templates and engine macros

## Status

This document records a possible later extension to Email Markup. It is **not part of the
first standalone compiler release**, its syntax is not accepted by the initial
`emc`, and none of it is required by the implementation plan in `PLAN.md`.

The initial compiler accepts `.em` plus JSON data and produces final HTML. This
extension exists for a future need: compiling a `.em` document into a template
for another engine so recipient data can be filled after Email Markup compilation.

Keeping this design here prevents the first release from carrying two execution
models while preserving the decisions already made about future macro syntax.

---

## Proposed boundary

Compiler expressions and deferred engine text use visibly different forms:

| Form | Owner | Meaning |
| --- | --- | --- |
| `@{ expr }` | Email Markup compiler | Evaluate immediately from props, loop variables, tokens and compile data |
| `@Name( … )` | Email Markup compiler | Component or compiler construct |
| `@[ payload ]` | selected engine definition | Bare deferred macro |
| `@Name[ … ]` | selected engine definition | Named deferred macro |

With no square-bracket forms, compilation produces final HTML. Once this
extension exists, a document containing square-bracket forms produces an HTML
template for the selected engine instead.

Square brackets defer the surrounding payload, but they do not hide explicit
Email Markup interpolation. Inside `[…]`, `@{expr}` is still evaluated by the compiler
and `@@` emits a literal `@`.

```email-markup
@For[var: rev, seq: @{something}]
  @Quote @[rev.text] @/Quote
@/For
```

Here `something` is resolved in the call-site compile scope before the selected
engine's `For` template is expanded. If it is missing, the diagnostic points to
that expression. To pass the literal characters `@{something}` to the engine,
write `@@{something}`.

Interpolation is single-pass. Text produced by `@{expr}` is never scanned again,
so a data string containing `@{other}` cannot trigger another evaluation.

---

## Engine definition files

A future `.emt` file declares the macros supported by one engine. It contains
only comments, `@DefineTemplate`, and `@DefineBareTemplate` at top level. It
cannot include other files or select an engine.

The entry `.em` document may select one engine:

```email-markup
@Engine("${EMAIL_MARKUP_LIB}/engines/django.emt");
```

Proposed precedence is:

1. `@Engine("…")` in the entry document.
2. `--engine <file>` when the source declares none.
3. The project configuration default.

Conflicting selections are errors. Engine portability is capability-based:
compilation fails when a document calls a macro the selected `.emt` does not
declare. An engine never claims a construct it cannot faithfully represent.

---

## Raw-by-default parameters

Macro parameters are untyped and unvalidated unless their declaration opts into
a validator. After explicit `@{…}` interpolation and framing escape decoding,
the resulting text is inserted into the macro template unchanged. It is not
HTML-escaped, string-unescaped, or parsed as an engine expression.

Macro parameters may appear only as whole `@{name}` placeholders in a macro
`@Template`. They are not member-access roots or general Email Markup expression
operands.

The optional validator surface is deliberately small:

| Declaration | Check |
| --- | --- |
| `value` or `value: raw` | none |
| `value: int` | final text is a signed integer |
| `value: decimal` | final text is a signed decimal containing a decimal point |
| `value: number` | final text is an integer or decimal |
| `value: bool` | final text is `true` or `false` |
| `value: name` | final text matches `[A-Za-z_][A-Za-z0-9_]*` |

`int`, `decimal`, and `number` may carry range/comparison constraints such as
`size: int(1..100) = 20`. A validator checks the final interpolated spelling but
does not turn the entire raw parameter into a runtime Email Markup value. `?` makes a
parameter optional and `= value` provides a default; neither implies a type.

Binding is determined only by declared parameter count:

| Declaration | Binding |
| --- | --- |
| zero parameters | bracket payload must be empty |
| exactly one parameter | entire payload binds positionally |
| two or more parameters | `name: value` entries separated by framing commas |

For a one-parameter macro, `@[a:b]` is the value `a:b`; the colon is never
reinterpreted as a parameter name. For multiple parameters, unknown, duplicate,
and missing names are errors.

Raw macro parameters are trusted engine source, not a safe channel for
untrusted runtime data. Supporting them does not make the compiler capable of
detecting injection in an engine language it does not parse.

---

## Framing and interpolation

The payload scanner understands Email Markup framing, `@{…}`, and `@@`; it does not try to
understand an engine's quotes, regular expressions, delimiters, or comments.

1. Outside `@{…}`, the first unescaped `]` closes the payload.
2. Inside `@{…}`, the Email Markup expression parser owns balanced delimiters and strings;
   `]` and commas there cannot close or split the macro call.
3. `\]` is a literal `]`, `\,` is a literal comma in a multi-parameter value,
   and `\\` is a literal backslash.
4. `@@` is a literal `@`, including `@@{` for literal `@{`.
5. A one-parameter payload is never split.
6. A multi-parameter payload splits on unescaped commas outside interpolation
   islands, then each entry splits on its first colon.
7. Layout around an entry, name, and outer value is trimmed. Interior text is
   preserved.

After framing and `@@` decoding and one evaluation of authored `@{…}` islands,
the parameter is inserted unchanged. Strings emit their contents, booleans emit
`true`/`false`, and numbers use locale-independent spelling. `null`, arrays, and
objects are not implicitly converted into engine source.

Syntax highlighting must visibly distinguish raw payload text, compiler-owned
`@{…}` islands, and `@@` escapes.

---

## Declarations, slots, and structural markers

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
    cond
  @/Params
  @Slots
    default: required
    else: optional
  @/Slots
  @Template
    {% if @{cond} %}
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
    var: name
    seq
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

`@DefineBareTemplate` declares `@[…]`, has one raw parameter, and has no body.
`@DefineTemplate(name: "Name")` declares `@Name[…]`.

`@Slots` alone defines body arity. A `default` slot means the macro takes a body;
without it the macro is void. Every declared slot must be referenced by the
template, and every referenced slot must be declared.

`slot.<name>` is a compile-time boolean indicating whether the call filled that
slot. `@Else` remains a compiler-owned structural marker: in a construct with an
`else` slot it routes the remainder of the body into that slot, and elsewhere it
is an error.

---

## Future implementation boundary

This extension should be added only after the final-HTML compiler is stable. It
would require, as one coherent later project:

- `.emt` parsing and validation;
- `@Engine`, `@[...]`, and `@Name[...]` grammar;
- raw payload framing and single-pass interpolation;
- engine capability schemas and conformance fixtures;
- provenance through macro call sites and definitions;
- CLI engine selection and project configuration;
- LSP completion, hover, definition, semantic tokens, and formatting for both
  `.em` and `.emt`;
- editor syntax scopes for raw payloads, `@{…}`, and `@@`;
- secured preview behavior for output that still contains deferred engine code;
- packaging of engine definitions;
- migration and parity tests for any product adopting the extension.

Initial candidate engines remain Django, Jinja2, Liquid, Handlebars, Mustache,
and ERB, but each ships only after passing its own capability and whitespace
suite. Mustache, for example, must not claim an aliased-loop capability it cannot
express.

The extension's tests must cover zero/one/many-parameter binding, validators,
all framing escapes, `@@`, `@{…}` inside payloads, non-recursive interpolation,
Unicode, malformed input, cancellation, output provenance, engine capability
failures, and syntax-highlighting scopes.
