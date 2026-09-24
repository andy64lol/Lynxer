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

## Creating sequences

| Builtin | Result |
|---------|--------|
| `range(n)` | `0 .. n-1` |
| `seqFromTo(start, end)` | the inclusive numeric range |
| `listRepeat(value, n)` | `n` copies of `value` |
| `tupleCreate(a, b, ...)` | a tuple from the arguments |

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
| `listFirst(list)` / `listLast(list)` | first / last element |
| `listMin(list)`, `listMax(list)`, `listAvg(list)` | aggregates |
| `sumOf(list)` | sum of the elements |
| `listToTuple(list)` | convert to a tuple |
| `listJsonArray`, `listJsonObject` | JSON conversion |

The length of a list, tuple, or string is `returnLength(value)`, and
`contains(container, value)` tests membership.

## Tuple builtins

| Builtin | Notes |
|---------|-------|
| `tupleGet(tuple, index)`, `tupleSlice(tuple, start, end)` | access |
| `tupleLen(tuple)`, `tupleCount(tuple, value)` | length / occurrences |
| `tupleContains(tuple, value)`, `tupleIndex(tuple, value)` | membership / position |
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
