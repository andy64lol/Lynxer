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
| `wynxer build [path]` | Compile a project into `build/` (JS, WASM, HTML, CSS) — planned |
| `wynxer serve [path] [--port N]` | Dev server with live reload — planned |
| `wynxer check [path]` | Parse + typecheck + WPF validation — planned |

## Running a program

```bash
wynxer build       # compile the project in the current directory
wynxer serve       # serve it on http://localhost:3000
```

Anything the parser does not recognise is treated as an error.

## Projects

A Wynxer project is a directory with a `wynxer.config` file and an `index.wpf`
entry page:

```
my-app/
  wynxer.config
  index.wpf
  app.wynx
  styles.css
  build/          # generated output
```

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
