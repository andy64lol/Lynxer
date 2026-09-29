# Build and install

Lynxer is a C++17 Linux executable. Building and running it does **not** need
Python; only the two check scripts in the test suite do.

## Requirements

- A C++17 compiler (`g++` or `clang++`) and `make`.
- `python3` to run `make testLynxer` (the module contract check).
- A Rust toolchain (`cargo`): it is **required**, not optional. Every
  Rust-backed stdlib module is built by `cargo`, and the interpreter links the
  Rust native-call engine (`lynxer/rust/ffi`). A missing toolchain or a compile
  error fails the build; there is no reduced build that drops a backend.
- `libffi` headers and `pkg-config` (e.g. `libffi-dev` on Debian/Ubuntu) for the
  native-call engine.
- Git and network access for the first `cargo` build: crates are fetched from
  crates.io. Most crates are pure Rust, but a few compile vendored C with the
  system C compiler — `zstd` (via `zstd-sys`), vendored Lua (via `mlua`) and
  bundled SQLite (via `rusqlite`) — so a C compiler is required alongside the
  Rust toolchain.

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

- `lynxer/lynxer` — the interpreter.
- `lynxer/lynxer-arm64` — the ARM64 interpreter, from `buildLynxerArm64`.
- `lynxer/stdlib/*.so` — the stdlib backends. The Rust-backed modules are
  produced by `cargo` under `lynxer/build/rust`; the C++ ones are compiled
  from `stdlib/*.cpp`.

- `lynxer/build/rust/release/liblynxer_ffi.a` — the native-call engine, a Rust
  `staticlib` built by `cargo` and linked into `lynxer/lynxer`. It is not a
  stdlib module and is not copied into `stdlib/`.

The Rust workspace has twenty-one member crates: the nineteen stdlib module
backends listed in `LYNXER_RUST_MODULE_NAMES` (`compress`, `crypto`, `encoding`,
`game`, `graphics`, `image`, `ini`, `json`, `lua`, `network`, `server`, `sound`,
`sqldb`, `toml`, `tui`, `uuid`, `watch`, `xml`, `yaml`), the shared `rust/abi`
crate, and `rust/ffi`, the required native-call engine described in
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
/usr/bin/lynxer                 a symlink to the real binary
```

Because the symlink resolves to the real binary (`/proc/self/exe`), an installed
`lynxer` finds its stdlib from any working directory, so `lynxer app.lynx` works
without keeping the build tree around. Set `LYNXER_PREFIX` to install somewhere
other than `/usr` — for example `LYNXER_PREFIX=$HOME/.local lynxer --install`
needs no root — and pass the same value to `--uninstall`.
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
| `LYNXER_OPT_REPORT=1` | the interpreter | print the AST optimizer counts to stderr after the run |
| `LYNXER_GAME_HEADLESS=1` | the `game` module | run without opening a window |
| `LYNXER_GRAPHICS_HEADLESS=1` | the `graphics` module | run without opening a window |
| `LYNXER_SKIP_DISPLAY=1` | `make testLynxer` | skip the display and audio fixtures (used by CI) |

The first three are described in more detail in
[CLI.md](CLI.md#environment-variables) and [limitations.md](limitations.md).

## See also

- [README.md](README.md) — the documentation index and what `make testLynxer` runs.
- [CLI.md](CLI.md) — every flag and exit code.
