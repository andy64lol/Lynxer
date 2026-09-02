# Installation

## Option 1 — Download the executable (recommended)

Download the latest pre-built binary for your platform from the
[GitHub Releases page](https://github.com/your-org/lynxer/releases).

No Python installation required. Unzip, place `lynxer` somewhere on your `PATH`, and you're done.
The native build targets the Linux host architecture. Supported hosts are
64-bit x86-64 (`amd64`) and ARM64 (`aarch64`). Other operating systems,
architectures, and Python ABIs are rejected before the build starts so their
native extensions and syscall tables cannot be mixed.

```bash
lynxer --version   # confirm it works
lynxer hello.lynx  # run a Lynxer source file
```

---

## Option 2 — Build from source

### Requirements

- Python 3.14 or later
- `cython` Python package (for `rawPyx` support)
- `setuptools` Python package (Cython shim on Python 3.12+)
- A C++ compiler (`g++` or `clang++`) for Lynxer's native memory extension
- A C compiler (`gcc` or `cc`) for Cython compilation

### Full build

Bundles the complete standard library (all stdlib modules included, heavier):

```bash
make build
```

The build automatically compiles `lynxer/cpp.cpp` for the active Python
interpreter and includes the resulting native extension in the executable.
It also installs and bundles the architecture-aware `system-calls` tables for
the active Linux host. Run `make platform-check` to perform the same platform
check without building.
To compile only that extension during development, run:

```bash
make buildCpp
```

Produces a single-file binary at `dist/lynxer`.

### Lite build

Produces a smaller binary with a reduced standard library — useful for
embedding or size-constrained targets where the full stdlib is not needed:

```bash
make buildLite
```

Produces `dist/lynxer-lite`. The lite build excludes heavier optional stdlib
modules while keeping the core language and essential utilities.

---

## Makefile targets

| Target | Description |
|--------|-------------|
| `make build` | Full build — all stdlib modules included |
| `make buildLite` | Lite build — reduced stdlib, smaller binary |
| `make buildCpp` | Compile the native C++ memory extension in place |
| `make clean` | Remove `__pycache__` and `.pyc` files |
| `make help` | Print available targets |

---

## Verifying the install

See the complete [CLI reference](CLI.md) for every command and alias.

```bash
lynxer --version        # Lynxer 0.1.8
lynxer --compile a.lynx # compile to bytecode
lynxer a.lynxc          # run compiled bytecode
lynxer --format a.lynx  # format the source file in place
lynxer --format-oneline a.lynx
lynxer --ast a.lynx    # print the parsed abstract syntax tree
lynxer --lint a.lynx    # check syntax without running the program
```

`--format` applies readable indentation and spacing while preserving ordinary
comments. `--format-oneline` compacts valid source into one line. Both format
commands rewrite the file in place. `--lint` only tokenizes and parses the
file; it does not execute the program.
