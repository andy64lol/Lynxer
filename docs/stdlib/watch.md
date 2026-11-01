# watch

Filesystem change events over inotify (Linux), kqueue `EVFILT_VNODE` (macOS and
the BSDs) or `ReadDirectoryChangesW` (Windows).

**Backend:** native — `stdlib/watch.so`, built from the Rust crate `rust/watch`.

**Import:** `import("watch")` → `watch.*`

## How events reach Lynxer

1. `watchAdd` returns an integer handle.
2. Wait for events with `watchWait(handle, timeoutMs)` — which blocks with the
   interpreter lock released. On POSIX you can instead register
   `watchFd(handle)` with `asyncPollRegister` and wait with `asyncPollWait`.
   Windows directory watches have no pollable descriptor, so `watchFd` returns
   `-1` there and `watchWait` is required.
3. After the wait, `watchDrain(handle)` reads the pending events as a JSON
   array of `{"path", "kind"}`, where `kind` is `create`, `modify`, `delete`,
   `moved_from`, `moved_to`, `attrib` or `other`.
4. `watchRemove` / `watchClose` stop watching and release the handle.

A failure is a scalar sentinel: `watchAdd`, `watchFd` and `watchWait` yield `-1`,
`watchDrain` yields `""` for an unknown handle, and the predicates yield `false`.

## Functions

| Function | Signature | Returns |
| --- | --- | --- |
| `watchAdd` | `(str path, bool recursive)` | a handle, or `-1` |
| `watchFd` | `(int handle)` | the backend descriptor, or `-1` (always `-1` on Windows) |
| `watchWait` | `(int handle, int timeoutMs)` | `1` readable, `0` timeout, `-1` unknown |
| `watchDrain` | `(int handle)` | a JSON array of events, or `""` |
| `watchSetDebounce` | `(int handle, int milliseconds)` | `true` / `false` |
| `watchRemove` | `(int handle)` | `true` / `false` |
| `watchClose` | `(int handle)` | `true` / `false` (alias of `watchRemove`) |

## Example

```lynx
fn setup(){ import("watch"); import("fileIO"); import("os"); }

fn main(){
    str root = "watched";
    if(os.exists(root)){ os.rmTree(root); }
    os.mkdir(root);

    int handle = watch.watchAdd(root, true);

    // Block until events are ready (the lock is released while it waits),
    // then drain. On POSIX you can alternatively register the descriptor with
    // `asyncPollRegister(watch.watchFd(handle))` and wait with
    // `asyncPollWait(1000)`.
    fileIO.writeFile(root + "/a.txt", "hi");
    println(watch.watchWait(handle, 1000));
    println(watch.watchDrain(handle));

    println(watch.watchClose(handle));
    os.rmTree(root);
}
```

---

## See also

- [Standard library contracts](../stdlib-contracts.md)
- [Built-in functions](../builtins.md)
- [Limitations](../limitations.md)
