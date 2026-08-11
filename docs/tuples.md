# Tuples

Tuples are immutable, ordered, fixed-length sequences. Once created, their elements cannot be modified. They are declared with the `tuple` type keyword.

---

## Declaration

```c
tuple point   = (int 10, int 20);
tuple rgb     = (int 255, int 128, int 0);
tuple mixed   = (str "hello", int 42, bool true);
tuple single  = (int 99,);          // single-element tuple — prints as (99,)
tuple empty   = ();                 // empty tuple
```

Each tuple element uses an explicit type, just like list elements. Tuple literals
use `(...)`; the older typed `[...]` form is still accepted and converted to a
tuple when the variable is declared as `tuple`, but it is deprecated and should
not be used in new code. You can also build a tuple with `tupleCreate()` or
convert an existing list with `listToTuple()`.

---

## Type system

`tuple` is a first-class type keyword. It participates in the same type-checking as `int`, `str`, `bool`, and `any`.

```c
tuple coords = (int 0, int 0);
coords = [int 1, int 2];              // legacy-compatible rebinding
coords = [int 1, int 2, int 3];       // legacy-compatible rebinding

// returnType() returns "tuple"
str t = returnType(coords);   // "tuple"

// returnLength() works on tuples
int n = returnLength(coords); // 3
```

Tuples are **immutable** in the sense that no built-in mutates an existing tuple; operations like `tupleConcat` and `tupleSlice` always return a **new** tuple.

---

## Built-in tuple functions

All of these are available everywhere without any `import`.

| Function | Signature | Returns | Description |
|----------|-----------|---------|-------------|
| `tupleCreate` | `tupleCreate(v1, v2, ...)` | `tuple` | Create a tuple from any number of arguments |
| `tupleGet` | `tupleGet(tuple t, int idx)` | element | Get element at `idx` (negative indices supported) |
| `tupleLen` | `tupleLen(tuple t)` | `int` | Number of elements |
| `tupleContains` | `tupleContains(tuple t, any val)` | `bool` | `true` if `val` is in the tuple |
| `tupleIndex` | `tupleIndex(tuple t, any val)` | `int` | First index of `val`, or `-1` |
| `tupleSlice` | `tupleSlice(tuple t, int start, int stop)` | `tuple` | Sub-tuple `[start, stop)` |
| `tupleToList` | `tupleToList(tuple t)` | `list` | Convert to a mutable list |
| `listToTuple` | `listToTuple(list l)` | `tuple` | Convert a list to a tuple |
| `tupleConcat` | `tupleConcat(tuple t1, tuple t2)` | `tuple` | Concatenate two tuples |
| `tupleCount` | `tupleCount(tuple t, any val)` | `int` | Count occurrences of `val` |
| `tupleFirst` | `tupleFirst(tuple t)` | element | First element (error on empty) |
| `tupleLast` | `tupleLast(tuple t)` | element | Last element (error on empty) |
| `tupleJsonArray` | `tupleJsonArray(tuple t)` | `str` | JSON array string, e.g. `"[1,2,3]"` |

### Examples

```c
global main(){
    tuple t = (int 10, int 20, int 30, int 20);

    int  len  = tupleLen(t);           // 4
    any  elem = tupleGet(t, 0);        // 10
    any  last = tupleLast(t);          // 20
    bool has  = tupleContains(t, 20);  // true
    int  idx  = tupleIndex(t, 30);     // 2
    int  cnt  = tupleCount(t, 20);     // 2

    tuple sl  = tupleSlice(t, 1, 3);   // (20, 30)
    tuple cat = tupleConcat(t, sl);    // (10, 20, 30, 20, 20, 30)

    list  lst = tupleToList(t);        // [10, 20, 30, 20]
    tuple t2  = listToTuple(lst);      // (10, 20, 30, 20)

    str   j   = tupleJsonArray(t);     // "[10, 20, 30, 20]"

    // Build dynamically
    tuple dyn = tupleCreate(1, "a", true);   // (1, a, true)

    println(strOf(len));
    println(strOf(elem));
    println(strOf(sl));
}
```

---

## Extra tuple functions — `typing` module

After `import("typing")` these additional functions become available via `global.typing.*`:

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

### Example

```c
global setup(){
    import("typing");
}

global main(){
    tuple nums = (int 5, int 3, int 8, int 1, int 3);

    tuple rev  = global.typing.tupleReverse(nums);  // (3, 1, 8, 3, 5)
    tuple srt  = global.typing.tupleSort(nums);     // (1, 3, 3, 5, 8)
    any   mn   = global.typing.tupleMin(nums);      // 1
    any   mx   = global.typing.tupleMax(nums);      // 8
    any   sm   = global.typing.tupleSum(nums);      // 20
    tuple uniq = global.typing.tupleUnique(nums);   // (5, 3, 8, 1)
    str   j    = global.typing.tupleJoin(nums, "-");// "5-3-8-1-3"

    tuple a = (int 1, int 2, int 3);
    tuple b = (int 4, int 5, int 6);
    any   z = global.typing.tupleZip(a, b);
    // z is a list: ['{"a":"1","b":"4"}', '{"a":"2","b":"5"}', '{"a":"3","b":"6"}']
}
```

---

## Comparison

Tuples support `is` / `not is` equality:

```c
tuple a = (int 1, int 2, int 3);
tuple b = (int 1, int 2, int 3);
tuple c = (int 1, int 2, int 9);

if(a is b){  println("equal");   }   // equal
if(a not is c){ println("different"); }
```

---

## Iteration

Use `tupleToList()` to iterate over a tuple with a `for` loop, or index manually:

```c
global main(){
    tuple t = (int 10, int 20, int 30);

    // Manual index loop
    int i = 0;
    while(i < tupleLen(t)){
        println(strOf(tupleGet(t, i)));
        i = i + 1;
    }

    // Or via list conversion + iterate
    list lst = tupleToList(t);
    for(any v : lst){
        println(strOf(v));
    }
}
```

---

## rawPy access

Tuple variables are exposed to `rawPy {}` blocks as Python tuples of their primitive values:

```c
global main(){
    tuple rgb = (int 255, int 128, int 0);
    rawPy(){
        r, g, b = rgb     // Python tuple unpacking
        print(f"Red={r}, Green={g}, Blue={b}")
    }
}
```
