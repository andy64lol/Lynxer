# CLI

The Clynxer executable is `clynxer`. There is **no** install step: `--install`
and `--uninstall` are reported as unavailable (see
[Not available](#not-available)), so the binary is always run from where it was
built.

```bash
make buildCLynxer
clynxer/clynxer --help
```

`--help` (or `-h`, or no arguments at all) prints the usage summary and exits
`0`. The rest of this page is the authoritative description of every flag.

## Running a program

```bash
clynxer program.lynx
clynxer --no-opt program.lynx    # run without the AST optimizer
```

Anything the parser does not recognise as a flag is treated as a **file path**,
not an option. `clynxer --bogus-flag` therefore reports
`clynxer: file not found: '--bogus-flag'` and exits `1`.

A `.lynxc` argument is rejected explicitly:
`clynxer: bytecode files are no longer supported; compile the .lynx source with --compile instead`.

## Exit codes

| Code | Meaning |
|------|---------|
| `0` | success (also `--help`, `--version`, a clean `--lint`) |
| `1` | any error: a missing file, a parse error, a runtime error, an unsupported flag |
| `130` | interrupted (SIGINT) |

## The AST optimizer

Before execution Clynxer rewrites the parsed program: constant folding,
short-circuit simplification of constant `and`/`or`, and dead-branch
elimination for a constant `if`, `while (false)`, and `iterate (0)`. The pass is
semantics-preserving, so output and exit codes are identical with and without
it. `--no-opt` (or `-no-opt`) disables it for the run; a **compiled** executable
ignores its command line and always optimizes.

`CLYNXER_OPT_REPORT=1` prints one line of transformation counts to stderr after
the program runs, for example
`optimizer: constant folds=10, short-circuits=2, dead branches=3`. See
[limitations.md](limitations.md#optimizer).

## Checking syntax

```bash
clynxer --lint program.lynx
```

`--lint` parses the file and reports the first syntax error without running it.
A clean file prints `Lint OK: <path>` and exits `0`. It requires exactly one
file; otherwise it prints
`clynxer: --lint requires exactly one file argument`.

## Compiling to an executable

`--compile` builds one standalone ELF that embeds the program, every
transitively imported `.lynx` module and `.so` library, and any additional
inputs. `--bundle` is an exact alias. There is **no** `.lynxc` bytecode path.

```bash
clynxer --compile app.lynx -o app
clynxer --compile app.lynx helpers.lynx vendor/lib.so \
        --include assets/message.txt -o app
```

| Flag | Aliases | Effect |
|------|---------|--------|
| `--compile` | `-c`, `--c`, `-compile`, `--bundle`, `-bundle` | compile the input files |
| `--include <file>` | `-i` | embed an extra module, native library, or data file |
| `-o <name>` | `--output`, `-name`, `--name` | name the output executable |

The first `.lynx` input is the program; any further `.lynx` or `.so` input is
embedded and importable by name. A `--include` file that is not `.lynx`/`.so` is
embedded as data and read from the program with `bundledFile(name)` /
`bundledFiles()`; see [builtins.md](builtins.md#bundled-files).

Inside a compiled executable, `--no-opt` has no effect: the payload is always
optimized.

## Listing the standard library

```bash
clynxer --list-stdlibs
```

Aliases: `--stdlibs`, `-stdlibs`, `-list-stdlibs`. It lists every module in the
interpreter's `stdlib/` directory together with its `////` docstring, and exits
`1` with `No stdlib directory found.` when that directory is missing. See
[modules.md](modules.md) for the docstring format.

## Version, help, easter egg

| Flag | Aliases | Effect |
|------|---------|--------|
| `--version` | `-v`, `-version`, `--v` | print `CLynxer <version>` |
| `--help` | `-h` | print the usage summary |
| `--easterEgg` | `-easterEgg`, `--idklmao`, `-wnwnerbcyunwrbygnubeuyxnqybxun` | print the easter egg |

The version string comes from the `version` key in `clynxer/clynxer.config`
(currently `0.1.8`).

## Not available

These names are recognised so the failure is explicit, and each exits `1` with
`clynxer: '<flag>' is not available in CLynxer yet`:

`--ast`, `--format`, `--format-oneline`, `--validate-executeable` (alias
`--validate-executable`), `--install`, `--uninstall`.

`--validate-executeable` in particular does **not** run a validator; the message
is the whole behaviour.

## Removed with the bytecode backend

These are recognised and exit `1` with
`clynxer: '<flag>' was removed with the bytecode backend; use clynxer --compile instead`:

`--view-bytecode` (aliases `--inspect-bytecode`, `--disasm`),
`--benchmark-compile` (alias `--bench-compile`), `--no-cache`.

## Environment variables

| Variable | Effect |
|----------|--------|
| `CLYNXER_OPT_REPORT=1` | print the optimizer counts to stderr after the run |
| `CLYNXER_GAME_HEADLESS=1` | run the `game` module without opening a window |

`CLYNXER_SKIP_DISPLAY=1` is a **build/test** variable, not a run-time one: it
makes `make testCLynxer` skip the display and audio fixtures. See
[install.md](install.md#environment-variables).

## See also

- [install.md](install.md) — build commands and artifacts.
- [modules.md](modules.md) — imports and `--compile` resolution.
- [parity.md](parity.md) — what is deliberately different from Python Lynxer.
