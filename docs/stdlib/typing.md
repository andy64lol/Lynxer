# typing

> **Note on tuples:** The core tuple built-ins (`tupleCreate`, `tupleGet`, `tupleLen`, `tupleContains`, `tupleIndex`, `tupleSlice`, `tupleToList`, `listToTuple`, `tupleConcat`, `tupleCount`, `tupleFirst`, `tupleLast`, `tupleJsonArray`) are always available without any import — they are language built-ins. This module provides higher-level utilities that compose those primitives.

String, list, number, char, and type-check utilities.

```c
global setup(){
    import("typing");
}
```

---

## Type checks

| Function | Signature | Returns | Description |
|----------|-----------|---------|-------------|
| `isNum` | `isNum(any val)` | `bool` | `true` if `val` is `int` or `float` (any numeric) |
| `isInt` | `isInt(any val)` | `bool` | `true` if `val` is an `int` |
| `isFloat` | `isFloat(any val)` | `bool` | `true` if `val` is a `float` |
| `isStr` | `isStr(any val)` | `bool` | `true` if `val` is a `str` |
| `isBool` | `isBool(any val)` | `bool` | `true` if `val` is a `bool` |
| `isList` | `isList(any val)` | `bool` | `true` if `val` is a `list` |
| `isTuple` | `isTuple(any val)` | `bool` | `true` if `val` is a `tuple` |
| `isChar` | `isChar(any val)` | `bool` | `true` if `val` is a `char` |
| `isNone` | `isNone(any val)` | `bool` | `true` if `val` is `none` |
| `isNumeric` | `isNumeric(str s)` | `bool` | `true` if the string parses as a number |
| `isAlpha` | `isAlpha(str s)` | `bool` | `true` if every character is a letter |
| `isDigit` | `isDigit(str s)` | `bool` | `true` if every character is a digit |
| `isAlphaNum` | `isAlphaNum(str s)` | `bool` | `true` if every character is alphanumeric |
| `isSpace` | `isSpace(str s)` | `bool` | `true` if every character is whitespace |
| `isSequence` | `isSequence(any val)` | `bool` | `true` if `val` is a `list` or `tuple` |

```c
global main(){
    any x = 42;
    if(global.typing.isInt(x)){   print("integer\n"); }

    char c = 'A';
    if(global.typing.isChar(c)){  print("char\n");    }

    any z = none;
    if(global.typing.isNone(z)){  print("none\n");    }
}
```

---

## Conversions

| Function | Signature | Returns | Description |
|----------|-----------|---------|-------------|
| `toStr` | `toStr(int n)` | `str` | Integer to string |
| `toInt` | `toInt(str s)` | `int` | String to int (0 on error) |
| `toFloat` | `toFloat(str s)` | `float` | String to float (0.0 on error) |
| `toBool` | `toBool(int n)` | `bool` | Non-zero → `true`, zero → `false` |
| `toChar` | `toChar(any val)` | `char` | String of length 1 or int code-point → `char`; `"\0"` on error |
| `toString` | `toString(any val)` | `str` | Convert any Lynxer value to its display string |
| `toNumber` | `toNumber(any val)` | `float` | Convert a number-like value to a float; `0.0` on error |

`toList()` and `toTuple()` provide type-aware sequence conversion:

| Function | Signature | Returns | Description |
|----------|-----------|---------|-------------|
| `toList` | `toList(any val)` | `list` | Convert a tuple to a list; return lists unchanged; unsupported values become an empty list |
| `toTuple` | `toTuple(any val)` | `tuple` | Convert a list to a tuple; return tuples unchanged; unsupported values become an empty tuple |
| `lenSequence` | `lenSequence(any val)` | `int` | Return the length of a list or tuple; return `0` for other values |

```c
global setup(){
    import("typing");
}

global main(){
    list values = range(3);
    tuple coords = (int 10, int 20);

    tuple convertedTuple = global.typing.toTuple(values); // (0, 1, 2)
    list  convertedList  = global.typing.toList(coords);  // [10, 20]
    bool  sequence       = global.typing.isSequence(coords); // true
    int   size           = global.typing.lenSequence(coords); // 2
}
```

---

## Char functions

| Function | Signature | Returns | Description |
|----------|-----------|---------|-------------|
| `charCodeOf` | `charCodeOf(any val)` | `int` | Unicode code point of a `char` or first character of a `str`; `-1` on error |
| `charAt` | `charAt(str s, int idx)` | `char` \| `none` | `char` at position `idx` in `s`; `none` if out of range |

```c
global main(){
    char c = 'A';
    int code = global.typing.charCodeOf(c);   // 65

    any got = global.typing.charAt("hello", 1);  // 'e'

    char fromCode = global.typing.toChar(90);    // 'Z'
}
```

---

## String functions

| Function | Signature | Returns | Description |
|----------|-----------|---------|-------------|
| `lenStr` | `lenStr(str s)` | `int` | Length of `s` in characters |
| `trim` | `trim(str s)` | `str` | Strip leading and trailing whitespace |
| `stripLeft` | `stripLeft(str s)` | `str` | Strip leading whitespace |
| `stripRight` | `stripRight(str s)` | `str` | Strip trailing whitespace |
| `upper` | `upper(str s)` | `str` | Convert to uppercase |
| `lower` | `lower(str s)` | `str` | Convert to lowercase |
| `titleCase` | `titleCase(str s)` | `str` | Capitalise first letter of each word |
| `swapCase` | `swapCase(str s)` | `str` | Swap upper↔lower for every character |
| `repeat` | `repeat(str s, int n)` | `str` | Repeat `s` exactly `n` times |
| `repeatStr` | `repeatStr(str s, int n)` | `str` | Same as `repeat` |
| `contains` | `contains(str haystack, str needle)` | `bool` | String membership helper inside `global.typing`; direct `contains(list_or_tuple, value)` is a built-in |
| `startsWith` | `startsWith(str s, str prefix)` | `bool` | `true` if `s` starts with `prefix` |
| `endsWith` | `endsWith(str s, str suffix)` | `bool` | `true` if `s` ends with `suffix` |
| `replace` | `replace(str s, str old, str new)` | `str` | Replace all occurrences of `old` with `new` |
| `indexOf` | `indexOf(str s, str sub)` | `int` | First index of `sub`, or `-1` |
| `countOccurrences` | `countOccurrences(str s, str sub)` | `int` | Non-overlapping count of `sub` in `s` |
| `substr` | `substr(str s, int start, int end)` | `str` | Slice `s[start:end]` (negative indices ok) |
| `padLeft` | `padLeft(str s, int width, str ch)` | `str` | Right-justify in field of `width`, pad with `ch` |
| `padRight` | `padRight(str s, int width, str ch)` | `str` | Left-justify in field of `width`, pad with `ch` |
| `center` | `center(str s, int width, str ch)` | `str` | Centre in field of `width`, pad with `ch` |
| `zfill` | `zfill(str s, int width)` | `str` | Zero-pad `s` on the left to `width` characters |
| `strReverse` | `strReverse(str s)` | `str` | Reverse the characters of `s` |
| `charCode` | `charCode(str s)` | `int` | Unicode code point of first character, or `-1` |
| `charOf` | `charOf(int code)` | `str` | Character for Unicode code point `code` |
| `spaces` | `spaces(int n)` | `str` | String of `n` space characters |
| `wordWrap` | `wordWrap(str s, int width)` | `str` | Wrap text to at most `width` characters per line |
| `expandTabs` | `expandTabs(str s, int tabSize)` | `str` | Expand tab characters to `tabSize`-width stops |
| `splitFirst` | `splitFirst(str s, str sep)` | `list` | Split on first occurrence of `sep`; returns 2-element list |
| `splitToList` | `splitToList(str s, str sep)` | `list` | Split `s` by `sep` into a list of strings |
| `linesOf` | `linesOf(str s)` | `list` | Split `s` by newlines into a list of lines |

---

## Number functions

| Function | Signature | Returns | Description |
|----------|-----------|---------|-------------|
| `abs` | `abs(num n)` | `num` | Absolute value |
| `sign` | `sign(num n)` | `int` | `1` if `n > 0`, `-1` if `n < 0`, `0` if `n == 0` |
| `maxOf` | `maxOf(num a, num b)` | `num` | Larger of `a` and `b` |
| `minOf` | `minOf(num a, num b)` | `num` | Smaller of `a` and `b` |
| `clamp` | `clamp(float n, float lo, float hi)` | `float` | Clamp `n` to the range `[lo, hi]` |
| `between` | `between(num n, num lo, num hi)` | `bool` | `true` if `lo <= n <= hi` |
| `roundTo` | `roundTo(float n, int digits)` | `float` | Round `n` to `digits` decimal places |

```c
global main(){
    float a = global.typing.abs(-4.5);          // 4.5
    int   s = global.typing.sign(-9);            // -1
    num   m = global.typing.maxOf(3, 7);         // 7
    bool  b = global.typing.between(5, 1, 10);   // true
    float r = global.typing.roundTo(3.14159, 2); // 3.14
    float c = global.typing.clamp(15.0, 0.0, 10.0); // 10.0
}
```

---

## List functions

These complement the language built-ins (`listPush`, `listGet`, `sortList`, etc.).

| Function | Signature | Returns | Description |
|----------|-----------|---------|-------------|
| `lenList` | `lenList(list lst)` | `int` | Number of elements (alias for `returnLength`) |
| `listFirst` | `listFirst(list lst)` | element | First element; runtime error if empty |
| `listLast` | `listLast(list lst)` | element | Last element; runtime error if empty |
| `listHead` | `listHead(list lst, int n)` | `list` | First `n` elements |
| `listTail` | `listTail(list lst, int n)` | `list` | Last `n` elements |
| `listSum` | `listSum(list lst)` | `float` | Sum of all numeric elements (alias for `sumOf`) |
| `listAvg` | `listAvg(list lst)` | `float` | Average (mean) of all numeric elements |
| `listCount` | `listCount(list lst, any val)` | `int` | Count occurrences of `val` in `lst` |
| `listRepeat` | `listRepeat(any val, int n)` | `list` | New list of `n` copies of `val` |
| `listZip` | `listZip(list lst1, list lst2)` | `list` | Zip two lists into JSON pair strings `{"a":v1,"b":v2}` |
| `listChunk` | `listChunk(list lst, int n)` | `list` | Split into chunks of size `n`; each chunk is a JSON-encoded string |
| `flatten` | `flatten(list lst)` | `list` | Flatten one level of nested lists |
| `unique` | `unique(list lst)` | `list` | Remove duplicates (order preserved) |

```c
global main(){
    list nums = range(6);   // [0,1,2,3,4,5]

    any   first = global.typing.listFirst(nums);          // 0
    any   last  = global.typing.listLast(nums);           // 5
    list  h     = global.typing.listHead(nums, 3);        // [0,1,2]
    list  t     = global.typing.listTail(nums, 3);        // [3,4,5]
    float avg   = global.typing.listAvg(nums);            // 2.5
    list  rep   = global.typing.listRepeat(0, 4);         // [0,0,0,0]

    list a = range(3);
    list b = range(3, 6, 1);
    list z = global.typing.listZip(a, b);  // ['{"a":"0","b":"3"}',…]
}
```

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
| `tupleJoin` | `tupleJoin(any t, str sep)` | All elements joined as a string with separator |

```c
global setup(){
    import("typing");
}

global main(){
    tuple nums = (int 5, int 3, int 8, int 1, int 3);
    tuple rev  = global.typing.tupleReverse(nums);   // (3, 1, 8, 3, 5)
    tuple srt  = global.typing.tupleSort(nums);      // (1, 3, 3, 5, 8)
    any   sm   = global.typing.tupleSum(nums);       // 20
    tuple uniq = global.typing.tupleUnique(nums);    // (5, 3, 8, 1)
    str   j    = global.typing.tupleJoin(nums, ","); // "5,3,8,1,3"
}
```
