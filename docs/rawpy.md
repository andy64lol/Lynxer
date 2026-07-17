# rawPy and rawPyx

Lynxer lets you drop into Python or Cython at any point inside a function.

---

## rawPy — inline Python

### Block form

Variables declared in Lynxer are visible inside the block.  
Changes to those variables are written back to Lynxer scope when the block exits.

```c
void main(){
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

### String form

Quick one-liner — prints directly to stdout, no variable bridging.

```c
rawPy("print('hello from Python')");
rawPy("import math; print(math.pi)");
```

---

## rawPyx — inline Cython

Same shape as `rawPy`, but the block is compiled with Cython before running.  
Use it for numeric hot loops that need native speed.

The first call for a given snippet pays a one-time compile cost (cached in `~/.cython/inline/`); subsequent calls reuse the compiled extension.

### Block form

```c
void main(){
    int result = 0;
    rawPyx(){
        result = 6 * 7
    }
    print(result); print("\n"); // 42
}
```

### String form

```c
rawPyx("print('compiled with Cython')");
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

Both are installed automatically by `make install`.

---

## Multi-block example

Multiple `rawPy`/`rawPyx` blocks can appear in the same function.  
Each is an independent Python `exec` scope (variables re-bridged each time).

```c
void printHeader(str text){
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
