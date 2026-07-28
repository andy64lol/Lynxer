# sys

Runtime information and process control wrapping Python's `sys` module.

```c
global setup(){ import("sys"); }
global main(){
    println(global.sys.version());
    println(global.sys.platform());
    println(global.sys.argv());
}
```

---

## Version & platform

| Function | Signature | Description |
|----------|-----------|-------------|
| `version` | `version()` | Full Python version string, e.g. `"3.11.4 (main, Jul 5 2023, ...)"`. |
| `versionInfo` | `versionInfo()` | Version info as a JSON object: `{"major":3,"minor":11,"micro":4,"releaselevel":"final","serial":0}`. |
| `platform` | `platform()` | Platform identifier: `"linux"`, `"darwin"`, `"win32"`. |
| `executable` | `executable()` | Absolute path to the current Python interpreter. |
| `prefix` | `prefix()` | `sys.prefix` — Python installation root directory. |
| `execPrefix` | `execPrefix()` | `sys.exec_prefix` — platform-specific installation root. |
| `implementation` | `implementation()` | Python implementation name, e.g. `"cpython"`. |
| `apiVersion` | `apiVersion()` | Python C API version as an integer. |

---

## Command-line arguments

`sys.argv` mirrors Python's command-line argument list.

| Function | Signature | Description |
|----------|-----------|-------------|
| `argv` | `argv()` | Return all arguments as a JSON array, e.g. `'["script.lynx","--flag","value"]'`. |
| `getArg` | `getArg(int index)` | Return `sys.argv[index]` as a string, or `""` if out of range. |
| `argCount` | `argCount()` | Number of arguments (`len(sys.argv)`). |

```c
global main(){
    int n = global.sys.argCount();
    for(int i = 0; i < n; i = i + 1){
        println(global.sys.getArg(i));
    }
}
```

---

## Process control

| Function | Signature | Description |
|----------|-----------|-------------|
| `exit` | `exit(int code)` | Exit the process with the given code (`0` = success). |
| `exitOk` | `exitOk()` | Exit with code `0`. |
| `exitError` | `exitError()` | Exit with code `1`. |

```c
if(not ok){
    global.sys.exitError();
}
```

---

## Module search path (sys.path)

| Function | Signature | Description |
|----------|-----------|-------------|
| `getPath` | `getPath()` | Return `sys.path` as a JSON array of strings. |
| `addPath` | `addPath(str path)` | Append `path` to `sys.path` (no-op if already present). |
| `prependPath` | `prependPath(str path)` | Insert `path` at the front of `sys.path` (highest priority). |
| `removeFromPath` | `removeFromPath(str path)` | Remove `path` from `sys.path` (no-op if absent). |

```c
global.sys.addPath("./mylibs");
```

---

## Loaded modules

| Function | Signature | Description |
|----------|-----------|-------------|
| `getModules` | `getModules()` | JSON array of all currently imported Python module names. |
| `isModuleLoaded` | `isModuleLoaded(str name)` | `true` if the named Python module is already imported. |

---

## Recursion limit

| Function | Signature | Description |
|----------|-----------|-------------|
| `getRecursionLimit` | `getRecursionLimit()` | Current recursion limit (default `1000`). |
| `setRecursionLimit` | `setRecursionLimit(int n)` | Set a new recursion limit. |

---

## Integer & memory limits

| Function | Signature | Description |
|----------|-----------|-------------|
| `getMaxSize` | `getMaxSize()` | `sys.maxsize` — largest positive integer in a C `ssize_t`. |
| `getByteOrder` | `getByteOrder()` | Native byte order: `"little"` or `"big"`. |

---

## Encoding

| Function | Signature | Description |
|----------|-----------|-------------|
| `getDefaultEncoding` | `getDefaultEncoding()` | Default string encoding, e.g. `"utf-8"`. |
| `getFilesystemEncoding` | `getFilesystemEncoding()` | Filesystem encoding for file names. |

---

## Miscellaneous

| Function | Signature | Description |
|----------|-----------|-------------|
| `isFrozen` | `isFrozen()` | `true` if running inside a PyInstaller frozen bundle. |
| `stdinName` | `stdinName()` | Name of the stdin stream, typically `"<stdin>"`. |
| `stdoutName` | `stdoutName()` | Name of the stdout stream, typically `"<stdout>"`. |
| `isatty` | `isatty()` | `true` if stdout is connected to a terminal (TTY). |

---

## Full example

```c
global setup(){ import("sys"); }

global main(){
    println("Python: " + global.sys.version());
    println("Platform: " + global.sys.platform());
    println("Executable: " + global.sys.executable());
    println("Args: " + global.sys.argv());
    println("Max size: " + strOf(global.sys.getMaxSize()));
    println("Byte order: " + global.sys.getByteOrder());
    println("Encoding: " + global.sys.getDefaultEncoding());
    println("Recursion limit: " + strOf(global.sys.getRecursionLimit()));
    println("Frozen: " + strOf(global.sys.isFrozen()));
}
```
