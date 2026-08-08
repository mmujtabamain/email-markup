# Bundled language servers

The CMake build stages `email-markup-lsp` and its `lib/` runtime assets into
`<platform>-<architecture>/` here. Release jobs package one matching directory
into each platform-specific VSIX. Server binaries and runtime assets are build
artifacts and are intentionally ignored by Git.
