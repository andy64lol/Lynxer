# sys

Runtime, process and platform information.

**Backend:** native — `stdlib/sys.so`, built from `stdlib/sys.cpp`.
**Import:** `import("sys")` → `global.sys.*`

| Function | Signature | Notes |
| --- | --- | --- |
| `platform` | `() -> str` | `linux`, `darwin`, `win32` or `unknown` |
| `version` | `() -> str` | Lynxer version, e.g. `Lynxer 0.1.8` |
| `versionInfo` | `() -> str` | JSON `{major, minor, micro, releaselevel, serial}` |
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
| `executable` | `() -> str` | Path of the running executable |
| `prefix` / `execPrefix` | `() -> str` | Directory containing the executable |
| `argv` | `() -> str` | Process command line as a JSON array |
| `argCount` | `() -> int` | Number of command-line entries |
| `getArg` | `(int index) -> str` | Entry at `index`, or `""` |
| `exit` | `(int code)` | Exits immediately with `code` |
| `exitOk` / `exitError` | `()` | Exits with `0` / `1` |

Because Lynxer has no Python runtime, `version()` reports the Lynxer version
and there is no `sys.path`, `sys.modules` or recursion-limit surface. Lynxer
does not forward extra arguments to a program, so `argv` describes the `lynxer`
process itself.

## Example

```lynx
global setup(){ import("sys"); }

global main(){
    println(global.sys.platform());
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
