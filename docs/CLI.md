# Lynxer CLI

The CLI is available as `clynxer` after installation. When running from the
source checkout, use:

```bash
python3 clynxer/shell.py <command>
```

Run `clynxer --help` at any time for the short command list.

## Run a program

Run a Lynxer source file:

```bash
clynxer program.lynx
```

Run a compiled bytecode file:

```bash
clynxer program.lynxc
```

The process exits with code `0` when the program succeeds and `1` when the
file is missing or Lynxer reports an error. `Ctrl-C` exits with code `130`.

## Inspect and validate source

### `--lint`

Tokenize and parse a source file without executing it:

```bash
clynxer --lint program.lynx
```

This prints `Lint OK` for valid source and reports the syntax error otherwise.

### `--ast`

Parse a source file and print its abstract syntax tree without executing it:

```bash
clynxer --ast program.lynx
```

The output is a readable tree of parser nodes and tokens. Source positions are
omitted so the output remains stable and useful for inspection.

### `--format`

Format a source file in place with readable indentation and spacing:

```bash
clynxer --format program.lynx
```

Ordinary comments are preserved.

### `--format-oneline`

Format valid source in place as one physical line:

```bash
clynxer --format-oneline program.lynx
```

Single-line comments are converted to safe `/// ... ///` delimited comments so
they do not comment out the remainder of the generated line.

## Compile and inspect bytecode

Compile a `.lynx` file to a `.lynxc` file:

```bash
clynxer --compile program.lynx
```

Aliases for `--compile` are `-c`, `--c`, and `-compile`.

Inspect bytecode metadata and its stored top-level structure:

```bash
clynxer --view-bytecode program.lynxc
```

Aliases are `--inspect-bytecode` and `--disasm`.

### Compile flags

| Flag | Effect |
|------|--------|
| `--no-cache` | Recompile even when the cached `.lynxc` is up to date |
| `--no-opt` | Skip the optimization hook (recorded in the bytecode metadata) |

```bash
clynxer --compile --no-cache program.lynx
clynxer --compile --no-opt program.lynx
```

Both flags apply to `--compile` only. The optimizer runs constant folding, so
`--no-opt` now produces a genuinely different (unfolded) AST while keeping the
same observable behaviour — see [bytecode.md](bytecode.md#what-changed-in-v9).

## Build a standalone executable

Bundle a Lynxer source program into a single native executable:

```bash
clynxer --bundle program.lynx
```

The compiled bytecode is stored in `build/bytecode/` and the executable is
written to `dist/program`. On Linux, bundling is host-native: it uses the
architecture of the machine where it runs, including x86-64 and ARM64
(`aarch64`) distributions. A bundled executable records that architecture and
refuses to start on a different Linux ABI instead of issuing syscalls with the
wrong table.
An optional second argument selects the executable name:

```bash
clynxer --bundle program.lynx my-program
```

Bundling requires PyInstaller, a working C++ extension build, and the
`system-calls` package. The syscall package and its table data are collected
into the executable, and startup verifies that the bundled runtime can resolve
the host architecture's syscall table.

## Discover modules and runtime information

Print the installed Lynxer version:

```bash
clynxer --version
```

Version aliases are `-v`, `-version`, and `--v`.

List standard-library modules available to `import()`:

```bash
clynxer --list-stdlibs
```

Aliases are `--stdlibs`, `-stdlibs`, and `-list-stdlibs`.

## Installation commands

Install the compiled executable as `/usr/bin/clynxer`:

```bash
clynxer --install
```

Remove that installed executable:

```bash
clynxer --uninstall
```

These operations may require administrator privileges. They are intended for
the compiled executable; when developing from source, run the shell script
directly instead.

## Help

Show the built-in usage summary:

```bash
clynxer --help
```

`-h` is an alias. Running `clynxer` without arguments also prints the help
summary.

## Common workflows

Validate, inspect, then run a source file:

```bash
clynxer --lint program.lynx
clynxer --ast program.lynx
clynxer program.lynx
```

Format, compile, inspect, and run bytecode:

```bash
clynxer --format program.lynx
clynxer --compile program.lynx
clynxer --view-bytecode program.lynxc
clynxer program.lynxc
```