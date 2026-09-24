# multiprocessing

Run shell commands in parallel.

**Backend:** native + pure — `stdlib/multiprocessing.so`
(`stdlib/multiprocessing.cpp`) runs the commands; `stdlib/multiprocessing.lynx`
reads the results back into Clynxer lists.
**Import:** `import("multiprocessing")` → `global.multiprocessing.*`

Commands run in worker threads, each spawning its own shell subprocess. Results
are always returned in input order, whatever order they finish in. No timeout is
applied.

| Function | Signature | Returns |
| --- | --- | --- |
| `workerCount` | `() -> int` | Number of online CPUs, at least `1` |
| `runParallel` | `(list commands) -> list` | Captured stdout per command |
| `runParallelSilent` | `(list commands) -> list` | Exit code per command |
| `runParallelProcess` | `(list commands) -> list` | Alias of `runParallel` |
| `mapShell` | `(str template, list items) -> list` | Replaces the first `{}` in `template` with each item, runs in parallel, returns stdout per item |
| `threadMap` | `(str template, list items) -> list` | Alias of `mapShell` |

Results are collected through a native handle registry rather than a joined
string, because command output can contain any character. The wrappers release
each handle automatically.

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
- [parity.md](../parity.md) — the parity scope with Python Clynxer.
- [limitations.md](../limitations.md) — the full divergence register.
