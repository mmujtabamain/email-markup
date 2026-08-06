# ELL — the Email Layout Language

A small, typed, component-based language for authoring HTML email.

This document describes ELL as it exists today: its design, its complete syntax,
its type system, how it compiles, and the tooling around it. It is written to be
read without access to the codebase.

---

## 1. What it is

ELL is a domain-specific language whose source is a text document and whose
output is a single HTML string ready to send. An email is written the way code
is written — in a text editor, under version control, with a compiler that
refuses invalid input — rather than assembled through a form or stored as a JSON
block tree.

```
@Paragraph Hi @{business.name} team, @/Paragraph

@Paragraph
  We rebuild sites like yours to load in under two seconds.
@/Paragraph

@Button(url: "https://example.com/contact")
  See a free mockup
@/Button

@If(business.review_count)
  @Callout
    @{business.rating} stars across @{business.review_count} reviews.
  @/Callout
@/If
```

That compiles to a complete, styled, client-hardened HTML email.

---

## 2. The problem it solves

HTML email is not HTML. The clients that matter render it through engines that
are decades behind browsers — Outlook renders through Microsoft Word, which has
no flexbox, no grid, and no `position`. Gmail strips `<style>` blocks when a
message is forwarded and truncates any message past roughly 102 KB, hiding
everything after the cut. Stylesheets do not survive; every rule has to be
inlined into a `style` attribute on the element it applies to.

The practical consequences are that email markup is verbose, repetitive, and
extremely easy to get subtly wrong, and that the mistakes are invisible until
they reach a recipient's inbox — by which point the message has already been
sent to a stranger.

ELL exists so that the hardened, hard-won markup for a button or a two-column
layout is written once, in one place, and then referred to by name. Everything
after that is composition.

---

## 3. Design principles

These are the commitments the language actually makes, not aspirations.

**Arity is syntactic.** Whether a tag closes is decided by punctuation, not by a
lookup. `@Divider;` is self-closing because of the semicolon; `@Panel … @/Panel`
takes a body because there isn't one. This means a document can be parsed — and
its syntax errors reported with a line and column — before anyone knows which
components exist.

**Nothing is coerced.** A value of the wrong type is an error at save time with a
line number, never a silent conversion. `width: "abc"` against a prop declared
`int` is refused. The rule is absolute, so a type in a declaration is a promise
rather than a hint.

**Declarations have exactly one home.** A component declares its props and slots
in its own source. The editor's completions, the validator, and the renderer all
read that same declaration. There is no second list to keep in step.

**Compilation happens on save, not at send.** A template's output is determined
entirely by its source plus the definition versions it pins, and both change only
when somebody saves. This is what lets the editor's preview be the actual email
rather than an approximation of it.

**The `@` sigil is rare.** `@` is only special before `{`, `*`, `/`, another `@`,
or a capitalised name. So `hello@example.org` and `padding: 10px @ 2` need no
escaping — the overwhelmingly common case costs nothing, and the rare literal
`@Word` is written `@@Word`.

**Editing a component never rewrites an approved email.** Definitions are
versioned and templates pin what they compiled against. Moving a template
forward is an explicit action.

---

## 4. Language reference

### 4.1 The five primitives

| Form | Meaning |
| --- | --- |
| `@Name(params) … @/Name` | A component with a body |
| `@Name(params);` | A component with no body |
| `@{path}` | An expression |
| `text` | Raw HTML, passed through verbatim |
| `@* … *@` | A comment, removed at parse |

`@@` is a literal at-sign.

### 4.2 Components

Components are capitalised, which is what lets the parser distinguish
`@Testimonial` from prose without consulting a registry. They take **named
parameters only** — a positional argument would require the reader to know the
declaration order to know what they are looking at.

```
@Button(url: "https://example.com", style: "wide")
  Book a call
@/Button
```

Commas are optional and a parameter run may span as many lines as it likes.

### 4.3 Values

Quotes are the only thing separating a literal from a variable path. This
distinction is load-bearing: without it there is no way to tell a colour from a
field name.

| Written | Kind |
| --- | --- |
| `"hello"` | string |
| `42` | int |
| `1.5` | number |
| `true` / `false` | bool |
| `null` | null |
| `business.rating` | path — resolved per recipient |
| `token.accent` | path into the design-token table |

### 4.4 Expressions

`@{path}` resolves innermost-first: a loop variable, then a declared prop, then
the recipient's merge context. `@{token.name}` is resolved at compile time and
never reaches a recipient as a reference.

Expressions also work inside string literals, so a URL can carry a merge field:

```
@Button(url: "https://example.com/c/@{lead.id}") Read more @/Button
```

### 4.5 Control flow

```
@If(business.review_count)
  …
@Else
  …
@/If

@For(review in business.reviews)
  @Quote(attribution: "@{review.author}") @{review.text} @/Quote
@/For
```

A condition on a value that is already known at compile time is decided then —
the branch nobody took never reaches the email at all, rather than shipping as a
conditional the sender re-evaluates against something that cannot change.

### 4.6 Slots

`@Slot` is the one construct whose arity carries meaning. The same word does two
jobs, told apart by punctuation:

- `@Slot(name);` — the **render point**, written inside a component's template.
- `@Slot(name) … @/Slot` — the **fill point**, written at a call site.

```
@Columns
  @Slot(left)  Left-hand copy.  @/Slot
  @Slot(right) Right-hand copy. @/Slot
@/Columns
```

A component's body with no explicit slot fills its `default` slot. Slot contents
are rendered in the **call site's** scope, not the component's — so a body
mentioning `@{business.name}` means the recipient's business, not something the
component declared.

### 4.7 Defining a component

Components are written in the same language they are used in, so adding one is
authoring rather than a schema change.

```
@DefineComponent(name: "Testimonial")
  @Props
    author: string
    role?: string
    stars: int(1..5) = 5
  @/Props
  @Slots
    default: required
  @/Slots
  @Template
    @Panel(style: "testimonial")
      @Quote(attribution: "@{author}") @Slot(default); @/Quote
    @/Panel
  @/Template
@/DefineComponent
```

A definition may instead declare `@Native("name")`, which binds it to a
hand-written renderer supplied by the host rather than to an ELL template. A
component has one or the other, never both.

### 4.8 Prop declarations

`@Props` uses a small table grammar — the one deliberate exception to the five
primitives, because prop declarations *are* a table and writing a table as nested
components would be worse for everyone who has to read one.

```
name?: type(min..max) >= bound = default
```

| Element | Meaning |
| --- | --- |
| `?` | Optional |
| `type` | One of `string`, `int`, `number`, `bool`, `url`, `email`, `color` |
| `(0..5)` | Inclusive range; for `string`, a character-length range |
| `>= 0` | A comparison — `>=`, `<=`, `>`, `<` |
| `= 16` | A default, which also makes the prop optional |

### 4.9 Slot declarations

```
@Slots
  default: required
  footer: optional
@/Slots
```

### 4.10 Design tokens

```
@DefineToken(name: "accent", value: "#7c5cff")
```

Referenced as `@{token.accent}` anywhere a value can go, including inside style
bundles and string literals. Resolved at compile, so moving a brand colour is
one edit rather than a search-and-replace across the library.

### 4.11 Styles

Because mail clients drop stylesheets, **a style is a named bundle of
declarations written into a `style` attribute at compile time** — not a CSS class
that ships. This is why the parameter that applies one is called `style:` rather
than `class:`, and why a bundle body holds declarations and nothing else: an
inline style cannot express a selector, a nesting level, or a pseudo-class, so
accepting one would produce a rule that works in the preview and does nothing in
the inbox.

```
@DefineStyle(name: "quote-box")
  margin: 6px 0;
  padding: 18px 20px;
  border-left: 3px solid @{token.accent};
@/DefineStyle

@Panel(style: "quote-box card", css: "margin-top: 8px")
  …
@/Panel
```

Precedence, lowest to highest: **the element's own styles → the bundles named in
`style:`, in the order written → `css:`.** The layers are merged by property, so
a property set by two of them is emitted once carrying the winning value rather
than stacking up as duplicate declarations for the client to resolve.

`style:` and `css:` are universal — every component accepts them, and neither
needs declaring.

### 4.12 Media queries

```
@Media("(max-width: 600px)")
  .stack-column { display: block !important; width: 100% !important; }
@/Media
```

This is the one real `<style>` block an email carries, and it is emitted **in
addition to** the inlined styles rather than instead of them. Honoured by Gmail,
Apple Mail, Yahoo and Samsung Mail; ignored by Outlook desktop, which renders at
full width — where stacking was never wanted anyway. Because Gmail strips
`<style>` on forward, a media query may only ever *improve* the rendering, never
be what makes it work.

`@Media` belongs to the shell rather than to an individual email, so that a send
does not carry a different stylesheet per message.

### 4.13 Scope

| Construct | Where it may appear |
| --- | --- |
| `@DefineComponent`, `@DefineStyle`, `@DefineToken`, `@Media` | Top level only |
| `@Native`, `@Props`, `@Slots`, `@Template` | Inside `@DefineComponent` only |
| `@Else` | Inside `@If` only |
| Everything else | Anywhere |

---

## 5. The type system

Types are checked where certainty exists and nowhere else. A **literal** is
checked at parse time. A **path** cannot be — its value does not exist until a
recipient does — so it is allowed through and the declared type stands as
documentation of what the author must supply.

| Declared | Accepts | Additional check |
| --- | --- | --- |
| `string` | string | Optional length range |
| `int` | int | Range, comparison |
| `number` | int, number | Range, comparison |
| `bool` | bool | — |
| `url` | string | Needs a scheme, or a root-relative path, or a merge field |
| `email` | string | Address shape |
| `color` | string | `#rrggbb`, `rgb(…)`, `hsl(…)`, or a CSS colour name |

An `int` satisfies `number` because that is subtyping, not coercion — the value
is used exactly as written. Nothing else crosses.

Two further rules make a component's props a real interface:

- **An undeclared parameter is an error**, not a shrug. `@Button(ur1: "…")`
  rendering a button with no link is worse than refusing to save it.
- **A required prop with no value is an error**, reported against the call site.

---

## 6. Compilation

```
parse
  → resolve definitions at the versions this template pins
  → expand slots, apply defaults, validate props
  → dispatch native components to their renderers
  → resolve tokens, expand style bundles, merge the style layers
  → inline any hand-written classes
  → wrap in the shell and emit its media-query <style>
```

Every step above depends only on the source and the pinned definitions, so all of
it happens on save and none of it can surprise anyone later.

The compiled output deliberately still carries merge syntax (`{{ }}` and
`{% %}`), which is what makes it recipient-independent and therefore the artefact
worth storing. The preview and the send then render that same stored string
through the same function — so the composer is not showing an approximation of
the email, it is showing the email.

Component expansion is capped at a fixed depth, which turns a component that
contains itself into an error message with a line number instead of an infinite
recursion.

---

## 7. The standard library

Fifteen built-in components ship with the language. Each declares its props and
slots in ELL and binds to a hardened renderer through `@Native`.

| Component | Purpose |
| --- | --- |
| `@Paragraph` | Body copy |
| `@Heading` | Section heading |
| `@Bullets` / `@Numbered` | Lists, holding `@Item` children |
| `@Item` | One line of a list |
| `@Callout` | An emphasised statement |
| `@Quote` | A quotation, with optional attribution |
| `@Button` | A call-to-action, drawn as a link styled as a button |
| `@Image` | An image, with optional alt text and link |
| `@Divider` | A horizontal rule |
| `@Spacer` | Vertical space |
| `@Panel` | A background-filled section |
| `@Columns` | A two-column layout with `left` and `right` slots |
| `@Unsubscribe` | The opt-out line |
| `@Shell` | The branded card every email is wrapped in |

The markup behind these is code rather than content — it exists as hand-written
renderers because it has been fought into shape against Outlook's Word engine
and Gmail's stripper, and it is not something a template should be able to
redefine. ELL does not reimplement any of it; the declaration exists so that
prop metadata has exactly one home.

Native renderers are deliberately **not** version-pinned. A fix to the button's
markup should reach every template at once, which is the one place the pinning
guarantee stops.

---

## 8. Versioning

Definitions are stored as versioned records. When a template is saved, the
compiler reports exactly which components, styles and tokens it actually
resolved, and those versions are pinned to the template.

The consequence is that editing a shared component cannot silently rewrite emails
somebody has already reviewed and approved. The cost is equally explicit: a fixed
component reaches nothing until each template is brought forward — which is why
the system reports what has moved on since a template was built, as a first-class
question rather than an afterthought. Bringing a template forward is an action
somebody takes, never something that happens to them.

History is append-only. Restoring an old version creates a new one, so going back
appears in the history as the edit it is.

---

## 9. Validation

Three independent layers, each answering the questions it is actually qualified
to answer.

**Language errors** come from the compiler: unknown components, unclosed tags,
type violations, undeclared props, scope violations, missing required slots.
Every one carries a line, a column, and a message written to say what to do
rather than what went wrong.

**Deliverability findings** come from a linter that reads the *compiled* output,
because the questions worth asking can only be answered there — a tag opened in
one component and closed by the next is not an error, and a component that emits
a `<script>` is one no matter how innocent its source looked. Two severities, and
the split is the whole design:

- **Errors** — the markup is broken or cannot be delivered. `<script>`, an
  external stylesheet, unbalanced tags. These block the save.
- **Warnings** — it will send, but a real client will render it worse than the
  composer does. Flexbox, an image with no alt text, an `http://` asset, a
  message large enough for Gmail to clip. These never block, because an editor
  that refuses to save until every hint is cleared gets worked around rather than
  listened to.

The rules encode what actually breaks in the clients being sent to, not what a
generic HTML validator dislikes.

**CSS validity** comes from a real CSS language service. Whether `padding: 18px
20q` is valid has a correct answer that already exists, and a hand-rolled
property table would only be somewhere for the two to disagree.

---

## 10. Editor support

ELL has a full editor integration today, built on the same engine as VS Code:

- **Syntax highlighting** — a grammar covering every construct, with embedded CSS
  handed to the CSS tokenizer so a brace inside a rule is never read as markup.
- **Completions**, driven entirely by the live definition set rather than a
  hard-coded list: components with their required props pre-filled, that
  component's own props and types, slot names, style bundle names, design tokens,
  and merge fields — each offered only where it applies.
- **Hovers** showing a component's full declared interface, a bundle's
  declarations, or a token's resolved value.
- **Diagnostics** from all three validation layers, inline.
- **Snippets** for every language construct.
- **Folding, indentation, comment toggling, and bracket matching** aware of
  `@Name … @/Name` pairs.

The editor colours and reports; it does not judge. Every diagnostic originates
from the compiler, the deliverability linter, or the CSS service — never from a
second opinion living in the editor, which would eventually disagree with the
thing that refuses the save.

---

## 11. Architecture

The implementation separates cleanly into three parts.

**The language core is pure** — text in, HTML out, no I/O and no database access.
It comprises the lexer, the parser, the AST, the type checker, the style
resolution, the definition registry, and the renderer.

**One module is the persistence seam.** It builds a registry from stored
definitions at pinned versions, compiles on save, writes the pins back, and
answers what has moved on. Everything database-shaped is confined to it.

**Native renderers are a separate module**, reached through a name table. The
core never contains markup; a `@Native("cta_button")` declaration names a
function and the core hands off.

The editor integration is a self-contained set of modules — grammar, language
configuration, completion and hover providers, and CSS mirroring — depending on
the language only through a served schema describing the current definition set.

---

## 12. Summary

ELL is a complete, working language with:

- a formal grammar of five primitives and twelve keywords
- a static type system with seven types, range and comparison constraints, and no
  coercion anywhere
- user-defined components declared and used in the same language
- design tokens and a three-layer style cascade resolved at compile time
- a compile-on-save model that makes the preview byte-identical to the send
- explicit versioning and pinning, so shared edits never rewrite approved work
- fifteen built-in components with client-hardened renderers
- three layers of validation, all surfaced live in the editor
- full editor tooling: highlighting, schema-driven completions, hovers, snippets,
  folding, and inline diagnostics
