# fileIO

File reading, writing, copying, moving and metadata helpers.

**Backend:** native — `stdlib/fileIO.so`, built from `stdlib/fileIO.cpp` over
`<fstream>` and `<filesystem>`. **Import:** `import("fileIO")` →
`global.fileIO.*`

Functions return `""`, `false` or `-1` on error.

| Function | Signature | Notes |
| --- | --- | --- |
| `readFile` | `(str path) -> str` | Entire file as UTF-8 text, or `""` |
| `writeFile` | `(str path, str content) -> bool` | Overwrites |
| `appendFile` | `(str path, str content) -> bool` | Appends |
| `fileExists` | `(str path) -> bool` | True for a regular file |
| `deleteFile` | `(str path) -> bool` | Removes the file |
| `readLines` | `(str path) -> list` | Content split on newlines; empty list on error |
| `copyFile` | `(str src, str dst) -> bool` | Overwrites `dst` |
| `moveFile` | `(str src, str dst) -> bool` | Renames or moves |
| `fileSize` | `(str path) -> int` | Bytes, or `-1` |
| `countLines` | `(str path) -> int` | Line count, or `-1` |
| `readLine` | `(str path, int n) -> str` | 0-based line, or `""` |
| `fileModTime` | `(str path) -> str` | `YYYY-MM-DD HH:MM:SS` local time, or `""` |
| `fileExtension` | `(str path) -> str` | Extension including the dot, or `""` |
| `fileIsEmpty` | `(str path) -> bool` | True for an existing zero-byte file |
| `tempFile` | `(str suffix) -> str` | Creates a temporary file and returns its path |
| `tempDir` | `() -> str` | Creates a temporary directory and returns its path |
| `stemName` | `(str path) -> str` | Filename without directory or extension |
| `readFileLines` | `(str path, str sep) -> str` | Lines joined with `sep` |

`readLines` splits the raw file content on `\n`, so a file that ends with a
newline produces a trailing empty element — matching the Python reference.

## Example

```lynx
global setup(){ import("fileIO"); }

global main(){
    global.fileIO.writeFile("/tmp/notes.txt", "alpha\nbeta\n");
    println(global.fileIO.readLines("/tmp/notes.txt"));
    println(global.fileIO.countLines("/tmp/notes.txt"));
    println(global.fileIO.readFileLines("/tmp/notes.txt", " | "));
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
