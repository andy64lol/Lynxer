# Enums

Rust-style enums: tagged, type-safe unions declared with a bracketed variant
list and an optional braced body.

```c
enum result = [
    Ok(int value),
    Err(str message)
]{
    // reserved for enum-associated code (see "The braced body" below)
}
```

---

## Declaration

```c
enum name = [
    Variant,
    Variant(type field),
    Variant(type field, type otherField)
]{}
```

- A variant may be **empty** (`Ready`) or **data-bearing** (`Ok(int value)`).
- Payload fields are declared with an explicit type and a field name.
- Variant names must be unique **within** one enum — a duplicate is a
  compile-time error that points at both declarations.
- The `[...]` list is required, and the `{...}` block is required too (write
  `{}` when the enum has no associated code).
- Enums are declared at the top level of a file, before `global main(){}`.

## Constructing values

```c
global setup(){}
enum status = [Ready, Failed(str reason)]{}
enum point  = [Two(int x, int y)]{}

global main(){
    any a = status.Ready;          // empty variant: no parentheses
    any b = status.Failed("disk"); // positional payloads
    any c = point.Two(3, 4);

    println(a);        // status.Ready
    println(c.x);      // 3  — payload field by name
    println(b.reason); // disk
}
```

Payload types are checked at construction: `point.Two("3", 4)` is a runtime
error reporting the field that was declared as `int`. The number of payload
values must match the variant exactly.

Enum values are tagged: they carry their enum identity, so a value from one
enum never compares equal to a value of another enum. `==` compares the enum,
the variant, and the payloads.

Store enum values in `any` (or pass them around as call arguments and return
values) — the declared variable type is not the enum name itself.

## Matching with `switch`

`case` accepts an enum pattern written as the qualified constructor. Payloads
bind to names that the case body can use:

```c
global setup(){}

enum result = [Ok(int value), Err(str message)]{}

global main(){
    any response = result.Ok(42);
    switch(response){
        case(result.Ok(number)){ println(number); }        // 42
        case(result.Err(why)){ println(why); }
        default(){ println("unknown"); }
    }
}
```

Patterns may be nested, and enum, list, and tuple patterns can be combined.
See [language.md](language.md#switch) for the pattern rules.

## Enums in modules

An enum declared in another file is reached through the module namespace:

```c
global setup(){ import("shapes"); }
global main(){
    any kind = global.shapes.kind.Circle(2);
}
```

## The braced body

The `{...}` block is parsed but **not executed**, and it may not contain
function declarations. Treat it as reserved space for enum-associated code:
put behaviour in ordinary `global` functions for now.

## Bytecode and bundling

Enums are part of the current bytecode format (version 8), so `.lynxc`
programs and bundled executables keep their variants, payload types, and
patterns. Stale bytecode is rejected with a recompile message.

## Current limitations

- Duplicate **enum names** in one file are not diagnosed; the last
  declaration silently wins.
- A name collision between an enum and a `func` declaration is not diagnosed.
- Payload values are ordinary mutable values; they do not participate in the
  ownership transfer/borrow rules.
- Payload types are validated when a value is constructed, not when a payload
  is reassigned.
