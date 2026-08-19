# Lynxer types

Lynxer has ordinary dynamic numeric values plus declarations that validate
fixed-width integer and floating-point ranges.

## Primitive types

| Type | Accepted values | Display |
|---|---|---|
| `int` | Arbitrary-size integers | Decimal integer |
| `float` | Host floating-point values | Decimal floating point |
| `num` | Any `int` or `float` | Underlying numeric value |
| `bool` | `true` or `false` | `true` / `false` |
| `numBool` | Integer `0` or `1` | `0` / `1` |
| `bit` | Integer `0` or `1` | `0` / `1` |
| `byte` | Integer `0..255` | Decimal integer |
| `char` | One Unicode character | Character |
| `str` | Text | String |
| `list` | Mutable sequence | `[ ... ]` |
| `tuple` | Immutable sequence | `( ... )` |
| `any` | Any value | Underlying value |

## Fixed-width numeric types

| Type | Range |
|---|---:|
| `int8` | `-128..127` |
| `int16` | `-32768..32767` |
| `int32` | `-2147483648..2147483647` |
| `int64` | `-9223372036854775808..9223372036854775807` |
| `uint8` | `0..255` |
| `uint16` | `0..65535` |
| `uint32` | `0..4294967295` |
| `uint64` | `0..18446744073709551615` |
| `float32` | finite values up to approximately `3.4028235e38` |
| `float64` | finite values up to approximately `1.7976931e308` |

These declarations validate values at declaration, parameter, list, tuple,
and assignment boundaries. They do not change the underlying host storage
representation used by the interpreter.

```c
global main(){
    numBool enabled = 1;
    bit ready = 0;
    byte mask = 255;
    int16 count = -12;
    uint32 total = 4000000000;
    float32 ratio = 0.25;
    float64 precise = 3.141592653589793;
}
```

`numBool` is deliberately different from `bool`: comparisons and logical
operators still produce `bool`, while `numBool` is useful for numeric APIs,
binary formats, and C interfaces that require `0` or `1`.