# os

OS and filesystem helpers inspired by Python's `os` and `os.path`.

Functions (selected):

- Directory and files: `getcwd()`, `chdir(path)`, `listdir(path)`, `mkdir(path)`, `makedirs(path)`, `rmdir(path)`, `remove(path)`, `rename(src,dst)`.
- Checks: `exists(path)`, `isFile(path)`, `isDir(path)`.
- Path helpers: `joinPath(a,b)`, `basename(path)`, `dirname(path)`, `absPath(path)`, `extname(path)`, `normPath(path)`, `expandUser(path)`.
- Environment/process: `getenv(key)`, `setenv(key,val)`, `getpid()`, `username()`, `homedir()`, `hostname()`.
- System info: `sep()` (path separator), `cpuCount()`, `diskTotal(path)`, `diskFree(path)`.
- Tree ops: `rmTree(path)`, `copyTree(src,dst)`, `listdirExt(path, ext)`, `tempDir()`.
- `walkFiles(path)` — recursively list files as newline-joined string (use `splitStr` to convert to list).

Notes:
- Functions return `true`/`false` or safe defaults on error. Use return checks when needed.