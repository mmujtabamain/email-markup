# Email Markup Language Support

This extension provides syntax highlighting and an LSP-backed editing
experience for `.em` files. In a trusted workspace it launches the bundled
`email-markup-lsp` binary for diagnostics, completion, navigation, formatting, and a
secure final-HTML preview.

Completions are context-sensitive: `@` enters Email Markup constructs, `<` enters HTML,
and CSS features activate in `<style>`, inline `style` attributes,
`@DefineStyle`, and `@Media`. HTML and CSS requests are forwarded through VS
Code so installed providers such as Emmet and Tailwind CSS IntelliSense can
contribute while retaining their user configuration. Providers that explicitly
support only on-disk `file:` documents may not participate in virtual embedded
documents.

Untrusted workspaces receive grammar-only highlighting. Preview HTML runs with
scripts disabled and no local resource roots. Remote images are replaced with
inert placeholders unless **Email Markup: Load Remote Preview Images** is invoked for the
current panel.

Literal `@Image` URLs are size-checked in a separate cancellable background pass,
so compiler diagnostics never wait on network requests. The warning threshold is
configured with `email-markup.images.base64WarningBytes`; use `embed: false` when
an image should remain remote.
