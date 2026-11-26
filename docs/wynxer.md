# Wynxer

Wynxer is a sibling project in this repository: a Lynxer-like language built
for the web. It borrows Lynxer's surface syntax — typed variables like
`int x = 5;` (never `let`), `fn` functions, `if`/`elif`/`else`, `while`, `for`,
macros — but targets websites instead of the native machine.

Wynxer compiles logic to JavaScript (and has a WebAssembly backend) and pages
written in **`.wpf` (Wynxer Page Format)** to HTML and CSS, so authors never
write HTML by hand. It is a separate binary (version 0.1.0) with a Rust
backend, built from the [`wynxer/`](https://github.com/andy64lol/Lynxer/tree/main/wynxer)
directory. The two codebases share only language concepts.

## Quick start

From the repository root:

```bash
make buildWynxer     # release binary at wynxer/target/release/wynxer
make testWynxer      # unit tests
make checkWynxer     # version + element count + example build/check
```

Or build directly:

```bash
cd wynxer
cargo build --release
./target/release/wynxer init my-app
cd my-app
./../../target/release/wynxer build     # generate build/index.html, app.js, styles.css
./../../target/release/wynxer run       # build, then serve on the configured port
```

## A tiny project

`index.wpf`:

```wpf
import script app;
import styles styles;

page index {
  page.title = "Hello";

  contents {
    element.header1("Hello, Wynxer!");
    element.text("This page was generated from .wpf.");
    element.newline();
    element.button("Click me!", app.onClick);
  }

  scripts {
  }
}
```

`app.wynx`:

```wynx
fn onClick() {
    int x = 5;
}
```

`wynxer build` turns these into `build/index.html`, `build/app.js` and
`build/styles.css`.

## Where to go next

| Page | Contents |
| --- | --- |
| [wynxer-language.md](wynxer-language.md) | Types, typed declarations, functions, control flow, structs, enums, macros |
| [wynxer-wpf.md](wynxer-wpf.md) | The page format and the 56 element kinds |
| [wynxer-cli.md](wynxer-cli.md) | Every `wynxer` command, config keys and tooling |
| [wynxer-js-abi.md](wynxer-js-abi.md) | JS interop ABI and Node/npm packages |

## Status

Pre-alpha. The parser, AST, lexer, WPF parser, build pipeline and CLI are
working; the WebAssembly backend and the web standard library are planned.
See `wynxer/todo.md` in the repository for the full roadmap.
