# shell

Run shell commands and capture output.

Functions:

- `runShell(command)` → runs `command` in a shell, inherits IO, returns exit code.
- `runShellCapture(command)` → returns captured stdout as string.
- `runShellSilent(command)` → runs command suppressing output, returns exit code.
- `runShellErr(command)` → returns captured stderr as string.
- `runShellCode(command)` → run (capture) and return exit code.
- `commandExists(cmd)` → `true` if `cmd` is found on `PATH`.

Notes:
- Use `runShellCapture` for programmatic output parsing; `runShell` is suitable when interactive I/O is desired.