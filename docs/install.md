# Installation

## Requirements

- Python 3.8 or later
- `cython` Python package (for `rawPyx` support)
- `setuptools` Python package (Cython shim on Python 3.12+)
- A C compiler (`gcc` or `cc`) for Cython compilation

---

## Build

Simply run:
```bash
make build
```
and you will end up with an executable binary at `dist/lynxer`.

The build process creates a Python virtual environment, installs dependencies (`flask`, `cython`, `setuptools`, `pyinstaller` and etc), and compiles a single-file binary.

---

## Makefile targets

| Target | Description |
|--------|-------------|
| `make build` | Build the package |
| `make clean` | Remove `__pycache__` and `.pyc` files |
| `make help` | Print available targets |

---

## Verifying the install

```bash
lynxer --version   # Lynxer 0.1.7b2
lynxer syntax.lynx # run the syntax showcase
```
