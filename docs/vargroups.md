# VarGroups

A **vargroup** is a named, typed record — like a C struct or a simple object — that groups related fields together. Fields are accessed and mutated with dot notation.

---

## Declaring a vargroup

```c
vargroup player = [
    str  username = "Andy",
    int  coins    = 250,
    bool online   = true
];
```

**Rules:**
- `vargroup` is a statement, valid anywhere a variable declaration is valid: inside `global main()`, inside any function, or inside `global setup()` (making it a global vargroup accessible from everywhere).
- Every field must have an explicit type (`int`, `float`, `str`, `bool`, `any`) and an initial value.
- Duplicate field names inside the same vargroup are a runtime error.
- `returnType(vg)` returns `"vargroup"`.

---

## Accessing fields

Use dot notation:

```c
print(player.username);   // Andy
print(player.coins);      // 250
print(player.online);     // true
```

---

## Assigning to fields

Dot-assignment requires an explicit type keyword matching the field's declared type:

```c
int  player.coins  = 500;
bool player.online = false;
println(player.coins);    // 500
println(player.online);   // false
```

The type keyword must match the field's declared type. A mismatch is a syntax/runtime error:

```c
str player.coins = 500;  // Runtime Error: field 'coins' declared as 'int' but assignment specifies 'str'
player.coins = 500;      // Syntax Error: vargroup field assignment requires an explicit type
```

---

## Nested vargroups

A field may itself be a vargroup using the `vargroup` type keyword:

```c
vargroup player = [
    str  username = "Andy",
    int  coins    = 250,
    vargroup stats = [
        int   level = 5,
        float speed = 3.5
    ]
];

println(player.stats.level);   // 5
println(player.stats.speed);   // 3.5

// typed dot-assignment — required even for nested fields
int   player.stats.level = 10;
float player.stats.speed = 7.25;
println(player.stats.level);   // 10
```

Dot chains can be as deep as needed.

---

## Adding fields at runtime

`addVarGroup(path, type name = value)` appends a new field to an existing vargroup:

```c
addVarGroup(player, str title = "Warrior");
print(player.title);   // Warrior

// Add a field to a nested vargroup
addVarGroup(player.stats, int xp = 1200);
print(player.stats.xp);   // 1200
```

Adding a field that already exists is a runtime error.

You can also add a nested vargroup field:

```c
addVarGroup(player, vargroup inventory = [
    int apples = 4,
    int swords = 1
]);
print(player.inventory.apples);  // 4
```

---

## Removing fields at runtime

`removeVarGroup(path, fieldName)` removes a field from an existing vargroup:

```c
removeVarGroup(player, title);
// player.title no longer exists — accessing it would be a runtime error

removeVarGroup(player.stats, xp);
```

Removing a field that does not exist is a runtime error.

---

## Global vargroups

Declare a vargroup inside `setup()` to make it globally visible:

```c
global setup(){
    vargroup config = [
        str  host  = "localhost",
        int  port  = 8080,
        bool debug = false
    ];
}

global main(){
    print(config.host);   // localhost
    config.port = 9000;
    print(config.port);   // 9000
}
```

---

## vargroup as `any` field

A field declared `any` accepts any type, including another vargroup:

```c
vargroup bag = [
    any item  = "sword",
    int count = 3
];
print(bag.item);   // sword
bag.item = 42;
print(bag.item);   // 42
```

---

## Printing a vargroup

`print(vg)` and `strOf(vg)` both produce a readable representation:

```c
vargroup cfg = [str host = "localhost", int port = 9000, bool debug = false];
print(strOf(cfg));
// vargroup cfg { str host = localhost, int port = 9000, bool debug = false }
```

---

## Reference semantics

Unlike regular variables (which are copied on every read), vargroups use **reference semantics**: all names that hold the same vargroup see the same data. This is what makes `addVarGroup` and `removeVarGroup` mutations visible without reassignment.

```c
vargroup a = [int x = 1];
// 'a' refers to the vargroup object
a.x = 99;
print(a.x);   // 99  — mutation is immediate
```

---

## Const vargroups and const fields

### `const vargroup`

Prefix the declaration with `const` to prevent the variable from being reassigned. The vargroup itself and its non-const fields are still mutable through dot-assignment.

```c
const vargroup cfg = [
    str host = "localhost",
    int port = 8080
];

// dot-assignment to a non-const field still works
int cfg.port = 9000;

// reassigning the variable is blocked
// cfg = someOtherVargroup;  // Runtime Error: Cannot assign to constant 'cfg'
```

### `const` fields

Mark individual fields with `const` to make them immutable after declaration:

```c
vargroup player = [
    str       username = "Andy",
    const int id       = 42,
    int       coins    = 100
];

println(player.id);      // 42
int player.coins = 200;  // ok — coins is not const

// assigning to a const field is a runtime error
// int player.id = 99;  // Runtime Error: Field 'id' of vargroup 'player' is const and cannot be changed
```

`strOf(vg)` and `print(vg)` show the `const` prefix on const fields:

```
vargroup player { str username = Andy, const int id = 42, int coins = 200 }
```

Both features can be combined: `const vargroup` with `const` fields gives a fully locked record.

---

## Summary

| Syntax | Purpose |
|--------|---------|
| `vargroup name = [ type field = value, ... ];` | Declare a vargroup |
| `vg.field` | Read a field |
| `type vg.field = value;` | Assign to a field (type required) |
| `vg.nested.field` | Read a nested field |
| `type vg.nested.field = value;` | Assign to a nested field (type required) |
| `addVarGroup(vg, type name = value);` | Add a new field |
| `addVarGroup(vg, vargroup name = [...]);` | Add a new nested vargroup field |
| `removeVarGroup(vg, name);` | Remove a field |
| `returnType(vg)` | Returns `"vargroup"` |
| `strOf(vg)` | Human-readable representation |
| `const vargroup name = [...];` | Vargroup variable that cannot be reassigned |
| `const type field = value` | Field that cannot be changed after declaration |
