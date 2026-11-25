# Wynxer language

Wynxer uses Lynxer-style syntax. The most important rule: **there is no
`let`**. Variables are declared with their type.

```wynx
int x = 5;
str name = "Ada";
float pi = 3.14;
bool ok = true;
```

## Functions

```wynx
fn greet(str name) -> str {
    str msg = "Hello, ";
    println(msg, name);
    return msg;
}

pub fn main() {
    greet("World");
}
```

Parameters can be written type-first (`str name`) or with a colon
(`name: str`). The return type follows `->`. Functions are emitted as global
JavaScript declarations so `.wpf` handlers like `app.onClick` can call them.

## Control flow

```wynx
if (x > 10) {
    println("big");
} elif (x > 5) {
    println("medium");
} else {
    println("small");
}

while (x < 10) {
    x = x + 1;
}

for (int i = 0; i < 3; i = i + 1) {
    println(i);
}
```

Assignment uses `=` on an existing name (`x = 5;`).

## Structs and enums

```wynx
struct Point { x: int, y: int; }
enum Status = [Draft, Published, Archived];
```

## Macros

Wynxer borrows Lynxer's macro features: `pub` visibility, `*args` variadics,
`...` rest markers, and codeblock parameters.

```wynx
macro repeat!(n, *args, body: code) {
    // ...
}
```

## Expressions and builtins

Expressions support arithmetic, comparison, boolean operators, calls, field
access (`obj.field`), indexing (`list[0]`), lists, tuples and maps.
Common builtins map to JavaScript: `println(...)` → `console.log(...)`,
`input(...)` → `prompt(...)`.

## Tooling

`wynxer lint <file>` flags unused variables, `let` declarations, and unknown
`.wpf` element kinds. `wynxer ast <file>` dumps the parsed AST, and
`wynxer format <file>` pretty-prints a script. Errors are reported with
`file:line:column` and a caret under the offending token.

See [wynxer-cli.md](wynxer-cli.md) and [wynxer-js-abi.md](wynxer-js-abi.md).
