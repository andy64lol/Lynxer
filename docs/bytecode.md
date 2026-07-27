# Bytecode Compilation (.lynxc)

Lynxer can pre-compile a `.lynx` source file into a compact binary bytecode
file with the `.lynxc` extension.  The bytecode is a serialised form of the
parsed AST, so the lexer and parser are skipped at load time — useful for
distributing code without shipping readable source, or for shaving parse
overhead on larger programs.

---

## Compiling a source file

```bash
lynxer --compile myfile.lynx      # produces myfile.lynxc in the same directory
```

The `--compile` flag (short form `-c`) reads the source, parses it, and writes
`<basename>.lynxc` next to the original file.

---

## Running a bytecode file

```bash
lynxer myfile.lynxc
```

Pass a `.lynxc` path directly — Lynxer detects the extension and skips the
lexer / parser entirely.

---

## Importing bytecode modules

`import()` accepts both `.lynx` and `.lynxc` filenames:

```c
global setup(){
    import("mylib.lynxc");   // explicit bytecode import
    import("mylib");         // auto-detects: uses mylib.lynxc if present,
                             //               otherwise mylib.lynx
}
```

**Auto-detection rule:** when you call `import("name")` (no extension),
Lynxer checks for `name.lynxc` in the same directory first.  If found, it
loads the bytecode.  If not, it falls back to `name.lynx` and then to the
built-in stdlib.

---

## Bytecode format

| Field | Details |
|-------|---------|
| Magic header | 6 bytes: `LYNXC\x00` |
| Payload | Python `pickle` stream containing `version`, `source` path, and the serialised AST node |

The bytecode format is tied to the Python version and the Lynxer AST — it is
**not** portable across major Python versions or Lynxer releases.  Always
recompile when upgrading.

---

## Notes

- Bytecode files contain the full AST, so they are roughly the same size as
  the original source for small files.
- Standard library modules (`.lynx` files in `stdlib/`) are not
  pre-compiled by default; compile them yourself if needed.
- Syntax and lexer errors are caught at compile time, not at run time.
- `rawPy` and `rawPyx` blocks are preserved verbatim inside the bytecode and
  evaluated at run time as usual.
