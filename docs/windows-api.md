# Windows API access

On Windows, Lynxer calls the Win32 API through the built-in `winAPI.*` surface.
It is **not a module** — there is no `import` — and it is gated by the same
`syscalls(...)` target selector the named Linux syscalls use:

```lynx
global setup(){ syscalls("winAPI", "amd64"); }

global main(){
    println(winAPI.getProcessId());
    println(winAPI.getComputerName());
    println(winAPI.getCurrentDirectory());
}
```

Most calls are `kernel32`; `getKnownFolderPath` uses `shell32`/`ole32` for the
Known Folder GUIDs. Both libraries ship with Windows, so nothing extra has to be
installed.

## Selecting the target

`syscalls("<os>", "<arch>")` must run before any `winAPI.*` call.

| Keyword | Kind | Selects |
| --- | --- | --- |
| `winAPI`, `windows`, `win32` | operating system | the Win32 surface |
| `amd64`, `x86-64` | architecture | x86-64 |
| `arm64`, `aarch64` | architecture | AArch64 |

- Both keywords must name the **host**. On Linux,
  `syscalls("winAPI", "amd64")` fails with
  `syscalls("winAPI") selects Windows, but this machine is Linux`; on a host
  that is neither Linux nor Windows the selector reports that it is unavailable.
- Matching is case-insensitive, so
  `syscalls(sys.platform(), sys.architecture())` asserts the host — `platform()`
  returns `win32` on Windows.
- Calling `winAPI.*` before the selection is an error:
  `winAPI calls are not available yet; call syscalls("winAPI", "<arch>") first`.
  A call after selecting a different target is an error too.
- The namespace prefix is matched case-insensitively, so `winAPI.getProcessId()`
  and `winapi.getProcessId()` are the same call. A misspelled function reports
  the closest match, e.g. `unknown winAPI function 'getProcesId'. You meant:
  getProcessId?`.

## Conventions

- Failures use the standard-library **scalar sentinels**: `-1` for a handle, a
  size, a position, a byte count or a free-space figure; `false` for a
  predicate; `""` for a failed read. `winAPI.getLastError()` returns the Win32
  error code for the most recent call.
- A **handle is an integer**. Everything it refers to must be released with
  `winAPI.closeHandle()`.
- Wrong argument types are source-located errors, e.g.
  `winAPI.readFile() failed: expects an integer handle and length`.
- Text is UTF-8 at the Lynxer boundary; the `*W` calls are used internally.

## System information

| Function | Returns |
| --- | --- |
| `winAPI.getProcessId()` | the current process id |
| `winAPI.getCurrentDirectory()` | the current directory |
| `winAPI.getComputerName()` | the computer name |
| `winAPI.getTempPath()` | the temporary directory (with a trailing separator) |
| `winAPI.getSystemDirectory()` | the system directory |
| `winAPI.getWindowsDirectory()` | the Windows directory |
| `winAPI.getModuleFileName()` | the running executable's full path |
| `winAPI.getTickCount()` | milliseconds since the system started |
| `winAPI.getLastError()` | the last Win32 error code |
| `winAPI.getDiskFreeBytes(path)` | free bytes on the volume, or `-1` |
| `winAPI.getKnownFolderPath(name)` | a Known Folder path, or `""` |
| `winAPI.getUserName()` | the current account name |
| `winAPI.isUserAnAdmin()` | whether the process has administrator rights |
| `winAPI.getSystemMetrics(index)` | a `GetSystemMetrics` value, or `0` |
| `winAPI.getLogicalDrives()` | the drive bitmask |
| `winAPI.getDriveType(root)` | the `DRIVE_*` type, or `0` |
| `winAPI.getOSVersion()` | `major.minor.build` from `RtlGetVersion` |
| `winAPI.getLocaleName()` | the user's locale name, e.g. `en-US` |
| `winAPI.getTimeZoneName()` | the current standard time-zone name |
| `winAPI.getSystemTime()` | milliseconds since the Unix epoch (UTC) |
| `winAPI.getSystemInfo()` | JSON `{processorArchitecture, pageSize, processorCount, allocationGranularity, processorLevel, processorRevision, activeProcessorMask}` |
| `winAPI.getMemoryStatus()` | JSON `{load, total, available, totalPageFile, availablePageFile, totalVirtual, availableVirtual}` in bytes / percent |
| `winAPI.getPowerStatus()` | JSON `{acLineStatus, batteryFlag, batteryLifePercent, batteryLifeSeconds, batteryFullLifeSeconds}` |
| `winAPI.getVolumeInfo(path)` | JSON `{label, filesystem, serial, maxComponentLength, flags}`, or `""` |
| `winAPI.getDiskSpace(path)` | JSON `{free, total, totalFree}` in bytes, or `""` |
| `winAPI.getACP()` | the ANSI code page |
| `winAPI.getOEMCP()` | the OEM code page |
| `winAPI.getCommandLine()` | the full command line |
| `winAPI.getCurrentProcess()` | a closable duplicate handle to this process, or `-1` |
| `winAPI.getModuleHandle(name)` | a module handle (`""` for the running image), or `-1` |
| `winAPI.getActiveProcessorCount(group)` | active processors in a group, or `-1` |
| `winAPI.formatMessage(code)` | the system message for a Win32 error code |
| `winAPI.sleep(milliseconds)` | — |
| `winAPI.beep(frequency, duration)` | whether it played |
| `winAPI.outputDebugString(text)` | — (goes to the debugger) |

`getKnownFolderPath` accepts `desktop`, `documents`, `downloads`, `music`,
`pictures`, `videos`, `localAppData`, `roamingAppData`, `programData`, `profile`,
`public`, `windows` and `system`; an unknown name returns `""`, as does a
folder the shell cannot resolve. It is the Windows counterpart of the XDG
directories exposed by `os.configDir`/`os.dataDir`/`os.cacheDir`.

## Environment

| Function | Returns |
| --- | --- |
| `winAPI.getEnvironmentVariable(name)` | the variable's value, or `""` when unset |
| `winAPI.setEnvironmentVariable(name, value)` | whether it was set |
| `winAPI.expandEnvironmentStrings(text)` | `text` with `%VAR%` expanded |

## Files and handles

| Function | Returns |
| --- | --- |
| `winAPI.createFile(path, mode)` | a handle, or `-1` |
| `winAPI.readFile(handle, length)` | up to `length` bytes, or `""` |
| `winAPI.writeFile(handle, text)` | bytes written, or `-1` |
| `winAPI.closeHandle(handle)` | whether it closed |
| `winAPI.fileSize(handle)` | size in bytes, or `-1` |
| `winAPI.seekFile(handle, offset)` | the new absolute position, or `-1` |
| `winAPI.deleteFile(path)` | whether it was removed |
| `winAPI.copyFile(source, destination, overwrite)` | whether it was copied |
| `winAPI.moveFile(source, destination)` | whether it was moved |
| `winAPI.createDirectory(path)` | whether it was created |
| `winAPI.removeDirectory(path)` | whether it was removed |
| `winAPI.getFileAttributes(path)` | the attribute bits, or `-1` |
| `winAPI.setFileAttributes(path, attributes)` | whether they were set |
| `winAPI.getFullPathName(path)` | the absolute path, or `""` |
| `winAPI.flushFileBuffers(handle)` | whether the buffers were flushed |
| `winAPI.getTempFileName(prefix)` | a new unique temp file path, or `""` |
| `winAPI.setCurrentDirectory(path)` | whether the working directory changed |
| `winAPI.getFileType(handle)` | the `FILE_TYPE_*` value, or `-1` |
| `winAPI.getFileTime(path)` | JSON `{creation, access, write}` in Unix milliseconds, or `""` |

`mode` is `"read"` (`OPEN_EXISTING`), `"write"` (`CREATE_ALWAYS`, truncated),
`"append"` (`OPEN_ALWAYS`, seek to end) or `"readwrite"` (`OPEN_ALWAYS`).
`seekFile` positions absolutely from the start of the file.

```lynx
global setup(){ syscalls("winAPI", "amd64"); }

global main(){
    str path = winAPI.getTempPath() + "demo.txt";

    int file = winAPI.createFile(path, "write");
    if (file < 0) {
        println("cannot create: " + winAPI.getLastError());
        return;
    }
    winAPI.writeFile(file, "hello");
    winAPI.closeHandle(file);

    int reader = winAPI.createFile(path, "read");
    println(winAPI.fileSize(reader));
    println(winAPI.readFile(reader, 64));
    winAPI.closeHandle(reader);
    winAPI.deleteFile(path);
}
```

## Shell, clipboard and dialogs

| Function | Returns |
| --- | --- |
| `winAPI.shellOpen(path)` | whether `ShellExecuteW` accepted the request |
| `winAPI.shellOpenWith(path, arguments, verb)` | the same, with an explicit verb such as `"edit"` |
| `winAPI.messageBox(text, title)` | the `MessageBoxW` result |
| `winAPI.clipboardGet()` | the clipboard's Unicode text, or `""` |
| `winAPI.clipboardSet(text)` | whether the clipboard was updated |
| `winAPI.notify(title, message)` | whether a tray balloon was queued |

`shellOpen`/`shellOpenWith` are fire-and-forget: they return as soon as the
shell accepts the request. `messageBox` blocks until the user dismisses the
dialog, so it is unsuitable for unattended programs. `notify` queues a tray
balloon for five seconds and returns immediately; it needs an interactive
console window. Clipboard and notification access need a window-station/desktop
that permits it; under a service or a headless session they return the failure
sentinel instead of raising.

## Named pipes

| Function | Returns |
| --- | --- |
| `winAPI.createNamedPipe(name)` | a server handle, or `-1` |
| `winAPI.connectNamedPipe(handle)` | whether a client connected |
| `winAPI.openNamedPipe(name)` | a client handle, or `-1` after a 5 s wait |
| `winAPI.readNamedPipe(handle, length)` | up to `length` bytes, or `""` |
| `winAPI.writeNamedPipe(handle, text)` | bytes written, or `-1` |
| `winAPI.disconnectNamedPipe(handle)` | whether the server disconnected |
| `winAPI.peekNamedPipe(handle)` | bytes available, or `-1` |

Names are relative to `\\.\pipe\`; a full `\\.\pipe\name` path is also
accepted. `openNamedPipe` retries while the server is still creating the pipe,
so a client may start before the server. Pipe handles are ordinary handles and
are released with `winAPI.closeHandle`. Reads and writes are blocking.

```lynx
global setup(){ syscalls("winAPI", "amd64"); }

global main(){
    int server = winAPI.createNamedPipe("lynxer-demo");
    // In another process:
    //   int client = winAPI.openNamedPipe("lynxer-demo");
    winAPI.connectNamedPipe(server);
    println(winAPI.readNamedPipe(server, 64));
    winAPI.writeNamedPipe(server, "pong");
    winAPI.disconnectNamedPipe(server);
    winAPI.closeHandle(server);
}
```

## Processes and threads

| Function | Returns |
| --- | --- |
| `winAPI.getCurrentThreadId()` | the calling thread's id |
| `winAPI.openProcess(processId)` | a handle with query/terminate/synchronize access, or `-1` |
| `winAPI.terminateProcess(handle, exitCode)` | whether it was terminated |
| `winAPI.getExitCodeProcess(handle)` | the exit code, or `-1` (`259` while still alive) |
| `winAPI.createProcess(commandLine)` | the new process's handle, or `-1` |
| `winAPI.waitForSingleObject(handle, milliseconds)` | `0` when signalled, `258` on timeout, `-1` on failure |
| `winAPI.getPriorityClass(handle)` | the `*_PRIORITY_CLASS` value, or `-1` |
| `winAPI.setPriorityClass(handle, class)` | whether it was set |

`createProcess` starts `commandLine` through `CreateProcessW` with the inherited
environment and no redirection; the thread handle is closed for you, so only the
returned process handle needs `closeHandle`.

## Console

These talk to the console the process is attached to; under a redirected stream
or a non-interactive session they report `-1`/`false` rather than failing the
call.

| Function | Returns |
| --- | --- |
| `winAPI.getStdHandle(stream)` | the handle for `"input"`, `"output"` or `"error"`, or `-1` |
| `winAPI.getConsoleMode(handle)` | the console mode, or `-1` |
| `winAPI.setConsoleMode(handle, mode)` | whether it was set |
| `winAPI.setConsoleTitle(text)` | whether the title was set |
| `winAPI.getConsoleOutputCP()` | the console output code page |
| `winAPI.setConsoleOutputCP(codePage)` | whether it was set |
| `winAPI.getConsoleWindow()` | the console window handle, or `-1` |
| `winAPI.getConsoleScreenBufferInfo()` | JSON `{width, height, bufferWidth, bufferHeight, cursorX, cursorY, attributes}`, or `""` |

## Timing

| Function | Returns |
| --- | --- |
| `winAPI.queryPerformanceCounter()` | the high-resolution counter, or `-1` |
| `winAPI.queryPerformanceFrequency()` | ticks per second, or `-1` |

## The long tail

Anything not in the tables above — other DLLs, the `*A`/`*W` pairs, structs,
`HWND`/`SOCKET` values and the 32-bit `__stdcall` convention — is reachable
through the FFI built-ins ([native-modules.md](native-modules.md)). On 64-bit
Windows the Win32 calling convention is the one `cdecl:` describes, so a Win32
call looks like any other:

```lynx
global setup(){ syscalls("winAPI", "amd64"); }

global main(){
    int user32 = ffiLoadLibrary("user32.dll");
    functionAddress box = ffiLookup(user32, "MessageBoxA");
    ffiCall(box, "cdecl:int32(int64,cstring,cstring,int32)",
            [int 0, str "hello", str "lynxer", int 0]);
    ffiCloseLibrary(user32);
}
```

A curated wrapper over that — more DLLs and first-class `HANDLE` values — is
tracked in [../todo.md](../todo.md).

## See also

- [syscalls.md](syscalls.md) — the target selector and the named Linux syscalls
- [windows.md](windows.md) — the Windows port status and what it excludes
- [native-modules.md](native-modules.md) — `ffiLoadLibrary`/`ffiLookup`/`ffiCall`
