# CLI

The Lynxer executable is `lynxer`. `--install` installs it under
`/usr/lib/lynxer` and links `/usr/bin/lynxer`; `--uninstall` removes both;
otherwise run it from where it was built.

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

## Projects

`--init` scaffolds a Lynxer project in the current directory:

```bash
mkdir my-app && cd my-app
lynxer --init            # project name defaults to the directory name
lynxer --init my-app     # or name it explicitly
```

It refuses to overwrite any file it would create. The resulting layout is:

```
README
src/main.lynx
lynxer.config
.gitignore
build/
```

`lynxer.config` is a `key = value` file (lines starting with `#` are
comments). Two keys drive the build:

| Key | Meaning |
|-----|---------|
| `entry` | Source file to compile, e.g. `src/main.lynx`. Required. |
| `name` | Executable name inside `build/`. Defaults to the entry's base name. |

`main` is accepted as an alias for `entry`. The project config is read
directly by the build commands; it does not replace the interpreter's own
`lynxer.config` that ships beside the executable.

```bash
lynxer --build   # compile `entry` into build/<name> only
lynxer --run     # compile, then execute build/<name> and pass through its exit code
```

Both commands require a `lynxer.config` in the working directory and take no
arguments. `--run` returns the program's exit status. **All build artifacts
live inside `build/`** — nothing is written next to the sources.

## IDEs

Lynxer ships two IDEs, both launched from the interpreter itself and both
listed by `--help`:

| Command | IDE | Needs |
|---------|-----|-------|
| `lynxer --gui-ide` | Native GUI IDE (iced, no webview): file tree, tabs, autocomplete, Problems/Output panels | A graphical display and the `lynxer-ide` executable |
| `lynxer --cli-ide [file.lynx]` | Built-in terminal IDE: a full-screen ANSI TUI with no separate binary | An interactive TTY on stdin **and** stdout |

### GUI IDE

`--gui-ide` resolves `lynxer-ide` next to the interpreter, in the installed
`<prefix>/lib/lynxer` layout, or in `editors/ide/target/{release,debug}`. Set
`LYNXER_IDE` to an explicit executable (or a directory containing it) to
override. The launcher passes its own absolute path as `--lynxer <path>`, so
Run / Lint / Format work even when `lynxer` is not on `PATH`. It takes no
arguments and returns the IDE's exit code.

### Terminal IDE

`--cli-ide` runs inside the interpreter. It opens in the current directory and
accepts one optional argument: a `.lynx` file to open, or a folder to browse.
It refuses to start when stdin or stdout is redirected, and restores the
terminal on exit (alternate screen, cursor and raw-input mode).

Features: an expandable file tree, a tabbed editor with line numbers and Lynxer
syntax highlighting (`.lynx` only), multi-key editing, search and go-to-line,
an output/problems panel with parsed `lynxer --lint` warnings and errors, and
Run / Lint / Format / Build / Run-project commands whose output is captured
into the panel instead of overwriting the screen.

Key bindings:

| Key | Action |
|-----|--------|
| `Ctrl+O` / `Ctrl+N` / `Ctrl+S` / `Ctrl+W` / `Ctrl+Q` | open / new / save / close tab / quit |
| `Ctrl+B` | change folder |
| `Ctrl+E` | switch focus between explorer and editor |
| `Ctrl+P` / `Ctrl+K` | toggle output panel / clear output |
| `Ctrl+G` / `Ctrl+F` / `F3` | go to line / find / next match |
| `Ctrl+Left` / `Ctrl+Right` | previous / next tab |
| `F1` / `Esc` | help / focus explorer |
| `F4` / `F5` / `F6` / `F7` | next problem / run file / lint file / format file |
| `F9` / `F10` | build project / run project |
| Explorer: Up/Down, Enter, Right, Left | select, open, expand, collapse |
| Editor: arrows, Home/End, PgUp/PgDn, Tab, Backspace, Delete, Enter | move and edit |

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
`optimizer: constant folds=10, short-circuits=2, dead branches=3`.

## Checking syntax

```bash
lynxer --lint program.lynx
```

`--lint` parses the file and runs conservative static checks without executing
it. It checks known initializer, assignment, return, call, method, and
constructor types; argument counts; unreachable statements; unused local
variables and parameters; and conversions that may lose information. Warnings
are printed but do not fail lint; static errors and syntax errors exit `1`.
Values and external calls whose types cannot be known are left to runtime
checks. A clean file prints `Lint OK: <path> (0 warning(s))` and exits `0`. It
requires exactly one file; otherwise it prints
`lynxer: --lint requires exactly one file argument`.

## Debugging

```bash
lynxer --debug program.lynx
```

The debugger pauses before the first source statement. Commands are `step`,
`continue`, `break [file:]line`, `delete [file:]line`, `locals`, `print name`,
`backtrace`, `frame n`, and `detach`. Breakpoints may be set at the initial
pause before continuing. `step` enters function calls; `frame 0` selects the
currently stopped frame, with larger indexes moving toward its callers.

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
to an earlier implementation's `--ast`. Named-type declarations (`struct`,
`template`, `class`, `enum`) are parse-time metadata held in the type registry
rather than part of the returned program, so `--ast` shows the executable AST
only.

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

`--compile` produces one self-contained executable that embeds the program, every
transitively imported `.lynx` module and `.so` library, and anything added with
`--include`. It is the interpreter with the program embedded, so it runs on its
own — the target machine does not need a separate Lynxer installation. There is
**no** `.lynxc` bytecode path.

```bash
lynxer --compile app.lynx -o app
lynxer --compile app.lynx helpers.lynx vendor/lib.so \
        --include assets/message.txt -o app
```

| Flag | Aliases | Effect |
|------|---------|--------|
| `--compile` | `-c`, `--c`, `-compile` | compile the input files |
| `--include <file>` | `-i` | embed an extra module, native library, or data file |
| `-o <name>` | `--output`, `-name`, `--name` | name the output executable |

The first `.lynx` input is the program; any further `.lynx` or `.so` input is
embedded and importable by name. A `--include` file that is not `.lynx`/`.so` is
embedded as data and read from the program with `bundledFile(name)` /
`bundledFiles()`; see [builtins.md](builtins.md#bundled-files). Without `-o`, the
output is named after the program (`app.lynx` → `app`).

The result is a native executable for the host platform — on Linux, a standalone
ELF. Inside it, `--no-opt` has no effect: the payload is always optimized.

## Emitting a C ABI library

`--emit-library` builds a shared library (`.so` on Linux, `.dll` on Windows)
that exposes a program's top-level `export`s as C symbols other languages can
call. It needs a C++ compiler and the matching embedding runtime
(`liblynxer.so`, or `lynxer.dll` plus its `.dll.a` import library on Windows),
so **Lynxer must be built first** (`make buildLynxer`); the feature is
unavailable from an unbuilt tree and fails with `build Lynxer first` when the
runtime is missing. The emitted library embeds the program and needs only that
runtime at run time. On Windows, put `lynxer.dll` beside the host executable or
on the DLL search path. See [embedding.md](embedding.md) and
[windows.md](windows.md).

```bash
lynxer --emit-library app.lynx -o libapp.so
lynxer --emit-library app.lynx --include helpers.lynx -o libapp.so
lynxer --emit-library app.lynx -o app.dll        # Windows
```

| Flag | Aliases | Effect |
|------|---------|--------|
| `--emit-library` | `--shared-library`, `-emit-library` | build an exporting shared library |
| `--include <file>` | `-i` | embed an extra module, native library, or data file |
| `-o <path>` | `--output` | name the output library |
| `--runtime <path>` | — | use an explicit embedding runtime (`liblynxer.so` or `lynxer.dll`) |
| `--cc <compiler>` | — | C++ compiler used to build the library |

See [embedding.md](embedding.md) for the `export` syntax and the C ABI mapping.
Alongside the library, `--emit-library` writes a header with the exported
prototypes (`libapp.so` → `libapp.h`); on Windows it also writes a `.dll.a`
import library. `make sdk` stages `lynxer.h`, `ffi_abi.h` and the runtime under
`lynxer/build/sdk/{include,lib,bin}`.

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
| `--version` | `-v`, `-version`, `--v`, `--ver`, `-ver` | print `Lynxer <version>` |
| `--help` | `-h` | print the usage summary |
| `--easterEgg` | `-easterEgg`, `--idklmao`, `-wnwnerbcyunwrbygnubeuyxnqybxun` | print the easter egg |

The version string comes from the `version` key in `lynxer/lynxer.config`
(currently `0.1.9b2`).

## Installing

```bash
sudo lynxer --install      # install into /usr/lib/lynxer, link /usr/bin/lynxer
sudo lynxer --uninstall    # remove the installed interpreter
```

`--install` copies the running executable (resolved through `/proc/self/exe`),
its `stdlib/` directory and `lynxer.config` into a self-contained tree under
`/usr/lib/lynxer`, then creates the symlink `/usr/bin/lynxer` to the real
binary. The symlink resolves through `/proc/self/exe`, so the installed
interpreter finds its stdlib from any working directory — `lynxer app.lynx`
works with no build tree present. Set `LYNXER_PREFIX` to use a prefix other than
`/usr` (for example `LYNXER_PREFIX=$HOME/.local`), and pass the same value to
`--uninstall`. Without write permission it prints the failure and a `sudo` hint
and exits `1`. `--uninstall` removes the symlink and the tree; when neither is
present it reports why and exits `1`.

On Windows the default prefix is `%LOCALAPPDATA%\Programs\Lynxer`, the launcher
is a copy (`bin\lynxer.exe`) rather than a symlink, and the installed binary is
`lynxer.exe`. See [windows.md](windows.md).

## Removed with the bytecode backend

These are recognised and exit `1` with
`lynxer: '<flag>' was removed with the bytecode backend; use lynxer --compile instead`:

`--view-bytecode` (aliases `--inspect-bytecode`, `--disasm`),
`--benchmark-compile` (alias `--bench-compile`), `--no-cache`.

## Environment variables

| Variable | Effect |
|----------|--------|
| `LYNXER_STDLIB=/path/to/stdlib` | use an existing directory for the bundled stdlib and `--list-stdlibs` |
| `LYNXER_OPT_REPORT=1` | print the optimizer counts to stderr after the run |
| `LYNXER_GAME_HEADLESS=1` | run the `game` module without opening a window |
| `LYNXER_GRAPHICS_HEADLESS=1` | run the `graphics` module without opening a window |
| `LYNXER_IDE=/path/to/lynxer-ide` | executable (or directory) used by `--gui-ide` |

`LYNXER_SKIP_DISPLAY=1` is a **build/test** variable, not a run-time one: it
makes `make testLynxer` skip the display and audio fixtures. See
[install.md](install.md#environment-variables).

If `LYNXER_STDLIB` is unset, empty, or does not name an existing directory,
Lynxer uses its normal executable-relative stdlib lookup. Imports still check
project and local stdlib paths before this bundled-stdlib location; see
[modules.md](modules.md#search-order).

## See also

- [install.md](install.md) — build commands and artifacts.
- [modules.md](modules.md) — imports and `--compile` resolution.
