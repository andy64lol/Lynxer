# Structs

Data-only named types. No methods.

```c
global setup(){}

struct Player {
    int health;
    str name;
}

global main(){
    Player first = new Player(100, "Ada");
    first.health = 90;
    println(first.name);
}
```

Rules:

- Every field has an explicit type and ends with `;`.
- `new StructName(...)` initializes fields positionally.
- Argument count and types are checked.
- Dot access for read/write.
- Prefer a `class` when you need methods or custom `init`.
