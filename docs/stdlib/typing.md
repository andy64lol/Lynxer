# typing

Type inspection, conversion, range, string, number and sequence helpers.

**Backend:** pure — `stdlib/typing.lynx` only, over the interpreter's builtins.
**Import:** `import("typing")` → `global.typing.*`

The interpreter's strings are byte strings: `returnLength`, `charAt`,
`substring` and the `charCode`/`charOf` builtins all count bytes, so code points
are byte values in `0..255` rather than Unicode scalar values.

## Type Checks

| Function | Signature | Returns | Description |
| --- | --- | --- | --- |
| `isNum` | `isNum(any val)` | `bool` | `true` if `val` is `int` or `float` |
| `isInt` | `isInt(any val)` | `bool` | `true` if `val` is an `int` |
| `isFloat` | `isFloat(any val)` | `bool` | `true` if `val` is a `float` |
| `isStr` | `isStr(any val)` | `bool` | `true` if `val` is a `str` |
| `isBool` | `isBool(any val)` | `bool` | `true` if `val` is a `bool` |
| `isList` | `isList(any val)` | `bool` | `true` if `val` is a `list` |
| `isTuple` | `isTuple(any val)` | `bool` | `true` if `val` is a `tuple` |
| `isChar` | `isChar(any val)` | `bool` | `true` if `val` is a `char` |
| `isNone` | `isNone(any val)` | `bool` | `true` if `val` is `none` |
| `isSequence` | `isSequence(any val)` | `bool` | `true` if `val` is a `list` or `tuple` |
| `isNumeric` | `isNumeric(any value)` | `bool` | `true` if `value` is an `int` or `float` |
| `isAlpha` / `isDigit` / `isAlphaNum` / `isSpace` | `(str value)` | `bool` | Every byte is a letter / digit / alphanumeric / whitespace; `false` for `""` |

`isNumeric` keeps the Lynxer meaning ("is a number"), not the legacy "the
string parses as a number"; use `isDigit` for digit-only strings.

## Integer and Float Ranges

| Function | Returns | Description |
| --- | --- | --- |
| `isNumBool` / `isBit` | `bool` | `int` equal to `0` or `1` |
| `isInt8` / `isInt16` / `isInt32` / `isInt64` | `bool` | Integer fits the signed range (`int` covers the full 64-bit range) |
| `isByte` / `isUInt8` | `bool` | Integer in `0..255` |
| `isUInt16` / `isUInt32` / `isUInt64` | `bool` | Integer in the unsigned range (`isUInt64` is `int >= 0`) |
| `isFloat32` / `isFloat64` | `bool` | Number within the float32 range / any number |

## Type Conversions

| Function | Signature | Returns | Description |
| --- | --- | --- | --- |
| `typeOf` | `typeOf(any value)` | `str` | The value's type name |
| `toStr` | `toStr(any value)` | `str` | Renders as text |
| `toString` | `toString(any val)` | `str` | Alias of `toStr` |
| `toInt` | `toInt(any value)` | `int` | Converts to an integer |
| `toFloat` | `toFloat(any value)` | `float` | Converts to a float |
| `toFloat32` / `toFloat64` | `(any value)` | `float` | Both return `floatOf(value)`; Lynxer `float` is a double, so there is no single-precision narrowing |
| `toBool` | `toBool(any value)` | `bool` | `value != 0` |
| `toNumBool` | `(any value)` | `numBool` | Normalizes to `0` or `1` |
| `toBit` | `(any value)` | `bit` | Normalizes to `0` or `1` |
| `toByte` | `(any value)` | `byte` | Converts and clamps to `0..255` |
| `toChar` | `(any val)` | `char` | One-byte string or `int` code in `0..255`; NUL byte on error |
| `toNumber` | `(any val)` | `float` | Converts a number to a float; `0.0` otherwise |

## Char Functions

| Function | Signature | Returns | Description |
| --- | --- | --- | --- |
| `charCodeOf` | `charCodeOf(any val)` | `int` | Byte value of a `char`, or of the first byte of a `str`; `-1` on error |
| `charCode` | `charCode(str value)` | `int` | Byte value of the first byte; `-1` for `""` |
| `charOf` | `charOf(int code)` | `char` | One-byte `char` for a code in `0..255`; NUL byte otherwise |
| `charAt` | `charAt(str value, int index)` | `char` \| `none` | The character at `index`, or `none` when out of range |

## String Functions

| Function | Signature | Returns | Description |
| --- | --- | --- | --- |
| `lenStr` | `lenStr(str value)` | `int` | Length in bytes |
| `trim` / `stripLeft` / `stripRight` | `(str value)` | `str` | Strip leading and trailing / leading / trailing whitespace |
| `upper` / `lower` / `titleCase` / `swapCase` | `(str value)` | `str` | Case conversion |
| `repeat` / `repeatStr` | `(str value, int n)` | `str` | Repeat `n` times |
| `contains` | `contains(str value, str needle)` | `bool` | Substring test (`true` for an empty needle) |
| `startsWith` / `endsWith` | `(str value, str affix)` | `bool` | Prefix / suffix test |
| `replace` | `(str value, str old, str new)` | `str` | Replace every occurrence |
| `indexOf` | `(str value, str needle)` | `int` | First index, or `-1`; `0` for an empty needle |
| `countOccurrences` | `(str value, str needle)` | `int` | Non-overlapping occurrences |
| `substr` | `(str value, int start, int end)` | `str` | Slice with negative indices and clamping |
| `padLeft` / `padRight` / `center` | `(str value, int width, str fill)` | `str` | Pad to `width` |
| `zfill` | `(str value, int width)` | `str` | Zero-pad, keeping a leading sign |
| `strReverse` | `(str value)` | `str` | Reverse the bytes |
| `spaces` | `spaces(int n)` | `str` | `n` space characters |
| `wordWrap` | `(str value, int width)` | `str` | Greedy wrap on spaces |
| `expandTabs` | `(str value, int tabSize)` | `str` | Expand tabs to `tabSize`-wide stops |
| `splitFirst` | `(str value, str sep)` | `list` | Two-element `[before, after]` |
| `splitToList` | `(str value, str sep)` | `list` | Split into a list of strings |
| `linesOf` | `(str value)` | `list` | Split on newlines; `[]` for `""` |

## Number Functions

| Function | Signature | Returns | Description |
| --- | --- | --- | --- |
| `abs` | `abs(num value)` | `num` | Absolute value |
| `sign` | `sign(num value)` | `int` | `1`, `-1` or `0` |
| `maxOf` / `minOf` | `(num a, num b)` | `num` | Larger / smaller of two numbers |
| `clamp` | `clamp(float value, float low, float high)` | `float` | Clamp to `[low, high]` |
| `between` | `between(num value, num low, num high)` | `bool` | `low <= value <= high` |
| `roundTo` | `roundTo(float value, int digits)` | `float` | Round to `digits` decimal places |

## List Functions

These complement the language built-ins (`listPush`, `listGet`, `sortList`, etc.).

| Function | Signature | Returns | Description |
| --- | --- | --- | --- |
| `lenList` | `(list lst)` | `int` | Number of elements |
| `listFirst` / `listLast` | `(list lst)` | element | First / last element; runtime error if empty |
| `listHead` / `listTail` | `(list lst, int n)` | `list` | First / last `n` elements |
| `listSum` / `listAvg` | `(list lst)` | `float` | Sum / mean of numeric elements |
| `listCount` | `(list lst, any val)` | `int` | Occurrences of `val` |
| `listRepeat` | `(any val, int n)` | `list` | `n` copies of `val` |
| `listZip` | `(list a, list b)` | `list` | JSON pair strings `{"a":"…","b":"…"}` |
| `listChunk` | `(list lst, int n)` | `list` | JSON-encoded chunks of size `n` |
| `flatten` | `(list lst)` | `list` | Flatten one level |
| `unique` | `(list lst)` | `list` | Remove duplicates, order preserved |

## Tuple Functions

These compose the language-level tuple built-ins.

| Function | Signature | Returns | Description |
| --- | --- | --- | --- |
| `tupleReverse` | `(any t)` | `tuple` | Reversed |
| `tupleSort` / `tupleSortDesc` | `(any t)` | `tuple` | Sorted ascending / descending |
| `tupleMin` / `tupleMax` | `(any t)` | `any` | Minimum / maximum (or `none`) |
| `tupleSum` / `tupleMean` | `(any t)` | `float` | Sum / arithmetic mean |
| `tupleAny` / `tupleAll` | `(any t)` | `bool` | Any / all elements truthy |
| `tupleUnique` | `(any t)` | `tuple` | Duplicates removed, order preserved |
| `tupleFlatten` | `(any t)` | `tuple` | Flatten one level |
| `tupleZip` | `(any a, any b)` | `list` | JSON pair strings |
| `tupleJoin` | `(any t, str sep)` | `str` | Join elements with `sep` |

## Sequence Conversion

| Function | Signature | Returns | Description |
| --- | --- | --- | --- |
| `toList` | `(any val)` | `list` | Tuple → list; lists unchanged; other values → `[]` |
| `toTuple` | `(any val)` | `tuple` | List → tuple; tuples unchanged; other values → `()` |
| `lenSequence` | `(any val)` | `int` | Length of a list or tuple; `0` otherwise |

## Example

```lynx
global setup(){ import("typing"); }

global main(){
    any x = 42;
    if(global.typing.isInt(x)){   print("integer\n"); }

    println(global.typing.abs(-4));            // 4
    println(global.typing.clamp(15.0, 0.0, 10.0)); // 10
    println(global.typing.roundTo(3.14159, 2));    // 3.14
    println(global.typing.charCodeOf("A"));    // 65
    println(global.typing.charAt("hello", 1)); // e
    println(global.typing.zfill("42", 5));     // 00042

    list nums = range(6);
    println(global.typing.listHead(nums, 3));  // [0, 1, 2]
    println(global.typing.listChunk(nums, 2)); // [[0, 1], [2, 3], [4, 5]]

    tuple coords = (int 10, int 20);
    println(global.typing.tupleJoin(coords, "-")); // 10-20
    println(global.typing.toList(coords));     // [10, 20]
}
```

---

## See also

- [stdlib-contracts.md](../stdlib-contracts.md) — the contract this module
  implements, including the error sentinel family it uses.
- [builtins.md](../builtins.md) — the functions the interpreter implements
  itself.
- [limitations.md](../limitations.md) — the full divergence register.
