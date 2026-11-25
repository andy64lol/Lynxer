# Wynxer CLI

The `wynxer` binary is built from `wynxer/` with `cargo build --release`.

## Commands

| Command | What it does |
| --- | --- |
| `wynxer` / `wynxer version` | Print the version (`wynxer 0.1.0`) |
| `wynxer --version` | Same |
| `wynxer elements` | List all 56 element kinds |
| `wynxer parse <file>` | Parse a `.wpf` or `.wynx` file and print its structure |
| `wynxer ast <file>` | Dump the full parsed AST |
| `wynxer build [path]` | Generate `build/index.html`, `app.js`, `styles.css` |
| `wynxer check [path]` | Parse + validate element kinds |
| `wynxer serve [path] [--port N]` | Serve the build directory |
| `wynxer run [path] [--port N]` | Build, then serve on the port from `.wynxcfg` |
| `wynxer init [name]` | Scaffold a project (refuses to overwrite) |
| `wynxer lint <file>` | Lint a script/page |
| `wynxer format <file> [--write]` | Format a script (stdout or write back) |
| `wynxer --add <pkg>... [--dev]` | npm install + record in `.wynxcfg` |
| `wynxer --remove <pkg>...` | npm uninstall + update config |

## Project scaffolding

`wynxer init my-app` creates:

```
my-app/
  .wynxcfg        # settings (name, entry, script, output, port, deps, node, type)
  package.json    # generated from .wynxcfg for npm/node tooling
  README.md
  index.wpf
  app.wynx
  styles.css
  .gitignore
  build/
```

`.wynxcfg` uses the same `key = value` format as `lynxer.config`:

```
name = my-app
entry = index.wpf
script = app.wynx
output = build
port = 3000
deps =
devDeps =
node = 20
type = commonjs
package_manager = npm
target = browser
```

`target = browser` (default) or `node` (uses `type = module|commonjs`).
`wynxer build` keeps `package.json` in sync with the config.

## Errors

Parse and lint errors are reported with a source excerpt and caret:

```text
app.wynx:3:1: error: expected Semicolon
     |
   3 | }
     | ^ expected Semicolon
```

## Exit codes

`0` on success, `1` on any error.
