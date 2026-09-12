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
- `if` / `else`, `while`, and C-style `for` loops

## Build and run

```bash
make
./clynxer examples/hello.lynx
make test
```

This is a foundation rather than a complete port. Unsupported Lynxer language
features fail with a source location instead of silently falling back to
Python.