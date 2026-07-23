# Installation

## Requirements

- Python 3.8 or later
- `cython` Python package (for `rawPyx` support)
- `setuptools` Python package (Cython shim on Python 3.12+)
- A C compiler (`gcc` or `cc`) for Cython compilation

---

## Build

Simply run:
`make build`
and you would end up with an executeable of lynxer.

---

## Makefile targets

| Target | Description |
|--------|-------------|
| `make build` | Build the package |
| `make test` | Run the full test suite |
| `make clean` | Remove `__pycache__` and `.pyc` files |
| `make help` | Print available targets |

---

## Verifying the install

```bash
lynxer --version   # Lynxer 0.1.4
lynxer syntax.lynx # run the syntax showcase
```
