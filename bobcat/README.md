# Bobcat

Bobcat is a standalone IDE project extracted from [Lynxer](../README.md)'s
former built-in editors. Lynxer's `--cli-ide`/`--gui-ide` and `lynxer/ide/`
have been removed, so Bobcat is now the only IDE for the toolchain.

| Component | Directory | What it is |
| --- | --- | --- |
| **Bobcat CLI** | [`cli/`](cli/) | A standalone full-screen terminal IDE (ratatui) for `.lynx`, `.wynx`, `.wpf` |
| **Bobcat IDE** | [`ide/`](ide/) | A native GUI IDE (iced), rebranded copy of `lynxer/ide` |

Both IDEs enter a **specialised Wynxer mode** when launched in a project
with `.wynxcfg` and a `.wpf` page: the entry page opens automatically and the
toolbar becomes Build & Serve / Check / Format for the whole project.

Both IDEs are clients of the language engines: they detect the file type and
drive `lynxer` for `.lynx` and `wynxer` for `.wynx`/`.wpf` (highlighting,
completions, build/lint/format). Neither embeds a compiler.

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

Bobcat is a separate frontend. Lynxer's built-in IDEs were removed after the
extraction:

- `bobcat/cli/` reimplements the former terminal IDE as a standalone binary.
- `bobcat/ide/` is a rebranded copy of the former native IDE (package
  `bobcat-ide`). It is now the only GUI IDE for the toolchain.

See [`cli/README.md`](cli/README.md) and [`ide/README.md`](ide/README.md) for
each component's details.
