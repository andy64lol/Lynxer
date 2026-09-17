# Clynxer language reference

Clynxer is the standalone C++ implementation of Lynxer. This page describes the
language as Clynxer actually runs it. For gaps versus the older Python
implementation, see [limitations.md](limitations.md).

## Program structure

Every program needs:

1. **`global setup(){}`** — first declaration. Globals and `import()` /
   `importAs()` live here.
2. **`global main(){}`** — default entry point, last declaration (unless
   `overrideMain` redirects it).

Global helpers, file-wide `func`s, classes, structs, enums, and vargroups go
**between** `setup` and `main`.

```c
global setup(){
    import("math");
    const str LANG = "Lynxer";
}

func banner() -> str {
    return "Clynxer";
}

global greet(str name) -> none {
    println("Hello, ", name, "!");
}

global main(){
    global.greet(LANG);
    println(banner());
    println(global.math.sqrt(144.0));
}
```

Rules:

- Executable code outside a function body is a syntax error.
- `import` / `importAs` may only appear inside `setup()`.
- When `overrideMain("name")` is used in `setup()`, that global runs instead of
  `main` (and `main` is optional).

## Comments

```c
// single-line

///
  multi-line delimiter comment
///

////
  Leading file docstring (shown by --list-stdlibs for modules).
////
```

`/* ... */` is not supported.

## Types

See [types.md](types.md) for the full table. Declarations validate ranges at
assignment and call boundaries.

## Variables

```c
global setup(){
    int count = 0;
    const str APP = "demo";
}

global main(){
    float x = 1.5;
    x += 0.5;
}
```

- Typed locals and globals: `type name = value;`
- `const` prevents reassignment.
- Fixed-width types (`int8`, `uint32`, `float32`, …) reject out-of-range values.

## Operators

Arithmetic (`+ - * / % ** //`), comparison, equality (`== !=`), word and
symbolic boolean ops (`and`/`or`/`not`, `&&`/`||`/`!!`), bitwise ops, and
compound assignment (`+=` …) are supported. Logical NOT is `!!value` or
`not value` — a bare `!` is invalid. String escape `\e` is accepted alongside
`\n \r \t \\ \"`.

## Control flow

- `if` / `elif` / `else`
- `switch` / `case` / `default` (including pattern forms where implemented)
- `while`, C-style `for`, `doWhile`, `iterate`, `forever`
- `break`, `continue`, `restart`
- `try` / `catch`
- `return`

```c
global main(){
    int n = 3;
    if(n > 0){
        println("positive");
    } elif(n == 0){
        println("zero");
    } else {
        println("negative");
    }

    for(int i = 0; i < 3; i = i + 1){
        println(i);
    }
}
```

## Functions

| Kind | Keyword | Scope |
|------|---------|--------|
| Global | `global` | Between `setup` and `main`; call with `global.name(...)` from other globals |
| File-wide | `func` | Same file by bare name; imported as `global.module.name(...)` |
| Local | `local` | Inside a function; call with `local.name(...)` |

### Optional return types

Clynxer accepts an optional return annotation after the parameter list. The
Python Lynxer reference did not document this form; Clynxer does, and **checks
it at runtime**.

```c
global add(int a, int b) -> int {
    return a + b;
}

func label(str name) -> str {
    return "hi " + name;
}

global main(){
    int sum = global.add(2, 3);   // 5
    println(label("Ada"));
}
```

Syntax (either form):

```c
global f(int x) -> int { return x; }
global g(int x): int { return x; }    // colon form also accepted
```

Behaviour:

- Omit the annotation (or use `-> any`) to leave the return unchecked.
- When a return type is set, the returned value is converted/validated with the
  same rules as typed parameters (`Environment::convertForType`). A mismatch is
  a source-located runtime error.
- Stdlib wrappers use `-> type` widely (for example
  `global get(str url) -> str { ... }` in `network`).

Typed parameters, defaults, and nested `local` functions work as in Lynxer.
Parameters without a type are treated as `any`.

### Codeblocks

Functions may declare named caller-supplied blocks after the parameter list:

```c
global repeat(str label){body}{
    print(label); print(": ");
    exec(){{body}}
    println("");
}

global main(){
    global.repeat("msg"){
        print("hello");
    }
}
```

`setup` and `main` cannot take codeblock parameters.

## Classes, structs, enums, vargroups

See the dedicated pages:

- [classes.md](classes.md)
- [structs.md](structs.md)
- [enums.md](enums.md)
- [vargroups.md](vargroups.md)

## Modules

`import("name")` and `importAs("name", "alias")` load `.lynx` source modules or
native `.so` libraries from the program directory or `clynxer/stdlib/`. See
[modules.md](modules.md) and [importAs.md](importAs.md).

There is **no** `.lynxc` bytecode import path in Clynxer.

## Not supported

These Lynxer features are rejected or stubbed in Clynxer:

- `rawPy` / `rawPyx`
- Async functions and `async*` builtins
- Bytecode (`.lynxc`) compile/run
- Python-only stdlibs (`tkinter`, `turtle`, `sound`, `sqldb`, Flask `server`, …)

See [limitations.md](limitations.md) and [builtins.md](builtins.md).
