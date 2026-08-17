# Built-in Functions

Built-in functions are always available — no `import()` needed. Call them directly by name, or via `global.<name>(...)`.

The complete implementation and registry for Lynxer language built-ins lives in
`lynxer/builtins.py`. The interpreter imports and registers that module after
its runtime value types have been defined.

---

## I/O

### `print(v, ...)`

Prints one or more values with **no automatic newline**. Multiple arguments are concatenated.

```c
print("Hello, "); print("World\n");  // Hello, World
print("x=", 10, " y=", 20, "\n");   // x=10 y=20
```

### `println(v, ...)`

Prints one or more values followed by a newline. Equivalent to `print(v, "\n")`.

```c
println("Hello, World");   // Hello, World\n
println(42);               // 42\n
println(true);             // true\n
println("x=", 10);        // x=10\n
```

### `input(prompt?)`

Prints the optional prompt, reads a line from stdin, and returns it as `str`.

```c
str name = input("Name: ");
str raw  = input();   // no prompt
```

### `inputln(prompt?)`

Like `input()` but appends a newline to the returned string.

---

## Assertions

### `assert(condition[, message])`

Checks a boolean or numeric condition. A zero or `false` condition raises a
runtime error; a non-zero condition succeeds. The optional message is shown
as the runtime error details.

```c
assert(true);
assert(2 + 2 == 4, "arithmetic is broken");
```

Assertions are always available and do not require importing the `debug`
module. The debug module also provides more specialized helpers such as
`global.debug.assertEq()` and `global.debug.assertContains()`.

---

## Type conversion

### `strOf(v)`

Converts any value to its string representation.

```c
str s = strOf(99);      // "99"
str f = strOf(3.14);    // "3.14"
str b = strOf(true);    // "true"
str l = strOf(range(3)); // "[0, 1, 2]"
```

### `intOf(v)`

Parses a value as an integer. Raises a runtime error on failure (use `try/catch` to handle).

```c
int n = intOf("42");    // 42
int m = intOf(3.9);     // 3
```

### `floatOf(v)`

Parses a value as a float. Raises a runtime error on failure.

```c
float f = floatOf("1.5");    // 1.5
```

---

## Introspection

### `returnType(v)`

Returns the type name of `v` as a `str`.

| Value | Result |
|-------|--------|
| `42` | `"int"` |
| `3.14` | `"float"` |
| `"hi"` | `"str"` |
| `true` / `false` | `"bool"` |
| `none` | `"none"` |
| `sentinel("MISSING")` | `"sentinel"` |
| `object()` | `"object"` |
| list | `"list"` |
| tuple | `"tuple"` |
| vargroup | `"vargroup"` |
| a function | `"function"` |

```c
print(returnType(42));            // int
print(returnType("hello"));      // str
print(returnType(true));         // bool
print(returnType(range(3)));     // list

vargroup cfg = {str host = "localhost", int port = 8080};
print(returnType(cfg));           // vargroup
```

### `sentinel([name])` → `sentinel`

Creates a unique marker value. Pass one string to give it a readable display
name; omit it for an unnamed sentinel. Every call creates a distinct value,
even when the names match. Sentinel values compare by identity and are useful
for distinguishing “missing” from `none`.

```c
sentinel missing = sentinel("MISSING");
any unnamed = sentinel();

print(strOf(missing));            // MISSING
print(returnType(missing));       // sentinel
assert(missing == missing);
assert(missing != sentinel("MISSING"));
```

Sentinel variables retain identity when read or assigned. Two separately
created sentinels are different even when they have the same display name.

### `object()` → `object`

Creates a unique unnamed opaque value. It accepts no arguments and cannot
define a display name. Object values compare by identity and are useful as
private marker values.

```c
any marker = object();
any other = object();

print(strOf(marker));             // <object>
print(returnType(marker));        // object
assert(marker == marker);
assert(marker != other);
```

### `returnLength(v)`

Returns the number of characters in a `str`, or the number of elements in a `list` or `tuple`.

```c
print(returnLength("hello"));     // 5
print(returnLength(range(5)));    // 5
```

---

## Sequences

### `range(stop)` / `range(start, stop)` / `range(start, stop, step)`

Returns a `list` of integers, mirroring Python's `range()`:
- `start` is **included**, `stop` is **excluded**, `step` defaults to `1`.
- `step` must not be `0`.

```c
any r = range(5);            // [0, 1, 2, 3, 4]
any r2 = range(2, 8);        // [2, 3, 4, 5, 6, 7]
any r3 = range(0, 10, 2);    // [0, 2, 4, 6, 8]
any r4 = range(10, 0, -1);   // [10, 9, 8, 7, 6, 5, 4, 3, 2, 1]
any empty = range(5, 5);     // []
```

### `seqFromTo(start, stop, step)`

Legacy alias similar to `range()` but always requires all 3 arguments.

```c
any nums = seqFromTo(0, 10, 2);  // [0, 2, 4, 6, 8]
```

---

## List operations

All list operations work with values produced by `range()`, `seqFromTo()`, or built up with `listPush()`. Declare list variables with the `list` keyword.

> **Value semantics:** `listPush`, `listSet`, and `listRemove` return a **new** list. Always reassign:
> ```c
> lst = listPush(lst, val);   // ✓ correct
> listPush(lst, val);          // ✗ original unchanged
> ```

### `listPush(lst, val)` → `list`
Return new list with `val` appended.

### `listPop(lst)` → value
Return the last element (does not modify the original).

### `listGet(lst, idx)` → value
Return element at `idx`. Negative indices count from the end (`-1` = last).

### `listSet(lst, idx, val)` → `list`
Return new list with element at `idx` replaced.

### `listRemove(lst, idx)` → `list`
Return new list with element at `idx` removed.

### `listSlice(lst, start, stop)` → `list`
Return new list with elements from `start` up to (not including) `stop`.

### `listContains(lst, val)` → `bool`
Return `true` if `val` is in `lst`.

### `contains(sequence, val)` → `bool`
Return `true` if `val` is in a `list` or `tuple`. This is the common membership
built-in for both sequence types.

```c
list numbers = [1, 2, 3];
tuple point = [10, 20];

bool hasTwo = contains(numbers, 2);  // true
bool has20 = contains(point, 20);    // true
bool hasNine = contains(point, 9);   // false
```

### `listJoin(lst, sep)` → `str`
Concatenate all elements as strings, separated by `sep`.

### `listIndex(lst, val)` → `int`
Return the index of the first match, or `-1` if not found.

### `anyOf(lst)` → `bool`
Return `true` if at least one element is truthy.

### `allOf(lst)` → `bool`
Return `true` if every element is truthy.

### `sumOf(lst)` → number
Return the sum of all numeric elements.

### `sortList(lst)` / `sortList(lst, reverse)` → `list`
Return new sorted list. Pass `true` as second argument to sort descending.

### `reverseList(lst)` → `list`
Return new list with elements in reverse order.

### `listMin(lst)` → value
Return the smallest element.

### `listMax(lst)` → value
Return the largest element.

### `splitStr(s, sep)` → `list`
Split string `s` by separator `sep` and return a list of strings.

```c
any parts = splitStr("a,b,c", ",");  // [a, b, c]
```

### `listFlatten(lst)` → `list`
Flatten one level of nested lists.

### `listUnique(lst)` → `list`
Return new list with duplicate values removed (order preserved).

### `listJsonArray(lst)` → `str`
Serialize a list to a JSON array string.

### `listJsonObject(lst)` → `str`
Build a JSON object string from a flat alternating key/value list. The list must have an even number of elements.

---

## Repeat loop

### `iterate(count) { body }`

Runs `body` exactly `count` times. `count` can be any integer expression. `break`, `continue`, and `restart` work as normal.

```c
iterate(3) {
    println("hello");
}

int n = 5;
iterate(n) {
    println("again");
}
```

---

## Forever loop

### `forever() { body }`

Runs `body` repeatedly until it executes `break;`. The loop waits `0.02`
seconds between iterations by default. Configure the delay once from
`global setup()` with `foreverDelay(seconds)`.

```c
global setup() {
    foreverDelay(0.05);
}

global main() {
    int count = 0;
    forever() {
        count = count + 1;
        println(count); 
        if(count == 3) { break; }
    }
}
```

Lynxer warns when a `forever()` body contains no `break;`, because it may run
until the process is stopped. If the loop is intentionally unbounded, suppress
that warning from `setup()`:

```c
global setup() {
    suppressForeverWarning();
}
```

`foreverDelay()` and `suppressForeverWarning()` are setup-only functions.
`break`, `continue`, and `restart` work inside `forever()` like they do in
the other loop forms.

---

## Timing

### `sleep(num)`

Blocks the current execution for the given number of seconds. The argument
may be an `int` or `float`; negative durations and other value types raise a
runtime error.

```c
sleep(1);       // one second
sleep(0.25);    // 250 milliseconds
```

---

## rawPy / rawPyx

See [language.md](language.md#rawpy-and-rawpyx) for full bridging rules.

### `rawPy("code")`

Execute a Python one-liner. No variable bridging — stdout only.

```c
rawPy("print('hello from Python')");
```

### `rawPyx("code")`

Compile and execute a Cython one-liner. Requires Cython.

```c
rawPyx("print('hello from Cython')");
```

---

## Cache

### `cleanRawPyxCache()`

Deletes the Cython inline cache (`~/.cython/inline/`). Useful when a cached `.so` becomes corrupted.

```c
cleanRawPyxCache();
```
