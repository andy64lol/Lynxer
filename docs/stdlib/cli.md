# cli

Command-line helpers: arguments, environment, terminal queries, process
execution and Click/Typer-style command builders.

**Backend:** native — `stdlib/cli.so`, built from `stdlib/cli.cpp` over POSIX
APIs. **Import:** `import("cli")` → `global.cli.*`

## Command builders

The Click/Typer builders are implemented natively; command definitions live in
integer handles. Call `clickInit()` (or `typerInit()`) before creating anything.

| Function | Signature | Notes |
| --- | --- | --- |
| `clickInit` | `()` | Resets the builder registry |
| `clickCommandCreate` | `(str name, str help) -> int` | Command handle, `0` on failure |
| `clickGroupCreate` | `(str name, str help) -> int` | Command group handle |
| `clickGroupAddCommand` | `(int group, int command, str name) -> int` | Registers a command in a group |
| `clickAddArgument` | `(int command, str name, bool required, int nargs, str help = "") -> int` | Positional argument; `nargs < 0` collects the rest |
| `clickAddOption` | `(int command, str declarations, str help, str default, bool flag, bool required, str kind, str envvar = "", bool multiple = false) -> int` | Option or flag |
| `clickCommandSetShell` | `(int command, str template) -> int` | Shell template run when invoked |
| `clickInvoke` | `(int handle, str argsJson) -> str` | JSON `{"params": …, "output": …, "exitCode": …}` |
| `clickGroupInvoke` | `(int handle, str argsJson) -> str` | The first argument selects the subcommand |
| `clickRun` | `(int command) -> str` | Invoke with the process command line |
| `clickLastParams` | `() -> str` | Parameters of the most recent invoke |

The Typer surface mirrors it: `typerInit`, `typerAppCreate(str name, str help,
bool noArgsHelp)`, `typerCommandCreate(int app, str name, str help)`,
`typerAddArgument`, `typerAddOption`, `typerCommandSetShell`,
`typerInvoke(int app, str argsJson)`, `typerRun(int app)`, `typerLastParams`.
A Typer app with a single command is invoked without the command name; with two
or more, the first argument selects the command.

- `declarations` is comma-separated, e.g. `"--verbose,-v"`. The parameter name
  is the first declaration without its dashes (`shout` for `"--shout,-s"`).
- `kind` is `text`, `int`, `float`, `bool` or `path`.
- `argsJson` is a JSON array of argument strings, e.g. `["Ada", "--shout"]`.
- `{parameterName}` placeholders in a shell template are replaced with the
  parsed values; `output` is the command's captured stdout.
- `--help` / `-h` returns `{"params": {}, "help": "…"}` with a generated usage
  block (arguments, options and, for a group, subcommands) instead of running
  the shell template. A group or single-command app created with
  `noArgsHelp = true` returns the same help when invoked with no arguments.
- A flag can be negated with `--no-<name>`: `--no-shout` sets `shout` to false.
- `envvar` is consulted when the option is absent, before `default`: the order
  is explicit argument, then environment variable, then declared default.
- `multiple` collects repeated occurrences into a JSON array; a `{name}`
  template substitutes those values joined by a space.
- A failed parse returns `{"error": "…"}` instead of failing the process.

```lynx
global setup(){ import("cli"); }

global main(){
    global.cli.clickInit();
    int command = global.cli.clickCommandCreate("greet", "Greet a person");
    global.cli.clickAddArgument(command, "name", true, 1);
    global.cli.clickAddOption(command, "--shout,-s", "Use uppercase", "", true, false, "bool");
    global.cli.clickCommandSetShell(command, "printf 'Hello {name}'");
    println(global.cli.clickInvoke(command, "[\"Ada\", \"--shout\"]"));
    // {"params": {"name": "Ada", "shout": true}, "output": "Hello Ada", "exitCode": 0}
}
```

## Arguments and environment

| Function | Signature | Notes |
| --- | --- | --- |
| `argv` | `() -> str` | Process command line as a JSON array |
| `argCount` | `() -> int` | Number of command-line entries |
| `getArg` | `(int index) -> str` | Entry at `index`, or `""` |
| `envGet` | `(str name) -> str` | Value, or `""` |
| `envHas` | `(str name) -> bool` | Whether the variable is set |
| `envAll` | `() -> str` | All variables as a JSON object |

Lynxer does not forward extra arguments to a program, so `argv` describes the
`lynxer` process itself.

## Terminal and IO

| Function | Signature | Notes |
| --- | --- | --- |
| `cwd` | `() -> str` | Working directory |
| `chdir` | `(str path) -> bool` | Changes the working directory |
| `stdinIsTty` / `stdoutIsTty` | `() -> bool` | Terminal detection |
| `readStdin` | `() -> str` | Reads all of stdin |
| `writeStdout` / `writeStderr` | `(str text)` | Writes without a trailing newline |
| `terminalSize` | `() -> str` | JSON `{"columns": N, "lines": M}` |
| `exit` | `(int code)` | Exits the process immediately |

## Paths and processes

| Function | Signature | Notes |
| --- | --- | --- |
| `pathExists` | `(str path) -> bool` | `access(2)` |
| `isFile` / `isDirectory` | `(str path) -> bool` | `stat(2)` type tests |
| `which` | `(str executable) -> str` | Resolves on `PATH`, or `""` |
| `run` | `(str command, bool capture) -> str` | With `capture` returns stdout, otherwise the exit code as text |
| `runCode` | `(str command) -> int` | Runs silently and returns the exit code |

## Example

```lynx
global setup(){ import("cli"); }

global main(){
    println(global.cli.argCount());
    if(global.cli.envHas("HOME")){
        println(global.cli.envGet("HOME"));
    }
    println(global.cli.run("printf ok", true));
    println(global.cli.runCode("false"));
}
```

---

## See also

- [stdlib-contracts.md](../stdlib-contracts.md) — the contract this module
  implements, including the error sentinel family it uses.
- [builtins.md](../builtins.md) — the functions the interpreter implements
  itself.
- [limitations.md](../limitations.md) — the full divergence register.
