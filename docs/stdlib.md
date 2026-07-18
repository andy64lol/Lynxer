# Standard Library

Import modules inside `setup()`:

```c
void setup(){
    import("math");
    import("typing");
    import("fileIO");
    import("shell");
}
```

Call functions via the module namespace:

```c
global.math.sqrt(144)
global.typing.toStr(99)
global.fileIO.readFile("data.txt")
global.shell.runShell("ls -la")
```

---

## `math`

| Function | Description |
|----------|-------------|
| `global.math.abs(n)` | Absolute value |
| `global.math.max(a, b)` | Larger of two values |
| `global.math.min(a, b)` | Smaller of two values |
| `global.math.clamp(v, lo, hi)` | Clamp `v` to `[lo, hi]` |
| `global.math.pow(base, exp)` | Exponentiation |
| `global.math.sqrt(n)` | Square root |
| `global.math.floor(n)` | Round down |
| `global.math.ceil(n)` | Round up |
| `global.math.roundNum(n)` | Round to nearest integer |
| `global.math.pi()` | π ≈ 3.14159… |

```c
void setup(){ import("math"); }

void main(){
    print(global.math.sqrt(144));      // 12
    print(global.math.clamp(20,0,10)); // 10
    print(global.math.pi());           // 3.14159...
    print("\n");
}
```

---

## `typing`

| Function | Description |
|----------|-------------|
| `global.typing.toStr(n)` | Number → string |
| `global.typing.toInt(s)` | String → int (0 on failure) |
| `global.typing.toFloat(s)` | String → float (0.0 on failure) |
| `global.typing.toBool(n)` | 0 → false, nonzero → true |
| `global.typing.isNumeric(s)` | 1 if parseable as number, else 0 |
| `global.typing.lenStr(s)` | Length of a string |
| `global.typing.repeat(s, n)` | Repeat string `n` times |
| `global.typing.contains(hay, needle)` | 1 if needle found in haystack, else 0 |

```c
void setup(){ import("typing"); }

void main(){
    print(global.typing.isNumeric("3.14")); // 1
    print(global.typing.repeat("ab", 3));   // ababab
    print(global.typing.contains("foobar","oba")); // 1
    print("\n");
}
```

---

## `fileIO`

| Function | Description |
|----------|-------------|
| `global.fileIO.readFile(path)` | Read file, return contents as `str` (empty on error) |
| `global.fileIO.writeFile(path, content)` | Write (overwrite) file, return `bool` success |
| `global.fileIO.appendFile(path, content)` | Append to file, return `bool` success |
| `global.fileIO.fileExists(path)` | Return `bool` — true if file exists |
| `global.fileIO.deleteFile(path)` | Delete file, return `bool` success |

```c
void setup(){ import("fileIO"); }

void main(){
    global.fileIO.writeFile("hello.txt", "Hello, Lynxer!\n");
    str content = global.fileIO.readFile("hello.txt");
    print(content);

    bool exists = global.fileIO.fileExists("hello.txt");
    print(exists); print("\n");   // 1

    global.fileIO.deleteFile("hello.txt");
}
```

---

## `shell`

| Function | Description |
|----------|-------------|
| `global.shell.runShell(cmd)` | Run command, return exit code (`int`) |
| `global.shell.runShellCapture(cmd)` | Run command, return stdout as `str` |
| `global.shell.runShellSilent(cmd)` | Run command silently, return exit code |

```c
void setup(){ import("shell"); }

void main(){
    int code = global.shell.runShell("echo hello");   // prints hello, returns 0

    str out = global.shell.runShellCapture("echo captured");
    print(out);   // "captured\n"

    global.shell.runShellSilent("rm -f tmp.txt");
}
```
