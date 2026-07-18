# Module System

## Importing

`import()` loads a `.lynx` file as a module and may only be called inside `setup()`.

```c
void setup(){
    import("math");       // stdlib module
    import("mylib");      // local file: mylib.lynx
}
```

**Search order:**
1. Same directory as the running script
2. The `stdlib/` folder bundled with Lynxer

The `.lynx` extension is optional — `import("math")` and `import("math.lynx")` are equivalent.

**Idempotency:** Importing the same module twice is safe. The second call is ignored — the module is executed once and cached.

---

## Calling module functions

Use `global.<module>.<function>()`:

```c
void setup(){
    import("math");
    import("typing");
}

void main(){
    print(global.math.sqrt(144));          // 12
    print(global.typing.toStr(99));        // 99
    print(global.typing.isNumeric("3.5")); // true
}
```

---

## Accessing module globals

Constants and variables declared in a module's `setup()` are accessible via `global.<module>.<name>`:

```c
/// config.lynx ///
void setup(){
    const str HOST = "localhost";
    const int PORT  = 8080;
}
void main(){}
```

```c
void setup(){ import("config"); }
void main(){
    print(global.config.HOST); print("\n");   // localhost
    print(global.config.PORT); print("\n");   // 8080
}
```

---

## Writing your own module

Any `.lynx` file is a valid module. Declare globals in `setup()`, implement functions in between, and include a no-op `void main(){}`:

```c
/// greetlib.lynx ///
void setup(){
    const str VERSION = "1.0";
}

void sayHi(){
    print("Hi!\n");
}

void greet(str name){
    print("Hello, "); print(name); print("!\n");
}

void main(){}
```

```c
void setup(){ import("greetlib"); }

void main(){
    print(global.greetlib.VERSION); print("\n");   // 1.0
    global.greetlib.sayHi();
    global.greetlib.greet("World");
}
```

---

## Available stdlib modules

| Module | Import | Key functions |
|--------|--------|---------------|
| `math` | `import("math")` | `abs`, `max`, `min`, `clamp`, `pow`, `sqrt`, `floor`, `ceil`, `roundNum`, `pi` |
| `typing` | `import("typing")` | `toStr`, `toInt`, `toFloat`, `toBool`, `isNumeric`, `lenStr`, `repeat`, `contains`, `toList`, `isList`, `lenList`, `flatten`, `unique` |
| `fileIO` | `import("fileIO")` | `readFile`, `writeFile`, `appendFile`, `fileExists`, `deleteFile` |
| `shell` | `import("shell")` | `runShell`, `runShellCapture`, `runShellSilent` |
| `os` | `import("os")` | `getcwd`, `chdir`, `listdir`, `mkdir`, `makedirs`, `rmdir`, `remove`, `rename`, `exists`, `isFile`, `isDir`, `joinPath`, `basename`, `dirname`, `absPath`, `getenv`, `getpid`, `sep` |
| `js` | `import("js")` | `runJS`, `runJSFile`, `evalJS`, `nodeVersion`, `nodeExists` |
| `json` | `import("json")` | `jsonValid`, `jsonParse`, `jsonGet`, `jsonGetInt`, `jsonGetFloat`, `jsonGetBool`, `jsonKeys`, `jsonStringify`, `jsonArray`, `jsonObject` |

See [stdlib.md](stdlib.md) for full API details.
