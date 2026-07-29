# Python Bridge: rawPy, rawPyx, importPy, embedPy

Lynxer offers four ways to reach into Python from Lynxer code.

| Feature | What it does |
|---------|-------------|
| [`rawPy{}`](#rawpy--inline-python) | Drop a block of Python code inline inside a function |
| [`rawPyx{}`](#rawpyx--inline-cython) | Same as `rawPy`, but compiled with Cython for native speed |
| [`importPy(){}`](#importpy--pre-import-python-modules) | Pre-import Python modules once in `setup()` so they are available in every `rawPy`/`rawPyx` block |
| [`embedPy`](#embedpy--call-python-functions-directly) | Call Python functions and access Python modules directly with Lynxer syntax — no block required |

---

## rawPy — inline Python

### Block form

Write any Python code inside `rawPy(){  }`. Variables declared in the enclosing
Lynxer scope are visible inside the block. When the block exits, any changes to
those variables are written back.

```c
global main(){
    int x = 0;
    rawPy(){
        x = 7 * 6          // x is 42 in Lynxer after this block
    }
    print(x); print("\n"); // 42

    str s = "";
    rawPy(){
        s = "hello".upper()
    }
    print(s); print("\n"); // HELLO
}
```

Only `int`, `float`, `str`, and `bool` values are bridged back. Other Python
objects (lists, dicts, custom instances, …) are ignored on write-back.

---

## rawPy isolation

**Each `rawPy` block runs in its own isolated Python `exec` scope.**

Variables are re-bridged from Lynxer into a fresh namespace every time the block
executes. No state — Python imports, helper variables, module aliases — persists
between blocks. Side effects that touch the filesystem or network do persist, but
the Python namespace itself is always clean at the start of each block.

```c
global main(){
    int x = 5;
    rawPy(){
        import math as _m    // _m visible only inside THIS block
        x = int(_m.sqrt(x))
    }
    rawPy(){
        // _m is NOT available here — independent exec scope
        x = x * 2
    }
    print(x); print("\n");   // 4
}
```

Use [`importPy`](#importpy--pre-import-python-modules) to share modules across
blocks without re-importing each time.

---

## rawPyx — inline Cython

Same shape as `rawPy`, but the block is compiled with Cython before running.
Use it for numeric hot loops that need native speed.

The first call for a given snippet pays a one-time compile cost (cached in
`~/.cython/inline/`); subsequent calls reuse the compiled extension.

```c
global main(){
    int result = 0;
    rawPyx(){
        result = 6 * 7
    }
    print(result); print("\n"); // 42
}
```

### Fallback behaviour

If Cython is unavailable or compilation fails (missing C compiler, corrupted
cache), `rawPyx` silently falls back to plain Python `exec`. The code still
runs — just without the Cython speedup.

To clear a corrupted Cython cache:

```c
cleanRawPyxCache();
```

### Requirements

- `cython` Python package
- A C compiler (`gcc` or `cc`) on the system `PATH`

---

## Multi-block example

Multiple `rawPy`/`rawPyx` blocks can appear in the same function. Each block is
**independent** — variables are re-bridged before each block and written back
after.

```c
global printHeader(str text){
    int n = returnLength(text);
    rawPy(){
        print("=" * n)
    }
    print(text); print("\n");
    rawPy(){
        print("=" * n)
    }
}
```

---

## importPy — pre-import Python modules

`importPy(){"module", ...}` must be called inside `global setup(){}`. It imports
the named Python modules once at startup and injects them into every subsequent
`rawPy` and `rawPyx` block automatically — no `import` statement needed inside
individual blocks.

```c
global setup(){
    importPy(){"os", "sys", "json"};
}

global main(){
    str cwd = "";
    rawPy(){
        // os, sys, json are already available — no import needed
        cwd = os.getcwd()
    }
    print(cwd); print("\n");
}
```

### Syntax

```
importPy(){"module1", "module2", ...};
```

- Module names are quoted strings inside `{ }`.
- The parentheses `()` take no arguments.
- `importPy` is only allowed inside `global setup(){}`.
- An empty `importPy(){}` is valid but does nothing.

### Why use importPy instead of inline imports?

Because each `rawPy` block has an isolated exec scope, a bare `import os`
written inside one block is invisible to the next. `importPy` solves this by
pre-loading the modules into a shared registry that every block can see:

```c
global setup(){
    importPy(){"re"};   // 're' available in all rawPy blocks below
}

global parseVersion(str text){
    str major = "";
    rawPy(){
        m = re.search(r"(\d+)\.", text)
        major = m.group(1) if m else ""
    }
    return major;
}
```

### Error handling

If a module name cannot be imported, Lynxer raises a runtime error with the
module name and the underlying Python `ImportError` message.

---

## embedPy — call Python functions directly

`embedPy` is a built-in namespace that lets you call Python functions and access
Python modules with ordinary Lynxer syntax — no `rawPy` block required.

```c
global main(){
    // call a Python builtin
    int n = embedPy.len("hello");
    print(n); print("\n");        // 5

    // call a function from a Python module
    float root = embedPy.math.sqrt(144.0);
    print(root); print("\n");     // 12.0

    // chain attribute access
    str cwd = embedPy.os.getcwd();
    print(cwd); print("\n");
}
```

### How attribute lookup works

`embedPy.name` resolves in this order:

1. **Python builtins** — `len`, `str`, `int`, `round`, `sorted`, `open`, etc.
2. **Python module** — tries `import name`; if it succeeds, returns the module.
3. **Error** — if neither matches, a clear runtime error is raised.

```c
global main(){
    // builtin
    str upper = embedPy.str("hello");   // "hello" (Python str constructor)

    // stdlib module
    str sep = embedPy.os.sep;           // "/" on Unix

    // third-party (must be installed first)
    any resp = embedPy.requests.get("https://example.com");
}
```

### Type conversions

Values crossing the Lynxer ↔ Python boundary are converted automatically:

| Python type | Lynxer type |
|-------------|-------------|
| `int` | `int` |
| `float` | `float` |
| `str` | `str` |
| `bool` | `bool` (`1` / `0`) |
| `bytes` | `str` (UTF-8 decoded) |
| `list` / `tuple` | Lynxer list |
| `dict` | `str` (JSON-serialised) |
| anything else | opaque `embedPy` object |

Opaque `embedPy` objects support further attribute access and calling, so you
can chain calls on objects that have no Lynxer equivalent:

```c
global main(){
    // pathlib.Path → opaque object, then chain .parent, .name, etc.
    any p    = embedPy.pathlib.Path("/tmp/demo.lynx");
    any name = p.name;
    print(name); print("\n");   // "demo.lynx"
}
```

### Storing embedPy results in typed variables

Assign a return value to a typed variable and Lynxer will coerce it:

```c
global main(){
    int   length  = embedPy.len("Lynxer");    // 6
    float root    = embedPy.math.sqrt(2.0);   // 1.41…
    str   joined  = embedPy.os.path.join("/tmp", "out.txt");
    bool  exists  = embedPy.os.path.exists("/tmp");
}
```

### embedPy vs rawPy

| | `rawPy{}` | `embedPy` |
|-|-----------|-----------|
| Syntax | Block of Python code | Single-expression calls |
| Variable bridging | Reads and writes Lynxer vars | Return value only |
| Arbitrary Python | ✓ (any statements) | ✗ (function calls / attribute access) |
| Multi-line logic | ✓ | ✗ |
| Imports needed | Only via `importPy` or inline | Never (auto-imported on access) |

Use `rawPy` for multi-line logic or when you need to set multiple Lynxer
variables at once. Use `embedPy` for concise one-liner calls.
