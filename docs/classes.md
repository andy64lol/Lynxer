# Classes

A **class** is a named, static singleton that groups typed fields and methods under one name. Unlike traditional OOP languages, Lynxer classes have **no instances** — there is exactly one copy of each class, and all access goes through `global.class.ClassName`.

---

## Defining a class

```c
global setup(){}

class Dog {
    str  name = "unnamed";
    int  age  = 0;

    def bark() {
        print(this.name); print(" says: Woof!\n");
    }

    def birthday() {
        int this.age = this.age + 1;
    }
}

global main(){
    str global.class.Dog.name = "Rex";
    int global.class.Dog.age  = 2;
    global.class.Dog.bark();       // Rex says: Woof!
    global.class.Dog.birthday();
    print(global.class.Dog.age);   // 3
    print("\n");
}
```

**Rules:**
- `class` is a top-level declaration — it must appear **between `global setup(){}` and `global main(){}`**.
- A class **must not be empty**: it needs at least one field or one method.
- Fields are declared as `[const] type name = defaultValue;` (semicolon required).
- Methods are declared with `def methodName(params) { body }`.
- Inside a method, `this` refers to the class itself — use `this.fieldName` to read a field and `type this.fieldName = value` to write it.
- A special method named `init` is called automatically when `global.class.ClassName()` is invoked.

---

## Calling / initialising a class

```c
global.class.ClassName()
```

If the class has no `init` method, this call is a no-op (still succeeds).

### With an `init` method

```c
global setup(){}

class Counter {
    int count = 0;

    def init() {
        int this.count = 0;
        print("Counter initialised!\n");
    }

    def increment() {
        int this.count = this.count + 1;
    }

    def value() {
        return this.count;
    }
}

global main() {
    global.class.Counter();                    // prints "Counter initialised!"
    global.class.Counter.increment();
    global.class.Counter.increment();
    print(global.class.Counter.count);         // 2
    print("\n");
    print(global.class.Counter.value());       // 2
    print("\n");
}
```

---

## Accessing and modifying fields

Field access uses the full `global.class.ClassName.fieldName` path:

```c
print(global.class.Dog.name);          // read a field
print(global.class.Dog.age); print("\n");
```

To **set** a field from outside the class, use the typed dot-assignment syntax:

```c
str global.class.Dog.name = "Rex";
int global.class.Dog.age  = 3;
```

The type keyword must match the field's declared type. A mismatch is a runtime error.

---

## Calling methods

```c
global.class.Dog.bark();            // call a method
global.class.Counter.increment();   // call a method
```

Methods with parameters:

```c
class Adder {
    int total = 0;
    def add(int n) {
        int this.total = this.total + n;
    }
}

global main(){
    global.class.Adder.add(5);
    global.class.Adder.add(10);
    print(global.class.Adder.total); print("\n");  // 15
}
```

---

## Const fields

Fields declared with `const` cannot be changed after initialisation:

```c
global setup(){}

class Config {
    const int MAX_SIZE = 100;
    int       current  = 0;
}

global main(){
    print(global.class.Config.MAX_SIZE); print("\n");  // 100
    // int global.class.Config.MAX_SIZE = 999;  // Runtime Error: const field
}
```

---

## `this` inside methods

Inside a method body, `this` always refers to the class blueprint. You can:
- **Read** a field: `this.fieldName`
- **Write** a field: `type this.fieldName = newValue;`
- **Call** another method: `this.methodName()` — not yet supported; call via `global.class.ClassName.methodName()` instead

```c
class Rect {
    int width  = 0;
    int height = 0;

    def setSize(int w, int h) {
        int this.width  = w;
        int this.height = h;
    }

    def area() {
        return this.width * this.height;
    }
}

global setup(){}

global main(){
    global.class.Rect.setSize(4, 5);
    print(global.class.Rect.area()); print("\n");  // 20
}
```

---

## Multiple classes

Multiple classes can be defined between `setup` and `main`. Each is independent:

```c
global setup(){}

class Foo {
    int x = 1;
    def get() { return this.x; }
}

class Bar {
    int y = 2;
    def get() { return this.y; }
}

global main(){
    print(global.class.Foo.get()); print("\n");  // 1
    print(global.class.Bar.get()); print("\n");  // 2
}
```

---

## VarGroups

A **vargroup** is a named, typed record with dot-accessed fields — similar to a C struct. Unlike classes, vargroups have no methods and multiple vargroup values can exist side by side.

```c
vargroup player = [
    str  username = "Andy",
    int  coins    = 250,
    bool online   = true,
    vargroup stats = [
        int   level = 5,
        float speed = 3.5
    ]
];

print(player.username);        // Andy
print(player.stats.level);     // 5

int player.coins       = 500;      // dot-assignment
int player.stats.level = 10;       // nested dot-assignment

print(returnType(player));     // vargroup
```

**Dynamic fields:**

```c
addVarGroup(player, str title = "Warrior");  // add a new field
print(player.title);                          // Warrior

removeVarGroup(player, title);               // remove a field
```

**Global vargroup** — declare in `setup()` to share across all functions:

```c
global setup(){
    vargroup config = [str host = "localhost", int port = 8080];
}
global main(){
    print(config.host);   // localhost
    int config.port = 9000;
}
```

See [vargroups.md](vargroups.md) for the full reference.
