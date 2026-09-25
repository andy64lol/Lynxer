# Types

Every declaration, parameter, assignment, and `-> type` return annotation is
validated against a type name. This page lists the names, what they accept, and
how values are converted. For a runnable overview see
[language.md](language.md).

## Scalar and container types

| Type | Accepts | Notes |
|------|---------|-------|
| `any` | anything | no checking |
| `int` | integers, or a float with no fractional part | `2.0` → `2`; `1.5` is rejected |
| `float` | any number | an `int` is widened |
| `num` | `int` or `float` | |
| `bool` | `true` / `false` | an integer `1` is **not** accepted |
| `numBool` | `0` or `1` | out-of-range values fail |
| `bit` | `0` or `1` | alias of the `numBool` range |
| `char` | one character, or a one-character string | `"AB"` fails with `string length 2 is not a char` |
| `str` | text | a number is **not** accepted |
| `list` | a list value | see [lists.md](lists.md) |
| `tuple` | a tuple value | Immutable, ordered, fixed-length sequence. See [lists.md](lists.md) for details.
| `sentinel` | a sentinel value | built with `sentinel()` / `sentinel("NAME")` |
| `object` | an object value | built with `object()` |
| `codeblock` | a stored block | see [language.md](language.md#codeblocks) |
| `functionAddress` | a native function address | integer handle |
| `struct` / `class` / `enum` name | a value of that named type | see the dedicated pages |

`none` is a **value**, not a declarator type: write `any x = none;`. It may be
used as a return annotation (`-> none`), where it accepts only the no-value
result. See [Return annotations](#return-annotations).

## Fixed-width numeric types

| Type | Range |
|------|------:|
| `int8` | `-128 .. 127` |
| `int16` | `-32768 .. 32767` |
| `int32` | `-2147483648 .. 2147483647` |
| `int64` | the full signed 64-bit range |
| `uint8` / `byte` | `0 .. 255` |
| `uint16` | `0 .. 65535` |
| `uint32` | `0 .. 4294967295` |
| `uint64` | the full unsigned 64-bit range |
| `float32` | finite values up to about `3.4e38` |
| `float64` | finite values up to about `1.8e308` |

Fixed-width values are checked at **every** boundary — declaration, parameter,
assignment, and return — and an out-of-range value is a source-located error,
for example `value 200 is out of range for type 'int8'`.

`uint64` values above `2^63 - 1` are held in a separate internal representation
so the full range survives; `18446744073709551615` prints and round-trips
exactly, including through the native-memory writes in
[builtins.md](builtins.md#native-memory).

```lynx
global setup(){}

func scaled(int16 n) -> int32 {
    return n * 2;
}

global main(){
    numBool enabled = 1;
    byte mask = 255;
    float32 ratio = 0.25;
    char initial = "Z";
    println(scaled(12));   // 24
    println(mask);         // 255
    println(ratio);        // 0.25
    println(initial);      // Z
}
```

## Conversion rules

- A float assigned to an `int` must have no fractional part (`2.0` is accepted,
  `2.5` is not).
- A one-character string may be assigned to a `char`.
- A number may **not** be assigned to a `str`; use `strOf(value)`.
- A `bool` must be a real boolean; `1` does not convert to `true`.
- A named type is enforced (assigning an unrelated value fails, e.g.
  `value cannot be assigned to type 'Counter'`).

## Return annotations

Use the arrow form:

```lynx
global f(int x) -> int { return x + 1; }
func label(str name) -> str { return "hi " + name; }
```

- `: type` is **not** valid syntax (`unexpected character ':'`).
- Omit the annotation (or write `-> any`) to leave the result unchecked.
- When a return type is set, the value is validated with the same rules as a
  typed parameter, and a mismatch is a source-located runtime error.
- `-> none` is accepted: the function must return nothing (a returned value
  fails with `value cannot be assigned to type 'none'`). Class methods take
  **no** return annotation at all; see [classes.md](classes.md).
