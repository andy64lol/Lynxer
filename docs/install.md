# Build and install

Lynxer is a C++17 executable with a cross-platform Windows host build. Building
and running it does **not** need Python; Python is used by the LSP and tooling.

## Requirements

- A C++17 compiler (`g++` or `clang++`) and `make`.
- `python3` for the LSP and development tooling.
- A Rust toolchain (`cargo`): it is **required**, not optional. Every
  Rust-backed stdlib module is built by `cargo`, and the interpreter links the
  Rust native-call engine (`lynxer/rust/ffi`). A missing toolchain or a compile
  error fails the build; there is no reduced build that drops a backend.
- `libffi` headers and `pkg-config` (e.g. `libffi-dev` on Debian/Ubuntu) for the
  native-call engine.
- Git and network access for the first `cargo` build: crates are fetched from
  crates.io. Most crates are pure Rust, but a few compile vendored C with the
  system C compiler — `zstd` (via `zstd-sys`), vendored Lua (via `mlua`),
  bundled SQLite (via `rusqlite`) and the `ring` crypto provider — so a C
  compiler is required alongside the Rust toolchain.

No system OpenSSL, Boost, CMake, cpp-httplib, Crow, or nlohmann/json is needed:
TLS is `rustls`, the HTTP stack is `ureq`/`tungstenite`/`axum`, and JSON is
`serde_json`.

## Build from the repository root

```bash
make buildLynxer       # interpreter + every stdlib module, C++ and Rust
make testLynxer        # the full suite (see README.md)
```

`buildLynxer` builds the interpreter and every backend, C++ and Rust, so a
separate `make cargo` is only useful to rebuild the Rust half on its own. Both
fail if `cargo` is missing.

```bash
make cargo              # just the Rust backends and the native-call engine
make buildLynxerArm64  # the ARM64 interpreter (needs an aarch64 host)
```

The Makefile lives at the repository root; there is no separate
`lynxer/Makefile`.

## Artifacts

- `lynxer/lynxer` or `lynxer/lynxer.exe` — the interpreter for the build host.
- `lynxer/lynxer-arm64` — the ARM64 interpreter, from `buildLynxerArm64`.
- `lynxer/stdlib/*.so` — the Linux stdlib backends. The 27 Rust modules built
  from the `lynxer_stdlib` package land in `lynxer/build/rust/release/examples/`,
  `json` lands in `lynxer/build/rust/release/`, and the Makefile copies each to
  `stdlib/<name>.so`; the C++ ones are compiled from `stdlib/*.cpp`.
- On Windows, `lynxer/stdlib/*.so` files contain MinGW PE DLLs while retaining
  the language-level module names used by the bundled wrappers.

- `lynxer/build/rust/release/liblynxer_ffi.a` — the native-call engine, a Rust
  `staticlib` built by `cargo` and linked into `lynxer/lynxer`. It is not a
  stdlib module and is not copied into `stdlib/`.

- `lynxer/liblynxer.so` on Linux, or `lynxer/lynxer.dll` plus
  `lynxer/lynxer.dll.a` on Windows — the embedding runtime: the interpreter core (minus
  `main.cpp`) linked as a shared library, exporting only the `lynxer_embed_*`
  entry points. Libraries built by `lynxer --emit-library` link against it.
  `lynxer/lynxer.h` and `lynxer/ffi_abi.h` are its public headers.

The Rust workspace has five member packages. `lynxer_stdlib` (the workspace
root) owns the 27 module backend `cdylib` example targets listed in
`LYNXER_RUST_MODULE_NAMES` except `json`: one `cargo build --examples` builds
them all, in parallel, while each backend keeps its own crate root under
`rust/<name>/src/`. `json` is a separate package because it is the only backend
that enables `serde_json`'s `preserve_order` feature; Cargo unifies features
across a package, and that feature would change the JSON key order every other
module emits. The two crates the backends share are `rust/abi` (the C-ABI
plumbing) and `rust/regex_engine` (the `fancy-regex` engine behind
`re`/`regex`). `rust/ffi` is the required native-call engine: a `staticlib`
linked into the interpreter through `lynxer/ffi_abi.h`, not a stdlib module. It
is built on the `libffi` crate, so `cargo` and `libffi` headers are required to
build the interpreter; see
[native-module-abi.md](native-module-abi.md#the-native-call-engine).

## Install

```bash
sudo ./lynxer/lynxer --install      # install into /usr/lib/lynxer, link /usr/bin/lynxer
sudo ./lynxer/lynxer --uninstall    # remove the installed interpreter
```

`--install` lays out one self-contained tree:

```
/usr/lib/lynxer/lynxer          the real binary
/usr/lib/lynxer/stdlib/*        every stdlib module
/usr/lib/lynxer/lynxer.config   the configuration file
/usr/lib/lynxer/liblynxer.so    the Linux embedding runtime (for --emit-library)
/usr/lib/lynxer/lynxer.h        the embedding public header
/usr/lib/lynxer/ffi_abi.h       the wire-type header
/usr/bin/lynxer                 a symlink to the real binary
```

Because the symlink resolves to the real binary (`/proc/self/exe`), an installed
`lynxer` finds its stdlib — and the embedding runtime plus its headers for
`--emit-library` — from any working directory, so `lynxer app.lynx` works
without keeping the build tree around. See [embedding.md](embedding.md). Set
`LYNXER_PREFIX` to install somewhere other than `/usr` — for example
`LYNXER_PREFIX=$HOME/.local lynxer --install` needs no root — and pass the same
value to `--uninstall`.

On Windows, `--install` defaults to `%LOCALAPPDATA%\Programs\Lynxer` (a
per-user install needs no elevation; override it with `LYNXER_PREFIX`) and lays
out the same tree — `bin\lynxer.exe` plus
`lib\lynxer\{lynxer.exe, lynxer.dll, lynxer.dll.a, stdlib, lynxer.config}` —
except that the launcher is a copy rather than a symlink, so the stdlib is
resolved from `lib\lynxer`. `--emit-library` is available with the MinGW
toolchain; consumers need `lynxer.dll` on the Windows DLL search path. Add
`<prefix>\bin` to `PATH` to run `lynxer` by name; the installer says so when it is missing. See
[windows.md](windows.md).

See [CLI.md](CLI.md#installing).

## Quick run

```bash
./lynxer/lynxer lynxer/examples/milestone5.lynx
./lynxer/lynxer --list-stdlibs
./lynxer/lynxer --compile lynxer/examples/milestone5.lynx -o /tmp/demo
```

Example programs live under `lynxer/examples/`, so run them with that prefix
from the repository root.

## Environment variables

| Variable | Used by | Effect |
|----------|---------|--------|
| `LYNXER_STDLIB=/path/to/stdlib` | the interpreter | use an existing directory as the bundled stdlib location and for `--list-stdlibs` |
| `LYNXER_OPT_REPORT=1` | the interpreter | print the AST optimizer counts to stderr after the run |
| `LYNXER_GAME_HEADLESS=1` | the `game` module | run without opening a window |
| `LYNXER_GRAPHICS_HEADLESS=1` | the `graphics` module | run without opening a window |
| `LYNXER_SKIP_DISPLAY=1` | `make testLynxer` | skip the display and audio fixtures (used by CI) |

`LYNXER_STDLIB` must name an existing directory; an unset, empty, or invalid
value leaves the normal executable-relative lookup in place. For imports, the
override is the final bundled-stdlib fallback, after project and local stdlib
paths. The remaining variables are described in more detail in
[CLI.md](CLI.md#environment-variables) and [limitations.md](limitations.md).

## See also

- [README.md](README.md) — the documentation index and what `make testLynxer` runs.
- [CLI.md](CLI.md) — every flag and exit code.
