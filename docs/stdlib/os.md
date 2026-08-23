# os

OS, filesystem, process, and Python-platform helpers inspired by Python's
`os`, `os.path`, and `platform` modules.

Import it inside `setup()`:

```lynx
global setup(){
    import("os");
}
```

## Files and directories

| Function | Description |
|----------|-------------|
| `getcwd()` | Current working directory |
| `chdir(path)` | Change directory; returns `true` on success |
| `listdir(path)` | Directory entries as a list |
| `mkdir(path)` | Create one directory |
| `makedirs(path)` | Create a directory tree |
| `rmdir(path)` | Remove an empty directory |
| `remove(path)` | Remove a file |
| `rename(src, dst)` | Rename or move a path |
| `exists(path)` | Whether a path exists |
| `isFile(path)` | Whether a path is a regular file |
| `isDir(path)` | Whether a path is a directory |
| `rmTree(path)` | Remove a directory tree |
| `copyTree(src, dst)` | Copy a directory tree |
| `listdirExt(path, ext)` | List entries with a matching suffix |
| `walkFiles(path)` | Recursive file paths joined by newlines |

## Paths and environment

| Function | Description |
|----------|-------------|
| `joinPath(a, b)` | Join path components |
| `basename(path)` | Final path component |
| `dirname(path)` | Parent directory component |
| `absPath(path)` | Absolute path |
| `extname(path)` | File extension, including the dot |
| `normPath(path)` | Normalized path |
| `expandUser(path)` | Expand `~` and `~user` |
| `sep()` | Platform path separator |
| `getenv(key)` | Environment value, or `""` |
| `setenv(key, value)` | Set a process environment value |
| `tempDir()` | System temporary directory |
| `homedir()` | Current user's home directory |
| `username()` | Current login name |
| `hostname()` | System hostname |
| `getpid()` | Current process ID |
| `cpuCount()` | CPU count, or `1` when unavailable |
| `diskTotal(path)` | Total disk capacity in bytes, or `-1` |
| `diskFree(path)` | Free disk capacity in bytes, or `-1` |

## Platform and runtime information

These helpers use Python's standard-library `platform` module and return
portable strings where possible:

| Function | Description |
|----------|-------------|
| `getSystemName()` | OS family, such as `Linux`, `Darwin`, or `Windows` |
| `getSystemRelease()` | OS release string |
| `getSystemVersion()` | OS version string |
| `getSystemMachine()` | Machine architecture name, such as `x86_64` |
| `getSystemProcessor()` | Processor name, when reported |
| `getSystemArchitecture()` | Executable bitness, usually `64bit` or `32bit` |
| `getSystemNode()` | Network node or host name |
| `getPythonVersion()` | Python runtime version |
| `getPythonImplementation()` | Python implementation, such as `CPython` |
| `getSystemUname()` | JSON object containing `platform.uname()` fields |
| `getSystemInfo()` | JSON object with OS, machine, Python, and executable details |
| `getSystemDistro()` | Linux distribution metadata as JSON, or `{}` elsewhere |

Example:

```lynx
global main(){
    println(global.os.getSystemName(), " ", global.os.getSystemRelease());
    println(global.os.getSystemMachine(), " ", global.os.getSystemArchitecture());
    println(global.os.getSystemInfo());
}
```

Filesystem and system queries return safe defaults on errors. JSON-returning
platform functions return `"{}"` when the information is unavailable.
