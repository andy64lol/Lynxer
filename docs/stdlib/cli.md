# cli

Extended command-line helpers for Lynxer. The module combines Python process
and terminal utilities with indexed Click and Typer builders.

Click and Typer objects are represented by integer handles. Structured
arguments and results use JSON strings so they can cross the Lynxer/Python
bridge reliably.

## Availability

| Function | Signature | Description |
|----------|-----------|-------------|
| `clickExists` | `clickExists()` | `true` when Click is importable |
| `typerExists` | `typerExists()` | `true` when Typer is importable |
| `clickVersion` | `clickVersion()` | Installed Click version |
| `typerVersion` | `typerVersion()` | Installed Typer version |

## Python CLI basics

| Function | Signature | Description |
|----------|-----------|-------------|
| `argv` | `argv()` | Process arguments as a JSON array |
| `argCount / getArg` | `argCount()`, `getArg(int index)` | Read process arguments |
| `envGet / envHas / envAll` | `envGet(str name)`, ... | Read environment variables |
| `cwd / chdir` | `cwd()`, `chdir(str path)` | Read or change the working directory |
| `stdinIsTty / stdoutIsTty` | `stdinIsTty()`, `stdoutIsTty()` | Detect interactive streams |
| `readStdin` | `readStdin()` | Read all standard input |
| `writeStdout / writeStderr` | `writeStdout(str text)`, ... | Write directly to a stream |
| `terminalSize` | `terminalSize()` | Terminal dimensions as JSON |
| `exit` | `exit(int code)` | End the process |
| `pathExists / isFile / isDirectory` | `...(str path)` | Filesystem checks |
| `which` | `which(str executable)` | Resolve an executable on `PATH` |
| `run / runCode` | `run(str command, bool capture)`, ... | Run a shell command |

The `shell` module remains the more complete API for shell selection,
captured stderr, and explicit shell execution.

## Click builders

Call `clickInit()` before creating resources.

| Function | Signature | Description |
|----------|-----------|-------------|
| `clickCommandCreate` | `clickCommandCreate(str name, str help)` | Create a command handle |
| `clickGroupCreate` | `clickGroupCreate(str name, str help)` | Create a command group handle |
| `clickGroupAddCommand` | `clickGroupAddCommand(group, command, str name)` | Register a command in a group |
| `clickAddArgument` | `clickAddArgument(command, str name, bool required, int nargs)` | Add a positional argument |
| `clickAddOption` | `clickAddOption(command, str decls, str help, str default, bool flag, bool required, str type)` | Add an option |
| `clickCommandSetShell` | `clickCommandSetShell(command, str template)` | Run a shell template as the command callback |
| `clickInvoke` | `clickInvoke(handle, str argsJson)` | Invoke a command and return JSON |
| `clickGroupInvoke` | `clickGroupInvoke(handle, str argsJson)` | Invoke a group and return JSON |
| `clickRun` | `clickRun(command, ...)` | Invoke a command with process arguments |
| `clickLastParams` | `clickLastParams()` | Last parsed parameters as JSON |

`type` accepts `"text"`, `"int"`, `"float"`, `"bool"`, and `"path"`.
Option declarations are comma-separated, for example
`"--verbose,-v"`. Shell templates substitute parsed values using
`{parameterName}` placeholders.

```lynx
global setup(){ import("cli"); }

global main(){
    global.cli.clickInit();
    int command = global.cli.clickCommandCreate("greet", "Greet a person");
    global.cli.clickAddArgument(command, "name", true, 1);
    global.cli.clickAddOption(command, "--shout,-s", "Use uppercase", "", true, false, "bool");
    global.cli.clickCommandSetShell(command, "printf 'Hello {name}'");
    println(global.cli.clickInvoke(command, "[\"Ada\", \"--shout\"]"));
}
```

## Typer builders

Call `typerInit()` before creating resources.

| Function | Signature | Description |
|----------|-----------|-------------|
| `typerAppCreate` | `typerAppCreate(str name, str help, bool noArgsHelp)` | Create a Typer app handle |
| `typerCommandCreate` | `typerCommandCreate(app, str name, str help)` | Register a command |
| `typerAddArgument` | `typerAddArgument(command, str name, bool required, int nargs)` | Add a positional argument |
| `typerAddOption` | `typerAddOption(command, str decls, str help, str default, bool flag, bool required, str type)` | Add an option |
| `typerCommandSetShell` | `typerCommandSetShell(command, str template)` | Run a shell template as the callback |
| `typerInvoke` | `typerInvoke(app, str argsJson)` | Invoke the app and return JSON |
| `typerRun` | `typerRun(app)` | Invoke with process arguments |
| `typerLastParams` | `typerLastParams()` | Last parsed parameters as JSON |

For a Typer app with one command, invoke the command arguments directly:
`["Ada", "--verbose"]`. For an app with multiple commands, include the
command name: `["greet", "Ada"]`.
