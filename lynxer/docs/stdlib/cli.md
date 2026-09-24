# cli

Command-line helpers: arguments, environment, terminal queries and process
execution.

**Backend:** native — `stdlib/cli.so`, built from `stdlib/cli.cpp` over POSIX
APIs. **Import:** `import("cli")` → `global.cli.*`

## Click / Typer compatibility

| Function | Signature | Returns |
| --- | --- | --- |
| `clickExists` / `typerExists` | `() -> bool` | Always `false` |
| `clickVersion` / `typerVersion` | `() -> str` | Always `""` |

Click and Typer are Python packages with no Lynxer equivalent, so the
`click*`/`typer*` builder functions are **not defined** — calling one is a hard
"unknown function" error.

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
- [parity.md](../parity.md) — the parity scope with Python Clynxer.
- [limitations.md](../limitations.md) — the full divergence register.
