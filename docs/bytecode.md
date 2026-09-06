# Bytecode Compilation (.lynxc)

Lynxer can pre-compile a `.lynx` source file into a compact binary bytecode
file with the `.lynxc` extension.  The bytecode is a compressed stack-machine
instruction stream, so the lexer and parser are skipped at load time — useful
for distributing code without shipping readable source, or for shaving parse
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

## Bytecode format (v8)

| Field | Details |
|-------|---------|
| Magic header | 6 bytes: `LYNXC\x00` |
| Payload | zlib-compressed metadata plus a postfix instruction stream |

### Metadata and instruction stream

The metadata dictionary uses a small tagged-value encoding.  It contains the
version, source/cache information, compiler statistics, native dependency
manifest, and the compiled instruction stream as bytes.  It does not contain
pickled Python objects or an AST object graph.

Instructions are executed by a postfix stack machine while loading the
program. Constants push values onto the stack; `BUILD_*` instructions pop
their operands and push a container, source position, or AST node. The final
stack value must be one `ProgramNode`, which is then executed by the Lynxer
runtime.

| Opcode | Meaning | Body |
|-----|---------|------|
| `0x20`–`0x22` | push `null`, `false`, `true` | — |
| `0x23` | push integer | zig-zag varint |
| `0x24` | push float | 8-byte little-endian double |
| `0x25` | push complex | two doubles |
| `0x26` | push string | varint length + UTF-8 bytes |
| `0x27` | push bytes | varint length + raw bytes |
| `0x28`–`0x2C` | build list, tuple, dict, set, frozenset | varint item count |
| `0x2D` | build source position | pops index, line, column, filename |
| `0x2E` | build AST node/token | class id + attribute count |

The class id is an index into the fixed table of encodable AST classes — every
`*Node` class plus `Token`, sorted by name and derived from the running
Lynxer interpreter.  Loading never imports or calls a class by name: the
instruction reader looks the id up in that fixed table, allocates the class
with `__new__`, and fills in its attributes.  A `Position` omits its original
source text and stores only its location.

### What changed in v8

v8 keeps the instruction stream format and bumps the compatibility version for
enum declarations, ownership metadata, and switch pattern nodes. A compiler
never loads an older payload as if it had the new semantics; stale files must
be recompiled.

The earlier v7 changes were:

1. **Actual bytecode** — the program is written as opcodes and operands for a
   stack machine instead of as a pickled or generically serialized AST.
2. **No pickle** — a `.lynxc` file cannot request arbitrary Python imports or
   execute pickle reducers while it is loaded.

3. **Native dependency manifest** — compiled bytecode records shared-library
   imports in `native_dependencies`. Bundlers use this manifest to stage the
   libraries beside the bytecode, so native modules continue to resolve inside
   a one-file executable.

4. **zlib compression** — the payload is compressed at maximum level
   before being written to disk.  In practice this cuts file size by 60–80 %
   compared to an uncompressed AST of the same program.

5. **Source-text stripping** — positions store only line, column, and filename
   data, not copies of the source text.

6. **Restricted loading and size limits** — malformed streams are rejected
   before execution. Do not treat `.lynxc` files from an untrusted source as an
   authorization boundary; a Lynxer program can still intentionally execute
   `rawPy` code after it has been loaded.

### Compatibility

The bytecode format is tied to the Python version and the Lynxer instruction
set/AST class table — it is **not** portable across major Python versions or
Lynxer releases.

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
