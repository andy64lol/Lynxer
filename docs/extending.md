# Extending Lynxer

There are two ways to add new functionality:

| Method | What you write | Where it lives |
|--------|---------------|----------------|
| **Built-in function** | Python inside `lynxer.py` | Always available, no `import()` needed |
| **Standard library module** | Lynxer inside `stdlib/<name>.lynx` | Available via `import("<name>")` in `setup()` |

Use a built-in when you need raw Python power (speed, system access, Python libraries).  
Use a stdlib module when pure Lynxer is enough — it is simpler and keeps the interpreter untouched.

---

## Part 1 — Adding a built-in function

Adding a built-in requires exactly **four edits** to `lynxer/lynxer.py`, all in the same region of the file.

### Step 1 — Declare the class variable

Around line 3376, `BuiltInFunction` has a block of `ClassVar` declarations — one per built-in. Add yours to the list:

```python
class BuiltInFunction(BaseFunction):
    print:   ClassVar["BuiltInFunction"]
    println: ClassVar["BuiltInFunction"]
    # ... existing entries ...
    myFunc:  ClassVar["BuiltInFunction"]   # ← add here
```

This is a type hint only; it lets static analysers and readers know the attribute exists.

### Step 2 — Write the `execute_` method

The `execute()` dispatcher calls `self.execute_<name>(args, exec_ctx)` automatically, where `name` is the string you passed to the constructor. Write that method anywhere inside `BuiltInFunction`:

```python
def execute_myFunc(self, args, exec_ctx):
    # validate arguments
    if len(args) != 2:
        return RTResult().failure(RTError(
            self.pos_start, self.pos_end,
            "myFunc() takes exactly 2 arguments",
            exec_ctx,
        ))

    # unwrap Lynxer values to Python values
    a = args[0].value   # Number.value → int/float, String.value → str
    b = args[1].value

    # do the work
    result = a + b

    # wrap and return
    return RTResult().success(Number(result))
```

**Argument types** arrive as Lynxer runtime objects. The common ones:

| Lynxer type | Python class | `.value` gives |
|-------------|-------------|----------------|
| `int` / `float` | `Number` | `int` or `float` |
| `str` | `String` | `str` |
| `list` | `List` | `list` of runtime objects |
| `bool` | `Number` | `1` (true) or `0` (false) |
| `none` | `Number` | `0` |

**Return values** must also be wrapped:

```python
# number
return RTResult().success(Number(42))
return RTResult().success(Number(3.14))

# string
return RTResult().success(String("hello"))

# list
elements = [Number(1), Number(2), String("x")]
return RTResult().success(List(elements))

# boolean (use the singletons)
return RTResult().success(Number.true)
return RTResult().success(Number.false)

# null / void
return RTResult().success(Number.null)
```

**Returning an error:**

```python
return RTResult().failure(RTError(
    self.pos_start,
    self.pos_end,
    "Something went wrong: <description>",
    exec_ctx,
))
```

### Step 3 — Instantiate the class variable

Around line 4058, after the class body, there is a block of `BuiltInFunction.name = BuiltInFunction("name")` lines. Add yours:

```python
BuiltInFunction.myFunc = BuiltInFunction("myFunc")
```

The string `"myFunc"` must match the method name suffix exactly (`execute_myFunc`).

### Step 4 — Register in both symbol tables

There are **two** places where built-ins are registered.

**a) `global_symbol_table`** (around line 5613) — makes it available to every Lynxer program:

```python
global_symbol_table.set("myFunc", BuiltInFunction.myFunc)
```

**b) `module_table`** inside `visit_ImportNode` (around line 5530) — makes it available inside imported stdlib modules:

```python
module_table.set("myFunc", BuiltInFunction.myFunc)
```

Both registrations are required. Skipping the `module_table` entry means the built-in silently disappears inside any imported `.lynx` file.

### Complete example — `clamp(value, lo, hi)`

```python
# Step 1 — ClassVar
clamp: ClassVar["BuiltInFunction"]

# Step 2 — execute method
def execute_clamp(self, args, exec_ctx):
    if len(args) != 3:
        return RTResult().failure(RTError(
            self.pos_start, self.pos_end,
            "clamp() takes exactly 3 arguments: clamp(value, lo, hi)",
            exec_ctx,
        ))
    val = args[0].value
    lo  = args[1].value
    hi  = args[2].value
    return RTResult().success(Number(max(lo, min(val, hi))))

# Step 3 — instantiate
BuiltInFunction.clamp = BuiltInFunction("clamp")

# Step 4 — register (both places)
global_symbol_table.set("clamp", BuiltInFunction.clamp)
module_table.set("clamp", BuiltInFunction.clamp)   # inside visit_ImportNode
```

After this, Lynxer programs can call `clamp(x, 0, 100)` without any import.

---

## Part 2 — Writing a stdlib module

A stdlib module is a plain `.lynx` file placed in `lynxer/stdlib/`. Users load it with `import("name")` inside `setup()` and call its functions as `global.name.functionName(...)`.

### Minimal skeleton

```c
/// lynxer/stdlib/mylib.lynx — one-line description ///

global setup(){}

global myHelper(int x){
    return x * 2;
}

// main() is required by the Lynxer runtime even in library files
global main(){}
```

`setup()` and `main()` must both be present (they can be empty). Every exported function is a top-level `global` function.

### Using `rawPy{}` to call Python

The block form `rawPy{ ... }` is the primary bridge for stdlib functions that need Python. Variables declared in the surrounding Lynxer function scope are automatically visible inside the block, and assignments to those same names flow back out:

```c
global sqrt(float n){
    float result = 0.0;
    rawPy(){
        import math as _m
        result = _m.sqrt(n)    // writes back to Lynxer's 'result'
    }
    return result;
}
```

Rules for `rawPy{}` blocks:
- Variable names must match their Lynxer declarations exactly.
- Only Lynxer-declared variables in the same function scope are bridged; Python-only temporaries (like `_m` above) are not visible in Lynxer.
- Use underscore-prefixed names (`_m`, `_tmp`) for Python temporaries to avoid collisions.
- Returning a boolean: assign `1` or `0` to an `int`, then compare with `not is 0` in Lynxer.

### Pattern — wrapping a Python stdlib function

```c
/// Returns s repeated n times. ///
global repeat(str s, int n){
    str result = "";
    rawPy(){
        result = s * n
    }
    return result;
}

/// Returns true if needle is found anywhere in haystack. ///
global contains(str haystack, str needle){
    int found = 0;
    rawPy(){
        found = 1 if needle in haystack else 0
    }
    if(found not is 0){ return true; }
    return false;
}
```

### Pattern — fallible conversion

```c
/// Parse s as an integer, return 0 on failure. ///
global toInt(str s){
    int result = 0;
    rawPy(){
        try:
            result = int(float(s))
        except Exception:
            result = 0
    }
    return result;
}
```

### Placement and naming

```
lynxer/
  stdlib/
    mylib.lynx    ← import("mylib")
    math.lynx     ← import("math")   (existing)
    typing.lynx   ← import("typing") (existing)
    ...
```

The file name (without `.lynx`) is the import key and the namespace prefix:

```c
global setup(){
    import("mylib");
}

global main(){
    int v = global.mylib.myHelper(21);   // 42
    println(v);
}
```

### What stdlib modules can use

Because the interpreter seeds `module_table` with the full set of built-ins before running the module file, every built-in is available inside a stdlib module without qualification — `print`, `println`, `splitStr`, `listPush`, `seqFromTo`, etc. all work exactly as they do in user code.

### Testing a new stdlib module

Write a small test file and run it with the interpreter directly:

```
/path/to/python3 lynxer/shell.py lynxer/tests/mylib_test.lynx
```

Use `println` assertions:

```c
global setup(){
    import("mylib");
}

global main(){
    int v = global.mylib.myHelper(21);
    println(v);   // expected: 42
}
```

There is no built-in assertion built-in yet — compare the printed output against your expectations manually or redirect stdout and diff.
