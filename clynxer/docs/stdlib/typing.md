# typing

Type conversion and numeric-range predicates.

**Backend:** pure — `stdlib/typing.lynx` only, over the interpreter's type
builtins. **Import:** `import("typing")` → `global.typing.*`

| Function | Signature | Notes |
| --- | --- | --- |
| `typeOf` | `(any value) -> str` | `returnType` of the value |
| `toStr` | `(any value) -> str` | Renders as text |
| `toInt` | `(any value) -> int` | Converts to an integer |
| `toFloat` | `(any value) -> float` | Converts to a float |
| `toBool` | `(any value) -> bool` | `value != 0` |
| `isNumeric` | `(any value) -> bool` | Whether the value is a number |
| `lenStr` | `(str value) -> int` | String length |
| `isInt8` | `(any value) -> bool` | Fits in a signed 8-bit range |
| `isInt16` | `(any value) -> bool` | Fits in a signed 16-bit range |
| `isInt32` | `(any value) -> bool` | Fits in a signed 32-bit range |
| `isByte` | `(any value) -> bool` | Fits in an unsigned 8-bit range |

This is a subset of the Python reference's 109 functions; the remaining
conversions and list/tuple helpers are not ported yet.

## Example

```lynx
global setup(){ import("typing"); }

global main(){
    println(global.typing.toInt("42"));
    println(global.typing.isByte(200));
    println(global.typing.isInt8(200));
}
```
