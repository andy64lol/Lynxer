# Tuples

A tuple is an **immutable, ordered, fixed-length** sequence. `tuple` is a
first-class type keyword: it participates in the same checking, conversion, and
comparison rules as `int`, `str`, or `list`.

Tuples are always available — no import is needed. The `typing` module adds
higher-level helpers that compose the built-ins (see
[typing.md](stdlib/typing.md#tuple-functions)).

## Declaration

Each element uses an explicit type, exactly like a list element:

```lynx
tuple point  = (int 10, int 20);
tuple rgb    = (int 255, int 128, int 0);
tuple mixed  = (str "hello", int 42, bool true);
tuple single = (int 99,);   // single-element tuple — prints as (99,)
tuple empty  = ();          // empty tuple
```

The trailing comma in `(int 99,)` is what distinguishes a single-element tuple
from a parenthesised expression.

You can also build a tuple dynamically with `tupleCreate(...)`, which infers
each element's type from the argument:

```lynx
tuple dyn = tupleCreate(1, "a", true);   // (1, a, true)
tuple none = tupleCreate();              // ()
```

## Type system

```lynx
tuple coords = (int 0, int 0);
println(returnType(coords));    // tuple
println(returnLength(coords));  // 2
println(tupleLen(coords));      // 2
```

- `returnType(t)` is `"tuple"`; `returnLength(t)` and `tupleLen(t)` return the
  element count.
- A `tuple` variable accepts any tuple value regardless of element types; the
  interpreter does not track per-element tuple types.
- Tuples are **immutable**: every operation returns a **new** tuple. No built-in
  mutates a tuple in place.

## Built-in functions

| Function | Signature | Returns | Description |
| --- | --- | --- | --- |
| `tupleCreate` | `(v1, v2, ...)` | `tuple` | Builds a tuple, inferring element types |
| `tupleGet` | `(tuple t, int idx)` | element | Element at `idx`; negative indices count from the end |
| `tupleLen` | `(tuple t)` | `int` | Number of elements |
| `tupleContains` | `(tuple t, any val)` | `bool` | Membership test |
| `contains` | `(tuple t, any val)` | `bool` | Membership test shared with lists |
| `tupleIndex` | `(tuple t, any val)` | `int` | First index of `val`, or `-1` |
| `tupleCount` | `(tuple t, any val)` | `int` | Occurrences of `val` |
| `tupleFirst` / `tupleLast` | `(tuple t)` | element | First / last element (error when empty) |
| `tupleSlice` | `(tuple t, int start, int stop)` | `tuple` | Sub-tuple `[start, stop)` |
| `tupleConcat` | `(tuple a, tuple b)` | `tuple` | Concatenation |
| `tupleReverse` | `(tuple t)` | `tuple` | Elements reversed |
| `tupleSort` / `tupleSortDesc` | `(tuple t)` | `tuple` | Sorted ascending / descending |
| `tupleMin` / `tupleMax` | `(tuple t)` | element | Minimum / maximum element |
| `tupleSum` / `tupleMean` | `(tuple t)` | `float` | Sum / arithmetic mean of numeric elements |
| `tupleAny` / `tupleAll` | `(tuple t)` | `bool` | Any / all elements truthy |
| `tupleUnique` | `(tuple t)` | `tuple` | Duplicates removed, order preserved |
| `tupleFlatten` | `(tuple t)` | `tuple` | One-level flatten of nested tuples |
| `tupleZip` | `(tuple a, tuple b)` | `tuple` | Pairwise JSON strings `{"a": 1, "b": 2}` |
| `tupleJoin` | `(tuple t, str sep)` | `str` | Joins elements with `sep` (via `strOf`) |
| `tupleJsonArray` | `(tuple t)` | `str` | JSON array string, e.g. `"[1, 2]"`
| `tupleToList` | `(tuple t)` | `list` | Converts to a mutable list |
| `listToTuple` | `(list l)` | `tuple` | Converts a list to a tuple |

```lynx
global setup(){}

global main(){
    tuple t = (int 10, int 20, int 30, int 20);

    println(tupleLen(t));            // 4
    println(tupleGet(t, -1));        // 20
    println(tupleSlice(t, 1, 3));    // (20, 30)
    println(tupleConcat(t, tupleCreate(40)));   // (10, 20, 30, 20, 40)
    println(tupleJsonArray(t));      // [10, 20, 30, 20]
    println(tupleJoin(t, "-"));      // 10-20-30-20
    println(tupleToList(t));         // [10, 20, 30, 20]
}
```

## Equality and comparison

`is` (`isnt`) compares tuples structurally — same length and equal elements:

```lynx
println((int 1, int 2) is (int 1, int 2));     // true
println((int 1, int 2) isnt (int 1, int 9));   // true
```

Tuples have no ordering operators; use `tupleToList()` + `sortList()` when you
need a sorted order.

## Iteration

There is no `for ... in` form. Iterate with a C-style `for` over the element
count, or convert to a list first:

```lynx
tuple t = (int 10, int 20, int 30);

for (int i = 0; i < tupleLen(t)) {     // the update `i = i + 1` is implicit
    println(tupleGet(t, i));
}

list values = tupleToList(t);
for (int i = 0; i < returnLength(values)) {
    println(listGet(values, i));
}
```

## Differences from the original Lynxer

- **Bracket literals are not accepted for tuples.** In the original, a `tuple`
  variable could be (re)bound with a `[int 1, int 2]` literal. The standalone
  runtime rejects that with `value cannot be assigned to type 'tuple'`; use the
  `(int 1, int 2)` form, `tupleCreate(...)`, or `listToTuple(...)`.
- **No `rawPy` bridging.** The original exposed tuples to `rawPy { }` blocks as
  Python tuples; there is no Python runtime here.
- **`tupleZip` returns a tuple**, not a list, and renders values as JSON
  (`{"a": 1, "b": 2}`). `typing.tupleZip` returns a list of
  `{"a":"1","b":"2"}` strings instead.

## See also

- [lists.md](lists.md) — `list` and its built-ins, plus the tuple summary table.
- [builtins.md](builtins.md#tuples) — the same built-ins in the built-in index.
- [stdlib/typing.md](stdlib/typing.md#tuple-functions) — tuple helpers in `typing`.
- [legacy-surface.md](legacy-surface.md) — original features not carried over.
