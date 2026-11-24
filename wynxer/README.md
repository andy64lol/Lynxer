# Wynxer

Wynxer is a Lynxer-like language built for websites. It compiles to
JavaScript and WebAssembly for logic, and to HTML and CSS for
presentation. Pages are written in **`.wpf` (Wynxer Page Format)**, a very
simplified DSL, so the author never has to write HTML directly.

Wynxer is a separate binary from `lynxer` (version 0.1.0) and uses a Rust
backend to generate everything.

## Status

Pre-alpha. The command-line shell is wired up; the compiler pipeline is
scaffolding. See `todo.md` for the roadmap.

## Build

```bash
cd wynxer
cargo build
cargo run -- version
```

## Example

See `examples/`: `index.wpf` is the page (using the WPF DSL), `app.wynx` is
the script, and `styles.css` is the stylesheet.

```bash
cargo run -- parse examples/index.wpf
cargo run -- elements
```
