# Classes

Reusable object types with fields and `local` methods.

```c
global setup(){}

class Counter {
    int value = 0;

    local init(int initial) {
        this.value = initial;
    }

    local increment() -> none {
        this.value = this.value + 1;
    }

    local get() -> int {
        return this.value;
    }
}

global main(){
    Counter first = new Counter(1);
    first.increment();
    println(first.get());   // 2
}
```

Rules:

- Declare between `setup` and `main`.
- Construct with `new ClassName(arguments...)`.
- Optional `local init(...)` is the constructor.
- Methods use `this.field`; call with `instance.method(...)`.
- Methods may use optional `-> type` return annotations like ordinary functions.
- Fields may be `const`.
- Typed variables such as `Counter c` enforce the class type.

See also [structs.md](structs.md) (data-only) and [vargroups.md](vargroups.md).
