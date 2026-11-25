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

## Start a project

Like `lynxer --init`, `wynxer init [name]` scaffolds a project in the current
directory and refuses to overwrite existing files. It generates `index.wpf`,
`app.wynx`, `styles.css`, a `README.md`, a `.gitignore`, and a `.wynxcfg`
configuration file (the Wynxer equivalent of `lynxer.config`):

```bash
wynxer init my-app
cd my-app
wynxer build        # generate build/index.html, app.js, styles.css
wynxer run          # build, then serve on the port from .wynxcfg (default 3000)
```

`.wynxcfg` uses `key = value` lines (`name`, `entry`, `script`, `output`,
`port`, plus node keys `deps`, `devDeps`, `node`, `type`, `package_manager`,
`target`). Override the port with `wynxer run --port N`. `wynxer build` keeps
`package.json` in sync with the config.

### Node packages and JS interop

Wynxer uses Node/npm to manage JS packages:

```bash
wynxer --add lodash chalk          # npm install + record in .wynxcfg
wynxer --add typescript --dev      # save as a dev dependency
wynxer --remove chalk              # npm uninstall + update config
```

Wynxer source can call JavaScript through the `wynxer_js_abi_v1` builtins:
`js("...")` (raw statement), `jsExpr("...")` (raw expression),
`jsImport("pkg", "names")` (hoisted package import), `jsRequire("pkg")` and
`jsGlobal("name")`. See [docs/js-abi.md](docs/js-abi.md).

## Example

See `examples/`: `index.wpf` is the page (using the WPF DSL), `app.wynx` is
the script, and `styles.css` is the stylesheet.

```bash
cargo run -- parse examples/index.wpf
cargo run -- elements
```
