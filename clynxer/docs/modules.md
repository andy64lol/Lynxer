# Modules

## Importing

`import` and `importAs` may only run inside `setup()`.

```c
global setup(){
    import("math");
    import("network");
    importAs("json", "j");
    import("mylib.so");          // native shared library
}
```

Search order:

1. Directory of the running program (or bundled payload for compiled apps)
2. `clynxer/stdlib/`

`import("math")` and `import("math.lynx")` are equivalent. Native modules use
the `.so` suffix (`import("math.so")` or via the `.lynx` wrapper that loads it).

Imports are idempotent: a second load of the same module is a no-op.

There is **no** `.lynxc` bytecode import in Clynxer.

## Calling module members

```c
global main(){
    println(global.math.max(2, 5));
    println(global.j.jsonGet("{\"a\": 1}", "a"));
}
```

Bare names resolve inside the module file itself; from the importing program,
use `global.<module>.<name>(...)`.

## Writing a module

Any `.lynx` file with `setup` / helpers / `main` is a module. Prefer a leading
`////` docstring so `--list-stdlibs` can describe it.

```c
////
Small helper module.
////
global setup(){}

global twice(int n) -> int {
    return n * 2;
}

global main(){}
```

Native backends export `lynxer_module_init_v1` — see
[native-module-abi.md](native-module-abi.md).

## Compiled programs

`--compile` embeds every transitively imported `.lynx` source and `.so`
library. Extra inputs and `--include` files are embedded too. See
[CLI.md](CLI.md).
