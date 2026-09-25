# Extending Lynxer

Lynxer has two extension layers:

| Extension | Implementation | Use it when |
| --- | --- | --- |
| **Built-in function** | Python in `lynxer/builtins.py` | The operation needs Python libraries, system access, or a runtime primitive. |
| **Standard-library module** | Lynxer in `lynxer/stdlib/<name>.lynx` | The operation can be expressed as Lynxer code or should be a normal imported module. |

Native shared libraries can also be imported as first-class modules when they
export the versioned registration ABI described in
[Native modules](native-modules.md).

Built-ins are available without `import()`. Standard-library modules are loaded
with `import("name")` and expose their global functions through
`global.name.function(...)`.

---

## Part 1 — Add a built-in function

All built-in definitions and their implementations live in
`lynxer/builtins.py`. The interpreter only imports the module after its runtime
value classes have been defined, then installs the registered functions into
the global and module symbol tables.

### The built-in class

`BuiltInFunction` is a `BaseFunction` whose `execute()` method dispatches by
name:

```python
method_name = f"execute_{self.name}"
method = getattr(self, method_name, self.no_visit_method)
return_value = res.register(method(args, exec_ctx))
```

Consequently, a built-in named `clamp` is implemented by a method named
`execute_clamp`. The method receives:

* `args`: a list of Lynxer runtime `Value` objects;
* `exec_ctx`: the call's runtime `Context`.

Return an `RTResult`: use `success(value)` for a Lynxer value and
`failure(RTError(...))` for a Lynxer runtime error.

### Example: `clamp(value, low, high)`

Add the name to `BUILTIN_FUNCTION_NAMES` and add its implementation to
`BuiltInFunction`:

```python
# in BUILTIN_FUNCTION_NAMES
"clamp",

class BuiltInFunction(BaseFunction):
    # ...
    def execute_clamp(self, args, exec_ctx):
        if len(args) != 3 or not all(isinstance(arg, Number) for arg in args):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "clamp(value, low, high) expects three numbers",
                    exec_ctx,
                )
            )

        value, low, high = (arg.value for arg in args)
        return RTResult().success(Number(max(low, min(value, high))))
```

The registry at the bottom of `builtins.py` creates the function instance and
installs it:

```python
for name in BUILTIN_FUNCTION_NAMES:
    register_builtin(name)
```

Do **not** add registrations to `lynxer.py`. The list and the `execute_...`
methods in `builtins.py` are the complete built-in definition.

### Runtime values

Arguments and return values must use Lynxer's runtime classes:

| Lynxer value | Runtime class | Python payload |
| --- | --- | --- |
| `int`, `float`, `bool` | `Number` | `.value`; booleans also set `is_bool=True` |
| `str` | `String` | `.value` |
| `char` | `Char` | `.value` |
| `list` | `List` | `.elements`, a list of runtime values |
| `tuple` | `LynxTuple` | `.elements`, a list of runtime values |
| `none` | `Null` | no payload |
| async result | `CoroutineValue` | `.coro`, a Python coroutine |

Use the singletons for language booleans and `none`:

```python
return RTResult().success(Number.true)
return RTResult().success(Number.false)
return RTResult().success(Number.null)
```

For a new list or tuple, wrap the elements:

```python
return RTResult().success(List([Number(1), String("two")]))
return RTResult().success(LynxTuple([Number(1), String("two")]))
```

`Value.set_context()` and `Value.set_pos()` are available when a newly created
value needs source/runtime metadata. The call visitor applies the call's
position and context to the returned value automatically.

### Validate arguments and report errors

Validate arity and runtime types before reading `.value` or `.elements`.
Errors should point to `self.pos_start` and `self.pos_end`:

```python
if len(args) != 1 or not isinstance(args[0], String):
    return RTResult().failure(
        RTError(
            self.pos_start,
            self.pos_end,
            'slugify(text) expects one string argument',
            exec_ctx,
        )
    )
```

Do not raise an ordinary Python exception for user input errors. Return an
`RTError` so Lynxer can show its normal traceback and source excerpt. Python
exceptions from an external library should generally be caught and converted
to an `RTError` as well.

### Register an implementation dynamically

`register_builtin` is also available for extensions that need to register a
handler after importing Lynxer:

```python
from lynxer.builtins import register_builtin

def execute_clamp(builtin, args, exec_ctx):
    # Return RTResult.success(...) or RTResult.failure(...)
    ...

register_builtin("clamp", execute_clamp)
```

The handler is attached to `BuiltInFunction`, an instance is stored in
`BUILTIN_FUNCTIONS`, and the global symbol table is updated immediately when
it has already been created. For an in-tree built-in, prefer the class method
plus `BUILTIN_FUNCTION_NAMES`; that keeps the complete built-in inventory
reviewable in one file.

The `@builtin("name")` decorator is equivalent to
`register_builtin("name", handler)`:

```python
@builtin("clamp")
def execute_clamp(builtin, args, exec_ctx):
    ...
```

### Testing a built-in

Create a small Lynxer program that calls the function directly and through an
imported module if the module path matters:

```lynx
global setup() {}

global main() {
    println(clamp(12, 0, 10));
}
```

Run it from the repository root:

```sh
python3 lynxer/shell.py /path/to/check.lynx
```

Also run the existing examples/tests after changing runtime code:

```sh
python3 lynxer/shell.py test/test.lynx
python3 lynxer/shell.py test/test2.lynx
```

---

## Part 2 — Add a standard-library module

Put a module in `lynxer/stdlib/`. Its filename becomes its import name:

```text
lynxer/stdlib/mylib.lynx  ->  import("mylib")
```

A module has `setup()` and `main()` declarations. `main()` is required by
the module parser even though `run_file()` does not execute it as an entry
point:

```lynx
/// Small example module ///

global setup() {}

global double(int value) {
    return value * 2;
}

global main() {}
```

Use it from a program like this:

```lynx
global setup() {
    import("mylib");
}

global main() {
    println(global.mylib.double(21));
}
```

Every built-in is seeded into the module's symbol table before the module is
run, so module code can call `print`, `range`, `listPush`, and the other
built-ins directly.

### Calling Python from a module

Use a `rawPy` block when a standard-library function needs Python:

```lynx
global sqrt(float value) {
    float result = 0.0;
    rawPy() {
        import math as _math
        result = _math.sqrt(value)
    }
    return result;
}
```

Variables declared in the same Lynxer function scope are bridged into the
block and assignments to those names are copied back. Python-only temporaries
should use underscore-prefixed names. `rawPy` blocks do not automatically
expose arbitrary Python objects as Lynxer values; convert results to numbers,
strings, or booleans before assigning them back.

For Cython-backed code, use `rawPyx` and keep the same conversion rule. The
string built-ins `rawPy("...")` and `rawPyx("...")` execute one-line code
without Lynxer variable bridging.

### Module checklist

1. Add `lynxer/stdlib/<name>.lynx`.
2. Include empty or real `global setup() {}` and `global main() {}`.
3. Export functions as top-level `global` functions.
4. Import the module in a test program.
5. Run the test program and the existing interpreter tests.

No changes to `lynxer.py` are needed for a normal built-in or standard-library
extension.