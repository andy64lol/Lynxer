# multiprocessing

Run shell commands in parallel.

**Backend:** native + pure — `stdlib/multiprocessing.so`
(`stdlib/multiprocessing.cpp`) runs the commands; `stdlib/multiprocessing.lynx`
reads the results back into Lynxer lists.
**Import:** `import("multiprocessing")` → `global.multiprocessing.*`

Commands run in worker threads, each spawning its own shell subprocess. Results
are always returned in input order, whatever order they finish in. A command's
`stderr` is captured into its output, and a command that outlives the timeout
(`LYNXER_MP_TIMEOUT` seconds, default `300`, `0` for no limit) is killed and
reports exit code `124`.
The timeout kills the complete process group, not only the shell wrapper.
Captured stdout and stderr are concatenated in their original pipe order.

| Function | Signature | Returns |
| --- | --- | --- |
| `workerCount` | `() -> int` | Number of online CPUs, at least `1` |
| `runParallel` | `(list commands) -> list` | Captured stdout per command |
| `runParallelSilent` | `(list commands) -> list` | Exit code per command |
| `runParallelProcess` | `(list commands) -> list` | Alias of `runParallel` |
| `mapShell` | `(str template, list items) -> list` | Replaces the first `{}` in `template` with each item, runs in parallel, returns stdout per item |
| `threadMap` | `(str template, list items) -> list` | Alias of `mapShell` |
| `runParallelHandle` | `(list commands) -> int` | Low-level result handle for output/code queries |

`resultCount(handle)`, `resultAt(handle, index)`, and `codeAt(handle, index)`
read one handle. `release(handle)` frees it and returns `true`; subsequent
queries return `0`, `""`, or `-1`. The list-returning convenience functions
release their handles automatically.

Results are collected through a native handle registry rather than a joined
string, because command output can contain any character. The wrappers release
each handle automatically. Native callers that use the handle operations
directly must call `release(handle)`; result queries on released or unknown
handles return their invalid-handle sentinels.

## Example

```lynx
global setup(){ import("multiprocessing"); }

global main(){
    list commands = listPush(listPush(listRepeat("", 0), "printf a"), "printf b");
    println(global.multiprocessing.runParallel(commands));

    list items = listPush(listPush(listRepeat("", 0), "x"), "y");
    println(global.multiprocessing.mapShell("printf {}", items));
}
```

---

## See also

- [stdlib-contracts.md](../stdlib-contracts.md) — the contract this module
  implements, including the error sentinel family it uses.
- [builtins.md](../builtins.md) — the functions the interpreter implements
  itself.
- [limitations.md](../limitations.md) — the full divergence register.
