# Bytecode Compilation (.lynxc)

Lynxer can pre-compile a `.lynx` source file into a compact binary bytecode
file with the `.lynxc` extension.  The bytecode is a serialised, compressed
form of the parsed AST, so the lexer and parser are skipped at load time —
useful for distributing code without shipping readable source, or for shaving
parse overhead on larger programs.

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

## Bytecode format (v2)

| Field | Details |
|-------|---------|
| Magic header | 6 bytes: `LYNXC\x00` |
| Payload | zlib-compressed Python `pickle` stream containing `version`, `source` path, and the serialised AST node |

### What changed in v2

Two optimisations were made over the original v1 format:

1. **zlib compression** — the pickle stream is compressed at maximum level
   before being written to disk.  In practice this cuts file size by 60–80 %
   compared to an uncompressed AST of the same program.

2. **Source-text stripping** — every token in the AST previously carried a
   copy of the entire source file inside its position metadata (`ftxt`), so a
   1 KB source file would embed thousands of redundant 1 KB strings into the
   archive.  In v2 these strings are omitted from the bytecode; only line/column
   numbers and the filename are kept.  The AST executes identically; runtime
   error messages will not show the source-pointer arrow, but the error
   location (file, line, column) is still reported correctly.

### Compatibility

The bytecode format is tied to the Python version and the Lynxer AST — it is
**not** portable across major Python versions or Lynxer releases.

If you load a v1 `.lynxc` file with the current runtime you will see a clear
error asking you to recompile.  Always recompile after upgrading Lynxer.

---

## Notes

- Bytecode files are significantly smaller than the original source for any
  non-trivial program thanks to compression and source-text stripping.
- Standard library modules (`.lynx` files in `stdlib/`) are not
  pre-compiled by default; compile them yourself if needed.
- Syntax and lexer errors are caught at compile time, not at run time.
- `rawPy` and `rawPyx` blocks are preserved verbatim inside the bytecode and
  evaluated at run time as usual.
