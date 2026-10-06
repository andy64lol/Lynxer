# sys

Runtime, process and platform information.

**Backend:** native — `stdlib/sys.so`, built from `stdlib/sys.cpp`.
**Import:** `import("sys")` → `global.sys.*`

| Function | Signature | Notes |
| --- | --- | --- |
| `platform` | `() -> str` | Host operating system: `linux`, `darwin`, `freebsd`, `netbsd`, `openbsd`, `dragonfly` or `win32`; otherwise `unknown` |
| `architecture` | `() -> str` | Canonical syscall architecture of this build: `amd64` or `arm64`; `""` otherwise. Pass with `platform()` to `syscalls(...)` |
| `cpuCount` | `() -> int` | Online processors (`0` when unknown) |
| `pageSize` | `() -> int` | Memory page size in bytes (`0` when unknown) |
| `memoryTotal` | `() -> int` | Total physical memory in bytes (`0` when unavailable) |
| `memoryAvailable` | `() -> int` | Available physical memory in bytes (`0` when unavailable) |
| `uptime` | `() -> int` | Seconds since boot (`0` when unavailable) |
| `bootTime` | `() -> int` | Boot time as a Unix timestamp (`0` when unavailable) |
| `loadAverage` | `() -> str` | JSON array `[1m, 5m, 15m]`; `[]` when unavailable |
| `version` | `() -> str` | Lynxer version, e.g. `Lynxer 0.1.8.3` |
| `versionInfo` | `() -> str` | JSON `{major, minor, micro, patch, releaselevel, serial}` |
| `implementation` | `() -> str` | `Lynxer` |
| `apiVersion` | `() -> str` | `0.1` |
| `isFrozen` | `() -> bool` | Always `false` |
| `getpid` | `() -> int` | Process id |
| `getMaxSize` | `() -> int` | Maximum value of `size_t` |
| `getByteOrder` | `() -> str` | `little` or `big` |
| `getDefaultEncoding` | `() -> str` | `utf-8` |
| `getFilesystemEncoding` | `() -> str` | `utf-8` |
| `isatty` | `() -> bool` | Whether stdout is a terminal |
| `stdinName` / `stdoutName` | `() -> str` | `<stdin>` / `<stdout>` |
| `executable` | `() -> str` | Path of the running executable, or `""` when the host does not expose it |
| `prefix` / `execPrefix` | `() -> str` | Directory containing the executable, or `""` when unavailable |
| `argv` | `() -> str` | The program's command line as a JSON array |
| `argCount` | `() -> int` | Number of program command-line entries |
| `getArg` | `(int index) -> str` | Entry at `index`, or `""` |
| `exit` | `(int code)` | Exits immediately with `code` |
| `exitOk` / `exitError` | `()` | Exits with `0` / `1` |

Because Lynxer has no Python runtime, `version()` reports the Lynxer version
and there is no `sys.path`, `sys.modules` or recursion-limit surface. `argv`
describes the **program's own** command line: entry 0 is the script path
(`lynxer prog.lynx a b` → `["prog.lynx", "a", "b"]`), or the executable itself
for a compiled program, followed by the arguments passed after it.

CPU/page-size/load-average queries use POSIX facilities; physical memory and
boot-time values use the host's system-information interfaces on Linux and the
supported BSD/macOS targets. Any unavailable host value uses the sentinel in
the table rather than fabricating a result.

## Example

```lynx
global setup(){ import("sys"); }

global main(){
    println(global.sys.platform());
    println(global.sys.architecture());
    println(global.sys.version());
    println(global.sys.argCount());
}
```

---

## See also

- [stdlib-contracts.md](../stdlib-contracts.md) — the contract this module
  implements, including the error sentinel family it uses.
- [builtins.md](../builtins.md) — the functions the interpreter implements
  itself.
- [limitations.md](../limitations.md) — the full divergence register.
