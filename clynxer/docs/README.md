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
| [language.md](language.md) | Clynxer syntax, including `global`, `func`, and `local` functions |
| [install.md](install.md) | Build commands, dependency staging, and installation |
| [limitations.md](limitations.md) | Divergences from the Python implementation, and what is not ported |

## Build and run

```bash
make                     # build the interpreter and every native stdlib module
clynxer program.lynx     # run a program
clynxer --list-stdlibs   # list available modules with their docstrings
```

From the repository root, `make buildCLynxer` builds the binary and the native
modules. The `game`, `image`, `json`, `lua`, `network`, `server`, `sound`,
`sqldb` and `tui` modules are Rust crates under `rust/` (see `make rust`); the
rest are C++ `stdlib/*.cpp`. A Rust toolchain (`cargo`) is required for those
nine modules; they are skipped with a warning when cargo is absent.
`make testCLynxer` runs the smoke and stdlib fixtures.

## Standard library modules

Every module below is implemented by a `stdlib/<name>.so` backend — C++
(`stdlib/<name>.cpp`) or, for the Rust-backed ones, a crate under `rust/` — and
exposed to Lynxer by `stdlib/<name>.lynx`. Modules marked *pure* are written in
Lynxer only and need no shared library. Modules marked *native* need a
compiler: the C++ ones need only a C++17 toolchain, and the Rust ones are
skipped with a warning when `cargo` is missing, so a plain `make` never fails on
an absent Rust toolchain.

There is no bytecode backend: `--compile` produces a standalone ELF executable
that embeds the program, its imported module sources and its native libraries.

Every module the program **imports** is collected automatically, and several
inputs can be bundled into the one executable — the first `.lynx` file is the
program, and any further `.lynx` or `.so` files are embedded and importable by
name. `--include <file>` adds a file of any kind; a non-`.lynx`/`.so` file is
embedded as data that the program reads through `bundledFile()`:

```bash
clynxer --compile app.lynx extras/helpers.lynx --include vendor/libcustom.so \
        --include assets/message.txt -o app
```

| Module | Implementation | Backend |
| --- | --- | --- |
| [cli](stdlib/cli.md) | native | POSIX process/env/terminal APIs |
| [colorlib](stdlib/colorlib.md) | pure | ANSI escape sequences |
| [csv](stdlib/csv.md) | native | hand-written CSV/TSV reader and writer |
| [debug](stdlib/debug.md) | native + pure | `<chrono>`, `getrusage`, assertions in Lynxer |
| [fileIO](stdlib/fileIO.md) | native | `<fstream>`, `<filesystem>` |
| [game](stdlib/game.md) | native | Rust `macroquad` (`rust/game`) |
| [image](stdlib/image.md) | native | Rust `image` (`rust/image`) |
| [js](stdlib/js.md) | native | the `node` binary |
| [json](stdlib/json.md) | native | Rust `serde_json` (`rust/json`) |
| [lua](stdlib/lua.md) | native | Rust `mlua` with vendored Lua 5.4 (`rust/lua`) |
| [math](stdlib/math.md) | native | `<cmath>` plus statistics and vector helpers |
| [multiprocessing](stdlib/multiprocessing.md) | native + pure | `std::thread` and shell subprocesses |
| [network](stdlib/network.md) | native | Rust `ureq` + `tungstenite` (rustls) |
| [os](stdlib/os.md) | native | `<filesystem>`, POSIX |
| [path](stdlib/path.md) | native | `<filesystem>`, POSIX `stat` |
| [random](stdlib/random.md) | native | seeded linear congruential generator in C++ |
| [re](stdlib/re.md) | native | `std::regex` |
| [regex](stdlib/regex.md) | native | `std::regex` with a named-pattern cache |
| [server](stdlib/server.md) | native | Rust `axum` + `tokio` |
| [shell](stdlib/shell.md) | native | `popen`, `std::system` |
| [sound](stdlib/sound.md) | native | Rust `rodio` + `cpal` + `symphonia` |
| [sqldb](stdlib/sqldb.md) | native | Rust `rusqlite` (bundled SQLite) |
| [sys](stdlib/sys.md) | native | C++ runtime and POSIX |
| [text](stdlib/text.md) | pure | Lynxer string builtins |
| [time](stdlib/time.md) | native | `<chrono>`, `<ctime>` |
| [tui](stdlib/tui.md) | native | Rust `ratatui` + `crossterm` |
| [typing](stdlib/typing.md) | pure | Lynxer type builtins |

`game`, `image`, `json`, `lua`, `network`, `server`, `sound`, `sqldb` and `tui`
are Rust crates under `rust/`, built by `cargo` and installed as
`stdlib/<name>.so` (see `make rust`). The rest are C++ compiled from
`stdlib/*.cpp`. There is no CMake staging step and no `third_party/` directory
any more: TLS is `rustls` (no system OpenSSL) and the HTTP/WebSocket stack is
pure Rust.

`venv` is intentionally excluded — see [limitations.md](limitations.md).

## Adding a stdlib module

1. Write `stdlib/<name>.cpp` exporting `lynxer_module_init_v1` and one
   `extern "C"` function per entry; see
   [native-module-abi.md](native-module-abi.md). It is picked up automatically
   by the `stdlib/*.cpp` wildcard.
2. Write `stdlib/<name>.lynx` whose `setup()` imports the library with
   `importAs("<name>.so", "native<Name>")`, and forward each function as
   `global f(...) { return global.nativeName.f(...); }`. Start the file with a
   `////` docstring — `clynxer --list-stdlibs` prints it.
3. A module can also be a Rust crate under `rust/`, as `game`, `image`, `json`,
   `lua`, `network`, `server`, `sound`, `sqldb` and `tui` are. Export
   `lynxer_module_init_v1` plus one `#[no_mangle] extern "C"` op per entry (the
   `clynxer_abi` crate provides the packing, panic guards and registration
   helper), add the crate to the workspace, and add its name to
   `RUST_MODULE_NAMES` in the Makefile. Rust modules are skipped with a warning
   when `cargo` is absent.

   **A Rust op must be registered with a packed signature** — `cdecl:int64(...)`,
   `cdecl:float64(...)` or `cdecl:cstring(...)` — never a fixed shape such as
   `cdecl:int64(int64)`. The `export_*!` macros generate the four-scalar packed
   prototype, so a fixed-shape declaration calls the symbol through the wrong C
   prototype and **segfaults on the first call**, with no build-time warning. See
   [native-module-abi.md](native-module-abi.md).
4. Add `examples/stdlib_<name>.lynx` plus a sibling `stdlib_<name>.expected`
   file. `make test` runs every `examples/stdlib_*.lynx` and diffs it against
   its `.expected` output.
5. Run `make test`. It first runs `scripts/check_module_contracts.py`, which
   compares the wrapper against the backend: every
   `global.native<Alias>.<op>(...)` call must name a registered op, and for a
   Rust backend every `args.<kind>(i)` read must be in range for the arguments
   the wrapper passes. This catches contract mismatches that a fixture cannot —
   a fixture records what the code does, so it will happily assert a module that
   is broken in a way the fixture reproduces.

A module that imports native libraries is picked up automatically by
`--compile`: every transitively imported `.lynx` source and `.so` library is
embedded in the resulting executable.

## File-wide functions

Use `func` for a helper that belongs to the current source file:

```c
global setup(){}

func double(int value) -> int {
    return value * 2;
}

global main(){
    println(double(21));
}
```

`func` declarations must be top-level and appear between `global setup()` and
`global main()`. They are called by bare name in their defining file. When a
file is imported as a module, callers use
`global.moduleName.functionName(...)`. `func` names are file-scoped, so two
different imported files may define the same helper name.
