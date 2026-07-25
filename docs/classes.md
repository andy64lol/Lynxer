# Classes

A **class** is a named, static namespace that groups related fields (data) and methods (functions) together under one name.  Unlike traditional object-oriented languages, Lynxer classes are **singletons** — there is exactly one instance of each class, and all access goes through the `global.class` namespace.

---

## Defining a class

```c
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
```

**Rules:**
- `class` is a top-level declaration — same level as `global` functions.
- A class **must not be empty**: it needs at least one field or one method.
- Fields are declared as `[const] type name = defaultValue;`.
- Methods are declared with `def methodName(params) { body }` (not `global`).
- Inside a method, `this` refers to the class itself — use `this.fieldName` to read a field and `type this.fieldName = value` to write it.
- A special method named `init` is called automatically when `global.class.ClassName()` is invoked.

---

## Calling / initialising a class

```c
global.class.Dog();           // runs init() if defined, otherwise a no-op
```

If the class has no `init` method the call simply succeeds and returns nothing.

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
    print(global.class.Counter.value()); print("\n");  // 2
}
```

---

## Accessing and modifying fields

Field access always uses the full `global.class.ClassName.fieldName` path:

```c
print(global.class.Dog.name);         // read a field
print(global.class.Dog.age); print("\n");
```

To **set** a field from outside the class, use the typed dot-assignment syntax (same as vargroups):

```c
str global.class.Dog.name = "Rex";
int global.class.Dog.age  = 3;
```

The type keyword must match the field's declared type. A mismatch is a runtime error.

---

## Calling methods

```c
global.class.Dog.bark();            // calls the bark method
global.class.Dog.birthday();        // calls the birthday method
```

Methods with parameters:

```c
class Greeter {
    str lang = "en";

    def greet(str who) {
        if(this.lang == "en") {
            print("Hello, "); print(who); print("!\n");
        }
        if(this.lang == "es") {
            print("Hola, "); print(who); print("!\n");
        }
    }
}
```

```c
global.class.Greeter.greet("World");
str global.class.Greeter.lang = "es";
global.class.Greeter.greet("Mundo");
```

---

## `const` fields

Fields declared `const` cannot be changed after the class is defined:

```c
class Config {
    const str HOST = "localhost";
    int port = 8080;
}
```

```c
print(global.class.Config.HOST);      // localhost
str global.class.Config.HOST = "x";  // Runtime Error: field is const
```

---

## Classes in imported modules

Any `.lynx` file can declare classes.  After importing, use the module's class namespace:

```c
/// animals.lynx ///
global setup() {}

class Cat {
    str name = "unnamed";

    def meow() {
        print(this.name); print(" says: Meow!\n");
    }
}
global main() {}

```

```c
global setup() { import("animals"); }

global main() {
    global.animals.class.Cat();
    str global.animals.class.Cat.name = "Whiskers";
    global.animals.class.Cat.meow();       // Whiskers says: Meow!
}
```

---

## Return values from methods

Methods can `return` a value just like regular functions:

```c
class MathHelper {
    float pi = 3.14159;

    def circleArea(float r) {
        return this.pi * r * r;
    }
}
```

```c
float area = global.class.MathHelper.circleArea(5.0);
print(area); print("\n");
```

---

## Summary

| Syntax | Meaning |
|--------|---------|
| `class Name { ... }` | Declare a class at the top level |
| `global.class.Name()` | Initialise the class (calls `init()` if defined) |
| `global.class.Name.field` | Read a class field |
| `type global.class.Name.field = value` | Set a class field |
| `global.class.Name.method(args)` | Call a class method |
| `this.field` | Inside a method, access the class field |
| `type this.field = value` | Inside a method, set the class field |
| `global.moduleName.class.Name()` | Use a class from an imported module |
