# Structs

Structs are data-only named types. Define a struct after `global setup()` and
before `global main()`:

```c
global setup(){}

struct Player {
    int health;
    str name;
}
```

Create values with the constructor form `new StructName(...)`:

```c
global main(){
    Player first = new Player(100, "Ada");
    Player second = new Player(50, "Lin");

    println(first.name);
    first.health = 90;
}
```

Rules:

- Every field must have an explicit type and ends with `;`.
- Fields are initialized positionally in declaration order.
- The constructor requires exactly the declared number of arguments.
- Constructor arguments are checked against their field types.
- Struct fields can be read and assigned with dot access.
- Structs do not support methods, inheritance, default values, or dynamic fields.
- Struct values are distinct mutable records; assigning a struct preserves its
  identity, like other object values.

Use a `class` when you need methods, constructors with custom behavior,
default field initializers, or inheritance. Use a `vargroup` for a convenient
record literal whose fields are initialized inline.