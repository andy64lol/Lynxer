# Modules

`import` and `importAs` load a module and bind its members under a namespace.

## Importing

Imports are **statements**, so they run from a function body — `setup()` is the
conventional place, but any function works:

```lynx
global setup(){
    import("math");
    importAs("json", "j");
}

global main(){}
```

A native shared library is named with its `.so` suffix:

```lynx
import("mylib.so");          // loads ./mylib.so or an stdlib library
```

A bare `import("math");` at file scope is a syntax error
(`expected top-level function declaration`).

Both arguments must be **string literals**; an expression such as
`importAs(path, "m")` is rejected with `expected module path string`. See
[importAs.md](importAs.md) for the alias rules.

## Search order

1. The directory of the running program (for a compiled executable, the modules
   embedded in its payload).
2. `lynxer/stdlib/`, next to the interpreter.

- `import("math")` and `import("math.lynx")` are equivalent.
- A native library is named with its `.so` suffix; the stdlib `.lynx` wrappers
  load their own `.so` (for example `math.lynx` imports `math.so` as
  `nativeMath`).
- Imports are idempotent: importing the same module twice is a no-op.
- A module that cannot be found reports
  `module 'nope' was not found` with the importing source location.

There is **no** `.lynxc` bytecode import in Lynxer.

## Calling module members

Members are reached through the module namespace: `global.<module>.<name>(...)`
(or `global.<alias>.<name>(...)` after `importAs`).

```lynx
global setup(){
    import("math");
    importAs("json", "j");
}

global main(){
    println(global.math.max(2, 5));                    // 5
    println(global.j.jsonGet("{\"a\": 1}", "a"));      // 1
}
```

Inside the module file itself, call its own functions by **bare name** — using
`global.<name>(...)` inside a module resolves to a core builtin, not the
module's own function (see [limitations.md](limitations.md)). A file-level
`func` in a module is reached from the importer as `global.<module>.<name>`
just like a `global` function.

## Writing a module

Any `.lynx` file with `setup` and helpers is a module. Open it with a `////`
docstring line so `--list-stdlibs` can describe it:

```lynx
////
Small helper module.
////
global setup(){}

global twice(int n) -> int {
    return n * 2;
}

global main(){}
```

A native backend exports `lynxer_module_init_v1`; see
[native-module-abi.md](native-module-abi.md).

## Compiled programs

`--compile` embeds every transitively imported `.lynx` source and `.so` library
into the executable, so a compiled program needs nothing from the build tree.
Positional extra inputs and `--include` files are embedded too. See
[CLI.md](CLI.md).

## See also

- [importAs.md](importAs.md)
- [native-module-abi.md](native-module-abi.md)
- [builtins.md](builtins.md) — the builtins available without an import.
