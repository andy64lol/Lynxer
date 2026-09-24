# Enums

An `enum` is a tagged union: a fixed set of variants, each optionally carrying
typed named fields. Declare it at the top level, before `main`.

## Declaration

```lynx
enum status = [
    Ready,
    Failed(str reason)
]{}
```

- The variant list is `[ ... ]` and the trailing `{ ... }` body is **required**;
  omitting it fails with `expected '{' after enum variants` (use `{}` when
  empty).
- A variant is either a bare name (`Ready`) or a name with typed fields
  (`Failed(str reason)`).
- Payload types and arity are checked when the variant is constructed.

## Construction

```lynx
any a = status.Ready;
any b = status.Failed("disk");
```

A payload field is read like a record field:

```lynx
println(b.reason);   // disk
```

An enum value prints as `Enum.Variant` (`println(a)` prints `status.Ready`).

## Matching with `switch`

An enum value is normally consumed with a `switch` pattern, which destructures
the payload and binds the field names for the case body:

```lynx
func describe(status value) -> str {
    switch (value) {
        case(status.Ready){ return "ready"; }
        case(status.Failed(reason)){ return "failed: " + reason; }
    }
    return "unknown";
}
```

See [language.md](language.md#switch--case--default) for the general `switch`
form.

## Equality is identity, not structure

Enum values compare by **identity**, so two separately built variants are never
equal — even two `Ready`s, and even a variant compared with itself after a
round trip:

```lynx
println(status.Ready is status.Ready);          // false
println(status.Failed("x") is status.Failed("x")); // false

any a = status.Ready;
println(a is a);                                // true
```

Use `switch` patterns to test which variant a value is; do not use `is`.

## Full example

```lynx
global setup(){}

enum status = [
    Ready,
    Failed(str reason)
]{}

func describe(status value) -> str {
    switch (value) {
        case(status.Ready){ return "ready"; }
        case(status.Failed(reason)){ return "failed: " + reason; }
    }
    return "unknown";
}

global main(){
    any a = status.Ready;
    any b = status.Failed("disk");
    println(a);              // status.Ready
    println(b.reason);       // disk
    println(describe(a));    // ready
    println(describe(b));    // failed: disk
}
```

## See also

- [structs.md](structs.md) and [classes.md](classes.md) for the other named types.
- [language.md](language.md#control-flow) for `switch`.
