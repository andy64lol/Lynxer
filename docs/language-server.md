# Language server

Lynxer includes a small Language Server Protocol process for editor support. It
uses Python's standard library and communicates over stdio:

```bash
LYNXER_EXECUTABLE="/path/to/lynxer" python3 lynxer/lsp.py
```

Set `LYNXER_EXECUTABLE` to the built Lynxer executable. Without it, the server
prefers `lynxer/lynxer` under `LYNXER_ROOT` and then falls back to `PATH`.
`LYNXER_ROOT` defaults to the directory above `lsp.py`.

The Zed extension in `editors/zed` supplies syntax highlighting and registers
the server as `lynxer-lsp`; it launches a bundled copy of `lsp.py`
automatically and sets `LYNXER_ROOT` to the workspace. Other editors can launch
`python3 /path/to/Lynxer/lynxer/lsp.py` for `.lynx` files with the project
directory as the workspace root. See
[editors/zed/README.md](../editors/zed/README.md) for Zed-specific overrides.

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
