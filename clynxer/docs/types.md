# Types

Clynxer validates declarations, parameters, assignments, and optional function
return annotations (`-> type`) against these type names.

## Primitive types

| Type | Accepted values |
|------|-----------------|
| `int` | Integers |
| `float` | Floating-point |
| `num` | `int` or `float` |
| `bool` | `true` / `false` |
| `numBool` / `bit` | Integer `0` or `1` |
| `byte` | Integer `0..255` |
| `char` | One character |
| `str` | Text |
| `list` | Mutable sequence |
| `tuple` | Immutable sequence |
| `any` | Any value |
| `none` | Empty / no value (useful on `-> none` returns) |
| `codeblock` | Stored caller-supplied block |
| named `struct` / `class` / `enum` | User-defined types |

## Fixed-width numeric types

| Type | Range |
|------|------:|
| `int8` | `-128..127` |
| `int16` | `-32768..32767` |
| `int32` | `-2147483648..2147483647` |
| `int64` | full signed 64-bit range |
| `uint8` | `0..255` |
| `uint16` | `0..65535` |
| `uint32` | `0..4294967295` |
| `uint64` | full unsigned 64-bit range |
| `float32` | finite values up to about `3.4e38` |
| `float64` | finite values up to about `1.8e308` |

Out-of-range values fail at the declaration, parameter, assignment, or return
boundary. Storage remains the interpreter's usual numeric representation.

```c
global setup(){}

global scaled(int16 n) -> int32 {
    return n * 2;
}

global main(){
    numBool enabled = 1;
    byte mask = 255;
    float32 ratio = 0.25;
    println(global.scaled(12));
}
```

## Return annotations

```c
global f(int x) -> int { return x + 1; }
global g(int x): str { return strOf(x); }
```

Both `-> type` and `: type` after the parameter list are accepted. When the
annotated type is not `any`, Clynxer converts the returned value to that type
before handing it to the caller. See [language.md](language.md#optional-return-types).
