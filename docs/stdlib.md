# Standard Library

Import modules inside `setup()`:

```c
void setup(){
    import("math");
    import("typing");
    import("fileIO");
    import("shell");
    import("os");
    import("js");
    import("json");
}
```

Call functions via `global.<module>.<function>()`:

```c
global.math.sqrt(144)
global.typing.toStr(99)
global.fileIO.readFile("data.txt")
global.shell.runShell("ls -la")
global.os.getcwd()
global.js.evalJS("1 + 1")
global.json.jsonGet(data, "key")
```

---

## `math`

| Function | Description |
|----------|-------------|
| `global.math.abs(n)` | Absolute value |
| `global.math.max(a, b)` | Larger of two values |
| `global.math.min(a, b)` | Smaller of two values |
| `global.math.clamp(v, lo, hi)` | Clamp `v` to `[lo, hi]` |
| `global.math.pow(base, exp)` | Exponentiation (integer) |
| `global.math.sqrt(n)` | Square root |
| `global.math.floor(n)` | Round down to nearest integer |
| `global.math.ceil(n)` | Round up to nearest integer |
| `global.math.roundNum(n)` | Round to nearest integer |
| `global.math.pi()` | π ≈ 3.14159… |

```c
void setup(){ import("math"); }

void main(){
    print(global.math.sqrt(144));       // 12
    print(global.math.clamp(20, 0, 10)); // 10
    print(global.math.pi());            // 3.141592653589793
    print("\n");
}
```

---

## `typing`

| Function | Description |
|----------|-------------|
| `global.typing.toStr(n)` | Any value → `str` |
| `global.typing.toInt(s)` | Parse as `int`; returns `0` on failure — validate first with `isNumeric` |
| `global.typing.toFloat(s)` | Parse as `float`; returns `0.0` on failure — validate first with `isNumeric` |
| `global.typing.toBool(n)` | `0` → `false`, nonzero → `true` |
| `global.typing.isNumeric(s)` | `true` if parseable as a number |
| `global.typing.lenStr(s)` | Character length of a string |
| `global.typing.repeat(s, n)` | Repeat string `n` times |
| `global.typing.contains(hay, needle)` | `true` if `needle` found in `hay` |
| `global.typing.toList(s, sep)` | Split string by `sep` → `list` |
| `global.typing.isList(val)` | `true` if `val` is a list |
| `global.typing.lenList(lst)` | Number of elements in a list |
| `global.typing.flatten(lst)` | Flatten one level of nested lists |
| `global.typing.unique(lst)` | Remove duplicates, preserving order |

> **`toInt` and `toFloat` failure:** Both return `0` / `0.0` on failure, which is indistinguishable from successfully parsing the string `"0"`. Use `global.typing.isNumeric(s)` first when the value might legitimately be zero.
>
> ```c
> str raw = input("Enter a number: ");
> if(global.typing.isNumeric(raw)){
>     int n = global.typing.toInt(raw);
>     // safe to use n
> } else {
>     print("Not a number\n");
> }
> ```

```c
void setup(){ import("typing"); }

void main(){
    print(global.typing.isNumeric("3.14")); print("\n"); // true
    print(global.typing.repeat("ab", 3));   print("\n"); // ababab
    print(global.typing.contains("foobar", "oba")); print("\n"); // true

    any parts = global.typing.toList("a,b,c", ",");
    print(strOf(parts)); print("\n");  // [a, b, c]

    any u = global.typing.unique(seqFromTo(1, 4, 1));
    print(strOf(u)); print("\n");      // [1, 2, 3]
}
```

---

## `fileIO`

| Function | Description |
|----------|-------------|
| `global.fileIO.readFile(path)` | Read file; returns `str` (empty string on error) |
| `global.fileIO.writeFile(path, content)` | Overwrite file; returns `bool` (`true` on success) |
| `global.fileIO.appendFile(path, content)` | Append to file; returns `bool` (`true` on success) |
| `global.fileIO.fileExists(path)` | `true` if file exists |
| `global.fileIO.deleteFile(path)` | Delete file; returns `bool` (`true` on success) |

```c
void setup(){ import("fileIO"); }

void main(){
    bool ok = global.fileIO.writeFile("hello.txt", "Hello, Lynxer!\n");
    print(ok); print("\n");   // true

    str content = global.fileIO.readFile("hello.txt");
    print(content);

    print(global.fileIO.fileExists("hello.txt")); print("\n");  // true
    global.fileIO.deleteFile("hello.txt");
}
```

---

## `shell`

| Function | Description |
|----------|-------------|
| `global.shell.runShell(cmd)` | Run command; return exit code (`int`) |
| `global.shell.runShellCapture(cmd)` | Run command; return stdout as `str` |
| `global.shell.runShellSilent(cmd)` | Run command silently; return exit code (`int`) |

```c
void setup(){ import("shell"); }

void main(){
    int code = global.shell.runShell("echo hello");   // prints "hello", returns 0

    str out = global.shell.runShellCapture("echo captured");
    print(out);   // "captured\n"

    global.shell.runShellSilent("rm -f tmp.txt");
}
```

---

## `os`

Port of Python's `os` and `os.path` modules.

| Function | Description |
|----------|-------------|
| `global.os.getcwd()` | Current working directory as `str` |
| `global.os.chdir(path)` | Change directory; `true` on success |
| `global.os.listdir(path)` | Directory entries as a `list` of strings; empty list on error |
| `global.os.mkdir(path)` | Create directory; `true` on success |
| `global.os.makedirs(path)` | Create directory tree (`mkdir -p`); `true` on success |
| `global.os.rmdir(path)` | Remove empty directory; `true` on success |
| `global.os.remove(path)` | Remove a file; `true` on success |
| `global.os.rename(src, dst)` | Rename/move; `true` on success |
| `global.os.exists(path)` | `true` if path exists (file or dir) |
| `global.os.isFile(path)` | `true` if path is a regular file |
| `global.os.isDir(path)` | `true` if path is a directory |
| `global.os.joinPath(a, b)` | Join two path components |
| `global.os.basename(path)` | Last path component |
| `global.os.dirname(path)` | Directory portion of path |
| `global.os.absPath(path)` | Absolute path |
| `global.os.getenv(key)` | Environment variable value, or `""` if unset |
| `global.os.getpid()` | Current process ID (`int`) |
| `global.os.sep()` | OS path separator (`"/"` on Unix) |

```c
void setup(){ import("os"); }

void main(){
    str cwd = global.os.getcwd();
    print(cwd); print("\n");

    // listdir returns a list of strings
    any entries = global.os.listdir(".");
    int n = returnLength(entries);
    print("files: "); print(n); print("\n");

    str joined = global.os.joinPath("/tmp", "demo.txt");
    print(joined); print("\n");   // /tmp/demo.txt

    print(global.os.exists("/tmp")); print("\n");  // true
    print(global.os.getenv("HOME")); print("\n");
}
```

---

## `js`

Runs JavaScript via a Node.js subprocess. Requires `node` on `PATH`.

| Function | Description |
|----------|-------------|
| `global.js.runJS(code)` | Run JS code string; return stdout as `str` |
| `global.js.runJSFile(path)` | Run a `.js` file; return stdout as `str` |
| `global.js.evalJS(expr)` | Evaluate a JS expression; return result as `str` |
| `global.js.nodeVersion()` | Node.js version string, e.g. `"v20.11.0"` |
| `global.js.nodeExists()` | `1` if `node` is on `PATH`, else `0` |

```c
void setup(){ import("js"); }

void main(){
    if(global.js.nodeExists() not is 1){
        print("node.js not found\n");
        return;
    }

    print(global.js.nodeVersion()); print("\n");   // v20.x.x

    str result = global.js.evalJS("2 ** 10");
    print(result); print("\n");   // 1024

    str out = global.js.runJS("
        const greet = name => `Hello, ${name}!`;
        console.log(greet('Lynxer'));
    ");
    print(out);   // Hello, Lynxer!
}
```

---

## `json`

JSON encode / decode via Python's `json` module.

| Function | Description |
|----------|-------------|
| `global.json.jsonValid(s)` | `true` if `s` is valid JSON |
| `global.json.jsonParse(s)` | Pretty-print JSON (2-space indent); `""` on error |
| `global.json.jsonGet(s, key)` | String value at `key` in JSON object; `""` if missing |
| `global.json.jsonGetInt(s, key)` | Integer at `key`; `0` on error |
| `global.json.jsonGetFloat(s, key)` | Float at `key`; `0.0` on error |
| `global.json.jsonGetBool(s, key)` | Bool at `key`; `false` on error |
| `global.json.jsonKeys(s)` | Comma-separated top-level keys of a JSON object |
| `global.json.jsonStringify(s)` | JSON-encode a Lynxer string (adds quotes and escaping) |
| `global.json.jsonArray(lst)` | Serialize a Lynxer list to a JSON array string |
| `global.json.jsonObject(lst)` | Build a JSON object from a flat key/value list (must be even-length; odd-length returns `"{}"`) |

```c
void setup(){ import("json"); }

void main(){
    str data = "{\"name\":\"Lynxer\",\"version\":1}";

    print(global.json.jsonValid(data));          print("\n"); // true
    print(global.json.jsonGet(data, "name"));    print("\n"); // Lynxer
    print(global.json.jsonGetInt(data, "version")); print("\n"); // 1
    print(global.json.jsonKeys(data));           print("\n"); // name,version

    // Build a JSON array
    any lst = seqFromTo(0, 0, 1);
    lst = listPush(lst, "a"); lst = listPush(lst, "b"); lst = listPush(lst, "c");
    print(global.json.jsonArray(lst)); print("\n");  // ["a", "b", "c"]

    // Build a JSON object from flat key/value pairs (must be even-length)
    any kv = seqFromTo(0, 0, 1);
    kv = listPush(kv, "lang");  kv = listPush(kv, "Lynxer");
    kv = listPush(kv, "year");  kv = listPush(kv, "2024");
    print(global.json.jsonObject(kv)); print("\n");
    // {"lang": "Lynxer", "year": "2024"}
}
```
