# Lists and tuples

Lynxer has two sequence types: `list` (built with `[...]`) and `tuple` (built
with `(...)`). Both use **value semantics** — see below — and share a large
family of builtins.

## Value semantics

A list or tuple is copied on assignment, and **no list builtin mutates its
argument**. The collection builtins return a new value that you assign back:

```lynx
list xs = [1, 2, 3];
xs = listPush(xs, 4);     // listPush returns a new list; assign it back
list ys = xs;             // ys is a copy
xs = listSet(xs, 0, 9);
println(ys);              // [1, 2, 3, 4]  (unchanged)
```

`listPush(xs, 4);` on its own discards the result and leaves `xs` unchanged.
The same is true of `sortList`, `reverseList`, `listExtend`, `listInsert`,
`listRemove`, `listSet` and `listClear`: each returns a new list.

The one exception is `listPop(list)`, which returns the **element** it removed
and leaves the list unchanged:

```lynx
list xs = [1, 2, 3];
println(listPop(xs));   // 3
println(xs);            // [1, 2, 3]
```

## Indexing

Indices are zero-based and may be negative (`-1` is the last element). An index
outside the list is a source-located error, for example
`listGet() index -5 out of range for list of length 3`.

```lynx
list xs = [1, 2, 3];
println(listGet(xs, 0));    // 1
println(listGet(xs, -1));   // 3
println(listSlice(xs, 1, 3));  // [2, 3]  (start inclusive, end exclusive)
```

## Typed element literals

An element may be prefixed with a type, which is validated:

```lynx
list nums = [int 1, int 2];
tuple pair = (int 10, int 20);
```

## Creating Sequences

### Lists

| Builtin | Result |
|---------|--------|
| `range(n)` | `0 .. n-1` |
| `seqFromTo(start, end)` | Inclusive numeric range |
| `seqFromTo(start, end, step)` | Inclusive numeric range with step |
| `listRepeat(value, n)` | `n` copies of `value` |

### Tuples

| Builtin | Result |
|---------|--------|
| `tupleCreate(a, b, ...)` | A tuple from the arguments |

### Conversions

| Builtin | Result |
|---------|--------|
| `listToTuple(list)` | Converts a list to a tuple |
| `tupleToList(tuple)` | Converts a tuple to a list |

## List builtins

| Builtin | Notes |
|---------|-------|
| `listPush(list, value)` | new list with `value` appended |
| `listPop(list)` | returns the **last element**; the list is unchanged |
| `listInsert(list, index, value)` | new list with `value` inserted |
| `listRemove(list, index)` | new list without that element |
| `listClear(list)` | an empty list |
| `listGet(list, index)` / `listSet(list, index, value)` | element access / new list with the element replaced |
| `listSlice(list, start, end)` | sub-list, `start` inclusive and `end` exclusive |
| `listContains(list, value)` | membership |
| `listIndex(list, value)` | first index of `value`, or `-1` |
| `listCount(list, value)` | number of occurrences of `value` |
| `listJoin(list, separator)` | join into a string |
| `listExtend(list, other)` | new list, both concatenated |
| `listZip(list, other)` | pair elements into `{a, b}` records |
| `listFlatten(list)` | flatten one level |
| `listUnique(list)` | de-duplicate |
| `sortList(list)` / `sortList(list, true)` | ascending / descending copy |
| `reverseList(list)` | reversed copy |
| `listHead(list, count)` / `listTail(list, count)` | first / last `count` elements |
| `listFirst(list)` / `listLast(list)` | First / last element |
| `listHead(list, count)` / `listTail(list, count)` | First / last `count` elements |

### Tuple Builtins

| Builtin | Notes |
|---------|-------|
| `tupleGet(tuple, index)` / `tupleSlice(tuple, start, end)` | Access elements or sub-tuples |
| `tupleLen(tuple)` / `tupleCount(tuple, value)` | Length / occurrences of a value |
| `tupleContains(tuple, value)` / `tupleIndex(tuple, value)` | Membership / position of a value |
| `tupleConcat(a, b)` / `tupleJoin(a, b)` / `tupleZip(a, b)` | Combine tuples |
| `tupleFlatten(tuple)` / `tupleReverse(tuple)` / `tupleUnique(tuple)` | Reshape tuples |
| `tupleSort(tuple)` / `tupleSortDesc(tuple)` | Sort ascending / descending |
| `tupleAll(tuple)` / `tupleAny(tuple)` | Predicates for truthiness |
| `tupleMin(tuple)` / `tupleMax(tuple)` / `tupleSum(tuple)` | Aggregates |
| `tupleFirst(tuple)` / `tupleLast(tuple)` | First / last element |
| `tupleToList(tuple)` | Converts a tuple to a list |
| `tupleJsonArray(tuple)` | Encodes a tuple as a JSON array string |
| `tupleMean(tuple)` | Arithmetic mean of numeric elements |
| `tupleZip(tuple1, tuple2)` | List of JSON pair strings `{"a":v1,"b":v2}` |

### Notes
- **Tuples are immutable**: Operations like `tupleConcat` and `tupleSlice` return a **new** tuple.
- **Iteration**: Use `tupleToList()` to iterate over a tuple with a `for` loop or index manually.
- **Conversions**: Use `listToTuple()` and `tupleToList()` to convert between lists and tuples.

The length of a list, tuple, or string is `returnLength(value)`, and
`contains(container, value)` tests membership for both lists and tuples.

## Tuple Builtins

| Builtin | Notes |
|---------|-------|
| `tupleGet(tuple, index)` / `tupleSlice(tuple, start, end)` | Access elements or sub-tuples |
| `tupleLen(tuple)` / `tupleCount(tuple, value)` | Length / occurrences of a value |
| `tupleContains(tuple, value)` / `tupleIndex(tuple, value)` | Membership / position of a value |
| `tupleConcat(a, b)` / `tupleJoin(a, b)` / `tupleZip(a, b)` | Combine tuples |
| `tupleFlatten(tuple)` / `tupleReverse(tuple)` / `tupleUnique(tuple)` | Reshape tuples |
| `tupleSort(tuple)` / `tupleSortDesc(tuple)` | Sort ascending / descending |
| `tupleAll(tuple)` / `tupleAny(tuple)` | Predicates for truthiness |
| `tupleMin(tuple)` / `tupleMax(tuple)` / `tupleSum(tuple)` | Aggregates |
| `tupleFirst(tuple)` / `tupleLast(tuple)` | First / last element |
| `tupleToList(tuple)` | Converts a tuple to a list |
| `tupleJsonArray(tuple)` | Encodes a tuple as a JSON array string |
| `tupleMean(tuple)` | Arithmetic mean of numeric elements |

### Typing Module Functions

The `typing` module provides additional tuple functions:

| Function | Description |
|----------|-------------|
| `tupleReverse(t)` | New tuple with elements in reversed order |
| `tupleSort(t)` | New tuple sorted ascending |
| `tupleSortDesc(t)` | New tuple sorted descending |
| `tupleMin(t)` | Minimum element |
| `tupleMax(t)` | Maximum element |
| `tupleSum(t)` | Sum of all numeric elements |
| `tupleAny(t)` | `true` if any element is truthy |
| `tupleAll(t)` | `true` if all elements are truthy |
| `tupleUnique(t)` | New tuple with duplicates removed (order preserved) |
| `tupleMean(t)` | Arithmetic mean of numeric elements |
| `tupleFlatten(t)` | One-level flatten: concatenate nested tuple elements |
| `tupleZip(t1, t2)` | List of JSON pair strings `{"a":v1,"b":v2}` |
| `tupleJoin(t, sep)` | All elements joined as a string with separator |

*Note:* These functions require importing the `typing` module (`import("typing")`).

| Group | Contains |
|-------|----------|
| `tupleConcat(a, b)`, `tupleJoin(a, b)`, `tupleZip(a, b)` | combination |
| `tupleFlatten`, `tupleReverse`, `tupleUnique` | reshaping |
| `tupleSort`, `tupleSortDesc` | ordering |
| `tupleAll`, `tupleAny` | predicates |
| `tupleMin`, `tupleMax`, `tupleSum`, `tupleMean` | aggregates |
| `tupleFirst`, `tupleLast`, `tupleToList`, `tupleCreate` | ends / conversion |
| `tupleJsonArray` | JSON conversion |

There is no `tupleSet`; use a `list` when you need to replace elements.

## Example

```lynx
global setup(){}

global main(){
    list xs = [1, 2, 3];
    xs = listPush(xs, 4);
    println(xs);                 // [1, 2, 3, 4]
    println(listGet(xs, -1));    // 4
    println(contains(xs, 9));    // false

    list ys = xs;                // a copy
    xs = listSet(xs, 0, 9);
    println(ys);                 // [1, 2, 3, 4]

    tuple pair = (10, 20);
    println(tupleGet(pair, 1));  // 20
    println(returnLength(pair)); // 2
}
```

See [builtins.md](builtins.md#sequences) for the full builtin reference and
[types.md](types.md) for how `list` and `tuple` behave as declared types.
