# Bobcat

Bobcat is a standalone IDE project extracted from [Lynxer](../README.md)'s
built-in editors. It contains two independent IDEs that share nothing with the
originals at runtime — the Lynxer `--cli-ide` (in `lynxer/shell.cpp`) and the
native GUI IDE (`lynxer/ide/`) are both left in place, untouched.

| Component | Directory | What it is |
| --- | --- | --- |
| **Bobcat CLI** | [`cli/`](cli/) | A standalone full-screen terminal IDE (ratatui) for `.lynx`, `.wynx`, `.wpf` |
| **Bobcat IDE** | [`ide/`](ide/) | A native GUI IDE (iced), rebranded copy of `lynxer/ide` |

Both IDEs are clients of the language engines: they shell out to `lynxer`
and/or `wynxer` for Run, Lint and Format. They do not embed a compiler.

## Quick start

```bash
# CLI IDE — no display needed
cd cli
cargo build --release
./target/release/bobcat-cli path/to/file.lynx      # or .wynx / .wpf

# GUI IDE — needs X11/Wayland + a GPU (or software renderer)
cd ../ide
cargo build --release
./target/release/bobcat-ide
```

## Keys (CLI IDE)

| Key | Action |
| --- | --- |
| `Ctrl+O` | Open a file (type a path, Enter) |
| `Ctrl+S` | Save |
| `F5` / `F6` / `F7` | Run / Lint / Format |
| `Ctrl+P` | Toggle the output panel |
| `F1` | Show the key help in the output panel |
| `Ctrl+Q` / `Esc` | Quit / close the output panel |

Editing uses standard arrows, Home/End, PageUp/PageDown, Backspace, Delete,
Enter, and `Tab` (four spaces).

## Relationship to Lynxer

Bobcat is a separate frontend. The extraction is a copy, not a move:

- `lynxer/shell.cpp` still contains the original `--cli-ide`.
- `lynxer/ide/` still contains the original native IDE.
- `bobcat/cli/` reimplements that terminal IDE as a standalone binary.
- `bobcat/ide/` is a rebranded copy of the native IDE (package `bobcat-ide`).

See [`cli/README.md`](cli/README.md) and [`ide/README.md`](ide/README.md) for
each component's details.
