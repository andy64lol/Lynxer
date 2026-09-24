# Installation

The repository contains two implementations: the original Python Lynxer and
the standalone Lynxer implementation. Lynxer build instructions and its
native dependency policy are documented in
[lynxer/docs/install.md](../lynxer/docs/install.md).

## Option 1 — Download the executable (recommended)

Download the latest pre-built binary for your platform from the
[GitHub Releases page](https://github.com/your-org/clynxer/releases).

No Python installation required. Unzip, place `clynxer` somewhere on your `PATH`, and you're done.
The native build targets the Linux host architecture. Supported hosts are
64-bit x86-64 (`amd64`) and ARM64 (`aarch64`). Other operating systems,
architectures, and Python ABIs are rejected before the build starts so their
native extensions and syscall tables cannot be mixed.

```bash
clynxer --version   # confirm it works
clynxer hello.lynx  # run a Lynxer source file
```

---

## Option 2 — Build from source

### Requirements

- Python 3.14 or later
- `cython` Python package (for `rawPyx` support)
- `system-calls` Python package (required for the named Linux syscall built-ins)
- `setuptools` Python package (Cython shim on Python 3.12+)
- A C++ compiler (`g++` or `clang++`) for Lynxer's native memory extension
- A C compiler (`gcc` or `cc`) for Cython compilation

### Full build

Bundles the complete standard library (all stdlib modules included, heavier):

```bash
make buildLynxer
```

The build automatically compiles `clynxer/cpp.cpp` for the active Python
interpreter and includes the resulting native extension in the executable.
It also installs and bundles the required architecture-aware `system-calls`
tables for the active Linux host. Run `make platform-check` to perform the
same platform check without building.
To compile only that extension during development, run:

```bash
make buildCpp
```

Produces a single-file binary at `dist/clynxer`.

### Lite build

Produces a smaller binary with a reduced standard library — useful for
embedding or size-constrained targets where the full stdlib is not needed:

```bash
make buildLynxerLite
```

Produces `dist/clynxer-lite`. The lite build excludes heavier optional stdlib
modules while keeping the core language and essential utilities.

---

## Makefile targets

| Target | Description |
|--------|-------------|
| `make build` | Everything — Python full + lite, plus Lynxer |
| `make buildLynxer` | Python full build — all stdlib modules included (`dist/clynxer`) |
| `make buildLynxerLite` | Python lite build — reduced stdlib, smaller binary (`dist/clynxer-lite`) |
| `make buildLynxer` | Lynxer binary plus its C++/Rust stdlib backends |
| `make buildCpp` | Compile the native C++ memory extension in place |
| `make clean` | Remove `__pycache__` and `.pyc` files |
| `make help` | Print available targets |

---

## Verifying the install

See the complete [CLI reference](CLI.md) for every command and alias.

```bash
clynxer --version        # Lynxer 0.1.8
clynxer --compile a.lynx # compile to bytecode
clynxer a.lynxc          # run compiled bytecode
clynxer --format a.lynx  # format the source file in place
clynxer --format-oneline a.lynx
clynxer --ast a.lynx    # print the parsed abstract syntax tree
clynxer --lint a.lynx    # check syntax without running the program
```

`--format` applies readable indentation and spacing while preserving ordinary
comments. `--format-oneline` compacts valid source into one line. Both format
commands rewrite the file in place. `--lint` only tokenizes and parses the
file; it does not execute the program.
