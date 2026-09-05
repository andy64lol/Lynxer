# Bytecode Compilation (.lynxc)

Lynxer can pre-compile a `.lynx` source file into a compact binary bytecode
file with the `.lynxc` extension.  The bytecode is a binary encoding of the
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

## Bytecode format (v6)

| Field | Details |
|-------|---------|
| Magic header | 6 bytes: `LYNXC\x00` |
| Payload | zlib-compressed tag stream (see below), holding `version`, `source` path, compiler metadata, and the encoded AST |

### The tag stream

Every value in the payload starts with one tag byte.  Integers and lengths are
written as unsigned LEB128 varints, and signed integers use zig-zag encoding
first, so the values that dominate an AST stay one or two bytes wide.  Strings
are UTF-8 with a varint length prefix, and floats are little-endian IEEE 754
doubles.

| Tag | Meaning | Body |
|-----|---------|------|
| `0x00`–`0x02` | `null`, `false`, `true` | — |
| `0x03` | integer | zig-zag varint |
| `0x04` | float | 8-byte double |
| `0x05` | complex | two 8-byte doubles |
| `0x06` | string | varint length + UTF-8 bytes |
| `0x07` | bytes | varint length + raw bytes |
| `0x08`–`0x0C` | list, tuple, dict, set, frozenset | varint item count + items |
| `0x0D` | back-reference | varint index into the value table |
| `0x0E` | AST node / token | varint class id + varint attribute count + (name, value) pairs |
| `0x0F` | source position | index, line, column, filename |

The class id is an index into the table of encodable AST classes — every
`*Node` class plus `Token`, sorted by name and derived from the running
Lynxer interpreter.  Reading never imports or calls anything by name: the
decoder looks the id up in that fixed table, allocates the class with
`__new__`, and fills in the attributes.  Containers and nodes are stored in a
value table on first use and referenced afterwards, so shared nodes stay
compact and reference cycles cannot hang the reader.

Positions get their own tag rather than the generic object form because a
`Position` also carries the whole source text (`ftxt`); only `idx`, `ln`,
`col`, and `fn` are written, which is what keeps the payload small.

### What changed in v6

v6 replaced the pickle payload with the tag stream above:

1. **No pickle** — the payload is now a plain data description.  A `.lynxc`
   file can no longer name arbitrary Python objects; it can only select AST
   classes from the fixed table, so loading one cannot execute code.

Earlier versions:

2. **Native dependency manifest** (v5) — compiled bytecode records shared-library
   imports in `native_dependencies`. Bundlers use this manifest to stage the
   libraries beside the bytecode, so native modules continue to resolve inside
   a one-file executable.

3. **zlib compression** (v5) — the payload is compressed at maximum level
   before being written to disk.  In practice this cuts file size by 60–80 %
   compared to an uncompressed AST of the same program.

4. **Source-text stripping** (v2) — every token in the AST previously carried a
   copy of the entire source file inside its position metadata (`ftxt`), so a
   1 KB source file would embed thousands of redundant 1 KB strings into the
   archive.  Since v2 these strings are omitted from the bytecode; only line/column
   numbers and the filename are kept.  The AST executes identically; runtime
   error messages will not show the source-pointer arrow, but the error
   location (file, line, column) is still reported correctly.

5. **Restricted loading and size limits** (v5) — the runtime caps the file and
   decompressed payload size, and rejects malformed streams (unknown tags,
   truncated values, dangling back-references, trailing data) instead of
   failing later during execution. Do not treat `.lynxc` files from an
   untrusted source as an authorization boundary; a Lynxer program can still
   intentionally execute `rawPy` code after it has been loaded.

### Compatibility

The bytecode format is tied to the Python version and the Lynxer AST — it is
**not** portable across major Python versions or Lynxer releases.

If you load an older `.lynxc` file with the current runtime you will see a
clear error asking you to recompile. Always recompile after upgrading Lynxer.

---

## Notes

- Bytecode files are significantly smaller than the original source for any
  non-trivial program thanks to compression and source-text stripping.
- Standard library modules (`.lynx` files in `stdlib/`) are not
  pre-compiled by default; compile them yourself if needed.
- Syntax and lexer errors are caught at compile time, not at run time.
- `rawPy` and `rawPyx` blocks are preserved verbatim inside the bytecode and
  evaluated at run time as usual.

---

## Cleaning compiled bytecode

`make clean` deliberately keeps two expensive artifacts:

| Target | Removes |
|--------|---------|
| `make clean` | `__pycache__`, `*.pyc`, `build/`, `dist/`, `*.spec`, and `.lynxc` files outside `stdlib/` |
| `make cleanLynxc` | every `.lynxc` file, including the pre-compiled stdlib modules |
| `make cleanCpp` | the built C++ extension (`lynxer/*.so`) and its build tree |

Recompiling the stdlib is slow, so a plain `make clean` leaves those `.lynxc`
files in place; use `make cleanLynxc` when you want a fully source-only tree
(for example after upgrading Lynxer, since bytecode is version-locked).
