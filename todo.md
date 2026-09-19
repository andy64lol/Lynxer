# Clynxer rebuild from scratch

Clynxer is the standalone C++ implementation being rebuilt beside the original
Python Lynxer. The Python implementation is the behavior reference; it is not a
runtime dependency and its internals are not copied into Clynxer.

## Next up — compiler pivot and stdlib consolidation

Planned order of work, newest direction first.

- [x] Migrate every implemented third-party backend to Rust behind the same C ABI.
  `json` is now `serde_json` (+`preserve_order`, byte-identical output),
  `network` is `ureq` (rustls) + `tungstenite`, and `server` is `axum` + `tokio`;
  `image` uses the Rust `image` crate and `lua` uses vendored Lua through
  `mlua` — removing nlohmann/json, cpp-httplib, Crow, Boost and the system
  OpenSSL dependency. All six are self-contained `cdylib`s under `rust/` that
  export `lynxer_module_init_v1` and their ops directly (no C++ shim, no
  `--whole-archive`); `rust/abi` (`clynxer_abi`) holds the shared FFI helpers.
  CMake, `cmake/FetchDeps.cmake`, `third_party/` and the `make cmake` /
  `clynxerDeps` / `cmake-modules` targets are gone. `stdlib/network.cpp`,
  `stdlib/server.cpp` and `stdlib/json.cpp` are deleted. Still C++ and not
  third-party: `native_json.hpp`, `native_regex.hpp`. The optional `image`
  (`image` crate) and `lua` (vendored `mlua`) backends now use the same Rust
  `cdylib` + C ABI path. `tui` remains intentionally unsupported and is tracked
  below. Docs:
  `docs/install.md`, `docs/README.md`, `docs/stdlib/{json,network,server}.md`.
- [x] Add the `game` stdlib module: `stdlib/game.lynx` wraps `stdlib/game.so`,
  a Rust + macroquad backend (`rust/game`, a `cdylib` exporting
  `lynxer_module_init_v1`/`lynxer_module_attach_v1` directly) exposed through
  the native-module C ABI. An example clicker lives in
  `examples/game_clicker.lynx`. Covers the window, draw loop with `setUpdateCallback` /
  `setDrawCallback`, shapes, text, input, sprites, sprite lists, textures,
  camera and grid helpers. Two additive native-module ABI extensions make this
  possible: the packed `cdecl:<ret>(...)` signature, and the optional
  `lynxer_module_attach_v1` host API that lets a module call a Lynxer function
  by name. `CLYNXER_GAME_HEADLESS=1` runs the module without a display; the
  `examples/stdlib_game.lynx` fixture exercises it in `make test`. Deferred:
  sound, scenes, tilemaps, physics, shape batches, animated sprites. Docs:
  `docs/stdlib/game.md`, `docs/native-module-abi.md`.
- [x] Merge `mathPlus` into `math`: the statistics/vector helpers (`median`,
  `std`, `variance`, `percentile`, `corrcoef`, `dot`, `linspace`, `cumsum`,
  `diff`, `clip`, `normalize`) now live in `stdlib/math.cpp` + `math.lynx`, and
  `maxInt()`/`minInt()` are exposed on the `math` namespace. `stdlib/mathPlus.*`,
  its fixture and its docs page are gone; coverage moved to
  `examples/stdlib_math.lynx`.
- [x] Add a GitHub Actions workflow that builds Clynxer and runs
  `make testCLynxer` on push and pull request
  (`.github/workflows/buildCLynxer.yml`), plus compile-and-run smoke checks for a
  plain program and for a program that imports stdlib modules.
- [x] Remove the Clynxer bytecode stack: `bytecode.*`, `vm.*`, `compiler.*`, the
  `CLYXC` container, `.lynxc` execution, `--view-bytecode`,
  `--benchmark-compile`, `--no-cache`, `--no-opt`, and the per-AST-node
  `compile(ProgramEmitter&)` methods are gone. `--compile` produces an ELF
  executable (the former `--bundle`); `--bundle` remains an alias, and a
  `.lynxc` argument now reports that bytecode is no longer supported.
- [x] Make compiled executables support module imports: the payload carries the
  program source, every transitively imported `.lynx` module source, and every
  imported native `.so`. Native libraries are materialized into a temporary
  directory at startup for `dlopen` and removed at exit, so the executable is
  self-contained and needs nothing from the build tree. Imports, stdlibs and
  every language feature behave exactly as in an interpreted run — including the
  features the old bytecode compiler rejected (field access, methods, codeblocks,
  switch patterns, local functions, imports).
- [x] Let `--compile` bundle more than one input file into a single executable:
  the first `.lynx` file is the program, and further `.lynx`/`.so` inputs are
  embedded and resolvable by `import` even when they live outside the module
  search path. `-o`/`--name` sets the output, and a trailing bare argument still
  works as the output name. Covered by the `bundle_app`/`bundle_extras` check in
  `make test`.
- [x] Add `--include <file>` for including further files in a compiled
  executable. Modules the program imports are still collected automatically;
  `--include` additionally embeds modules, native libraries, and data files of
  any other kind. Data files are materialized into the executable's private
  temporary directory at startup and exposed through the new `bundledFile(name)`
  and `bundledFiles()` builtins. Covered by the `bundle_assets` check in
  `make test`.

## Rebuild rules

- Keep Clynxer under `clynxer/`, separate from `lynxer/`.
- Use C++17 and the standard library only unless a future milestone explicitly
  adds a native dependency.
- Build one small vertical slice at a time: lexer, parser, runtime, CLI,
  fixture, and a passing `make test`.
- Prefer explicit source-located errors over partial support or Python fallback.
- Add a focused `.lynx` fixture for every user-visible language feature.
- Keep the original Lynxer runnable so behavior can be compared during the
  rewrite.

## Milestone 0 — small executable

- [x] Create a standalone C++17 executable and Makefile.
- [x] Read and run a `.lynx` source file.
- [x] Add `--help` and `--version`.
- [x] Add a source-located error path.

## Milestone 1 — language core

- [x] Lex comments, identifiers, numbers, strings, operators, and punctuation.
- [x] Support `/// ... ///` multiline comments as a delimiter without
  consuming comment-like text inside strings.
- [x] Parse `global setup(){}` and `global main(){}`.
- [x] Enforce both required global entry points.
- [x] Implement typed variables, assignment, and shared setup/main state.
- [x] Implement literals, arithmetic, comparisons, equality, and booleans.
- [x] Implement `print(...)` and `println(...)`.

## Milestone 2 — control flow

- [x] Implement `if` / `else`.
- [x] Implement `while`.
- [x] Implement `for`, including the implicit `i = i + 1` update.
- [x] Implement `doWhile`, including the no-condition form.
- [x] Implement `iterate(count)`.
- [x] Implement `forever()`.
- [x] Implement `break`, `continue`, and `restart`.
- [x] Test nested loops and loop-control behavior with bounded fixtures.

## Milestone 3 — runtime model

- [x] Replace the initial value variant with an extended value model:
  list, tuple, sentinel, and object values with identity comparison, plus
  Python-style number formatting.
- [x] Add list and tuple literal syntax and the list/tuple built-in families
  with Lynxer value semantics (new-list results, negative indices,
  string-based membership).
- [x] Add `inter"..."` interpolation in `print`, `println`, `input`, and
  `inputln` arguments, including `\{`, `\}`, and `\\` escapes and the
  empty/missing/stray-brace syntax errors.
- [x] Add the remaining documented scalar types: `num`, `char`, `numBool`,
  `bit`, `byte`, `int8`..`uint64`, `float32`/`float64`, and the `codeblock`
  type.
- [x] Add typed element literals for sequences: `[int 1, int 2]` and
  `(int 10, int 20)`.
- [x] Add lexical scopes and match Lynxer declaration lifetime rules.
- [x] Add `const` declarations whose reassignment is a runtime error.
- [x] Add structs, classes, enums, vargroups, and pattern matching.

## Milestone 4 — operators, statements, and errors

- [x] Implement bitwise operators `&`, `|`, `^`, `!&`, `!^`, `!|`, `~`, `<<`,
  `>>`, exponent `**`, and integer division `/%`.
- [x] Implement the NAND/NOR logic forms `!&&` and `!||`.
- [x] Implement the word operators `and`, `or`, and `not`.
- [x] Accept the legacy equality forms `is` and `not is` with deprecation
  warnings.
- [x] Implement `switch` / `case` / `default` and `elif`.
- [x] Support the `\e` string escape.
- [x] Implement `try` / `catch` with source-located error values.

## Milestone 5 — functions and code blocks

- [x] Support file-wide `func` declarations and named `global` functions
  beyond `setup`/`main`.
- [x] Add typed and default parameters and return values.
- [x] Add local functions.
- [x] Add caller-supplied code blocks and multiple code blocks.
- [x] Add named `codeblock` values and `exec(){{name}}`.
- [x] Implement the `overrideMain` entry-point override.
- [x] Implement classes and class methods.

## Milestone 6 — module system and standard library

- [x] Implement `import` and `importAs` with nested/path-based imports.
- [x] Support calling module functions and accessing module globals.
- [x] Serve `--list-stdlibs` from Clynxer's own stdlib directory. The command
  resolves the directory next to the running executable, so it works from any
  current working directory, and lists only regular `.lynx` module files.
- [x] Port standard-library modules one small module at a time. Clynxer ships
  every feasible package-free module from `lynxer/stdlib/`; `mathPlus` is merged
  into `math`, `http`/`net` are replaced by `network`, and the unsupported
  modules are documented in `clynxer/docs/limitations.md`.
- [x] Keep optional `-> type` return annotations available while stdlibs use
  the shared registration ABI.
- [x] Implement the first Rust-backed third-party set: `game`, `image`, `json`,
  `lua`, `network`, and `server` as self-contained `cdylib` modules exporting
  the shared ABI directly. Their Rust implementations and Clynxer wrappers are
  complete; future work below is about portability and parity, not initial
  Rust ports.
- [ ] Implement the remaining Python third-party stdlib alternatives as Rust
  `cdylib` backends behind the shared C ABI. The Lynxer-facing
  `stdlib/<name>.lynx` module is the wrapper; its `setup()` imports
  `stdlib/<name>.so`, and the Rust backend registers the native operations that
  the wrapper calls. Use the Python implementation and package behavior as the
  reference, not as a Clynxer runtime dependency:
  - [ ] `sound`: replace the Python Arcade audio backend with Rust
    `rodio`/`cpal`, adding `symphonia` where decoding is needed. Preserve
    loading, streaming, play/loop/stop, pause/resume, volume, duration, and
    release handles.
  - [ ] `sqldb`: replace Python `sqlite3` with Rust `rusqlite`/`libsqlite3-sys`.
    Preserve execute, scripts, parameterized queries, JSON row results, scalar
    values, last-insert IDs, table inspection, and cleanup.
  - [ ] `tui`: replace Python Rich with Rust `ratatui`/`crossterm` or a
    deliberately smaller terminal backend. Preserve styled text, Markdown,
    panels, tables, prompts, progress, and recorded console output, with a
    headless rendering mode for fixtures.
- [ ] Decide whether `tkinter`, `tkinterPlus`, and `turtle` belong in this Rust
  backend phase. If they do, define Rust GUI/drawing candidates and the
  wrapper/ABI contracts first; otherwise document them as intentionally
  deferred rather than implying they are already ported.
- [ ] For every new Rust backend, add the crate to the Rust workspace, export
  `lynxer_module_init_v1` through `clynxer_abi`, add the module to the Makefile,
  create the matching `stdlib/<name>.lynx` forwarding wrapper, document the
  API, and add a sibling expected-output fixture.
- [ ] Freeze each module's operation names, signatures, handle ownership,
  string lifetime, error sentinels, callbacks, interruption behavior, and
  cleanup before introducing a second backend. Keep third-party calls behind
  backend-local adapters so the Lynxer wrapper never depends on crate-specific
  types or APIs.
- [ ] Add Rust-backend fixtures for success, malformed input, invalid handles,
  missing files, timeouts, cleanup, optional-dependency failures, and the
  compiled/bundled executable path. Compare the wrapper's behavior with the
  Python reference where the API is intended to remain compatible.
- [ ] Only extend the ABI when a real module cannot be expressed with its
  scalar/string/handle conventions; every additive ABI change needs C and Rust
  examples, compatibility coverage, and documentation.

## Milestone 7 — native APIs

- [x] Port built-ins with explicit unsupported-feature errors for everything
  not yet implemented (I/O, conversions, introspection, sequences, lists,
  tuples, `assert`, `sleep`, forever helpers).
- [x] Port the native-memory family: allocator, typed accessors, endian
  operations, `memoryTypeSize`/`memoryTypeAlignment`, and `sizeOf`.
- [x] Port the named Linux syscall wrappers through a host syscall table with
  errno surfaced as source-located errors.
- [ ] Port the managed filesystem, process, networking, async, sound, FFI,
  and native-thread APIs.

## Milestone 8 — compiler, bytecode, and CLI surface

> Superseded by the compiler pivot above: bytecode, the `CLYXC` container and
> `--view-bytecode` are being removed, and `--compile` now produces an ELF
> executable. The checked items below record what was built before the pivot.

- [x] CLI parity for run/help/version/lint/list-stdlibs/easter egg via
  `shell.cpp`, with the version and message templates in `clynxer.config`.
- [x] Fail explicitly for bundle, ast, format, benchmark, validate, install.
- [x] Define a stable Clynxer bytecode: opcode set, string table, constant
  pool, per-section trap tables, and eager load validation (jump targets,
  stack depths, table ranges) behind the `CLYXC` container (format v1,
  uncompressed; intentionally unrelated to the Python `.lynxc` container).
- [x] Add bytecode generation: AST-to-bytecode compiler with constant folding
  (`--no-opt` disables) and jump backpatching for break/continue/restart.
- [x] Add a native stack-machine VM executing `.lynxc` files with
  byte-identical output, exit codes, and source-located errors versus the
  interpreter path (fixture parity is enforced by `make test`).
- [x] Implement `--compile` (`-c`, `--no-cache`, `--no-opt`), direct `.lynxc`
  execution, and `--view-bytecode` disassembly.
- [x] Implement `--bundle <file.lynx> [name]`: compile to bytecode and append
  it to a copy of the clynxer executable (`CLYXPAYLD` trailer); a bundled
  executable detects its payload at startup and runs the embedded program
  directly, with the same output parity as `.lynxc` runs.
- [x] Merge the Lynxer and Clynxer Makefile entry points, including root
  `buildCLynxer`, `testCLynxer`, `cleanCLynxer`, and combined build/test/clean
  targets.
- [ ] Add an optimization pass beyond constant folding.

## Milestone 9 — compatibility gates

- [x] Baseline comparison against the Python test fixtures: 15 of 55 pass
  (2026-09-13); failure causes catalogued (functions, bitwise/word operators,
  `const`, typed element literals, module imports, unsupported native APIs).
- [ ] Compare lexer output against the original implementation.
- [ ] Compare parser and runtime behavior for supported fixtures.
- [ ] Add golden tests for output and diagnostic text.
- [ ] Run the complete Clynxer test suite on every milestone.
- [ ] Document intentional differences and dropped Python-only features.

## Known parity bugs

- [ ] test25: a double `memoryFree()` must raise a source-located error, not
  abort with a glibc double-free (Python: "address refers to freed memory").
- [ ] test26: reading an invalid address must raise "invalid native memory
  address", not segfault.
- [ ] test22: the zero-size allocation path crashes with a `stoll` interpreter
  failure instead of a clean error.

## Current boundary

Clynxer currently supports the core language, all loop forms, the extended
value model (list/tuple/sentinel/object/char/codeblock), the full documented
scalar type set (`num`, `numBool`, `bit`, `byte`, `int8`..`uint64`,
`float32`/`float64`), typed element literals (`[int 1, int 2]` and
`(int 10, int 20)`), a flat per-function lexical scope, `const` with
reassignment errors, and structs/classes/enums/vargroups with `switch` /
pattern matching. The list/tuple/IO/conversion/introspection built-ins, the
native-memory family, and the named syscalls are also implemented.

There is no bytecode backend: `--compile` writes a standalone ELF executable
that embeds the program, every transitively imported module source, and every
imported native library, so a compiled program supports imports and stdlibs and
behaves exactly like an interpreted one. The root Makefile builds and tests both
Lynxer and Clynxer targets, and a GitHub Actions workflow builds Clynxer and runs
its suite. The bundled standard library covers `math`, `json`, `re`, `regex`,
`os`, `path`, `fileIO`, `csv`, `time`, `debug`, `sys`, `shell`, `cli`, `js`,
`multiprocessing`, `random`, `image`, `lua`, `game`, `network`, `server`, and
`text`/`typing`/`colorlib`. The remaining Python reference modules not yet
ported are `sound`, `sqldb`, `tui`, `tkinter`, `tkinterPlus`, and `turtle`;
the older `http`/`net` modules are superseded by `network`/`server`.
