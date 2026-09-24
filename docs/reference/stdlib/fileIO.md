# fileIO

File input/output helpers and utilities.

Common return conventions:
- Many functions return `""` or `0`/`false` on error. Check return values accordingly.

Functions:

- `readFile(path)` → string contents of `path` ("" on error).
- `writeFile(path, content)` → `true` on success, `false` on error.
- `appendFile(path, content)` → `true` on success, `false` on error.
- `fileExists(path)` → `true` if `path` is a regular file.
- `deleteFile(path)` → `true` on success, `false` on error.
- `readLines(path)` → list of lines (empty list on error).
- `copyFile(src, dst)` → `true` on success, `false` on error.
- `fileSize(path)` → size in bytes or `-1` on error.
- `moveFile(src, dst)` → `true` on success, `false` on error.
- `countLines(path)` → number of lines or `-1` on error.
- `readLine(path, n)` → nth (0-based) line or `""` on error.
- `fileModTime(path)` → formatted timestamp `YYYY-MM-DD HH:MM:SS` or `""` on error.
- `fileExtension(path)` → file extension (including dot) or `""`.
- `fileIsEmpty(path)` → `true` if file exists and size is 0.
- `tempFile(suffix)` → path to newly created temporary file or `""` on error.
- `tempDir()` → path to temporary directory or `""` on error.
- `stemName(path)` → filename without directory or extension.
- `readFileLines(path, sep)` → read file and join lines using `sep`.

Notes:
- Functions fallback to safe defaults on exceptions; inspect return values in calling code.