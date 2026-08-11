# VarGroups

See [Language Reference — VarGroups](language.md#vargroups) for the full reference.

A **vargroup** is a named, typed record with dot-accessed fields — similar to a C struct. Fields must be declared with explicit types and default values.

Vargroups use `{...}` so they are visually distinct from lists and tuples. The older
`[...]` form is still accepted for compatibility, but it is deprecated and should
not be used in new code.

```c
vargroup player = {
    str  username = "Andy",
    int  coins    = 250,
    bool online   = true,
    vargroup stats = {
        int   level = 5,
        float speed = 3.5
    }
};

print(player.username);      // Andy
print(player.stats.level);   // 5

int player.coins = 500;           // dot-assignment (type must match)
int player.stats.level = 10;      // nested dot-assignment

addVarGroup(player, str title = "Warrior");   // add a new field
removeVarGroup(player, title);               // remove a field
```

For the complete reference including `const` fields, global vargroups, and `any`-typed fields, see [language.md](language.md#vargroups).
