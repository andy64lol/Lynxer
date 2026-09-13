# Clynxer rebuild from scratch

Clynxer is the standalone C++ implementation being rebuilt beside the original
Python Lynxer. The Python implementation is the behavior reference; it is not a
runtime dependency and its internals are not copied into Clynxer.

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
- [ ] Add the remaining documented scalar types: `num`, `char`, `numBool`,
  `bit`, `byte`, `int8`..`uint64`, `float32`/`float64`, and the `codeblock`
  type.
- [ ] Add typed element literals for sequences: `[int 1, int 2]` and
  `(int 10, int 20)`.
- [ ] Add lexical scopes and match Lynxer declaration lifetime rules.
- [ ] Add `const` declarations whose reassignment is a runtime error.
- [ ] Add structs, classes, enums, vargroups, and pattern matching.

## Milestone 4 — operators, statements, and errors

- [ ] Implement bitwise operators `&`, `|`, `^`, `!&`, `!^`, `!|`, `~`, `<<`,
  `>>`, exponent `**`, and integer division `/%`.
- [ ] Implement the NAND/NOR logic forms `!&&` and `!||`.
- [ ] Implement the word operators `and`, `or`, and `not`.
- [ ] Accept the legacy equality forms `is` and `not is` with deprecation
  warnings.
- [ ] Implement `switch` / `case` / `default` and `elif`.
- [ ] Support the `\e` string escape.
- [ ] Implement `try` / `catch` with source-located error values.

## Milestone 5 — functions and code blocks

- [ ] Support file-wide `func` declarations and named `global` functions
  beyond `setup`/`main`.
- [ ] Add typed and default parameters and return values.
- [ ] Add local functions.
- [ ] Add caller-supplied code blocks and multiple code blocks.
- [ ] Add named `codeblock` values and `exec(){{name}}`.
- [ ] Implement the `overrideMain` entry-point override.

## Milestone 6 — module system and standard library

- [ ] Implement `import` and `importAs` with nested/path-based imports.
- [ ] Support calling module functions and accessing module globals.
- [ ] Serve `--list-stdlibs` from Clynxer's own stdlib directory.
- [ ] Port standard-library modules one small module at a time (29 modules in
  `lynxer/stdlib/`).

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

- [x] CLI parity for run/help/version/lint/list-stdlibs/easter egg via
  `shell.cpp`, with the version and message templates in `clynxer.config`.
- [x] Fail explicitly for compile, bundle, view-bytecode, ast, format,
  benchmark, validate, install, and `.lynxc` bytecode running.
- [ ] Define a stable Clynxer AST and parser diagnostics.
- [ ] Add bytecode generation and a native bytecode VM.
- [ ] Implement `--compile`, `--bundle`, `--view-bytecode`, `--ast`,
  `--format`, `--format-oneline`, `--benchmark-compile`,
  `--validate-executeable`, and `--install`/`--uninstall`.

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
value model (list/tuple/sentinel/object), the list/tuple/IO/conversion/
introspection built-ins, the native-memory family, and the named syscalls.
Functions beyond setup/main, modules, classes/structs/enums/vargroups,
bitwise and word operators, `const`, `switch`/`elif`, and `try`/`catch` are
not implemented yet and must fail explicitly rather than silently invoking
Python.
