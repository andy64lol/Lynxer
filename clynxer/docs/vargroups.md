# VarGroups

Named typed records with defaulted fields and dot access. Prefer `{...}`
literals; the older `[...]` form is deprecated.

```c
global setup(){}

global main(){
    vargroup player = {
        str username = "Ada",
        int coins = 250,
        bool online = true
    };

    println(player.username);
    int player.coins = 500;
}
```

Rules:

- Fields need explicit types and defaults.
- Nested vargroups are allowed.
- Dot assignment requires a matching type prefix: `int player.coins = 500;`.
- Optional return types on helpers that build/return records use ordinary
  `->` annotations on the function, not on the vargroup itself.

See also [structs.md](structs.md) for positional constructors without defaults.
