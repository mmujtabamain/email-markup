# ELL examples

These ten unbranded email examples each demonstrate one ELL feature. Every
folder contains a `message.ell` source file, a `data.json` fixture, an
`ell.json` project configuration, and the generated `message.html`.

| Example | Feature |
| --- | --- |
| `01-interpolation` | Escaped JSON value interpolation |
| `02-conditionals` | `@If` and `@Else` branches |
| `03-loops` | Array iteration with `@For` |
| `04-expressions` | Arithmetic and comparison expressions |
| `05-typed-props` | A component with typed and defaulted props |
| `06-named-slots` | A component with two named slots |
| `07-tokens` | Reusable design tokens |
| `08-includes` | A component loaded from another ELL file |
| `09-css-inlining` | `@DefineStyle` inlined into component HTML |
| `10-responsive-media` | Preserved email media queries |

Regenerate every HTML file from the repository root:

```bash
./examples/compile.sh
```

On Windows:

```bat
examples\compile.bat
```

Pass `release` as the first argument to either script for a release build. Set
`ELLC` to use an existing compiler binary and skip the compiler build.
