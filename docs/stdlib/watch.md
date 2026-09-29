# watch

Filesystem change events over inotify.

**Backend:** native — `stdlib/watch.so`, built from the Rust crate `rust/watch`
over `inotify`. Linux only.

**Import:** `import("watch")` → `global.watch.*`

## How events reach Lynxer

A module cannot release the interpreter lock, so `watch` never blocks. Instead:

1. `watchAdd` returns an integer handle.
2. `watchFd(handle)` returns the inotify descriptor; register it with
   `asyncPollRegister` and wait with `asyncPollWait`.
3. When the descriptor is ready, `watchDrain(handle)` reads the pending events
   as a JSON array of `{"path", "kind"}`, where `kind` is `create`, `modify`,
   `delete`, `moved_from`, `moved_to`, `attrib` or `other`.
4. `watchRemove` / `watchClose` stop watching and release the handle.

A failure is a scalar sentinel: `watchAdd` and `watchFd` yield `-1`,
`watchDrain` yields `""` for an unknown handle, and the predicates yield `false`.

## Functions

| Function | Signature | Returns |
| --- | --- | --- |
| `watchAdd` | `(str path, bool recursive)` | a handle, or `-1` |
| `watchFd` | `(int handle)` | the inotify descriptor, or `-1` |
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

    // Wait on the descriptor with the interpreter's poll set, then drain.
    // global.asyncPollRegister(global.watch.watchFd(handle));
    // global.asyncPollWait(1000);

    global.fileIO.writeFile(root + "/a.txt", "hi");
    sleep(0.3);
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
