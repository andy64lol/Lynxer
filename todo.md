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

- [ ] Replace the initial value variant with a complete Clynxer value model.
- [ ] Add lexical scopes and match Lynxer declaration lifetime rules.
- [ ] Add functions, parameters, returns, and global function lookup.
- [ ] Add lists, tuples, indexing, and iteration.
- [ ] Add structs, classes, enums, and pattern matching.

## Milestone 4 — compiler and standard library

- [ ] Define a stable Clynxer AST and parser diagnostics.
- [ ] Add bytecode generation and a native bytecode VM.
- [ ] Port built-ins with explicit unsupported-feature errors.
- [ ] Add native filesystem, process, networking, and time APIs.
- [ ] Port standard-library modules one small module at a time.

## Milestone 5 — compatibility gates

- [ ] Compare lexer output against the original implementation.
- [ ] Compare parser and runtime behavior for supported fixtures.
- [ ] Add golden tests for output and diagnostic text.
- [ ] Run the complete Clynxer test suite on every milestone.
- [ ] Document intentional differences and dropped Python-only features.

## Current boundary

Clynxer currently supports the core language and all original Lynxer loop
forms. Features not listed above are intentionally not implemented yet and
must fail explicitly rather than silently invoking Python.