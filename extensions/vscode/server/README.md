# Bundled language servers

The CMake build stages `ell-lsp` into `<platform>-<architecture>/` here. Release
jobs package one matching directory into each platform-specific VSIX. Server
binaries are build artifacts and are intentionally ignored by Git.
