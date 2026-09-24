# Structs

A `struct` is a data-only named type: a fixed list of typed fields, constructed
positionally with `new`, with no methods. Declare it between `global setup()`
and `global main()`.

## Declaration

```lynx
struct Player {
    int health;
    str name;
}
```

Every field has an explicit type and ends with `;`. Fields may **not** have
default values (`int health = 100;` fails with
`expected ';' after struct field`), and a struct has no methods — use a
[class](classes.md) when you need either.

## Construction

```lynx
Player first = new Player(100, "Ada");
```

- Arguments are matched **positionally** to the declared field order.
- The argument count is checked: `new Player(100)` reports
  `struct 'Player' expects 2 arguments, received 1`.
- Each argument is type-checked: a `str` where an `int` is expected reports
  `value cannot be assigned to type 'int'`.

## Access and assignment

Read and write fields with dot access; no type prefix is needed:

```lynx
println(first.name);   // Ada
first.health = 90;
println(first.health); // 90
```

(That differs from [vargroups](vargroups.md), whose field assignment requires
the type.)

## Full example

```lynx
global setup(){}

struct Player {
    int health;
    str name;
}

global main(){
    Player first = new Player(100, "Ada");
    first.health = 90;
    println(first.name);    // Ada
    println(first.health);  // 90
}
```

## See also

- [classes.md](classes.md) — the same shape plus methods and `init`.
- [vargroups.md](vargroups.md) — records with defaulted fields.
- [types.md](types.md) — how a struct name works as a declared type.
