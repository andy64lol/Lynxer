# Lynxer

`lynxer` is the small, standalone C++ implementation of Lynxer. It runs `.lynx`
programs without a Python runtime and ships its standard library as native
shared libraries.

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
- bundled package-free stdlibs: `math` (including the statistics and vector
  helpers formerly in `mathPlus`), `json`, `re`, `regex`, `os`,
  `path`, `fileIO`, `csv`, `time`, `debug`, `multiprocessing`, `cli`, `js`,
  `shell`, `sys`, `random`, `image`, `lua`, `game`, `network`, `server`,
  `sound`, `sqldb`, `tui` and `text`/`typing`/`colorlib`. Native modules are
  built from either `stdlib/<name>.cpp` or the Rust crates under `rust/` into
  `stdlib/<name>.so` and wrapped by `stdlib/<name>.lynx`; see `../docs/README.md`
  for the full reference and `../docs/native-module-abi.md` for the shared C ABI
- Rust-backed modules (`game`, `image`, `json`, `lua`, `network`, `server`,
  `sound`, `sqldb`, `tui`) are skipped with a warning when `cargo` is missing, so
  a plain `make` never depends on a Rust toolchain
- `--compile <a.lynx> [more.lynx|lib.so ...] [--include <file> ...] [-o name]`
  builds one standalone ELF executable. The program's **imported modules are
  collected automatically**, and every later input is embedded too: `.lynx`
  modules and `.so` libraries become importable by name, and any other file
  becomes a data asset readable with `bundledFile(name)` and listed by
  `bundledFiles()`. Running the result needs nothing from the build tree.
  `--bundle` is an alias
- CLYXC bytecode, the stack-machine VM, `--view-bytecode`, `--benchmark-compile`,
  `--no-cache` and the `--compile` cache were removed in favour of the executable
  backend above

## Build and run

```bash
make
./lynxer examples/hello.lynx
make test
```

This is a foundation rather than a complete port. Unsupported Lynxer language
features fail with a source location. Native `.so` modules remain governed by
the shared registration ABI documented in `../docs/native-module-abi.md`; they
are not reinterpreted as source.
Every C++ stdlib backend placed in `stdlib/*.cpp` is built automatically as
the matching `stdlib/*.so` by `make`; dependency-heavy libraries remain
explicit opt-in modules rather than hidden package requirements.n package requirements.