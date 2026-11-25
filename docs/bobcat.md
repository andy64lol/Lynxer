# Bobcat

Bobcat is a separate IDE project extracted from Lynxer's editors. It contains
two independent frontends; the originals in `lynxer/shell.cpp` (`--cli-ide`)
and `lynxer/ide/` are left untouched.

| Component | Directory | What it is |
| --- | --- | --- |
| **Bobcat CLI** | `bobcat/cli/` | Standalone full-screen terminal IDE (ratatui) for `.lynx`, `.wynx`, `.wpf` |
| **Bobcat IDE** | `bobcat/ide/` | Native GUI IDE (iced) — rebranded copy of `lynxer/ide` |

Both IDEs are clients of the language engines: they shell out to `lynxer`
and/or `wynxer` for Run, Lint and Format. Neither embeds a compiler.

## Quick start

```bash
# CLI IDE — no display needed
cd bobcat/cli
cargo build --release
./target/release/bobcat-cli path/to/file.lynx

# GUI IDE — needs X11/Wayland + a GPU (or software renderer)
cd ../ide
cargo build --release
./target/release/bobcat-ide
```

## Where to go next

| Page | Contents |
| --- | --- |
| [bobcat-cli.md](bobcat-cli.md) | Keys, engine detection, build and run |
| [bobcat-ide.md](bobcat-ide.md) | Native GUI features and runtime dependency |

## Relationship to Lynxer

The extraction is a copy, not a move:

- `lynxer/shell.cpp` still contains the original `--cli-ide`.
- `lynxer/ide/` still contains the original native IDE.
- `bobcat/cli/` reimplements the terminal IDE as a standalone binary, and
  keeps a verbatim reference copy of the C++ code under `extracted/`.
- `bobcat/ide/` is a rebranded copy of the native IDE (package `bobcat-ide`).
