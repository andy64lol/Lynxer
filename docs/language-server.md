# Language server

Lynxer includes a small Language Server Protocol process for editor support. It
uses Python's standard library and communicates over stdio:

```bash
LYNXER_EXECUTABLE="/path/to/lynxer" python3 lynxer/lsp.py
```

Set `LYNXER_EXECUTABLE` to the built Lynxer executable. Without it, the server
prefers `lynxer/lynxer` next to the script and then falls back to `PATH`.
Configure your editor to launch
`python3 /path/to/Lynxer/lynxer/lsp.py` for `.lynx` files and use the project
directory as the workspace root.

The server provides:

- live diagnostics from `lynxer --lint`, including static warnings and errors;
- completion for language keywords, types, built-ins, and declarations in open
  files;
- hover hints with declaration signatures and relevant Lynxer documentation;
- go-to-definition, references, document symbols, and workspace symbols across
  open files.

The first implementation indexes open documents. It does not yet provide
workspace-wide indexing for unopened files, rename, formatting, or semantic
completion of imported module members. Diagnostics share `--lint`'s
conservative type inference, so unresolved external types are left to runtime
checks. Set `LYNXER_EXECUTABLE` in the editor process if the binary is not
named `lynxer` on `PATH`.
