# watch

Filesystem change events over inotify (Linux) or kqueue `EVFILT_VNODE` (macOS
and the BSDs).

**Backend:** native — `stdlib/watch.so`, built from the Rust crate `rust/watch`.

**Import:** `import("watch")` → `global.watch.*`

## How events reach Lynxer

1. `watchAdd` returns an integer handle.
2. Wait for the descriptor, either with `watchWait(handle, timeoutMs)` — which
   blocks with the interpreter lock released — or by registering
   `watchFd(handle)` with `asyncPollRegister` and waiting with `asyncPollWait`.
3. When the descriptor is ready, `watchDrain(handle)` reads the pending events
   as a JSON array of `{"path", "kind"}`, where `kind` is `create`, `modify`,
   `delete`, `moved_from`, `moved_to`, `attrib` or `other`.
4. `watchRemove` / `watchClose` stop watching and release the handle.

A failure is a scalar sentinel: `watchAdd`, `watchFd` and `watchWait` yield `-1`,
`watchDrain` yields `""` for an unknown handle, and the predicates yield `false`.

## Functions

| Function | Signature | Returns |
| --- | --- | --- |
| `watchAdd` | `(str path, bool recursive)` | a handle, or `-1` |
| `watchFd` | `(int handle)` | the backend descriptor, or `-1` |
| `watchWait` | `(int handle, int timeoutMs)` | `1` readable, `0` timeout, `-1` unknown |
| `watchDrain` | `(int handle)` | a JSON array of events, or `""` |
| `watchSetDebounce` | `(int handle, int milliseconds)` | `true` / `false` |
| `watchRemove` | `(int handle)` | `true` / `false` |
| `watchClose` | `(int handle)` | `true` / `false` (alias of `watchRemove`) |

## Example

```lynx
global setup(){ import("watch"); import("fileIO"); import("os"); }

global main(){
    str root = "watched";
    if(global.os.exists(root)){ global.os.rmTree(root); }
    global.os.mkdir(root);

    int handle = global.watch.watchAdd(root, true);

    // Block until the descriptor is readable (the lock is released while it
    // waits), then drain. Alternatively, register the descriptor with
    // `asyncPollRegister(global.watch.watchFd(handle))` and wait with
    // `asyncPollWait(1000)`.
    global.fileIO.writeFile(root + "/a.txt", "hi");
    println(global.watch.watchWait(handle, 1000));
    println(global.watch.watchDrain(handle));

    println(global.watch.watchClose(handle));
    global.os.rmTree(root);
}
```

---

## See also

- [Standard library contracts](../stdlib-contracts.md)
- [Built-in functions](../builtins.md)
- [Limitations](../limitations.md)
