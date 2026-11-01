# Windows API standard libraries

On Windows, Lynxer exposes Win32 calls through the native-backed
`winAPI-amd64` and `winAPI-arm64` standard-library modules. Import the module
matching the process architecture with `importAs`:

```lynx
fn setup() {
    importAs("winAPI-amd64", "windowsApi");
}

fn main() {
    println(windowsApi.getProcessId());
    println(windowsApi.getComputerName());
    println(windowsApi.getCurrentDirectory());
}
```

Most calls are `kernel32`; `getKnownFolderPath` uses `shell32`/`ole32` for the
Known Folder GUIDs. Both libraries ship with Windows, so nothing extra has to be
installed.

On Windows arm64, import `winAPI-arm64` instead. The module checks the host
operating system and architecture for each operation. The API catalog below
uses the `windowsApi` alias for either module.

## Host availability

- Calls require a Windows runtime and validate that the imported module
  matches the running process; there is no cross-architecture dispatch.
- The legacy `winAPI.*` built-ins remain available but are deprecated. New
  programs should import `winAPI-amd64` or `winAPI-arm64`.
- A misspelled operation in the legacy surface reports the closest match, e.g.
  `unknown winAPI function 'getProcesId'. You meant: getProcessId?`.

## Conventions

- Failed Windows operations use the native API's **scalar sentinels**: `-1`
  for a handle, size, position, byte count or free-space figure; `false` for a
  predicate; `""` for a failed read. `windowsApi.getLastError()` returns the Win32
  error code for the most recent call.
- A **handle is an integer**. Everything it refers to must be released with
  `windowsApi.closeHandle()`.
- Wrong argument types are source-located errors, e.g.
  `windowsApi.readFile() failed: expects an integer handle and length`.
- Text is UTF-8 at the Lynxer boundary; the `*W` calls are used internally.

## System information

| Function | Returns |
| --- | --- |
| `windowsApi.getProcessId()` | the current process id |
| `windowsApi.getCurrentDirectory()` | the current directory |
| `windowsApi.getComputerName()` | the computer name |
| `windowsApi.getTempPath()` | the temporary directory (with a trailing separator) |
| `windowsApi.getSystemDirectory()` | the system directory |
| `windowsApi.getWindowsDirectory()` | the Windows directory |
| `windowsApi.getModuleFileName()` | the running executable's full path |
| `windowsApi.getTickCount()` | milliseconds since the system started |
| `windowsApi.getLastError()` | the last Win32 error code |
| `windowsApi.getDiskFreeBytes(path)` | free bytes on the volume, or `-1` |
| `windowsApi.getKnownFolderPath(name)` | a Known Folder path, or `""` |
| `windowsApi.getUserName()` | the current account name |
| `windowsApi.isUserAnAdmin()` | whether the process has administrator rights |
| `windowsApi.getSystemMetrics(index)` | a `GetSystemMetrics` value, or `0` |
| `windowsApi.getLogicalDrives()` | the drive bitmask |
| `windowsApi.getDriveType(root)` | the `DRIVE_*` type, or `0` |
| `windowsApi.getOSVersion()` | `major.minor.build` from `RtlGetVersion` |
| `windowsApi.getLocaleName()` | the user's locale name, e.g. `en-US` |
| `windowsApi.getTimeZoneName()` | the current standard time-zone name |
| `windowsApi.getSystemTime()` | milliseconds since the Unix epoch (UTC) |
| `windowsApi.getSystemInfo()` | JSON `{processorArchitecture, pageSize, processorCount, allocationGranularity, processorLevel, processorRevision, activeProcessorMask}` |
| `windowsApi.getMemoryStatus()` | JSON `{load, total, available, totalPageFile, availablePageFile, totalVirtual, availableVirtual}` in bytes / percent |
| `windowsApi.getPowerStatus()` | JSON `{acLineStatus, batteryFlag, batteryLifePercent, batteryLifeSeconds, batteryFullLifeSeconds}` |
| `windowsApi.getVolumeInfo(path)` | JSON `{label, filesystem, serial, maxComponentLength, flags}`, or `""` |
| `windowsApi.getDiskSpace(path)` | JSON `{free, total, totalFree}` in bytes, or `""` |
| `windowsApi.getACP()` | the ANSI code page |
| `windowsApi.getOEMCP()` | the OEM code page |
| `windowsApi.getCommandLine()` | the full command line |
| `windowsApi.getCurrentProcess()` | a closable duplicate handle to this process, or `-1` |
| `windowsApi.getModuleHandle(name)` | a module handle (`""` for the running image), or `-1` |
| `windowsApi.getActiveProcessorCount(group)` | active processors in a group, or `-1` |
| `windowsApi.formatMessage(code)` | the system message for a Win32 error code |
| `windowsApi.sleep(milliseconds)` | — |
| `windowsApi.beep(frequency, duration)` | whether it played |
| `windowsApi.outputDebugString(text)` | — (goes to the debugger) |

`getKnownFolderPath` accepts `desktop`, `documents`, `downloads`, `music`,
`pictures`, `videos`, `localAppData`, `roamingAppData`, `programData`, `profile`,
`public`, `windows` and `system`; an unknown name returns `""`, as does a
folder the shell cannot resolve. It is the Windows counterpart of the XDG
directories exposed by `os.configDir`/`os.dataDir`/`os.cacheDir`.

## Environment

| Function | Returns |
| --- | --- |
| `windowsApi.getEnvironmentVariable(name)` | the variable's value, or `""` when unset |
| `windowsApi.setEnvironmentVariable(name, value)` | whether it was set |
| `windowsApi.expandEnvironmentStrings(text)` | `text` with `%VAR%` expanded |

## Files and handles

| Function | Returns |
| --- | --- |
| `windowsApi.createFile(path, mode)` | a handle, or `-1` |
| `windowsApi.readFile(handle, length)` | up to `length` bytes, or `""` |
| `windowsApi.writeFile(handle, text)` | bytes written, or `-1` |
| `windowsApi.closeHandle(handle)` | whether it closed |
| `windowsApi.fileSize(handle)` | size in bytes, or `-1` |
| `windowsApi.seekFile(handle, offset)` | the new absolute position, or `-1` |
| `windowsApi.deleteFile(path)` | whether it was removed |
| `windowsApi.copyFile(source, destination, overwrite)` | whether it was copied |
| `windowsApi.moveFile(source, destination)` | whether it was moved |
| `windowsApi.createDirectory(path)` | whether it was created |
| `windowsApi.removeDirectory(path)` | whether it was removed |
| `windowsApi.getFileAttributes(path)` | the attribute bits, or `-1` |
| `windowsApi.setFileAttributes(path, attributes)` | whether they were set |
| `windowsApi.getFullPathName(path)` | the absolute path, or `""` |
| `windowsApi.flushFileBuffers(handle)` | whether the buffers were flushed |
| `windowsApi.getTempFileName(prefix)` | a new unique temp file path, or `""` |
| `windowsApi.setCurrentDirectory(path)` | whether the working directory changed |
| `windowsApi.getFileType(handle)` | the `FILE_TYPE_*` value, or `-1` |
| `windowsApi.getFileTime(path)` | JSON `{creation, access, write}` in Unix milliseconds, or `""` |

`mode` is `"read"` (`OPEN_EXISTING`), `"write"` (`CREATE_ALWAYS`, truncated),
`"append"` (`OPEN_ALWAYS`, seek to end) or `"readwrite"` (`OPEN_ALWAYS`).
`seekFile` positions absolutely from the start of the file.

```lynx
fn main(){
    str path = windowsApi.getTempPath() + "demo.txt";

    int file = windowsApi.createFile(path, "write");
    if (file < 0) {
        println("cannot create: " + windowsApi.getLastError());
        return;
    }
    windowsApi.writeFile(file, "hello");
    windowsApi.closeHandle(file);

    int reader = windowsApi.createFile(path, "read");
    println(windowsApi.fileSize(reader));
    println(windowsApi.readFile(reader, 64));
    windowsApi.closeHandle(reader);
    windowsApi.deleteFile(path);
}
```

## Shell, clipboard and dialogs

| Function | Returns |
| --- | --- |
| `windowsApi.shellOpen(path)` | whether `ShellExecuteW` accepted the request |
| `windowsApi.shellOpenWith(path, arguments, verb)` | the same, with an explicit verb such as `"edit"` |
| `windowsApi.messageBox(text, title)` | the `MessageBoxW` result |
| `windowsApi.clipboardGet()` | the clipboard's Unicode text, or `""` |
| `windowsApi.clipboardSet(text)` | whether the clipboard was updated |
| `windowsApi.notify(title, message)` | whether a tray balloon was queued |

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
| `windowsApi.createNamedPipe(name)` | a server handle, or `-1` |
| `windowsApi.connectNamedPipe(handle)` | whether a client connected |
| `windowsApi.openNamedPipe(name)` | a client handle, or `-1` after a 5 s wait |
| `windowsApi.readNamedPipe(handle, length)` | up to `length` bytes, or `""` |
| `windowsApi.writeNamedPipe(handle, text)` | bytes written, or `-1` |
| `windowsApi.disconnectNamedPipe(handle)` | whether the server disconnected |
| `windowsApi.peekNamedPipe(handle)` | bytes available, or `-1` |

Names are relative to `\\.\pipe\`; a full `\\.\pipe\name` path is also
accepted. `openNamedPipe` retries while the server is still creating the pipe,
so a client may start before the server. Pipe handles are ordinary handles and
are released with `windowsApi.closeHandle`. Reads and writes are blocking.

```lynx
fn main(){
    int server = windowsApi.createNamedPipe("lynxer-demo");
    // In another process:
    //   int client = windowsApi.openNamedPipe("lynxer-demo");
    windowsApi.connectNamedPipe(server);
    println(windowsApi.readNamedPipe(server, 64));
    windowsApi.writeNamedPipe(server, "pong");
    windowsApi.disconnectNamedPipe(server);
    windowsApi.closeHandle(server);
}
```

## Processes and threads

| Function | Returns |
| --- | --- |
| `windowsApi.getCurrentThreadId()` | the calling thread's id |
| `windowsApi.openProcess(processId)` | a handle with query/terminate/synchronize access, or `-1` |
| `windowsApi.terminateProcess(handle, exitCode)` | whether it was terminated |
| `windowsApi.getExitCodeProcess(handle)` | the exit code, or `-1` (`259` while still alive) |
| `windowsApi.createProcess(commandLine)` | the new process's handle, or `-1` |
| `windowsApi.waitForSingleObject(handle, milliseconds)` | `0` when signalled, `258` on timeout, `-1` on failure |
| `windowsApi.getPriorityClass(handle)` | the `*_PRIORITY_CLASS` value, or `-1` |
| `windowsApi.setPriorityClass(handle, class)` | whether it was set |

`createProcess` starts `commandLine` through `CreateProcessW` with the inherited
environment and no redirection; the thread handle is closed for you, so only the
returned process handle needs `closeHandle`.

## Console

These talk to the console the process is attached to; under a redirected stream
or a non-interactive session they report `-1`/`false` rather than failing the
call.

| Function | Returns |
| --- | --- |
| `windowsApi.getStdHandle(stream)` | the handle for `"input"`, `"output"` or `"error"`, or `-1` |
| `windowsApi.getConsoleMode(handle)` | the console mode, or `-1` |
| `windowsApi.setConsoleMode(handle, mode)` | whether it was set |
| `windowsApi.setConsoleTitle(text)` | whether the title was set |
| `windowsApi.getConsoleOutputCP()` | the console output code page |
| `windowsApi.setConsoleOutputCP(codePage)` | whether it was set |
| `windowsApi.getConsoleWindow()` | the console window handle, or `-1` |
| `windowsApi.getConsoleScreenBufferInfo()` | JSON `{width, height, bufferWidth, bufferHeight, cursorX, cursorY, attributes}`, or `""` |

## Timing

| Function | Returns |
| --- | --- |
| `windowsApi.queryPerformanceCounter()` | the high-resolution counter, or `-1` |
| `windowsApi.queryPerformanceFrequency()` | ticks per second, or `-1` |

## The long tail

Anything not in the tables above — other DLLs, the `*A`/`*W` pairs, structs,
`HWND`/`SOCKET` values and the 32-bit `__stdcall` convention — is reachable
through the FFI built-ins ([native-modules.md](native-modules.md)). On 64-bit
Windows the Win32 calling convention is the one `cdecl:` describes, so a Win32
call looks like any other:

```lynx
fn main(){
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

- [syscalls.md](syscalls.md) — the Linux named-syscall standard-library modules
- [windows.md](windows.md) — the Windows port status and what it excludes
- [Windows amd64 WinAPI](winapi/windows-amd64.md)
- [Windows arm64 WinAPI](winapi/windows-arm64.md)
- [native-modules.md](native-modules.md) — `ffiLoadLibrary`/`ffiLookup`/`ffiCall`
