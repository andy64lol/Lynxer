# Enums

Tagged unions with optional payloads.

```c
global setup(){}

enum status = [
    Ready,
    Failed(str reason)
]{}

global main(){
    any a = status.Ready;
    any b = status.Failed("disk");
    println(a);
    println(b.reason);
}
```

Rules:

- Declare at top level before `main`.
- Variants may be empty or carry typed named fields.
- Construct as `Enum.Variant` or `Enum.Variant(args...)`.
- Payload types and arity are checked at construction.
- The trailing `{...}` body is required (use `{}` when empty).

Pattern matching via `switch`/`case` is available where implemented; see
examples under `clynxer/examples/`.
