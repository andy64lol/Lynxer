# Classes

See [Language Reference — Classes](language.md#classes) for the full reference.

A **class** in Lynxer is a reusable object definition. `new` creates an
independent instance with its own fields, and `local` methods are dispatched
on the instance they are called on.

```c
global setup(){}

class Counter {
    int value = 0;

    local init(int initial) {
        this.value = initial;
    }

    local increment() {
        this.value = this.value + 1;
    }

    local get() {
        return this.value;
    }
}

global main(){
    Counter first = new Counter(1);
    Counter second = new Counter(10);
    first.increment();
    second.increment();
    print(first.get()); print("\n");   // 2
    print(second.get()); print("\n");  // 11
}
```

**Key rules:**
- Declare classes between `global setup(){}` and `global main(){}`.
- Create instances with `new ClassName(arguments...)`.
- Define an optional `local init(...)` method as the constructor.
- Call methods on an instance with `instance.methodName(...)`.
- `this.field` accesses the field on the particular receiver instance.
- Use `this.field = value;` inside methods or constructors to update it.
- Declare object variables and typed parameters with the class name, such as
  `Counter counter`; assignments and calls enforce that type.
- Fields can be `const` to prevent mutation.
- Field initializers run once per instance, so mutable/object state is not
  shared between instances.

`global.class.ClassName` remains available as a legacy class-definition
namespace. Its old zero-argument call and static field/method behavior are
preserved; use `new` for normal object-oriented code.

For the complete reference, see [language.md](language.md#classes).
