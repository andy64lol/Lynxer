# CLI

The Wynxer executable is `wynxer`. `--version` (or `-v`) prints the
version and exits `0`; otherwise run it from where it was built.

```bash
cargo build
wynxer --version
```

## Subcommands

| Subcommand | What it does |
| --- | --- |
| `wynxer` (no subcommand) | Print the version and exit |
| `wynxer version` | Print the version and exit |
| `wynxer elements` | List every element kind Wynxer understands (56 kinds) |
| `wynxer parse <file.wpf>` | Parse a `.wpf` file and print its structure |
| `wynxer build [path]` | Compile a project into `build/` (JS, WASM, HTML, CSS) |
| `wynxer serve [path] [--port N]` | Dev server serving the build directory |
| `wynxer check [path]` | Parse + validate element kinds |
| `wynxer init [name]` | Scaffold a new project in the current directory |
| `wynxer run [path] [--port N]` | Build, then serve on the port from `.wynxcfg` |
| `wynxer ast <file>` | Print the parsed AST of a `.wynx` or `.wpf` file |
| `wynxer lint <file>` | Lint a script/page (unused variables, no `let`, unknown elements) |
| `wynxer format <file> [--write]` | Format a `.wynx` script (print or write back) |
| `wynxer --add <pkg>... [--dev]` | Install npm packages via Node/npm; record in `.wynxcfg` + `package.json` |
| `wynxer --remove <pkg>...` | Uninstall npm packages via Node/npm; update `.wynxcfg` + `package.json` |

## Node packages and the JS ABI

```bash
wynxer --add lodash chalk           # npm install, record in .wynxcfg deps
wynxer --add typescript --dev       # save as devDependencies
wynxer --remove chalk               # npm uninstall
```

`.wynxcfg` carries the node keys (`deps`, `devDeps`, `node`, `type`,
`package_manager`, `target`). `wynxer build` keeps `package.json` in sync and
selects the JS module target from `target`/`type`. Wynxer can call JavaScript
through the `wynxer_js_abi_v1` builtins (`js`, `jsExpr`, `jsImport`,
`jsRequire`, `jsGlobal`) — see [js-abi.md](js-abi.md).

## Errors and tooling

Parse and lint errors are reported with a source excerpt and caret:

```text
app.wynx:3:1: error: expected Semicolon
     |
   3 | }
     | ^ expected Semicolon
```

`wynxer ast` dumps the full AST (`{:#?}`) for debugging the parser.
`wynxer lint` flags unused variables, `let` declarations (Wynxer uses typed
declarations), and unknown `.wpf` element kinds. `wynxer format` normalises
indentation/operators and writes to stdout unless `--write` is given.

## Running a program

```bash
wynxer init my-app # scaffold a project (creates .wynxcfg)
cd my-app
wynxer build       # compile the project in the current directory
wynxer run         # build, then serve on http://localhost:<port>
```

Anything the parser does not recognise is treated as an error.

## Projects

A Wynxer project is a directory with a `.wynxcfg` file and an `index.wpf`
entry page. `wynxer --init` generates this layout and refuses to overwrite
existing files:

```
my-app/
  .wynxcfg        # project settings (name, entry, script, output, port)
  README.md
  index.wpf
  app.wynx
  styles.css
  .gitignore
  build/          # generated output
```

`.wynxcfg` uses the same `key = value` format as `lynxer.config`. `--init`
creates a starter page, script, and stylesheet; `--run` reads `output` and
`port` from the config (override the port with `wynxer run --port N`).

## Exit codes

| Code | Meaning |
| --- | --- |
| `0` | success |
| `1` | any error |

## Environment variables

| Variable | Meaning |
| --- | --- |
| `WYNXER_ROOT` | Override the project root used by the language server |

## See also

- [wpf.md](wpf.md) — the Wynxer Page Format
- [elements.md](elements.md) — the complete element API
- [language.md](language.md) — the Wynxer language
- [js-abi.md](js-abi.md) — JS interop and Node/npm packages
