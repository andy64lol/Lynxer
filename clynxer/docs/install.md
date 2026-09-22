# Build and install

Clynxer is a C++17 Linux executable. It does **not** need Python.

## Requirements

- A C++17 compiler (`g++` or `clang++`)
- `make`
- A Rust toolchain (`cargo`) for the `game`, `json`, `network` and `server`
  modules. They are skipped with a warning when `cargo` is not on `PATH`; the
  rest of Clynxer still builds.
- Git and network access for the first `cargo` build (crates are fetched from
  crates.io). Nothing is compiled from vendored C/C++ sources any more.

No system OpenSSL, Boost, CMake, cpp-httplib, Crow or nlohmann/json is needed:
TLS is `rustls`, the HTTP stack is `ureq`/`tungstenite`/`axum`, and JSON is
`serde_json`.

## Build from the repository root

```bash
make cargo              # build the Rust backends (game, image, json, lua, network, server, sound, sqldb, tui, ffi)
make buildCLynxer       # build the interpreter and every native stdlib module
make testCLynxer        # contract check + smoke + stdlib fixtures
```

`buildCLynxer` already pulls in `cargo`, so the first line is only needed to
build the Rust backends on their own. The Makefile lives at the repository
root and builds both Lynxer and Clynxer; there is no separate
`clynxer/Makefile`.

Outputs:

- `clynxer/clynxer` — interpreter
- `clynxer/stdlib/*.so` — stdlib backends. `game`, `json`, `network` and
  `server` are produced by `cargo` (`clynxer/build/rust`); the rest are
  compiled from `stdlib/*.cpp`.

## Install

```bash
./clynxer/clynxer --install     # copies to /usr/bin/lynxer
./clynxer/clynxer --uninstall
```

## Quick run

```bash
./clynxer/clynxer examples/hello.lynx
./clynxer/clynxer --list-stdlibs
./clynxer/clynxer --compile examples/milestone5.lynx -o /tmp/demo
```
