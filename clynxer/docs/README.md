# Clynxer documentation

Clynxer is the standalone C++ implementation of Lynxer. It runs `.lynx`
programs without a Python runtime and ships its standard library as native
shared libraries.

## Contents

| Document | What it covers |
| --- | --- |
| [builtins.md](builtins.md) | Every function implemented by the interpreter itself |
| [native-module-abi.md](native-module-abi.md) | How to write a native `.so` module: entry point, signatures, data conventions |
| [stdlib/](stdlib/) | One page per standard-library module |
| [limitations.md](limitations.md) | Divergences from the Python implementation, and what is not ported |

## Build and run

```bash
make                     # build the interpreter and every dependency-free stdlib
clynxer program.lynx     # run a program
clynxer --list-stdlibs   # list available modules with their docstrings
```

From the repository root, `make buildCLynxer` builds the binary and the native
modules. `make testCLynxer` additionally runs the smoke and stdlib fixtures.

## Standard library modules

Every module below is implemented by `stdlib/<name>.cpp`, compiled to
`stdlib/<name>.so`, and exposed to Lynxer by `stdlib/<name>.lynx`. Modules
marked *pure* are written in Lynxer only and need no shared library. Modules
marked *opt-in* are skipped with a warning when their system library is missing,
so a plain `make` never fails on absent optional dependencies.

There is no bytecode backend: `--compile` produces a standalone ELF executable
that embeds the program, its imported module sources and its native libraries.

| Module | Implementation | Backend |
| --- | --- | --- |
| [cli](stdlib/cli.md) | native | POSIX process/env/terminal APIs |
| [colorlib](stdlib/colorlib.md) | pure | ANSI escape sequences |
| [csv](stdlib/csv.md) | native | hand-written CSV/TSV reader and writer |
| [debug](stdlib/debug.md) | native + pure | `<chrono>`, `getrusage`, assertions in Lynxer |
| [fileIO](stdlib/fileIO.md) | native | `<fstream>`, `<filesystem>` |
| [js](stdlib/js.md) | native | the `node` binary |
| [json](stdlib/json.md) | native | hand-written JSON parser (`native_json.hpp`) |
| [math](stdlib/math.md) | native | `<cmath>` plus statistics and vector helpers |
| [multiprocessing](stdlib/multiprocessing.md) | native + pure | `std::thread` and shell subprocesses |
| [os](stdlib/os.md) | native | `<filesystem>`, POSIX |
| [path](stdlib/path.md) | native | `<filesystem>`, POSIX `stat` |
| [random](stdlib/random.md) | pure | deterministic LCG in Lynxer |
| [re](stdlib/re.md) | native | `std::regex` |
| [regex](stdlib/regex.md) | native | `std::regex` with a named-pattern cache |
| [shell](stdlib/shell.md) | native | `popen`, `std::system` |
| [sys](stdlib/sys.md) | native | C++ runtime and POSIX |
| [text](stdlib/text.md) | pure | Lynxer string builtins |
| [time](stdlib/time.md) | native | `<chrono>`, `<ctime>` |
| [typing](stdlib/typing.md) | pure | Lynxer type builtins |

Planned opt-in modules that are not part of the build yet: `http` (libcurl),
`net` (POSIX sockets), `lua` (Lua 5.4), `tui` (ncurses/ANSI), `image` (libpng),
`game` (SDL2). `venv` is intentionally excluded — see
[limitations.md](limitations.md).

## Adding a stdlib module

1. Write `stdlib/<name>.cpp` exporting `lynxer_module_init_v1` and one
   `extern "C"` function per entry; see
   [native-module-abi.md](native-module-abi.md). It is picked up automatically
   by the `stdlib/*.cpp` wildcard.
2. Write `stdlib/<name>.lynx` whose `setup()` imports the library with
   `importAs("<name>.so", "native<Name>")`, and forward each function as
   `global f(...) { return global.nativeName.f(...); }`. Start the file with a
   `////` docstring — `clynxer --list-stdlibs` prints it.
3. If the module needs a system library, add it to `stdlib/libs.mk`
   (`MODULE_PKG_<name>`) and to `OPTIONAL_MODULE_NAMES` in the Makefile so it is
   skipped when the library is absent.
4. Add `examples/stdlib_<name>.lynx` plus a sibling `stdlib_<name>.expected`
   file. `make test` runs every `examples/stdlib_*.lynx` and diffs it against
   its `.expected` output.

A module that imports native libraries is picked up automatically by
`--compile`: every transitively imported `.lynx` source and `.so` library is
embedded in the resulting executable.
