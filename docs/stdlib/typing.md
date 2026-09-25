# typing

Type conversion, numeric-range predicates, and sequence utilities.

**Backend:** pure — `stdlib/typing.lynx` only, over the interpreter's type
builtins. **Import:** `import("typing")` → `global.typing.*`

## Type Checks

| Function | Signature | Returns | Description |
| --- | --- | --- | --- |
| `isNum` | `isNum(any val)` | `bool` | `true` if `val` is `int` or `float` (any numeric) |
| `isInt` | `isInt(any val)` | `bool` | `true` if `val` is an `int` |
| `isFloat` | `isFloat(any val)` | `bool` | `true` if `val` is a `float` |
| `isStr` | `isStr(any val)` | `bool` | `true` if `val` is a `str` |
| `isBool` | `isBool(any val)` | `bool` | `true` if `val` is a `bool` |
| `isList` | `isList(any val)` | `bool` | `true` if `val` is a `list` |
| `isTuple` | `isTuple(any val)` | `bool` | `true` if `val` is a `tuple` |
| `isChar` | `isChar(any val)` | `bool` | `true` if `val` is a `char` |
| `isNone` | `isNone(any val)` | `bool` | `true` if `val` is `none` |
| `isNumeric` | `isNumeric(any value)` | `bool` | `true` if `value` is a number (`int` or `float`) |
| `isSequence` | `isSequence(any val)` | `bool` | `true` if `val` is a `list` or `tuple` |

## Type Conversions

| Function | Signature | Returns | Description |
| --- | --- | --- | --- |
| `typeOf` | `(any value) -> str` | `str` | `returnType` of the value |
| `toStr` | `(any value) -> str` | `str` | Renders as text |
| `toInt` | `(any value) -> int` | `int` | Converts to an integer |
| `toFloat` | `(any value) -> float` | `float` | Converts to a float |
| `toBool` | `(any value) -> bool` | `bool` | `value != 0` |
| `toNumBool` | `(any value) -> numBool` | `numBool` | Normalizes to `0` or `1` |
| `toBit` | `(any value) -> bit` | `bit` | Normalizes to `0` or `1` |
| `toByte` | `(any value) -> byte` | `byte` | Converts and clamps to `0..255` |
| `toChar` | `(any val) -> char` | `char` | Converts to a character |
| `toString` | `(any val) -> str` | `str` | Converts any value to its display string |
| `toNumber` | `(any val) -> float` | `float` | Converts a number-like value to a float; `0.0` on error |

## String Functions

| Function | Signature | Returns | Description |
| --- | --- | --- | --- |
| `lenStr` | `(str value) -> int` | `int` | Length of `value` in characters |

## List Functions

These complement the language built-ins (`listPush`, `listGet`, `sortList`, etc.).

| Function | Signature | Returns | Description |
| --- | --- | --- | --- |
| `lenList` | `(list lst) -> int` | `int` | Number of elements (alias for `returnLength`) |
| `listFirst` | `(list lst) -> element` | `element` | First element; runtime error if empty |
| `listLast` | `(list lst) -> element` | `element` | Last element; runtime error if empty |
| `listHead` | `(list lst, int n) -> list` | `list` | First `n` elements |
| `listTail` | `(list lst, int n) -> list` | `list` | Last `n` elements |
| `listSum` | `(list lst) -> float` | `float` | Sum of all numeric elements (alias for `sumOf`) |
| `listAvg` | `(list lst) -> float` | `float` | Average (mean) of all numeric elements |
| `listCount` | `(list lst, any val) -> int` | `int` | Count occurrences of `val` in `lst` |
| `listRepeat` | `(any val, int n) -> list` | `list` | New list of `n` copies of `val` |
| `listZip` | `(list lst1, list lst2) -> list` | `list` | Zip two lists into JSON pair strings `{"a":v1,"b":v2}` |
| `flatten` | `(list lst) -> list` | `list` | Flatten one level of nested lists |
| `unique` | `(list lst) -> list` | `list` | Remove duplicates (order preserved) |

## Tuple Functions

These compose the language-level tuple built-ins. Access via `global.typing.*`.

| Function | Signature | Returns | Description |
| --- | --- | --- | --- |
| `tupleReverse` | `(any t) -> tuple` | `tuple` | New tuple with elements in reversed order |
| `tupleSort` | `(any t) -> tuple` | `tuple` | New tuple sorted ascending |
| `tupleSortDesc` | `(any t) -> tuple` | `tuple` | New tuple sorted descending |
| `tupleMin` | `(any t) -> any` | `any` | Minimum element |
| `tupleMax` | `(any t) -> any` | `any` | Maximum element |
| `tupleSum` | `(any t) -> float` | `float` | Sum of all numeric elements |
| `tupleAny` | `(any t) -> bool` | `bool` | `true` if any element is truthy |
| `tupleAll` | `(any t) -> bool` | `bool` | `true` if all elements are truthy |
| `tupleUnique` | `(any t) -> tuple` | `tuple` | New tuple with duplicates removed (order preserved) |
| `tupleMean` | `(any t) -> float` | `float` | Arithmetic mean of numeric elements |
| `tupleFlatten` | `(any t) -> tuple` | `tuple` | Concatenate nested tuple elements one level deep |
| `tupleZip` | `(any t1, any t2) -> list` | `list` | List of JSON pair strings `{"a":v1,"b":v2}` |
| `tupleJoin` | `(any t, str sep) -> str` | `str` | All elements joined as a string with separator |

## Sequence Conversion

| Function | Signature | Returns | Description |
| --- | --- | --- | --- |
| `toList` | `(any val) -> list` | `list` | Convert a tuple to a list; return lists unchanged; unsupported values become an empty list |
| `toTuple` | `(any val) -> tuple` | `tuple` | Convert a list to a tuple; return tuples unchanged; unsupported values become an empty tuple |
| `lenSequence` | `(any val) -> int` | `int` | Return the length of a list or tuple; return `0` for other values |

## Example

```lynx
global setup(){ import("typing"); }

global main(){
    // Type checks
    any x = 42;
    if(global.typing.isInt(x)){   print("integer\n"); }

    // Type conversions
    str s = global.typing.toStr(42);
    int i = global.typing.toInt("42");
    float f = global.typing.toFloat("3.14");

    // List functions
    list nums = range(6);   // [0,1,2,3,4,5]
    any first = global.typing.listFirst(nums);          // 0
    any last = global.typing.listLast(nums);           // 5
    list h = global.typing.listHead(nums, 3);        // [0,1,2]
    list t = global.typing.listTail(nums, 3);        // [3,4,5]
    float avg = global.typing.listAvg(nums);            // 2.5
    list rep = global.typing.listRepeat(0, 4);         // [0,0,0,0]

    // Tuple functions
    tuple numsTuple = (int 5, int 3, int 8, int 1, int 3);
    tuple rev = global.typing.tupleReverse(numsTuple);   // (3, 1, 8, 3, 5)
    tuple srt = global.typing.tupleSort(numsTuple);      // (1, 3, 3, 5, 8)
    any sm = global.typing.tupleSum(numsTuple);       // 20
    tuple uniq = global.typing.tupleUnique(numsTuple);    // (5, 3, 8, 1)
    str j = global.typing.tupleJoin(numsTuple, ","); // "5,3,8,1,3"

    // Sequence conversion
    list a = range(3);
    list b = range(3, 6, 1);
    list z = global.typing.listZip(a, b);  // ['{"a":"0","b":"3"}', ...]
    tuple convertedTuple = global.typing.toTuple(a); // (0, 1, 2)
    list convertedList = global.typing.toList(numsTuple);  // [5, 3, 8, 1, 3]
}
```

## Example

```lynx
global setup(){ import("typing"); }

global main(){
    println(global.typing.toInt("42"));
    println(global.typing.isByte(200));
    println(global.typing.isInt8(200));
}
```

---

## See also

- [stdlib-contracts.md](../stdlib-contracts.md) — the contract this module
  implements, including the error sentinel family it uses.
- [builtins.md](../builtins.md) — the functions the interpreter implements
  itself.
- [limitations.md](../limitations.md) — the full divergence register.
