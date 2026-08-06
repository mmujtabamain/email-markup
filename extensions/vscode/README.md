# ELL Language Support

This extension provides syntax highlighting and an LSP-backed editing
experience for `.ell` files. In a trusted workspace it launches the bundled
`ell-lsp` binary for diagnostics, completion, navigation, formatting, and a
secure final-HTML preview.

Untrusted workspaces receive grammar-only highlighting. Preview HTML runs with
scripts disabled and no local resource roots. Remote images are replaced with
inert placeholders unless **ELL: Load Remote Preview Images** is invoked for the
current panel.
