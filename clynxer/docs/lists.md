# Lists

Mutable sequences. Create with `[...]` literals or builtins such as `range`,
`seqFromTo`, and `listRepeat`.

```c
global main(){
    list xs = [1, 2, 3];
    listPush(xs, 4);
    println(listGet(xs, 0));
    println(returnLength(xs));
}
```

Core builtins: `listPush`, `listPop`, `listGet`, `listSet`, `listSlice`,
`listContains`, `listJoin`, `sortList`, `reverseList`, `listFlatten`,
`listUnique`, and the rest listed in [builtins.md](builtins.md#sequences).
