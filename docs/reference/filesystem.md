# Filesystem API

Lynxer exposes a small handle-based filesystem API through concise camelCase
`filesystem*` built-ins. Paths are passed as strings and filesystem failures are
reported as runtime errors containing both the operation and the original
errno number/text.

```lynx
global main() {
    int file = filesystemOpen("notes.txt", "w");
    filesystemWrite(file, "hello\n");
    filesystemClose(file);
    println(filesystemStat("notes.txt"));
}
```

## Functions

| Function | Description |
|---|---|
| `filesystemOpen(path, mode, permissions?)` | Open a file and return a managed handle. Modes are `r`, `w`, `a`, `r+`, `w+`, and `a+`. |
| `filesystemRead(handle, maxBytes)` | Read UTF-8 bytes and return a string. |
| `filesystemWrite(handle, data)` | Write UTF-8 data and return the byte count. |
| `filesystemClose(handle)` | Close a handle deterministically. |
| `filesystemStat(path)` | Return JSON metadata using `type`, `size`, `mode`, and timestamps. Symlinks are reported without following them. |
| `filesystemList(path)` | Return sorted direct child names. |
| `filesystemMkdir(path, parents?)` | Create a directory, optionally including missing parents. |
| `filesystemRemove(path)` | Remove a file, symlink, or empty directory. |
| `filesystemRename(source, target)` | Rename a filesystem entry. |
| `filesystemLink(source, target, symbolic?)` | Create a hard link or symbolic link. |
| `filesystemReadLink(path)` | Read a symbolic link target. |
| `filesystemChmod(path, mode)` | Set numeric permission bits. |

Handles left open by a program are closed when the Lynxer runtime exits. An
unknown or already closed handle is an error rather than an implicit fallback.