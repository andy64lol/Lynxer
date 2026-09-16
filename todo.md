# Clynxer rebuild from scratch

Clynxer is the standalone C++ implementation being rebuilt beside the original
Python Lynxer. The Python implementation is the behavior reference; it is not a
runtime dependency and its internals are not copied into Clynxer.

## Next up — compiler pivot and stdlib consolidation

Planned order of work, newest direction first.

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
- [ ] Serve `--list-stdlibs` from Clynxer's own stdlib directory.
- [ ] Port standard-library modules one small module at a time (29 modules in
  `lynxer/stdlib/`, not every module is possible, so implement what can be implemented).
- [x] Keep optional `-> type` return annotations available while stdlibs use
  the shared registration ABI.

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
- [ ] Keep rawPy, rawPyx, and embedPy unsupported with explicit errors
  (intentional difference; document in Milestone 9).

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
`multiprocessing`, `random` and `text`/`typing`/`colorlib`; the remaining
optional modules (`http`, `net`, `lua`, `tui`, `image`, `game`) are still to be
ported, and their system-library build table already exists.
