# Built-in Functions

These are always available — no `import()` needed.

---

## I/O

### `print(v)`

Prints any value with no automatic newline.

```c
print("Hello"); print("\n");
print(42);      print("\n");
print(3.14);    print("\n");
```

### `input("prompt")`

Prints the prompt, reads a line from stdin, returns `str`.

```c
str name = input("Name: ");
```

---

## Type conversion

### `str_of(v)`

Converts any value to a string.

```c
str s = str_of(99);     // "99"
str f = str_of(3.14);   // "3.14"
```

### `int_of(v)`

Parses a value as an integer.

```c
int n = int_of("42");   // 42
int m = int_of(3.9);    // 3
```

### `float_of(v)`

Parses a value as a float.

```c
float f = float_of("1.5");   // 1.5
```

---

## Introspection

### `returnType(v)`

Returns the type name of `v` as a string.

| Value | Result |
|-------|--------|
| `42` | `"int"` |
| `3.14` | `"float"` |
| `"hi"` | `"str"` |
| `true` | `"bool"` |
| `none` | `"none"` |
| list from `seqFromTo` | `"list"` |
| a function | `"function"` |

```c
print(returnType(42));      // int
print(returnType("hello")); // str
```

### `returnLength(v)`

Returns the length of a `str` or a `list`.

```c
print(returnLength("hello"));        // 5
print(returnLength(seqFromTo(0,5,1))); // 5
```

---

## Sequences

### `seqFromTo(start, stop, step)`

Builds a list of integers like Python's `range()`.

```c
any nums = seqFromTo(0, 10, 2);  // [0, 2, 4, 6, 8]
any odds = seqFromTo(1, 10, 2);  // [1, 3, 5, 7, 9]
any down = seqFromTo(5, 0, -1);  // [5, 4, 3, 2, 1]
```

`step` must not be `0`.

---

## rawPy / rawPyx

See [rawpy.md](rawpy.md) for the block and string forms.

### `rawPy("code")`

Execute a Python one-liner (stdout only, no variable bridging).

```c
rawPy("print('hello from Python')");
```

### `rawPyx("code")`

Compile and execute a Cython one-liner.

```c
rawPyx("print('hello from Cython')");
```

---

## Cache

### `cleanRawPyxCache()`

Deletes the Cython inline cache (`~/.cython/inline/`).  
Useful when a cached `.so` becomes corrupted.

```c
cleanRawPyxCache();
```
