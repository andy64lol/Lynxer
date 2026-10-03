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

Every call is `kernel32`, so nothing extra has to be installed or linked.

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
| `winAPI.sleep(milliseconds)` | — |
| `winAPI.beep(frequency, duration)` | whether it played |
| `winAPI.outputDebugString(text)` | — (goes to the debugger) |

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

## The long tail

Anything not in the tables above — other DLLs, `user32`, `advapi32`, the
`*A`/`*W` pairs, structs, `HANDLE`-typed parameters — is reachable through the
FFI built-ins ([native-modules.md](native-modules.md)). On 64-bit Windows the
Win32 calling convention is the one `cdecl:` describes, so a Win32 call looks
like any other:

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
