# ELL 1 pre-release language review

The implemented grammar and tooling were reviewed at the 1.0 boundary. This
record captures decisions that would otherwise be easy to mistake for unfinished
work.

## Decisions retained

- Capitalized directive and component names remain the lexical boundary after
  `@`; props, slots, loop bindings, and JSON paths remain lower-case identifiers.
  This keeps ordinary email addresses and prose free of escapes.
- Body arity remains syntactic: `;` is bodyless and a matching `@/Name` closes a
  body. Component metadata never changes how the parser groups source.
- `@Props` and `@Slots` retain their line-table grammar. These declarations are
  tables in practice, and nested directive syntax would add punctuation without
  adding type safety or recovery value.
- Named props remain mandatory. Positional arguments would make a call depend on
  declaration order and make refactors unsafe.
- Missing data, wrong types, invalid values, unknown props/slots, and unsupported
  deferred syntax remain errors. There is no implicit string or empty-value
  coercion.
- Formatting remains conservative and idempotent. It normalizes line endings and
  directive indentation without rewriting raw HTML, CSS, or author prose.
- Diagnostic codes remain grouped by compiler area and machine output retains
  source ranges, related locations, and JSON paths without recipient values.

## Implementation choices

The measured latency and memory baseline does not justify arena replacement,
incremental parsing, or identifier interning for 1.0. Full immutable snapshots
remain the simpler ownership model. Gmail clipping is linted from final bytes;
there is no minifier without evidence that a tree-aware implementation improves
real delivery safely.

CSS class inlining deliberately supports simple class selectors and comma lists,
with stylesheet source order followed by explicit inline declarations. Media
rules remain in the shell. Rich browser-only selector behavior is not silently
promised by the email compiler, even though editor CSS services can explain and
highlight general CSS while authoring.

## Rejected from ELL 1

`.ellt`, `@Engine`, square-bracket calls, host-native component renderers,
definition/database pinning, and deferred template-engine output are outside the
version. Negative conformance tests enforce the syntax boundary. `templates.md`
is a future proposal and must receive a separate versioned review before any part
of it enters the grammar or editor capabilities.
