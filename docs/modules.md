# Module System

> See also: [importAs](importAs.md) for importing a module under a custom alias.

## Importing

`import()` loads a `.lynx` or `.lynxc` file as a module and may only be called inside `setup()`.

```c
global setup(){
    import("math");           // stdlib module
    import("mylib");          // local file: mylib.lynx (or mylib.lynxc if present)
    import("mylib.lynxc");    // explicit bytecode import
}
```

**Search order:**
1. Same directory as the running script — checks for a compiled `.lynxc` first, then `.lynx`
2. The `stdlib/` folder bundled with Lynxer

The `.lynx` extension is optional — `import("math")` and `import("math.lynx")` are equivalent.  
You may also pass `.lynxc` explicitly: `import("mylib.lynxc")`.

**Bytecode auto-detection:** when you call `import("name")` without an extension, Lynxer looks for `name.lynxc` in the same directory first.  If found, the bytecode is loaded instead of the source.  This lets you distribute compiled modules alongside (or in place of) source files transparently.

**Idempotency:** Importing the same module twice is safe. The second call is ignored — the module is executed once and cached.

---

## Calling module functions

Use `global.<module>.<function>()`:

```c
global setup(){
    import("math");
    import("typing");
}

global main(){
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
global setup(){
    const str HOST = "localhost";
    const int PORT  = 8080;
}
global main(){}
```

```c
global setup(){ import("config"); }
global main(){
    print(global.config.HOST); print("\n");   // localhost
    print(global.config.PORT); print("\n");   // 8080
}
```

---

## Writing your own module

Any `.lynx` file is a valid module. Declare globals in `setup()`, implement functions in between, and include a no-op `global main(){}`:

```c
/// greetlib.lynx ///
global setup(){
    const str VERSION = "1.0";
}

global sayHi(){
    print("Hi!\n");
}

global greet(str name){
    print("Hello, "); print(name); print("!\n");
}

global main(){}
```

```c
global setup(){ import("greetlib"); }

global main(){
    print(global.greetlib.VERSION); print("\n");   // 1.0
    global.greetlib.sayHi();
    global.greetlib.greet("World");
}
```

---

## Available stdlib modules

| Module | Import | What it provides |
|--------|--------|-----------------|
| `math` | `import("math")` | Arithmetic, trig, rounding, random numbers |
| `typing` | `import("typing")` | Type conversion, string manipulation, list utilities |
| `fileIO` | `import("fileIO")` | Read, write, append, copy, move, delete files |
| `shell` | `import("shell")` | Run external shell commands, capture output |
| `os` | `import("os")` | Directory navigation, path utilities, environment vars |
| `json` | `import("json")` | JSON encode / decode / query |
| `js` | `import("js")` | Run JavaScript via Node.js |

See [stdlib.md](stdlib.md) for the full function reference.
