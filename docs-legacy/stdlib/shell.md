# shell

Run shell commands, select a shell explicitly, and inspect installed shells.

Functions:

- `runShell(command)` → runs `command` in a shell, inherits IO, returns exit code.
- `runShellCapture(command)` → returns captured stdout as string.
- `runShellSilent(command)` → runs command suppressing output, returns exit code.
- `runShellErr(command)` → returns captured stderr as string.
- `runShellCode(command)` → run (capture) and return exit code.
- `commandExists(cmd)` → `true` if `cmd` is found on `PATH`.
- `checkShell(shellName)` → `true` if a shell executable is found on `PATH`.
- `shellPath(shellName)` → resolved shell executable path, or `""`.
- `currentShell()` → the shell path advertised by `SHELL` or `ComSpec`.
- `shellVersion(shellName)` → first version line, or `""` if unavailable.
- `availableShells()` → JSON array of common shells found on `PATH`.
- `runShellAs(shellName, command)` → runs through a selected shell and returns its exit code.
- `runShellCaptureAs(shellName, command)` → runs through a selected shell and captures stdout.

Notes:
- Use `runShellCapture` for programmatic output parsing; `runShell` is suitable when interactive I/O is desired.
- `checkShell` accepts executable names such as `bash`, `zsh`, `fish`, `sh`, `pwsh`, and `cmd`.
- `runShellAs` uses `-c` for Unix-like shells, `-Command` for PowerShell, and `/c` for Windows `cmd`.
- Selected-shell functions return safe defaults when the requested shell is unavailable: `127` for `runShellAs`, `""` for string results, and `false` for `checkShell`.

Example:

```lynx
global setup(){
    import("shell");
}

global main(){
    bool hasBash = global.shell.checkShell("bash");
    bool hasZsh = global.shell.checkShell("zsh");
    str current = global.shell.currentShell();
    str shells = global.shell.availableShells();
    str output = global.shell.runShellCaptureAs("bash", "printf 'hello'");
    println(strOf(hasBash), "|", strOf(hasZsh), "|", current, "|", shells, "|", output);
}
```