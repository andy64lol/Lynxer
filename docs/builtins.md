# Built-in Functions

Built-in functions are always available — no `import()` needed. Call them directly by name, or via `global.<name>(...)`.

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

---

## Type conversion

### `strOf(v)`

Converts any value to its string representation.

```c
str s = strOf(99);      // "99"
str f = strOf(3.14);    // "3.14"
str b = strOf(true);    // "true"
str l = strOf(seqFromTo(0,3,1)); // "[0, 1, 2]"
```

### `intOf(v)`

Parses a value as an integer. Returns `0` on failure.

> Use `global.typing.isNumeric(s)` to validate before converting when `0` could be a valid result.

```c
int n = intOf("42");    // 42
int m = intOf(3.9);     // 3
int bad = intOf("hi");  // 0
```

### `floatOf(v)`

Parses a value as a float. Returns `0.0` on failure.

> Use `global.typing.isNumeric(s)` to validate before converting when `0.0` could be a valid result.

```c
float f = floatOf("1.5");    // 1.5
float bad = floatOf("hi");   // 0.0
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
| list | `"list"` |
| vargroup | `"vargroup"` |
| a function | `"function"` |

```c
print(returnType(42));            // int
print(returnType("hello"));      // str
print(returnType(true));         // bool
print(returnType(seqFromTo(0,2,1))); // list

vargroup cfg = [str host = "localhost", int port = 8080];
print(returnType(cfg));           // vargroup
```

### `returnLength(v)`

Returns the number of characters in a `str`, or the number of elements in a `list`.

```c
print(returnLength("hello"));             // 5
print(returnLength(seqFromTo(0, 5, 1)));  // 5
```

---

## Sequences

### `seqFromTo(start, stop, step)`

Returns a `list` of integers identical to Python's `range(start, stop, step)`.

```c
any nums = seqFromTo(0, 10, 2);  // [0, 2, 4, 6, 8]
any down = seqFromTo(5, 0, -1);  // [5, 4, 3, 2, 1]
any empty = seqFromTo(0, 0, 1);  // []  — empty list
```

`step` must not be `0`.

---

## List operations

All list operations work with values produced by `seqFromTo()` or built up with `listPush()`. See [lists.md](lists.md) for a full tutorial.

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

Runs `body` exactly `count` times. `count` can be any integer expression. `break` and `continue` work as normal.

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
