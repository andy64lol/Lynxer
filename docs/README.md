# Lynxer documentation

Lynxer is a small, standalone C++ toolchain for the Lynxer language. It runs
`.lynx` programs without a Python runtime and ships its standard library as
native shared libraries. It provides standalone ELF executables, 27 natively
backed modules, an AST optimizer, a frozen native-module ABI, and CI on amd64
and arm64. See [limitations.md](limitations.md) for the behaviour that is
deliberately constrained or not implemented.

## Contents

**Getting started**

| Document | What it covers |
| --- | --- |
| [install.md](install.md) | Requirements, build commands, artifacts, environment variables |
| [CLI.md](CLI.md) | Every flag, exit code, and error behaviour of the `lynxer` executable |

**The language**

| Document | What it covers |
| --- | --- |
| [language.md](language.md) | Program structure, comments, variables, operators, control flow, functions, scoping, interpolation |
| [types.md](types.md) | Every type name, ranges, and conversion rules |
| [lists.md](lists.md) | `list` and `tuple` values and their builtins |
| [structs.md](structs.md) | Data-only records |
| [classes.md](classes.md) | Fields plus `local` methods |
| [enums.md](enums.md) | Tagged unions and `switch` patterns |
| [vargroups.md](vargroups.md) | Records with defaulted fields |
| [modules.md](modules.md) | `import`, search order, module member access |
| [importAs.md](importAs.md) | Aliasing an imported module |

**Internals and reference**

| Document | What it covers |
| --- | --- |
| [builtins.md](builtins.md) | Every function the interpreter implements itself |
| [native-module-abi.md](native-module-abi.md) | Writing a native `.so` module: entry point, signatures, data conventions |
| [stdlib-contracts.md](stdlib-contracts.md) | The frozen contract every wrapper and backend must satisfy |
| [extending.md](extending.md) | Adding a stdlib module end to end |
| [stdlib/](stdlib/) | One page per standard-library module |
| [limitations.md](limitations.md) | Deliberate constraints and what is not implemented |

## Build and run

```bash
make buildLynxer        # interpreter + every native stdlib module
lynxer/lynxer program.lynx
lynxer/lynxer --list-stdlibs
```

The root `Makefile` builds both implementations; `make` alone builds everything.
`make buildLynxer` builds the interpreter and the native modules, and
`make buildLynxerArm64` cross-builds the ARM64 interpreter.
`make testLynxer` runs the whole suite:

1. `lynxer/scripts/check_module_contracts.py` — wrapper/backend contract check.
2. `lynxer/scripts/check_golden.py` — the CLI and diagnostic golden cases in
   `lynxer/golden/cases.json`.
3. Every `lynxer/examples/*.expected` fixture, diffed on stdout+stderr,
   including the optimizer and low-level native-memory/syscall fixtures.
4. Interpreted-versus-`--compile` parity for a set of fixtures.
5. The `--format`/`--format-oneline` formatter fixture (output, idempotence, and
   that the formatted file still runs) and the `--validate-executeable`
   self-check.
6. The bundled-executable, `--include`, and bytecode-removal checks.

Both Lynxer CI workflows (`.github/workflows/build-lynxer-amd.yml` and
`build-lynxer-arm.yml`) run `make testLynxer LYNXER_SKIP_DISPLAY=1`, which
drops the fixtures that need a display or an audio device (a CI runner has
neither, and the graphics backend crashes without a display).

A Rust toolchain is optional: the nine Rust-backed modules are skipped with a
warning when `cargo` is absent, and everything else still builds. `python3` is
required for the two check scripts in the test suite.

## Standard library modules

There are 27 bundled modules. A module is exposed to Lynxer by
`stdlib/<name>.lynx` and backed by a `stdlib/<name>.so`. The backends marked
*Rust* come from a crate under `rust/`; *pure* modules are written in Lynxer
only and need no shared library.

| Module | Backend | Implementation |
| --- | --- | --- |
| [cli](stdlib/cli.md) | native | POSIX process/env/terminal APIs |
| [colorlib](stdlib/colorlib.md) | pure | ANSI escape sequences |
| [csv](stdlib/csv.md) | native | hand-written CSV/TSV reader and writer |
| [debug](stdlib/debug.md) | native + pure | `<chrono>`, `getrusage`, assertions in Lynxer |
| [fileIO](stdlib/fileIO.md) | native | `<fstream>`, `<filesystem>` |
| [game](stdlib/game.md) | Rust | `macroquad` (`rust/game`) |
| [image](stdlib/image.md) | Rust | `image` (`rust/image`) |
| [js](stdlib/js.md) | native | the `node` binary |
| [json](stdlib/json.md) | Rust | `serde_json` (`rust/json`) |
| [lua](stdlib/lua.md) | Rust | `mlua` with vendored Lua 5.4 (`rust/lua`) |
| [math](stdlib/math.md) | native | `<cmath>` plus statistics and vector helpers |
| [multiprocessing](stdlib/multiprocessing.md) | native + pure | `std::thread` and shell subprocesses |
| [network](stdlib/network.md) | Rust | `ureq` + `tungstenite` over `rustls` |
| [os](stdlib/os.md) | native | `<filesystem>`, POSIX |
| [path](stdlib/path.md) | native | `<filesystem>`, POSIX `stat` |
| [random](stdlib/random.md) | native | seeded linear congruential generator in C++ |
| [re](stdlib/re.md) | native | `std::regex` |
| [regex](stdlib/regex.md) | native | `std::regex` with a named-pattern cache |
| [server](stdlib/server.md) | Rust | `axum` + `tokio` |
| [shell](stdlib/shell.md) | native | `popen`, `std::system` |
| [sound](stdlib/sound.md) | Rust | `rodio` + `cpal` + `symphonia` |
| [sqldb](stdlib/sqldb.md) | Rust | `rusqlite` (bundled SQLite) |
| [sys](stdlib/sys.md) | native | C++ runtime and POSIX |
| [text](stdlib/text.md) | pure | Lynxer string builtins |
| [time](stdlib/time.md) | native | `<chrono>`, `<ctime>` |
| [tui](stdlib/tui.md) | Rust | `ratatui` + `crossterm` |
| [typing](stdlib/typing.md) | pure | Lynxer type builtins |

The Rust workspace has ten member crates
(`LYNXER_RUST_MODULE_NAMES` in the Makefile): the nine module backends listed
above, plus `ffi`, an intentional **no-op** cdylib — the `ffi*` builtins are
implemented in C++ (`lynxer/builtins.cpp`), not by that crate. There is no
CMake staging step and no `third_party/` directory: TLS is `rustls` (no system
OpenSSL) and the HTTP/WebSocket stack is pure Rust.

`venv` and the GUI/turtle modules are intentionally excluded — see
[limitations.md](limitations.md).

## Compiling a program

`--compile` (alias `--bundle`) produces one standalone ELF executable that
embeds the program, every transitively imported `.lynx` source and `.so`
library, plus anything added with `--include`:

```bash
lynxer/lynxer --compile app.lynx extras/helpers.lynx \
        --include vendor/libcustom.so --include assets/message.txt -o app
```

The first `.lynx` file is the program; further `.lynx`/`.so` inputs are embedded
and importable by name. A non-`.lynx`/`.so` include is embedded as data and read
with `bundledFile(name)` / `bundledFiles()`. See [CLI.md](CLI.md#compile-to-an-executable).

## Adding a stdlib module

1. Write the backend. A C++ module is `stdlib/<name>.cpp` exporting
   `lynxer_module_init_v1` and one `extern "C"` function per entry; it is picked
   up automatically by the `stdlib/*.cpp` wildcard. A Rust module is a crate
   under `rust/` exporting the same entry point plus one
   `#[no_mangle] extern "C"` op per entry (the `lynxer_abi` crate provides the
   packing, panic guards, and registration helper); add it to the workspace and
   to `LYNXER_RUST_MODULE_NAMES` in the root Makefile. See
   [native-module-abi.md](native-module-abi.md).

   **A Rust op must be registered with a packed signature** —
   `cdecl:int64(...)`, `cdecl:float64(...)`, or `cdecl:cstring(...)` — never a
   fixed shape such as `cdecl:int64(int64)`. The `export_*!` macros generate a
   four-scalar packed prototype, so a fixed-shape declaration calls the symbol
   through the wrong C prototype and **segfaults on the first call**, with no
   build-time warning.

2. Write `stdlib/<name>.lynx`. Its `setup()` imports the library with
   `importAs("<name>.so", "native<Name>")` and forwards each function as
   `global f(...) { return global.nativeName.f(...); }`. Start the file with a
   line containing exactly `////`, then the description, then another `////`;
   `lynxer --list-stdlibs` prints that block.

3. Add `examples/stdlib_<name>.lynx` plus a sibling `stdlib_<name>.expected`.
   The suite runs every `examples/stdlib_*.lynx` and diffs it against its
   `.expected` output.

4. Run `make testLynxer`. It runs
   `lynxer/scripts/check_module_contracts.py`, which compares the wrapper
   against the backend: every `global.native<Alias>.<op>(...)` call must name a
   registered op, and for a Rust backend every `args.<kind>(i)` read must be in
   range for the arguments the wrapper passes. This catches contract mismatches
   a fixture cannot — a fixture records what the code does, so it will happily
   assert a broken module whose behaviour it reproduces.

See [extending.md](extending.md) and [stdlib-contracts.md](stdlib-contracts.md)
for the full checklist and the contract.

## Environment variables

| Variable | Effect |
|----------|--------|
| `LYNXER_OPT_REPORT=1` | Print the AST optimizer's transformation counts to stderr after the run |
| `LYNXER_GAME_HEADLESS=1` | Run the `game` module without opening a window |
| `LYNXER_SKIP_DISPLAY=1` | `make testLynxer` skips the display/audio fixtures (used by CI) |

## File-wide functions

Use `func` for a helper that belongs to the current source file:

```lynx
global setup(){}

func double(int value) -> int {
    return value * 2;
}

global main(){
    println(double(21));   // 42
}
```

`func` declarations must be top-level and appear between `global setup()` and
`global main()`. They are called by bare name in their defining file. When a
file is imported as a module, callers use `global.moduleName.functionName(...)`.
`func` names are file-scoped, so two different imported files may define the
same helper name. `global func name(...)` is not valid syntax; see
[language.md](language.md#functions).
