# Build and install

Clynxer is a C++17 Linux executable. It does **not** need Python.

## Requirements

- A C++17 compiler (`g++` or `clang++`)
- `make`
- `cmake` (for `network` / `server` and their deps)
- OpenSSL + Boost (for `network.so` / `server.so`)
- Git and network access for the header-only native dependencies:
  cpp-httplib, Crow, and nlohmann/json

Optional system libraries (skipped with a warning when missing): SDL2, ncurses,
Lua 5.4, libpng — see `stdlib/libs.mk`.

## Build from the repository root

```bash
make cmake              # wipe third_party/, then configure clynxer/build
make clynxerDeps        # fetch cpp-httplib + Crow + nlohmann/json
make buildCLynxer       # clean third-party inputs, fetch deps, build everything
make testCLynxer        # smoke + stdlib fixtures
```

Or from `clynxer/`:

```bash
make cmake              # also clears third_party/ before configuring
make deps               # fetches clean copies of all native headers
make all                # rebuilds the interpreter and native stdlibs
make test
```

Outputs:

- `clynxer/clynxer` — interpreter
- `clynxer/stdlib/*.so` — native stdlib backends (including `network.so` /
  `server.so` via CMake)

Every Clynxer build intentionally removes `third_party/*` and the staged
`stdlib/httplib.h` before fetching dependencies again. This avoids reusing
partial or stale third-party checkouts. The JSON backend includes
`third_party/json/single_include/nlohmann/json.hpp`.

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
