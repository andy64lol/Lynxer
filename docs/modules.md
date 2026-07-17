# Module System

## Importing

`import()` may only be called inside `setup()`.

```c
void setup(){
    import("math");       // stdlib module
    import("mylib");      // local file: mylib.lynx
}
```

The runtime looks for the file in this order:

1. Same directory as the running script
2. The `stdlib/` folder bundled with Lynxer

The `.lynx` extension is optional — `import("math")` and `import("math.lynx")` are equivalent.

---

## Calling module functions

After importing, access functions via `<module>.global.<function>`:

```c
math.global.sqrt(144)
typing.global.toStr(99)
mylib.global.myFunction()
```

---

## Writing a module

A module is a plain `.lynx` file with `void setup(){}` and one or more `void` functions.  
`void main(){}` is optional (ignored when imported).

```c
/// greetlib.lynx ///

void setup(){}

void sayHi(){
    print("Hi from greetlib!\n");
}

void greet(str name){
    print("Hello, "); print(name); print("!\n");
}
```

Use it:

```c
void setup(){
    import("greetlib");
    const str GREETLIB_VERSION = "1.0";
}

void main(){
    greetlib.global.sayHi();
    greetlib.global.greet("World");
}
```

---

## Global variables from modules

Global variables declared in a module's `setup()` are accessible via the module namespace:

```c
/// config.lynx ///
void setup(){
    const str API_URL = "https://example.com";
    const int TIMEOUT = 30;
}
void main(){}
```

```c
void setup(){
    import("config");
}
void main(){
    // config globals are in config.global scope
    print(config.global.API_URL); print("\n");
}
```

---

## Available stdlib modules

| Module | Import | Contents |
|--------|--------|----------|
| `math` | `import("math")` | `abs`, `max`, `min`, `clamp`, `pow`, `sqrt`, `floor`, `ceil`, `roundNum`, `pi` |
| `typing` | `import("typing")` | `toStr`, `toInt`, `toFloat`, `toBool`, `isNumeric`, `lenStr`, `repeat`, `contains` |
| `fileIO` | `import("fileIO")` | `readFile`, `writeFile`, `appendFile`, `fileExists`, `deleteFile` |
| `shell` | `import("shell")` | `runShell`, `runShellCapture`, `runShellSilent` |

See [stdlib.md](stdlib.md) for full API details.
