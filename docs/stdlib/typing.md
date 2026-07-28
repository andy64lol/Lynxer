# typing

> **Note on tuples:** The core tuple built-ins (`tupleCreate`, `tupleGet`, `tupleLen`, `tupleContains`, `tupleIndex`, `tupleSlice`, `tupleToList`, `listToTuple`, `tupleConcat`, `tupleCount`, `tupleFirst`, `tupleLast`, `tupleJsonArray`) are always available without any import — they are language built-ins. This module provides higher-level utilities that compose those primitives.

# typing

String, list and simple type utilities.

Main helpers:

- Conversion: `toStr(n)`, `toInt(s)`, `toFloat(s)`, `toBool(n)`.
- Checks: `isNumeric(s)`, `isList(val)`, `isAlpha(s)`, `isDigit(s)`, `isAlphaNum(s)`, `isSpace(s)`.
- String length/list length: `lenStr(s)`, `lenList(lst)`.
- List and string helpers: `toList(s,sep)`, `repeat(s,n)`, `repeatStr(s,n)`, `contains(haystack,needle)`, `indexOf(s,sub)`, `splitFirst(s,sep)`.
- Transformations: `trim(s)`, `upper(s)`, `lower(s)`, `substr(s,start,end)`, `padLeft(s,width,ch)`, `padRight(s,width,ch)`, `strReverse(s)`, `charCode(s)`, `charOf(code)`, `countOccurrences(s,sub)`, `replace(s,old,new)`, `titleCase(s)`, `swapCase(s)`, `center(s,width,ch)`.
- List utilities: `flatten(lst)`, `unique(lst)`.
- Helpers: `spaces(n)`, `wordWrap(s,width)`, `expandTabs(s,tabSize)`.

Notes:
- Many functions use Python fallbacks and return safe defaults on error.

---

## Tuple extra functions

These compose the language-level tuple built-ins. Access via `global.typing.*`.

| Function | Signature | Description |
|----------|-----------|-------------|
| `tupleReverse` | `tupleReverse(any t)` | New tuple with elements in reversed order |
| `tupleSort` | `tupleSort(any t)` | New tuple sorted ascending |
| `tupleSortDesc` | `tupleSortDesc(any t)` | New tuple sorted descending |
| `tupleMin` | `tupleMin(any t)` | Minimum element |
| `tupleMax` | `tupleMax(any t)` | Maximum element |
| `tupleSum` | `tupleSum(any t)` | Sum of all numeric elements |
| `tupleAny` | `tupleAny(any t)` | `true` if any element is truthy |
| `tupleAll` | `tupleAll(any t)` | `true` if all elements are truthy |
| `tupleUnique` | `tupleUnique(any t)` | New tuple with duplicates removed (order preserved) |
| `tupleMean` | `tupleMean(any t)` | Arithmetic mean of numeric elements |
| `tupleFlatten` | `tupleFlatten(any t)` | Concatenate nested tuple elements one level deep |
| `tupleZip` | `tupleZip(any t1, any t2)` | List of JSON pair strings `{"a":v1,"b":v2}` |
| `tupleJoin` | `tupleJoin(any t, str sep)` | All elements joined as a string |

```c
global setup(){
    import("typing");
}

global main(){
    tuple nums = [5, 3, 8, 1, 3];
    tuple rev  = global.typing.tupleReverse(nums);   // (3, 1, 8, 3, 5)
    tuple srt  = global.typing.tupleSort(nums);      // (1, 3, 3, 5, 8)
    any   sm   = global.typing.tupleSum(nums);       // 20
    tuple uniq = global.typing.tupleUnique(nums);    // (5, 3, 8, 1)
    str   j    = global.typing.tupleJoin(nums, ","); // "5,3,8,1,3"
}
```