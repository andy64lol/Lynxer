# os

Filesystem, process, environment and platform helpers.

**Backend:** native — `stdlib/os.so`, built from `stdlib/os.cpp` over
`<filesystem>` and POSIX APIs. **Import:** `import("os")` → `global.os.*`

Functions return safe defaults on failure (`false`, `""`, `-1`, an empty list).

## Files and directories

| Function | Signature | Notes |
| --- | --- | --- |
| `getcwd` | `() -> str` | Current working directory |
| `chdir` | `(str path) -> bool` | Changes the working directory |
| `listdir` | `(str path) -> list` | Directory entries, in filesystem order |
| `listdirExt` | `(str path, str ext) -> list` | Entries whose name ends with `ext` |
| `walkFiles` | `(str path) -> str` | Recursive file paths as a newline-joined string |
| `mkdir` | `(str path) -> bool` | Creates one directory; `false` if it exists |
| `makedirs` | `(str path) -> bool` | Creates a directory tree (existing is fine) |
| `rmdir` | `(str path) -> bool` | Removes an empty directory |
| `remove` | `(str path) -> bool` | Removes a file |
| `rename` | `(str src, str dst) -> bool` | Renames or moves |
| `rmTree` | `(str path) -> bool` | Recursively removes a tree |
| `copyTree` | `(str src, str dst) -> bool` | Recursively copies; fails if `dst` exists |
| `exists` / `isFile` / `isDir` | `(str path) -> bool` | Path predicates |

## Paths and environment

| Function | Signature | Notes |
| --- | --- | --- |
| `joinPath` | `(str a, str b) -> str` | Joins with the platform separator |
| `basename` / `dirname` | `(str path) -> str` | Final / parent component |
| `absPath` | `(str path) -> str` | Absolute, normalized path |
| `extname` | `(str path) -> str` | Extension including the dot |
| `normPath` | `(str path) -> str` | Collapses `.`, `..` and repeated separators |
| `expandUser` | `(str path) -> str` | Expands a leading `~` or `~user` |
| `sep` | `() -> str` | `/` on POSIX hosts |
| `getenv` / `setenv` | `(str key[, str value])` | Reads / writes an environment variable |
| `tempDir` | `() -> str` | `$TMPDIR`, `$TEMP`, `$TMP`, else `/tmp` |
| `homedir` | `() -> str` | `$HOME` |
| `username` | `() -> str` | Login name from the password database |
| `hostname` | `() -> str` | `gethostname(2)` |
| `getpid` | `() -> int` | Process id |
| `cpuCount` | `() -> int` | Online CPUs, at least `1` |
| `diskTotal` / `diskFree` | `(str path) -> int` | Bytes via `statvfs`; `-1` on error |

## Platform information

`getSystemName`, `getSystemRelease`, `getSystemVersion`, `getSystemMachine`,
`getSystemProcessor`, `getSystemNode` — all `() -> str` from `uname(2)`.
`getSystemArchitecture() -> str` returns `"64bit"` or `"32bit"`.

JSON-returning helpers:

| Function | Signature | Returns |
| --- | --- | --- |
| `getSystemUname` | `() -> str` | `{system, node, release, version, machine, processor}` |
| `getSystemInfo` | `() -> str` | The same fields plus `architecture`, `python`, `pythonImplementation`, `pythonExecutable` |
| `getSystemDistro` | `() -> str` | Parsed `/etc/os-release`, or `{}` |

Because Clynxer has no Python runtime, `getPythonVersion()` returns `""`,
`getPythonImplementation()` returns `"CLynxer"`, and the `python*` fields of
`getSystemInfo` mirror that.

## Example

```lynx
global setup(){ import("os"); }

global main(){
    println(global.os.getcwd());
    list entries = global.os.listdir(".");
    println(returnLength(entries));
    println(global.os.getSystemName());
}
```

---

## See also

- [stdlib-contracts.md](../stdlib-contracts.md) — the contract this module
  implements, including the error sentinel family it uses.
- [builtins.md](../builtins.md) — the functions the interpreter implements
  itself.
- [parity.md](../parity.md) — the parity scope with Python Lynxer.
- [limitations.md](../limitations.md) — the full divergence register.
