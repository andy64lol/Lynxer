# rawPy and rawPyx

Lynxer lets you drop into Python or Cython at any point inside a function.

---

## Isolation

**Each `rawPy` and `rawPyx` block runs in its own isolated Python `exec` scope.**

Variables are re-bridged from Lynxer into a fresh namespace every time the block executes. No state — Python imports, helper variables, or module aliases — persists between blocks. Side effects that touch the filesystem, network, or Python global state do persist (they go through the OS), but the Python namespace itself is always clean at the start of each block.

```c
global main(){
    int x = 5;
    rawPy(){
        import math as _m    // _m is visible only inside THIS block
        x = int(_m.sqrt(x))
    }
    rawPy(){
        // _m is NOT available here — this is a new, independent exec scope
        x = x * 2
    }
    print(x); print("\n");   // 6
}
```

This isolation applies equally to `rawPyx` blocks.

---

## rawPy — inline Python

### Block form

Variables declared in Lynxer are visible inside the block.  
Changes to those variables are written back to Lynxer scope when the block exits.

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

Only `int`, `float`, `str`, and `bool` values are bridged. Other Python objects (lists, dicts, etc.) are ignored.

---

## rawPyx — inline Cython

Same shape as `rawPy`, but the block is compiled with Cython before running.  
Use it for numeric hot loops that need native speed.

The first call for a given snippet pays a one-time compile cost (cached in `~/.cython/inline/`); subsequent calls reuse the compiled extension.

### Block form

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

If Cython is unavailable or compilation fails (e.g. missing C compiler, corrupted cache), `rawPyx` silently falls back to plain Python `exec`. The code still runs — just without the Cython speedup.

To clear a corrupted cache:

```c
cleanRawPyxCache();
```

### Requirements

- `cython` Python package
- A C compiler (`gcc` or `cc`) on the system `PATH`

---

## Multi-block example

Multiple `rawPy`/`rawPyx` blocks can appear in the same function.  
Each block is an **independent, isolated Python `exec` scope** — variables are re-bridged from the current Lynxer scope before each block runs, and written back after.

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
