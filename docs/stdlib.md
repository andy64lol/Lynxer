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
math.global.sqrt(144)
typing.global.toStr(99)
fileIO.global.readFile("data.txt")
shell.global.runShell("ls -la")
```

---

## `math`

| Function | Description |
|----------|-------------|
| `math.global.abs(n)` | Absolute value |
| `math.global.max(a, b)` | Larger of two values |
| `math.global.min(a, b)` | Smaller of two values |
| `math.global.clamp(v, lo, hi)` | Clamp `v` to `[lo, hi]` |
| `math.global.pow(base, exp)` | Exponentiation |
| `math.global.sqrt(n)` | Square root |
| `math.global.floor(n)` | Round down |
| `math.global.ceil(n)` | Round up |
| `math.global.roundNum(n)` | Round to nearest integer |
| `math.global.pi()` | π ≈ 3.14159… |

```c
void setup(){ import("math"); }

void main(){
    print(math.global.sqrt(144));      // 12
    print(math.global.clamp(20,0,10)); // 10
    print(math.global.pi());           // 3.14159...
    print("\n");
}
```

---

## `typing`

| Function | Description |
|----------|-------------|
| `typing.global.toStr(n)` | Number → string |
| `typing.global.toInt(s)` | String → int (0 on failure) |
| `typing.global.toFloat(s)` | String → float (0.0 on failure) |
| `typing.global.toBool(n)` | 0 → false, nonzero → true |
| `typing.global.isNumeric(s)` | 1 if parseable as number, else 0 |
| `typing.global.lenStr(s)` | Length of a string |
| `typing.global.repeat(s, n)` | Repeat string `n` times |
| `typing.global.contains(hay, needle)` | 1 if needle found in haystack, else 0 |

```c
void setup(){ import("typing"); }

void main(){
    print(typing.global.isNumeric("3.14")); // 1
    print(typing.global.repeat("ab", 3));   // ababab
    print(typing.global.contains("foobar","oba")); // 1
    print("\n");
}
```

---

## `fileIO`

| Function | Description |
|----------|-------------|
| `fileIO.global.readFile(path)` | Read file, return contents as `str` (empty on error) |
| `fileIO.global.writeFile(path, content)` | Write (overwrite) file, return `bool` success |
| `fileIO.global.appendFile(path, content)` | Append to file, return `bool` success |
| `fileIO.global.fileExists(path)` | Return `bool` — true if file exists |
| `fileIO.global.deleteFile(path)` | Delete file, return `bool` success |

```c
void setup(){ import("fileIO"); }

void main(){
    fileIO.global.writeFile("hello.txt", "Hello, Lynxer!\n");
    str content = fileIO.global.readFile("hello.txt");
    print(content);

    bool exists = fileIO.global.fileExists("hello.txt");
    print(exists); print("\n");   // 1

    fileIO.global.deleteFile("hello.txt");
}
```

---

## `shell`

| Function | Description |
|----------|-------------|
| `shell.global.runShell(cmd)` | Run command, return exit code (`int`) |
| `shell.global.runShellCapture(cmd)` | Run command, return stdout as `str` |
| `shell.global.runShellSilent(cmd)` | Run command silently, return exit code |

```c
void setup(){ import("shell"); }

void main(){
    int code = shell.global.runShell("echo hello");   // prints hello, returns 0

    str out = shell.global.runShellCapture("echo captured");
    print(out);   // "captured\n"

    shell.global.runShellSilent("rm -f tmp.txt");
}
```
