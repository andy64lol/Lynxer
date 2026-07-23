# Module System

## Importing

`import()` loads a `.lynx` file as a module and may only be called inside `setup()`.

```c
global setup(){
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

Too lazy to write all new modules

See [stdlib list](stdlib/README.md) for full stdlib details.
