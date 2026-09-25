# Python → C/C++ migration

This document is the Stage 1 contract for the native toolchain migration.
Stage 1 keeps the Python implementation behavior-compatible while making each
pipeline unit independently portable to C++. The checked-in golden corpus is
the compatibility gate for this work.

## Interop decision

The native build follows option **(b)** from `todo.md`:

- the native build does not embed `libpython`;
- EmbedPy, `rawPy`/`rawPyx`, `exec`, Cython-inline blocks, and Python-hosted
  graphical/audio backends remain available in the Python toolchain;
- the Python toolchain is the `lynxer-py` reference during the transition;
- native replacements are added behind native APIs rather than making C++
  depend on Python;
- native execution reports an explicit “module or feature is not available in
  the native build” error when a Python-only feature is requested.

This keeps the first native port deterministic. It also avoids making every
future C++ translation unit own Python interpreter lifetime and GIL rules.
The decision can be revisited, but a native module must not silently fall back
to Python.

## Target dependency direction

The target graph is layered. A module may depend on an earlier layer, but not
on a later one:

```text
diagnostics
    ↓
lexer → ast
    ↓      ↓
parser ────┘
    ↓
bytecode (encode/decode)
    ↓
values
    ↓
interpreter
    ↓
builtins
    ↓
stdlib glue
    ↓
CLI / bundling / installation
```

The following boundaries are intentional:

| Layer | Public responsibility | Must not own |
| --- | --- | --- |
| `diagnostics` | positions, errors, warning messages, formatting | interpreter state |
| `lexer` | source text → tokens | AST construction or runtime values |
| `ast` | syntax node data and source spans | parsing decisions or evaluation |
| `parser` | tokens → AST | built-in registration or execution |
| `bytecode` | stable v9 serialization and execution adapter | CLI policy |
| `values` | runtime value/type protocol and conversions | global interpreter singletons |
| `interpreter` | evaluation and explicit execution context | CLI argument parsing |
| `builtins` | built-in function implementations and registration | parser internals |
| `stdlib glue` | `.lynx` module discovery and native module bridge | compiler internals |
| `CLI` | command-line behavior, bundling, installation | language semantics |

`lynxer.py` remains a compatibility facade while this work is in progress.
New code should import the owning layer directly; it should not add another
re-export to the facade.

## Porting-unit contract

Every Stage 1 module that will later become a C++ translation unit must have:

1. a documented public API;
2. imports only from earlier layers;
3. no module-level runtime singleton;
4. no dependency on another module's private underscore names;
5. behavior covered by the golden corpus or a focused regression test;
6. an API that can be implemented without Python object identity assumptions.

The v9 bytecode format is frozen during Stage 1. Any refactor that changes
serialized output is a failed gate, even if source execution still succeeds.

## Initial port order

The first low-risk order is:

1. diagnostics and source formatting;
2. lexer and AST data;
3. parser;
4. bytecode encoding;
5. values and conversion helpers;
6. interpreter execution context;
7. built-in categories;
8. native-backed standard-library modules;
9. CLI and CMake packaging.

The existing native bytecode decoder is reused once the Python encoder has a
stable parity test. Python-only modules are not porting blockers for the
native core.