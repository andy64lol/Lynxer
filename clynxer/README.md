# clynxer

`clynxer` is the small, standalone C++ implementation of Lynxer. It is being
built beside the original Python implementation in `../lynxer`, so the two
implementations can evolve independently.

The first slice intentionally stays small:

- C++17 only; no Python runtime or third-party dependencies
- `.lynx` comments (`//` and delimiter-based `/// ... ///`), strings,
  integers, floats, booleans, and arithmetic
- required `global setup(){...}` and `global main(){...}` entry points
- typed declarations for `int`, `float`, `str`, `bool`, and `any`
- assignment and `print(...)` / `println(...)`
- arithmetic, comparison, equality, and boolean expressions
- `while`, `for`, `doWhile`, `iterate`, and `forever` loops
- `break`, `continue`, and `restart` loop controls
- `if` / `elif` / `else`, `switch` / `case` / `default`, `try` / `catch`,
  `while`, and C-style `for` loops
- word and symbolic boolean operators, bitwise operators, exponentiation,
  floor division, legacy equality warnings, and the `\e` string escape
- file-wide `func`, named `global`, and nested `local` functions with typed
  parameters, defaults, returns, and lexical call frames
- caller-supplied/multiple codeblocks, stored `codeblock` values,
  `exec(){{name}}`, and `overrideMain`
- source modules loaded with `import()`/`importAs()`, exposed through
  `global.<module>.<name>`, and optional `-> type` return annotations
- bundled package-free stdlibs: `math`, `colorlib`, and `typing`
- CLYXC compilation for Milestone 5 uses a validated source-fallback section
  so interpreter and bytecode entry points retain identical behavior

## Build and run

```bash
make
./clynxer examples/hello.lynx
make test
```

This is a foundation rather than a complete port. Unsupported Lynxer language
features fail with a source location instead of silently falling back to
Python. Native `.so` modules remain governed by the shared registration ABI
documented in `../docs/native-modules.md`; they are not reinterpreted as source.