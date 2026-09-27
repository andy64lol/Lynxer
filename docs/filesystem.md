# Filesystem API

A small handle-based filesystem API exposed through the `filesystem*` built-ins.
Paths are strings, and failures are reported as source-located runtime errors
carrying the operation and the original errno number and text.

```lynx
global setup(){}

global main(){
    int file = filesystemOpen("notes.txt", "w");
    filesystemWrite(file, "hello\n");
    filesystemClose(file);
    println(filesystemStat("notes.txt"));
}
```

## Functions

| Function | Notes |
| --- | --- |
| `filesystemOpen(path, mode, permissions?)` | Opens a file and returns a managed handle. Modes are `r`, `w`, `a`, `r+`, `w+`, `a+`; `permissions` defaults to `0666` (decimal `438`). |
| `filesystemRead(handle, maxBytes)` | Reads UTF-8 bytes and returns a string. |
| `filesystemWrite(handle, data)` | Writes UTF-8 data and returns the byte count. |
| `filesystemClose(handle)` | Closes a handle. |
| `filesystemStat(path)` | Returns JSON metadata with the fields `type`, `size`, `mode`, `modifiedTime`, `accessTime` and `changeTime`. Uses `lstat`, so a symlink is reported without being followed. |
| `filesystemList(path)` | Returns the sorted direct child names. |
| `filesystemMkdir(path, parents?)` | Creates a directory; `parents` also creates missing parents. |
| `filesystemRemove(path)` | Removes a file, symlink, or empty directory. |
| `filesystemRename(source, target)` | Renames an entry; returns `0` on success. |
| `filesystemLink(source, target, symbolic?)` | Creates a hard link, or a symbolic link when `symbolic` is true; returns `0` on success. |
| `filesystemReadLink(path)` | Returns a symbolic link's target. Reading a path that is not a symlink fails with `EINVAL`. |
| `filesystemChmod(path, mode)` | Sets numeric permission bits; returns `0` on success. |

Example output of `filesystemStat`:

```json
{"type":"file","size":5,"mode":420,"modifiedTime":1790494188.5760658,"accessTime":1790494188.5760658,"changeTime":1790494188.5760658}
```

## Handles

An unknown or already closed handle is an error rather than an implicit
fallback. Handles a program leaves open are not closed explicitly by the
interpreter — they are released when the process exits (the OS closes the
descriptors). `lynxer/examples/builtin_filesystem.lynx` exercises every function
above.
