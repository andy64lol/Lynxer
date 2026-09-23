# VarGroups

A `vargroup` is an inline typed record: a set of named fields, each with a type
and a default value, created from a `{ ... }` literal and read with dot access.

## Literal

```lynx
vargroup player = {
    str username = "Ada",
    int coins = 250,
    bool online = true
};
```

- The literal is `{ type name = value, ... }`. Only the brace form exists —
  `[ ... ]` is always a **list** literal, not a vargroup.
- Fields are separated by commas. A single trailing `;` before the closing `}`
  is also accepted.
- Every field needs an explicit type and a default value.
- A field may be `const`.

## Access and assignment

Read a field with dot access. **Assigning** to a field requires the field's
type prefix:

```lynx
println(player.username);   // Ada
int player.coins = 500;     // required form
println(player.coins);      // 500
```

Omitting the prefix fails with
`Vargroup and legacy class-field assignment requires an explicit type`, and a
prefix that does not match the declared field type fails with
`Field 'coins' of instance '' is declared as 'int' but received a 'str' value`.

Assigning to a `const` field fails with
`Field 'n' of instance '' is const and cannot be changed`.

(This is stricter than [classes](classes.md), where an instance field may be
written as `instance.field = value;`.)

## Nesting

Fields may themselves be vargroups, and nested fields are reached by repeating
the path:

```lynx
vargroup nested = { int n = 1, vargroup sub = { str k = "x" } };
println(nested.sub.k);   // x
```

## Full example

```lynx
global setup(){}

global main(){
    vargroup player = {
        str username = "Ada",
        int coins = 250,
        bool online = true
    };

    println(player.username);   // Ada
    println(player.online);     // true

    int player.coins = 500;
    println(player.coins);      // 500

    vargroup nested = { int n = 1, vargroup sub = { str k = "x" } };
    println(nested.sub.k);      // x
}
```

## See also

- [structs.md](structs.md) — positional constructor, no defaults, no methods.
- [classes.md](classes.md) — fields plus methods.
