# CLI

The Clynxer executable is `clynxer` (or `lynxer` after `clynxer --install`).

```bash
make buildCLynxer
./clynxer/clynxer --help
```

## Run

```bash
clynxer program.lynx
```

Exit code `0` on success, `1` on error.

## Inspect and format

| Flag | Effect |
|------|--------|
| `--lint <file>` | Parse only; report syntax errors |
| `--ast <file>` | Print the abstract syntax tree |
| `--format <file>` | Format the file in place |
| `--format-oneline <file>` | Compact to one physical line |

## Compile to an executable

`--compile` (alias `--bundle`) builds one standalone ELF that embeds the
program, imported `.lynx` modules, imported `.so` libraries, and optional data
files. There is **no** `.lynxc` bytecode path.

```bash
clynxer --compile app.lynx -o app
clynxer --compile app.lynx helpers.lynx vendor/lib.so \
        --include assets/message.txt -o app
```

| Flag | Effect |
|------|--------|
| `--include <file>` | Embed a module, native library, or data asset |
| `-o` / `--output <name>` | Output executable name |

Inside a compiled program, `bundledFile(name)` and `bundledFiles()` expose
non-`.lynx`/`.so` includes. See [builtins.md](builtins.md#bundled-files).

## Other flags

| Flag | Effect |
|------|--------|
| `--list-stdlibs` | List stdlib modules and their `////` docstrings |
| `--validate-executeable` | Run the interpreter self-check |
| `--version` | Print version |
| `--install` | Install as `/usr/bin/lynxer` (may need sudo) |
| `--uninstall` | Remove `/usr/bin/lynxer` |

Run install/uninstall from the built binary, not from sources alone.
