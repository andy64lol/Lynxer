# Lynxer IDE (native Rust)

A **native GUI** IDE for the Lynxer language, built with [iced](https://iced.rs)
in pure Rust. No Tauri, no webview, no React, no Monaco: the file explorer,
tabbed editor, Lynxer syntax highlighting, Run/Lint/Format and the output panel
are all Rust.

## Features

- Modern dark IDE layout: a compact top command bar, a left activity rail, a
  collapsible explorer, styled tabs, a **breadcrumb** bar, a bottom panel with
  **Problems / Output** tabs, and a status bar showing error/warning counts,
  `Ln/Col`, encoding, language and interpreter version. A welcome screen is
  shown when no file is open. `EX` / `OUT` in the rail toggle the side panels.
- **Official logo**: `assets/lynxer.png` (a copy of the repository root
  `assets/lynxer.png`, the teal hexagon with an "L") is used as the window /
  taskbar icon and as the brand mark in the top bar and welcome screen.
- **SVG icons**: 16 symbolic icons (`assets/icons/*.svg`) on the toolbar,
  activity rail, tabs, tree chevrons and panel tabs. They are embedded with
  `include_bytes!` (no runtime asset paths) and tinted to the theme at runtime
  via iced's SVG colour filter.
- **Error and warning detection**: `lynxer --lint` output (warnings, lint
  errors, parser/source errors, missing files) is parsed into a Problems panel.
  Files are linted automatically on open/save/format; rows are clickable and
  jump to the offending line. Counts appear in the panel tab and status bar.
- **Autocomplete**: a compact suggestion popup (at most 6 entries) offers
  Lynxer keywords, types, the full builtin registry (286 names) and words from
  the current document. It only appears after **3 characters**, never inside a
  comment/string or in the middle of a word, and hides when there is nothing
  left to complete. `Up`/`Down` navigate, `Tab`/`Enter` accept, `Esc` dismiss
  (and a dismissed word stays quiet until it changes). The popup is rendered in
  a permanently present layer, so **typing continues uninterrupted** while it
  is visible.
- Expandable **directory tree**: folders open in place to any depth with
  `[+]` / `[-]` markers and level indentation, plus a **Refresh** button.
  Expanded folders survive a refresh; clicking a file opens it.
- **Tab inserts four spaces** of indentation (iced discards the Tab key by
  default); every other editor key binding is preserved.
- Rich Lynxer syntax highlighting (a port of the old Monaco Monarch grammar,
  extended with builtins, function calls, constants, attributes, doc comments,
  multi-character operators and hex/binary/float/exponent numbers), implemented
  against iced's `Highlighter` trait. **Highlighting is enabled only for
  `.lynx` files**; any other file opens as plain text.
- Run (`F5`), Lint (`--lint`) and Format (`--format`) through the `lynxer`
  interpreter, executed off the UI thread with a 30s timeout
- Output panel with stdout, stderr, exit code and timeout status
- `Ctrl+S` to save, `F5` to run, `Tab` to indent (or accept a completion)
- Configurable `lynxer` executable path via **Set Lynxer** (the picked binary
  is validated before it is accepted)
- Error checking: the interpreter is validated at startup and before every
  command, spawn failures and timeouts are reported as errors, `Set Lynxer`
  rejects invalid picks, and file operations report exactly where they failed

## Assets

`assets/lynxer.png` is the official Lynxer logo, copied verbatim from the
repository root `assets/`. It is embedded with `include_bytes!` in
`src/icons.rs`, decoded once at startup for the window icon, and drawn with
iced's image widget for the in-app brand marks. Keep it in sync with the root
copy if the logo ever changes.

`assets/icons/` holds the 16 monochrome 24x24 SVG icons used by the chrome.
Each is a simple alpha mask (`<svg viewBox="0 0 24 24" stroke="#ffffff">`), so
`iced`'s `svg::Style.color` filter can tint it to any palette colour. Add a new
icon by dropping an SVG in that directory, adding an `include_bytes!` constant
in `src/icons.rs`, and listing it in the `ALL` test array. Validate with
`rsvg-convert` or the `icons::tests` unit test.

## Dependencies

- **Build time:** a Rust toolchain plus the system libraries iced needs to open
  a window (X11 / Wayland, and a GPU or software renderer). All Rust
  dependencies are crates.io packages resolved by Cargo.
- **Runtime (required): the `lynxer` interpreter.** The IDE is a client of it:
  startup validation and every Run / Lint / Format action shell out to
  `lynxer`. It is *not* needed to build the IDE, but the IDE refuses to run a
  program when it is missing and explains why in the status bar and output
  panel. Provide it in one of three ways:
  - build it from the repository root with `make build` (or `make lynxer`
    here), producing `../../lynxer/lynxer`;
  - put `lynxer` on `PATH`;
  - click **Set Lynxer** in the toolbar and pick the executable;
  - start the IDE with `--lynxer <path>` to preselect an interpreter (the
    interpreter supplies this itself when it launches the IDE).

  `make check-lynxer` verifies the dependency, `make deps` checks everything,
  and `make where` prints its absolute path. `make run` checks it before
  launching.

## Build and run

```bash
make            # list all targets (help)
make build      # debug build
make release    # optimized build
make run        # build + launch
make check      # cargo check
make clean      # cargo clean
make where      # print absolute paths of the executables
```

Requires a Rust toolchain and the system libraries iced needs to open a
window (X11 / Wayland, and a GPU or software renderer). All Rust dependencies
are crates.io packages resolved by Cargo.

## Where the executable is

- Debug: `target/debug/lynxer-ide`
- Release: `target/release/lynxer-ide`

`make build` / `make release` print the absolute path when they finish;
`make where` prints both.

## Running Lynxer programs

The IDE shells out to the `lynxer` interpreter. Put `lynxer` on `PATH`, or
click **Set Lynxer** in the toolbar to pick the executable. A build of this
repository produces it at `../../lynxer/lynxer` (relative to `lynxer/ide/`);
build it with `make build` from the repo root.
