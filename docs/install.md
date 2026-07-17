# Installation

## Requirements

- Python 3.8 or later
- `cython` Python package (for `rawPyx` support)
- `setuptools` Python package (Cython shim on Python 3.12+)
- A C compiler (`gcc` or `cc`) for Cython compilation

`make install` handles the Python packages automatically.

---

## Option A — System-wide via Makefile

```bash
git clone https://github.com/andy64lol/Lynxer.git
cd Lynxer

# installs Python deps, then copies lynxer to /usr/local/bin
make install          # may need sudo

# or install to ~/.local/bin (no sudo needed)
make install PREFIX=~/.local
```

After installing, the `lynxer` command is available everywhere:

```bash
lynxer my_program.lynx
lynxer --version
lynxer --help
```

---

## Option B — pip (editable / development)

```bash
git clone https://github.com/andy64lol/Lynxer.git
cd Lynxer
pip install -e .
```

This installs `cython` and `setuptools` automatically via `install_requires`.

---

## Option C — Run without installing

No install needed; just run directly:

```bash
python lynxer/shell.py my_program.lynx
# or
python -m lynxer my_program.lynx
```

---

## Uninstall

```bash
make uninstall   # removes /usr/local/bin/lynxer and /usr/local/share/lynxer
```

---

## Makefile targets

| Target | Description |
|--------|-------------|
| `make install` | Install deps + copy files to `$(PREFIX)/bin` and `$(PREFIX)/share` |
| `make install PREFIX=~/.local` | Install to a user-local prefix (no sudo) |
| `make uninstall` | Remove installed files |
| `make pip-install` | Editable pip install for development |
| `make test` | Run the full test suite |
| `make clean` | Remove `__pycache__` and `.pyc` files |
| `make help` | Print available targets |

---

## Verifying the install

```bash
lynxer --version   # Lynxer 0.1.2
lynxer syntax.lynx # run the syntax showcase
```
