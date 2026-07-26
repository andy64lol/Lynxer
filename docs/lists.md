# Lists

See [Built-in Functions — List operations](builtins.md#list-operations) for the full list API.

Lists are first-class values created with `range()`, `seqFromTo()`, or built up with `listPush()`. They use **value semantics** — mutating built-ins like `listPush`, `listSet`, and `listRemove` return a **new** list. Always reassign the result:

```c
any lst = range(5);
lst = listPush(lst, 10);    // ✓ correct — lst is now [0,1,2,3,4,10]
listPush(lst, 10);           // ✗ original unchanged
```

Declare list variables as `any`:

```c
any nums = range(5);          // [0, 1, 2, 3, 4]
any empty = range(0, 0);      // []
```

See [builtins.md](builtins.md#list-operations) for the complete list of built-in list functions.
