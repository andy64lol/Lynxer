# Build and install

Clynxer is a C++17 Linux executable. It does **not** need Python.

## Requirements

- A C++17 compiler (`g++` or `clang++`)
- `make`
- `cmake` (for `network` / `server` and their deps)
- OpenSSL + Boost (for `network.so` / `server.so`)

Optional system libraries (skipped with a warning when missing): SDL2, ncurses,
Lua 5.4, libpng — see `stdlib/libs.mk`.

## Build from the repository root

```bash
make cmake              # configure clynxer/build
make clynxerDeps        # fetch cpp-httplib + Crow
make buildCLynxer       # configure, fetch deps, build binary + stdlibs
make testCLynxer        # smoke + stdlib fixtures
```

Or from `clynxer/`:

```bash
make cmake
make deps
make all
make test
```

Outputs:

- `clynxer/clynxer` — interpreter
- `clynxer/stdlib/*.so` — native stdlib backends (including `network.so` /
  `server.so` via CMake)

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
