# Lynxer CLI

The CLI is available as `lynxer` after installation. When running from the
source checkout, use:

```bash
python3 lynxer/shell.py <command>
```

Run `lynxer --help` at any time for the short command list.

## Run a program

Run a Lynxer source file:

```bash
lynxer program.lynx
```

Run a compiled bytecode file:

```bash
lynxer program.lynxc
```

The process exits with code `0` when the program succeeds and `1` when the
file is missing or Lynxer reports an error. `Ctrl-C` exits with code `130`.

## Inspect and validate source

### `--lint`

Tokenize and parse a source file without executing it:

```bash
lynxer --lint program.lynx
```

This prints `Lint OK` for valid source and reports the syntax error otherwise.

### `--ast`

Parse a source file and print its abstract syntax tree without executing it:

```bash
lynxer --ast program.lynx
```

The output is a readable tree of parser nodes and tokens. Source positions are
omitted so the output remains stable and useful for inspection.

### `--format`

Format a source file in place with readable indentation and spacing:

```bash
lynxer --format program.lynx
```

Ordinary comments are preserved.

### `--format-oneline`

Format valid source in place as one physical line:

```bash
lynxer --format-oneline program.lynx
```

Single-line comments are converted to safe `/// ... ///` delimited comments so
they do not comment out the remainder of the generated line.

## Compile and inspect bytecode

Compile a `.lynx` file to a `.lynxc` file:

```bash
lynxer --compile program.lynx
```

Aliases for `--compile` are `-c`, `--c`, and `-compile`.

Inspect bytecode metadata and its stored top-level structure:

```bash
lynxer --view-bytecode program.lynxc
```

Aliases are `--inspect-bytecode` and `--disasm`.

## Build a standalone executable

Bundle a Lynxer source program into a single native executable:

```bash
lynxer --bundle program.lynx
```

The compiled bytecode is stored in `build/bytecode/` and the executable is
written to `dist/program`. An optional second argument selects the executable
name:

```bash
lynxer --bundle program.lynx my-program
```

Bundling requires PyInstaller and a working C++ extension build.

## Discover modules and runtime information

Print the installed Lynxer version:

```bash
lynxer --version
```

Version aliases are `-v`, `-version`, and `--v`.

List standard-library modules available to `import()`:

```bash
lynxer --list-stdlibs
```

Aliases are `--stdlibs`, `-stdlibs`, and `-list-stdlibs`.

## Installation commands

Install the compiled executable as `/usr/bin/lynxer`:

```bash
lynxer --install
```

Remove that installed executable:

```bash
lynxer --uninstall
```

These operations may require administrator privileges. They are intended for
the compiled executable; when developing from source, run the shell script
directly instead.

## Help

Show the built-in usage summary:

```bash
lynxer --help
```

`-h` is an alias. Running `lynxer` without arguments also prints the help
summary.

## Common workflows

Validate, inspect, then run a source file:

```bash
lynxer --lint program.lynx
lynxer --ast program.lynx
lynxer program.lynx
```

Format, compile, inspect, and run bytecode:

```bash
lynxer --format program.lynx
lynxer --compile program.lynx
lynxer --view-bytecode program.lynxc
lynxer program.lynxc
```