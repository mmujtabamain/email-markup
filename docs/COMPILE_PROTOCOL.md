# JSON compilation protocol

`emc compile --request-stdin` compiles a complete virtual Email Markup source
tree without reading source, recipient data, imports, or the shell from disk.
The process reads one UTF-8 JSON object from standard input and writes one JSON
object to standard output.

The protocol identifier is `email-markup.compile`; its current integer version
is `1`. Protocol input is limited to 1 MiB. Existing compiler limits still apply
to every source, JSON value, include, expansion, loop, AST, output, and diagnostic.

```json
{
  "protocol": "email-markup.compile",
  "version": 1,
  "entry_path": "/templates/message.em",
  "source": "@Paragraph Hello @{business.name}. @/Paragraph",
  "files": [{ "path": "/library/builtins.em", "source": "..." }],
  "include_directories": ["/library"],
  "imports": ["/library/builtins.em"],
  "shell": { "path": "/shell.em", "source": "..." },
  "engine": { "path": "/library/engines/django.emt", "source": "..." },
  "output_context": "html",
  "recipient": {
    "lead": {},
    "business": { "name": "Example" },
    "rep": {},
    "unsubscribe_url": "https://example.test/unsubscribe"
  }
}
```

All virtual paths are absolute, normalized POSIX-style paths. Email Markup
source paths end in `.em`; the optional engine source ends in `.emt`.
`output_context` is `html` by default or `subject` for text-only header-safe
output. Equivalent duplicate paths, invalid UTF-8, attempts to traverse
above `/`, and files beyond compiler limits are rejected. Relative nested
`@Include` paths resolve first beside the including file and then through
`include_directories`; imports and the shell use the same resolver.

The response always identifies the protocol and compiler version:

```json
{
  "protocol": "email-markup.compile",
  "version": 1,
  "compiler_version": "<release version>",
  "success": true,
  "html": "<!doctype html>...",
  "output_kind": "engine-template",
  "target": {
    "name": "django",
    "engine": "/library/engines/django.emt"
  },
  "emir": { "format": "email-markup-ir", "version": 1 },
  "dependencies": [
    "/library/builtins.em",
    "/shell.em",
    "/templates/message.em"
  ],
  "diagnostics": []
}
```

`output_kind` is always present and is either `final-html` or
`engine-template`. `target` and canonical `emir` are present only for a
successful engine-template compilation. The `html` field then contains emitted
target source for compatibility with protocol v1; hosts that persist or hash
the IR use the `emir` object instead of interpreting it.

Exit code `0` means compilation succeeded. Exit code `1` means the request was
valid but compiler diagnostics prevented output. Exit code `2` means the JSON
envelope, protocol version, virtual files, or recipient object was invalid. An
exit-2 response uses the diagnostic code `EMPROTO` and never emits partial HTML.
