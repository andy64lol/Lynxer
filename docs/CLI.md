# CLI

The Lynxer executable is `lynxer`. `--install` copies it to `/usr/bin/lynxer`
and `--uninstall` removes it; otherwise run it from where it was built.

```bash
make buildLynxer
lynxer/lynxer --help
```

`--help` (or `-h`, or no arguments at all) prints the usage summary and exits
`0`. The rest of this page is the authoritative description of every flag.

## Running a program

```bash
lynxer program.lynx
lynxer --no-opt program.lynx    # run without the AST optimizer
```

Anything the parser does not recognise as a flag is treated as a **file path**,
not an option. `lynxer --bogus-flag` therefore reports
`lynxer: file not found: '--bogus-flag'` and exits `1`.

A `.lynxc` argument is rejected explicitly:
`lynxer: bytecode files are no longer supported; compile the .lynx source with --compile instead`.

## Exit codes

| Code | Meaning |
|------|---------|
| `0` | success (also `--help`, `--version`, a clean `--lint`) |
| `1` | any error: a missing file, a parse error, a runtime error, an unsupported flag |
| `130` | interrupted (SIGINT) |

## The AST optimizer

Before execution Lynxer rewrites the parsed program: constant folding,
short-circuit simplification of constant `and`/`or`, and dead-branch
elimination for a constant `if`, `while (false)`, and `iterate (0)`. The pass is
semantics-preserving, so output and exit codes are identical with and without
it. `--no-opt` (or `-no-opt`) disables it for the run; a **compiled** executable
ignores its command line and always optimizes.

`LYNXER_OPT_REPORT=1` prints one line of transformation counts to stderr after
the program runs, for example
`optimizer: constant folds=10, short-circuits=2, dead branches=3`. See
[limitations.md](limitations.md#optimizer).

## Checking syntax

```bash
lynxer --lint program.lynx
```

`--lint` parses the file and reports the first syntax error without running it.
A clean file prints `Lint OK: <path>` and exits `0`. It requires exactly one
file; otherwise it prints
`lynxer: --lint requires exactly one file argument`.

## Printing the AST

```bash
lynxer --ast program.lynx
```

`--ast` lexes and parses the file and prints the resulting tree without running
it: the header `Lynxer AST` / `===========`, then a position-free, indented
dump of every top-level function and its statements. Source line/column are
omitted. A syntax error is reported with its source location and exits `1`; the
flag requires exactly one file argument.

The tree uses Lynxer's own node and field names, so it is not byte-comparable
to the Python reference's `--ast`. Named-type declarations (`struct`, `class`,
`enum`) are parse-time metadata held in the type registry rather than part of
the returned program, so `--ast` shows the executable AST only.

## Formatting

```bash
lynxer --format program.lynx
lynxer --format-oneline program.lynx
```

Both rewrite the file **in place**. `--format` gives canonical spacing and
four-space indentation; `--format-oneline` collapses everything onto a single
physical line, turning each `//` comment into the delimited `///...///` form so
it survives next to code. Comments — line and `///`/`////` blocks — are
preserved verbatim, and formatting is idempotent (running it twice changes
nothing). The file is parsed first, so a syntax error is reported with its
source location and the file is left untouched. Each flag requires exactly one
file argument.

The formatter never changes tokens or reorders them; it only decides spacing and
line breaks.

## Interpreter self-check

```bash
lynxer --validate-executeable
```

Runs a built-in corpus of small programs that must either run cleanly or raise a
specific source-located error, and prints `ok <name>` per case plus a summary.
It exits `1` if any check fails. It needs no external files, so it also works
from an installed binary. The alias `--validate-executable` is accepted.

## Compiling to an executable

`--compile` builds one standalone ELF that embeds the program, every
transitively imported `.lynx` module and `.so` library, and any additional
inputs. `--bundle` is an exact alias. There is **no** `.lynxc` bytecode path.

```bash
lynxer --compile app.lynx -o app
lynxer --compile app.lynx helpers.lynx vendor/lib.so \
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
lynxer --list-stdlibs
```

Aliases: `--stdlibs`, `-stdlibs`, `-list-stdlibs`. It lists every module in the
interpreter's `stdlib/` directory together with its `////` docstring, and exits
`1` with `No stdlib directory found.` when that directory is missing. See
[modules.md](modules.md) for the docstring format.

## Version, help, easter egg

| Flag | Aliases | Effect |
|------|---------|--------|
| `--version` | `-v`, `-version`, `--v` | print `Lynxer <version>` |
| `--help` | `-h` | print the usage summary |
| `--easterEgg` | `-easterEgg`, `--idklmao`, `-wnwnerbcyunwrbygnubeuyxnqybxun` | print the easter egg |

The version string comes from the `version` key in `lynxer/lynxer.config`
(currently `0.1.8`).

## Installing

```bash
sudo lynxer --install      # copy this executable to /usr/bin/lynxer
sudo lynxer --uninstall    # remove /usr/bin/lynxer
```

`--install` copies the running executable (resolved through `/proc/self/exe`)
to `/usr/bin/lynxer` and makes it executable. Keep the matching `stdlib/`
directory next to the installed binary, or imports will not resolve. Without
write permission it prints the failure and a `sudo` hint and exits `1`.
`--uninstall` removes `/usr/bin/lynxer`; if it is absent it reports why and
exits `1`.

## Removed with the bytecode backend

These are recognised and exit `1` with
`lynxer: '<flag>' was removed with the bytecode backend; use lynxer --compile instead`:

`--view-bytecode` (aliases `--inspect-bytecode`, `--disasm`),
`--benchmark-compile` (alias `--bench-compile`), `--no-cache`.

## Environment variables

| Variable | Effect |
|----------|--------|
| `LYNXER_OPT_REPORT=1` | print the optimizer counts to stderr after the run |
| `LYNXER_GAME_HEADLESS=1` | run the `game` module without opening a window |

`LYNXER_SKIP_DISPLAY=1` is a **build/test** variable, not a run-time one: it
makes `make testLynxer` skip the display and audio fixtures. See
[install.md](install.md#environment-variables).

## See also

- [install.md](install.md) — build commands and artifacts.
- [modules.md](modules.md) — imports and `--compile` resolution.
