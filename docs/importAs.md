# importAs

`importAs` imports a module and binds it under a custom alias. This is useful when:
- you want a shorter name for a frequently-used module
- you need to import two modules whose function names could be confused
- you import the same module multiple times under different names (though idempotent — each alias loads once)

---

## Syntax

```c
importAs("moduleName", "alias");
```

Both arguments are string literals. The call must appear inside `setup()`, exactly like `import()`.

---

## Usage

```c
global setup(){
    importAs("math", "m");
    importAs("server", "srv");
    importAs("re", "regex");
}

global main(){
    any sq = global.m.sqrt(9.0);         // 3.0
    println(strOf(sq));

    global.srv.init("0.0.0.0", 8080);
    global.srv.get("/", "Hello!");
    global.srv.run();
}
```

---

## vs. `import`

| Feature | `import("mod")` | `importAs("mod", "alias")` |
|---------|-----------------|---------------------------|
| Binding name | same as module filename (without extension) | custom alias string |
| Access | `global.mod.fn()` | `global.alias.fn()` |
| `setup()` only | yes | yes |
| Idempotent | yes (by module name) | yes (by alias) |
| `.lynxc` support | yes | yes |

---

## Aliases must be valid identifiers

The alias must be a valid Lynxer identifier (letters, digits, underscores, not starting with a digit). The interpreter rejects invalid aliases at runtime.

```c
importAs("math", "my-math");   // error — hyphens not allowed in identifiers
importAs("math", "m2");        // ok
importAs("math", "_m");        // ok
```

---

## Multiple aliases for the same file

You can import the same module under two different aliases and they each get their own independent namespace object:

```c
global setup(){
    importAs("math", "mathA");
    importAs("math", "mathB");
}
```

However, note that the idempotency check is per-alias, so `mathA` and `mathB` will both be loaded (two separate module executions).

---

## stdlib modules

All standard library modules work with `importAs`:

```c
global setup(){
    importAs("json", "j");
    importAs("fileIO", "fs");
    importAs("sys", "system");
    importAs("re", "rx");
}

global main(){
    str raw = global.fs.readFile("data.json");
    bool ok = global.j.jsonValid(raw);
    str plat = global.system.platform();
    bool found = global.rx.test("[0-9]+", raw);
}
```
