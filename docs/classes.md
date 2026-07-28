# Classes

See [Language Reference — Classes](language.md#classes) for the full reference.

A **class** in Lynxer is a named, static singleton that groups typed fields and `local` methods. There is exactly one instance per class — no `new` keyword.

```c
global setup(){}

class Counter {
    int count = 0;

    local init() {
        int this.count = 0;
    }

    local increment() {
        int this.count = this.count + 1;
    }

    local value() {
        return this.count;
    }
}

global main(){
    global.class.Counter();           // runs init()
    global.class.Counter.increment();
    global.class.Counter.increment();
    print(global.class.Counter.value()); print("\n");  // 2
}
```

**Key rules:**
- Declare classes between `global setup(){}` and `global main(){}`.
- Access via `global.class.ClassName`.
- Call `global.class.ClassName()` to run `init()` (optional).
- Call methods via `global.class.ClassName.methodName()`.
- `this.field` accesses the class field inside a method.
- Dot-assignment inside methods: `type this.field = value;`.
- Fields can be `const` to prevent mutation.
- Class definitions are singletons — there is only one instance per class in the program.

For the complete reference, see [language.md](language.md#classes).
