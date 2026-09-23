# Classes

A `class` is a named type with typed fields and `local` methods, constructed
with `new`. Declare it between `global setup()` and `global main()`.

## Declaration

```lynx
class Counter {
    int value = 0;
    const int step = 1;

    local init(int initial) {
        this.value = initial;
    }

    local increment() {
        this.value += this.step;
    }

    local get() {
        return this.value;
    }
}
```

- Fields are `type name = initialValue;`. A field may be `const`, which makes
  later writes an error:
  `Field 'step' of instance 'Counter' is const and cannot be changed`.
- `local init(...)` is the constructor. It runs on `new` and receives the
  constructor arguments through `this`.
- Methods use `this.field` to read and write fields.

## Construction

```lynx
Counter first = new Counter(1);
```

`new` runs `init` with the given arguments. A typed variable
(`Counter first`) enforces the class: assigning an unrelated value fails with
`value cannot be assigned to type 'Counter'`.

## Methods

Call a method on an instance with `instance.method(...)`:

```lynx
first.increment();
println(first.get());   // 2
```

Two restrictions distinguish methods from [ordinary functions](language.md#functions):

- Methods take **no** return annotation. `local get() -> int { ... }` fails with
  `expected '{' before method body`. Return the value with a bare `return`.
- Method parameters may be typed but may **not** have default values.
  `local set(int x = 2)` fails with `expected ')' after parameters`.

## Fields and assignment

Reading and writing a field on an instance needs no type prefix, and compound
assignment works on fields as well as on plain variables:

```lynx
first.value = 90;
first.value += 10;         // 100
```

Inside a method, use `this`:

```lynx
this.value += this.step;
```

## Full example

```lynx
global setup(){}

class Counter {
    int value = 0;
    const int step = 1;

    local init(int initial) {
        this.value = initial;
    }

    local increment() {
        this.value += this.step;
    }

    local get() {
        return this.value;
    }
}

global main(){
    Counter first = new Counter(1);
    first.increment();
    println(first.get());     // 2
    println(first.value);     // 2
}
```

## See also

- [structs.md](structs.md) — data-only records, positionally constructed.
- [vargroups.md](vargroups.md) — records with defaulted fields.
- [enums.md](enums.md) — tagged unions.
