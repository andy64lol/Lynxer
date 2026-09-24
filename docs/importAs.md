# importAs

`importAs("module", "alias")` loads a module exactly like `import`, but binds it
under a name you choose instead of the module's own name.

## Syntax

```lynx
global setup(){
    importAs("math", "m");
    importAs("network", "net");
    importAs("json.so", "nativeJson");   // a native library under an alias
}

global main(){
    println(global.m.sqrt(9.0));         // 3
}
```

## Rules

- Both arguments are **string literals**. An expression is rejected with
  `expected module path string`.
- The alias is used as `global.<alias>`, so `importAs("math", "m")` is reached
  as `global.m.<name>(...)`.
- Like `import`, it is a statement and may appear in any function body, not only
  in `setup()`.
- Stdlib `.lynx` wrappers use `importAs("name.so", "nativeName")` internally,
  then expose typed `global f(...) -> T { ... }` forwards; that is how a module
  wraps a native backend.

## See also

- [modules.md](modules.md) — the search order, resolution, and compiled-
  executable behaviour.
