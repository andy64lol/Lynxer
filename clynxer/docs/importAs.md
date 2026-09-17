# importAs

`importAs("module", "alias")` loads a module and binds it under a custom name.
It must appear inside `setup()`, like `import()`.

```c
global setup(){
    importAs("math", "m");
    importAs("network", "net");
    importAs("json.so", "nativeJson");   // native library under an alias
}

global main(){
    println(global.m.sqrt(9.0));
    println(global.net.urlencode("a b"));
}
```

Rules:

- Both arguments are string expressions (usually literals).
- The alias becomes `global.<alias>`.
- Stdlib `.lynx` wrappers often use `importAs("name.so", "nativeName")`
  internally, then expose typed `global f(...) -> T { ... }` forwards.

See also [modules.md](modules.md).
