# shell

Run external shell commands and inspect available shells.

**Backend:** native — `stdlib/shell.so`, built from `stdlib/shell.cpp` over
`popen` and `std::system`. **Import:** `import("shell")` → `global.shell.*`

Commands run through the platform shell (`/bin/sh -c`).

| Function | Signature | Notes |
| --- | --- | --- |
| `runShell` | `(str command) -> int` | Runs with inherited streams; returns the exit code |
| `runShellCapture` | `(str command) -> str` | Captures stdout |
| `runShellSilent` | `(str command) -> int` | Runs with output discarded |
| `runShellErr` | `(str command) -> str` | Captures stderr only |
| `runShellCode` | `(str command) -> int` | Runs silently; returns the exit code |
| `runShellAs` | `(str shell, str command) -> int` | Runs via the named shell |
| `runShellCaptureAs` | `(str shell, str command) -> str` | Captures stdout via the named shell |
| `commandExists` | `(str command) -> bool` | Resolves on `PATH` |
| `checkShell` | `(str shell) -> bool` | Whether the shell is available |
| `shellPath` | `(str command) -> str` | Resolved path, or `""` |
| `shellVersion` | `(str shell) -> str` | First line of `shell --version` |
| `availableShells` | `() -> str` | JSON array of installed shells |
| `currentShell` | `() -> str` | `$SHELL`, or `""` |

## Example

```lynx
global setup(){ import("shell"); }

global main(){
    println(global.shell.runShellCapture("printf hello"));
    println(global.shell.shellPath("sh"));
    println(global.shell.availableShells());
    println(global.shell.runShellCode("exit 3"));
}
```

---

## See also

- [stdlib-contracts.md](../stdlib-contracts.md) — the contract this module
  implements, including the error sentinel family it uses.
- [builtins.md](../builtins.md) — the functions the interpreter implements
  itself.
- [limitations.md](../limitations.md) — the full divergence register.
