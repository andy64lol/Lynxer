# Bobcat CLI IDE

A standalone full-screen terminal IDE, extracted from Lynxer's former
`--cli-ide` (now removed from `lynxer/shell.cpp`). Built with
[ratatui](https://ratatui.rs) and [crossterm](https://github.com/crossterm-rs/crossterm).

## Features

- Full-screen editor with line numbers, cursor highlight, scrolling, and a
  status bar (`Ln/Col`, engine, dirty marker).
- Output panel (toggle with `Ctrl+P`) showing stdout/stderr/exit code from
  Run / Lint / Format.
- Auto-detects the engine from the file extension:
  - `.lynx` → `lynxer` (`lynxer <file>`, `--lint`, `--format`)
  - `.wynx` / `.wpf` → `wynxer` (`wynxer build <dir>`, `lint`, `format --write`)
- Saves before running, reloads the file after formatting.
- No compiler embedded: `lynxer` and/or `wynxer` must be on `PATH`.

## Build and run

```bash
cd bobcat/cli
cargo build --release
./target/release/bobcat-cli            # start empty
./target/release/bobcat-cli app.lynx   # open a file on startup
```

## Keys

| Key | Action |
| --- | --- |
| `Ctrl+O` | Open file (prompt at the status bar) |
| `Ctrl+S` | Save |
| `F5` | Run |
| `F6` | Lint |
| `F7` | Format |
| `Ctrl+P` | Toggle output panel |
| `F1` | Key help |
| `Esc` | Close output panel |
| `Ctrl+Q` | Quit |
| Arrows / Home / End / PageUp / PageDown | Move cursor |
| Backspace / Delete / Enter / Tab | Edit (`Tab` = 4 spaces) |
