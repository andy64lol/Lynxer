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

| Function | Signature | Description |
|----------|-----------|-------------|
| `abs` | `(int n)` | Absolute value |
| `max` | `(int a, int b)` | Larger of two values |
| `min` | `(int a, int b)` | Smaller of two values |
| `clamp` | `(int v, int lo, int hi)` | Clamp `v` to `[lo, hi]` |
| `pow` | `(int base, int exp)` | Integer exponentiation |
| `sqrt` | `(float n)` | Square root |
| `floor` | `(float n)` | Round down to nearest integer |
| `ceil` | `(float n)` | Round up to nearest integer |
| `round` | `(float n)` | Round to nearest integer |
| `roundNum` | `(float n)` | Alias for `round` (legacy) |
| `truncate` | `(float n)` | Truncate toward zero (removes fractional part) |
| `pi` | `()` | π ≈ 3.14159… |
| `e` | `()` | Euler's number ≈ 2.71828… |
| `log` | `(float n)` | Natural logarithm; `0.0` if `n ≤ 0` |
| `log2` | `(float n)` | Base-2 logarithm; `0.0` if `n ≤ 0` |
| `log10` | `(float n)` | Base-10 logarithm; `0.0` if `n ≤ 0` |
| `sin` | `(float n)` | Sine of `n` (radians) |
| `cos` | `(float n)` | Cosine of `n` (radians) |
| `tan` | `(float n)` | Tangent of `n` (radians) |
| `degrees` | `(float n)` | Convert radians to degrees |
| `radians` | `(float n)` | Convert degrees to radians |
| `sign` | `(float n)` | Returns `-1`, `0`, or `1` |
| `isEven` | `(int n)` | `true` if `n` is even |
| `isOdd` | `(int n)` | `true` if `n` is odd |
| `factorial` | `(int n)` | `n!`; returns `1` for `n ≤ 0` |
| `gcd` | `(int a, int b)` | Greatest common divisor |
| `hypot` | `(float a, float b)` | Hypotenuse: `sqrt(a² + b²)` |
| `randInt` | `(int lo, int hi)` | Random integer in `[lo, hi]` (inclusive) |
| `randFloat` | `(float lo, float hi)` | Random float in `[lo, hi)` |

```c
void setup(){ import("math"); }

void main(){
    print(global.math.sqrt(144));            // 12.0
    print(global.math.clamp(20, 0, 10));     // 10
    print(global.math.pi());                 // 3.141592653589793
    print(global.math.e());                  // 2.718281828459045
    print(global.math.log10(1000.0));        // 3.0
    print(global.math.sin(0.0));             // 0.0
    print(global.math.degrees(global.math.pi())); // 180.0
    print(global.math.factorial(5));         // 120
    print(global.math.gcd(12, 8));           // 4
    print(global.math.hypot(3.0, 4.0));      // 5.0
    print(global.math.sign(-7));             // -1
    print(global.math.isEven(4));            // true
    print(global.math.randInt(1, 6));        // random 1–6
    print("\n");
}
```

---

## `typing`

| Function | Signature | Description |
|----------|-----------|-------------|
| `toStr` | `(int n)` | Any value → `str` |
| `toInt` | `(str s)` | Parse as `int`; returns `0` on failure |
| `toFloat` | `(str s)` | Parse as `float`; returns `0.0` on failure |
| `toBool` | `(int n)` | `0` → `false`, nonzero → `true` |
| `isNumeric` | `(str s)` | `true` if parseable as a number |
| `lenStr` | `(str s)` | Character length of a string |
| `repeat` | `(str s, int n)` | Repeat string `n` times |
| `contains` | `(str hay, str needle)` | `true` if `needle` found in `hay` |
| `toList` | `(str s, str sep)` | Split string by `sep` → `list` |
| `isList` | `(any val)` | `true` if `val` is a list |
| `lenList` | `(any lst)` | Number of elements in a list |
| `flatten` | `(any lst)` | Flatten one level of nested lists |
| `unique` | `(any lst)` | Remove duplicates (order preserved) |
| `trim` | `(str s)` | Strip leading and trailing whitespace |
| `upper` | `(str s)` | Convert to uppercase |
| `lower` | `(str s)` | Convert to lowercase |
| `startsWith` | `(str s, str prefix)` | `true` if `s` starts with `prefix` |
| `endsWith` | `(str s, str suffix)` | `true` if `s` ends with `suffix` |
| `replace` | `(str s, str old, str new)` | Replace all occurrences of `old` with `new` |
| `indexOf` | `(str s, str sub)` | Index of first `sub` in `s`; `-1` if not found |
| `substr` | `(str s, int start, int end)` | Slice `s[start:end]` (Python-style; negatives ok) |
| `padLeft` | `(str s, int width, str ch)` | Left-pad `s` with `ch` to at least `width` chars |
| `padRight` | `(str s, int width, str ch)` | Right-pad `s` with `ch` to at least `width` chars |
| `strReverse` | `(str s)` | Reverse `s` character by character |

> **`toInt` / `toFloat` failure:** Both return `0` / `0.0` on failure, which is indistinguishable from a valid `"0"` parse. Use `isNumeric(s)` first when `0` could be a legitimate result.

```c
void setup(){ import("typing"); }

void main(){
    print(global.typing.trim("  hi  "));           // "hi"
    print(global.typing.upper("hello"));            // "HELLO"
    print(global.typing.lower("HELLO"));            // "hello"
    print(global.typing.startsWith("Lynxer","Lyn")); // true
    print(global.typing.replace("foo bar","bar","baz")); // "foo baz"
    print(global.typing.indexOf("foobar","bar"));   // 3
    print(global.typing.substr("Lynxer", 0, 3));    // "Lyn"
    print(global.typing.padLeft("5", 3, "0"));      // "005"
    print(global.typing.strReverse("abc"));         // "cba"
    print("\n");
}
```

---

## `fileIO`

| Function | Signature | Description |
|----------|-----------|-------------|
| `readFile` | `(str path)` | Read entire file; returns `""` on error |
| `writeFile` | `(str path, str content)` | Overwrite file; returns `bool` (`true` on success) |
| `appendFile` | `(str path, str content)` | Append to file; returns `bool` |
| `fileExists` | `(str path)` | `true` if file exists |
| `deleteFile` | `(str path)` | Delete file; returns `bool` (`false` if missing) |
| `readLines` | `(str path)` | Read file as a `list` of lines; empty list on error |
| `copyFile` | `(str src, str dst)` | Copy `src` to `dst`; returns `bool` |
| `fileSize` | `(str path)` | File size in bytes; `-1` on error |

```c
void setup(){ import("fileIO"); }

void main(){
    global.fileIO.writeFile("data.txt", "line1\nline2\nline3");
    any lines = global.fileIO.readLines("data.txt");
    int n = returnLength(lines);               // 3
    str first = listGet(lines, 0);             // "line1"

    int sz = global.fileIO.fileSize("data.txt"); // bytes
    global.fileIO.copyFile("data.txt", "backup.txt");
    global.fileIO.deleteFile("data.txt");
    global.fileIO.deleteFile("backup.txt");
    print(n); print("\n");
}
```

---

## `shell`

| Function | Signature | Description |
|----------|-----------|-------------|
| `runShell` | `(str cmd)` | Run command (inherits stdio); returns exit code |
| `runShellCapture` | `(str cmd)` | Run command, capture stdout; returns `str` |
| `runShellSilent` | `(str cmd)` | Run command, suppress all output; returns exit code |
| `runShellErr` | `(str cmd)` | Run command, capture stderr only; returns `str` |
| `runShellCode` | `(str cmd)` | Run command silently; returns exit code |
| `commandExists` | `(str cmd)` | `true` if `cmd` is found on `PATH` |

```c
void setup(){ import("shell"); }

void main(){
    int code = global.shell.runShell("echo Hello");
    str out  = global.shell.runShellCapture("date");
    str err  = global.shell.runShellErr("ls /no_such_path");
    bool hasPy = global.shell.commandExists("python3");
    print(out); print(hasPy); print("\n");
}
```

---

## `os`

| Function | Signature | Description |
|----------|-----------|-------------|
| `getcwd` | `()` | Current working directory |
| `chdir` | `(str path)` | Change directory; returns `bool` |
| `listdir` | `(str path)` | Directory entries as a `list`; empty on error |
| `mkdir` | `(str path)` | Create directory; returns `bool` |
| `makedirs` | `(str path)` | Create directory tree (like `mkdir -p`); returns `bool` |
| `rmdir` | `(str path)` | Remove empty directory; returns `bool` |
| `remove` | `(str path)` | Remove a file; returns `bool` |
| `rename` | `(str src, str dst)` | Rename/move; returns `bool` |
| `exists` | `(str path)` | `true` if path exists (file or directory) |
| `isFile` | `(str path)` | `true` if path is a regular file |
| `isDir` | `(str path)` | `true` if path is a directory |
| `joinPath` | `(str a, str b)` | Join two path components |
| `basename` | `(str path)` | Last component of path |
| `dirname` | `(str path)` | Directory portion of path |
| `absPath` | `(str path)` | Absolute path |
| `normPath` | `(str path)` | Normalize path (resolve `..`, double slashes) |
| `expandUser` | `(str path)` | Expand leading `~` to home directory |
| `extname` | `(str path)` | File extension including dot (e.g. `".txt"`); `""` if none |
| `getenv` | `(str key)` | Environment variable value; `""` if not set |
| `setenv` | `(str key, str val)` | Set environment variable; returns `bool` |
| `getpid` | `()` | Current process ID |
| `sep` | `()` | OS path separator (`"/"` on Unix, `"\\"` on Windows) |

```c
void setup(){ import("os"); }

void main(){
    str cwd = global.os.getcwd();
    str ext = global.os.extname("program.lynx");   // ".lynx"
    str home = global.os.expandUser("~");
    bool ok = global.os.setenv("MY_VAR", "hello");
    str val = global.os.getenv("MY_VAR");           // "hello"
    print(cwd); print("\n");
    print(ext); print("\n");
    print(home); print("\n");
}
```

---

## `js`

Runs JavaScript via a Node.js subprocess. Requires `node` on `PATH`.

| Function | Signature | Description |
|----------|-----------|-------------|
| `nodeExists` | `()` | `1` if Node.js is on `PATH`, `0` otherwise |
| `nodeVersion` | `()` | Node.js version string (e.g. `"v20.11.0"`); `""` if not found |
| `runJS` | `(str code)` | Run JS code string; returns stdout, or error message on failure |
| `runJSFile` | `(str path)` | Run a `.js` file; returns stdout, or error message on failure |
| `evalJS` | `(str expr)` | Evaluate a JS expression; wraps in `console.log()` and returns the result |

```c
void setup(){ import("js"); }

void main(){
    int ok = global.js.nodeExists();
    if(ok){
        str ver = global.js.nodeVersion();
        println(ver);                                    // e.g. v24.13.0

        str arith = global.js.evalJS("6 * 7");
        println(arith);                                  // 42

        str out = global.js.runJS("console.log(\"Hello from JS!\");");
        print(out);                                      // Hello from JS!

        // multi-line script
        str script = "const greet = n => `Hi, ${n}!`; console.log(greet(\"Lynxer\"));";
        print(global.js.runJS(script));                  // Hi, Lynxer!
    }
}
```

---

## `json`

JSON encode / decode via Python's `json` module.

| Function | Signature | Description |
|----------|-----------|-------------|
| `jsonValid` | `(str s)` | `true` if `s` is valid JSON |
| `jsonParse` | `(str s)` | Pretty-print JSON (2-space indent); `""` on error |
| `jsonGet` | `(str s, str key)` | String value at `key`; `""` if missing |
| `jsonGetInt` | `(str s, str key)` | Integer at `key`; `0` on error |
| `jsonGetFloat` | `(str s, str key)` | Float at `key`; `0.0` on error |
| `jsonGetBool` | `(str s, str key)` | Bool at `key`; `false` on error |
| `jsonKeys` | `(str s)` | Comma-separated top-level keys of a JSON object |
| `jsonStringify` | `(str s)` | JSON-encode a Lynxer string (adds quotes and escaping) |
| `jsonArray` | `(any lst)` | Serialize a Lynxer list to a JSON array string |
| `jsonObject` | `(any lst)` | Build a JSON object from a flat alternating key/value list |
| `jsonHas` | `(str s, str key)` | `true` if `key` exists in the JSON object |
| `jsonLength` | `(str s)` | Number of keys (object) or elements (array); `0` on error |
| `jsonSet` | `(str s, str key, str val)` | Set `key` to JSON-encoded `val`; returns new JSON string |
| `jsonDelete` | `(str s, str key)` | Remove `key`; returns new JSON string |
| `jsonMerge` | `(str a, str b)` | Merge two JSON objects; keys from `b` win on conflict |

> **`jsonObject` list requirement:** The list must have an even number of elements (alternating key, value). An odd-length list returns `"{}"`.

> **`jsonSet` value encoding:** Pass the value already JSON-encoded if you want a non-string type, e.g. `"42"` for a number or `"true"` for a boolean. If the value is not valid JSON it is stored as a raw string.

```c
void setup(){ import("json"); }

void main(){
    str data = "{\"name\":\"Lynxer\",\"version\":1}";

    print(global.json.jsonValid(data));               // true
    print(global.json.jsonGet(data, "name"));         // Lynxer
    print(global.json.jsonGetInt(data, "version"));   // 1
    print(global.json.jsonHas(data, "name"));         // true
    print(global.json.jsonLength(data));              // 2

    str updated = global.json.jsonSet(data, "version", "2");
    print(global.json.jsonGetInt(updated, "version")); // 2

    str deleted = global.json.jsonDelete(data, "name");
    print(global.json.jsonHas(deleted, "name"));      // false

    str a = "{\"x\":1}";
    str b = "{\"x\":99,\"y\":2}";
    str merged = global.json.jsonMerge(a, b);
    print(global.json.jsonGetInt(merged, "x"));       // 99
    print(global.json.jsonGetInt(merged, "y"));       // 2
    print("\n");
}
```
