# Bobcat IDE (native GUI)

A native GUI IDE built with [iced](https://iced.rs) in pure Rust. It is a
rebranded copy of `lynxer/ide` (the original remains in place). No Tauri, no
webview, no React, no Monaco: the file explorer, tabbed editor, syntax
highlighting, Run/Lint/Format and output panel are all Rust.

## Features

- Modern dark IDE layout: top command bar, activity rail, collapsible
  explorer, styled tabs, breadcrumb bar, Problems / Output panel, and a status
  bar with error/warning counts, `Ln/Col`, encoding and engine version.
- Official-style logo and 16 embedded SVG icons, tinted to the theme at
  runtime (`include_bytes!`, no runtime asset paths).
- Error and warning detection: engine lint output is parsed into a Problems
  panel with clickable rows that jump to the offending line.
- Autocomplete: keywords, types, builtins and words from the current document
  (after 3 characters), with `Up`/`Down` navigation and `Tab`/`Enter` accept.
- Expandable directory tree with Refresh, and `Tab` inserts four spaces.
- Lynxer syntax highlighting for `.lynx` files (other files open as plain
  text).
- Run (`F5`), Lint and Format through the external engine, off the UI thread
  with a timeout. `Ctrl+S` to save.

## Build and run

```bash
cd bobcat/ide
cargo build --release
./target/release/bobcat-ide
```

Requires a Rust toolchain and the system libraries iced needs to open a
window (X11 / Wayland, a GPU or software renderer). All Rust dependencies are
crates.io packages resolved by Cargo.

## Runtime dependency

The IDE is a client of the `lynxer` interpreter: startup validation and every
Run / Lint / Format action shell out to it. It is not needed to build the IDE,
but the IDE refuses to run a program when the engine is missing. Provide it by:

- building it from the repository root with `make build` (or `make lynxer`);
- putting `lynxer` on `PATH`;
- clicking **Set Lynxer** in the toolbar and picking the executable;
- starting with `--lynxer <path>`.

## Relationship to the original

`bobcat/ide/src/main.rs` differs from `lynxer/ide/src/main.rs` only in the
window title and module doc comment; the package is renamed to `bobcat-ide`.
The original IDE stays in `lynxer/ide/`.
