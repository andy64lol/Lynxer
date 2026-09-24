# Lists

See [Built-in Functions — List operations](builtins.md#list-operations) for the full list API.

Lists are first-class values created with typed list literals, `range()`, `seqFromTo()`, or built up with `listPush()`. They use **value semantics** — mutating built-ins like `listPush`, `listSet`, and `listRemove` return a **new** list. Always reassign the result:

List literals declare the type of every element directly before its value:

```c
list nums = [int 1, int 2, int 3];
list words = [str "hello", str "world"];
list flags = [bool true, bool false];
```

Each element is checked against its declared type when the list is evaluated. An
empty list can be written as `[]`.

```c
list lst = range(5);
lst = listPush(lst, 10);    // ✓ correct — lst is now [0,1,2,3,4,10]
listPush(lst, 10);           // ✗ original unchanged
```

Declare list variables with the `list` keyword:

```c
list nums = range(5);         // [0, 1, 2, 3, 4]
list empty = range(0, 0);     // []
```

See [builtins.md](builtins.md#list-operations) for the complete list of built-in list functions.
