# Build and install

Lynxer is a C++17 Linux executable. Building and running it does **not** need
Python; only the two check scripts in the test suite do.

## Requirements

- A C++17 compiler (`g++` or `clang++`) and `make`.
- `python3` to run `make testLynxer` (the contract and golden checks).
- Optional: a Rust toolchain (`cargo`) for the nine Rust-backed modules. They
  are **skipped with a warning** when `cargo` is not on `PATH`; the rest of
  Lynxer still builds.
- Git and network access for the first `cargo` build: crates are fetched from
  crates.io. Nothing is compiled from vendored C/C++ sources.

No system OpenSSL, Boost, CMake, cpp-httplib, Crow, or nlohmann/json is needed:
TLS is `rustls`, the HTTP stack is `ureq`/`tungstenite`/`axum`, and JSON is
`serde_json`.

## Build from the repository root

```bash
make buildLynxer       # interpreter + every native stdlib module
make testLynxer        # the full suite (see README.md)
```

`buildLynxer` already builds the Rust backends, so a separate `make cargo` is
only useful to build them on their own:

```bash
make cargo              # just the Rust backends
make buildLynxerArm64  # cross-build the ARM64 interpreter
```

The Makefile lives at the repository root and builds **both** implementations;
there is no separate `lynxer/Makefile`.

## Artifacts

- `lynxer/lynxer` — the interpreter.
- `lynxer/lynxer-arm64` — the ARM64 interpreter, from `buildLynxerArm64`.
- `lynxer/stdlib/*.so` — the stdlib backends. The Rust-backed modules are
  produced by `cargo` under `lynxer/build/rust`; the C++ ones are compiled
  from `stdlib/*.cpp`.

The Rust workspace has ten member crates
(`LYNXER_RUST_MODULE_NAMES` in the Makefile): the nine module backends `game`,
`image`, `json`, `lua`, `network`, `server`, `sound`, `sqldb`, `tui`, plus
`ffi`, which is an intentional no-op cdylib — the `ffi*` builtins are
implemented in C++.

## Install

```bash
sudo ./lynxer/lynxer --install      # copy to /usr/bin/lynxer
sudo ./lynxer/lynxer --uninstall    # remove /usr/bin/lynxer
```

`--install` copies the running executable to `/usr/bin/lynxer`; `--uninstall`
removes it. Keep the matching `stdlib/` directory next to the installed binary,
or imports will not resolve. See [CLI.md](CLI.md#installing).

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
| `LYNXER_SKIP_DISPLAY=1` | `make testLynxer` | skip the display and audio fixtures (used by CI) |

The first three are described in more detail in
[CLI.md](CLI.md#environment-variables) and [limitations.md](limitations.md).

## See also

- [README.md](README.md) — the documentation index and what `make testLynxer` runs.
- [CLI.md](CLI.md) — every flag and exit code.
