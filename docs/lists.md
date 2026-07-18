# Lists

Lists hold ordered sequences of values. They are created with `seqFromTo()` or built up manually using `listPush()`.

---

## Creating lists

```c
// From a range (like Python's range())
any nums = seqFromTo(0, 5, 1);   // [0, 1, 2, 3, 4]
any evens = seqFromTo(0, 10, 2); // [0, 2, 4, 6, 8]
any down = seqFromTo(5, 0, -1);  // [5, 4, 3, 2, 1]

// Build an empty list then push values
any items = seqFromTo(0, 0, 1);  // []
items = listPush(items, 10);
items = listPush(items, 20);
items = listPush(items, 30);
// items is now [10, 20, 30]
```

List variables must use the `any` type — there is no dedicated `list` keyword yet.

> **Important:** `listPush` and `listSet` return a **new** list. Always reassign:
> ```c
> lst = listPush(lst, val);   // ✓ correct
> listPush(lst, val);          // ✗ original is unchanged
> ```

---

## Reading elements

```c
any lst = seqFromTo(0, 5, 1);   // [0, 1, 2, 3, 4]

int first = listGet(lst, 0);    // 0
int last  = listGet(lst, 4);    // 4
int len   = returnLength(lst);  // 5
```

Negative indices count from the end: `listGet(lst, -1)` returns the last element.

---

## Modifying lists

| Function | Effect |
|----------|--------|
| `listPush(lst, val)` | Return new list with `val` appended |
| `listPop(lst)` | Return the last element (does not shrink original) |
| `listSet(lst, idx, val)` | Return new list with element at `idx` replaced |
| `listRemove(lst, idx)` | Return new list with element at `idx` removed |

```c
any lst = seqFromTo(1, 4, 1);    // [1, 2, 3]

lst = listPush(lst, 99);          // [1, 2, 3, 99]
int last = listPop(lst);          // last = 99
lst = listSet(lst, 0, 100);       // [100, 2, 3, 99]
lst = listRemove(lst, 1);         // [100, 3, 99]
```

---

## Searching

```c
any lst = seqFromTo(10, 15, 1); // [10, 11, 12, 13, 14]

int idx   = listIndex(lst, 12);    // 2  (-1 if not found)
bool has  = listContains(lst, 13); // true
```

---

## Slicing

`listSlice(lst, start, stop)` returns a new list with elements from index `start` up to (not including) `stop`.

```c
any lst = seqFromTo(0, 6, 1);     // [0, 1, 2, 3, 4, 5]
any mid = listSlice(lst, 2, 5);   // [2, 3, 4]
```

---

## Aggregates

```c
any nums = seqFromTo(1, 6, 1);   // [1, 2, 3, 4, 5]

int total = sumOf(nums);          // 15
int mn    = listMin(nums);        // 1
int mx    = listMax(nums);        // 5
bool any_ = anyOf(nums);          // true — at least one truthy
bool all_ = allOf(nums);          // true — all truthy
```

---

## Sorting and reversing

`sortList` returns a new sorted list (ascending by default). Pass `true` as a second argument to sort descending.

```c
any lst = seqFromTo(0, 0, 1);
lst = listPush(lst, 3);
lst = listPush(lst, 1);
lst = listPush(lst, 4);
lst = listPush(lst, 1);

any asc  = sortList(lst);          // [1, 1, 3, 4]
any desc = sortList(lst, true);    // [4, 3, 1, 1]
any rev  = reverseList(lst);       // [1, 4, 1, 3] (original order reversed)
```

---

## Converting lists to strings

`listJoin(lst, sep)` concatenates all elements with a separator.

```c
any words = seqFromTo(0, 0, 1);
words = listPush(words, "a");
words = listPush(words, "b");
words = listPush(words, "c");

str joined = listJoin(words, ", ");  // "a, b, c"
str csv    = listJoin(words, ",");   // "a,b,c"
```

---

## Printing a list

```c
any lst = seqFromTo(1, 4, 1);
print(strOf(lst)); print("\n");    // [1, 2, 3]
```

---

## typing stdlib helpers for lists

The `typing` module adds a few extra list utilities:

```c
void setup(){ import("typing"); }

void main(){
    // Split a string into a list
    any parts = global.typing.toList("a,b,c", ",");
    print(strOf(parts)); print("\n");   // [a, b, c]

    // Check if a value is a list
    print(global.typing.isList(parts)); print("\n");   // true
    print(global.typing.isList(42));    print("\n");   // false

    // Remove duplicate values
    any lst = seqFromTo(0, 0, 1);
    lst = listPush(lst, 1); lst = listPush(lst, 2);
    lst = listPush(lst, 1); lst = listPush(lst, 3);
    any u = global.typing.unique(lst);  // [1, 2, 3]
    print(strOf(u)); print("\n");

    // Flatten one level of nested lists
    any inner = seqFromTo(4, 6, 1);   // [4, 5]
    any outer = seqFromTo(1, 3, 1);   // [1, 2]
    outer = listPush(outer, inner);
    any flat = global.typing.flatten(outer); // [1, 2, 4, 5]
    print(strOf(flat)); print("\n");
}
```
