# Language

Wynxer borrows its surface syntax from Lynxer: `fn`, typed variables
(`int x = 5;`, never `let`), control flow, and macros. It targets the web,
so its standard library is web-focused (DOM, fetch, storage, routing) rather
than OS-focused.

## Program structure

```wynx
fn setup() { }

fn main() {
    alert("Hello, Wynxer!");
}

fn onClick() {
    alert("clicked");
}

export onClick;
```

## Types

| Type | Meaning |
| --- | --- |
| `int` | 64-bit integer |
| `float` | 64-bit floating point |
| `str` | UTF-8 string |
| `bool` | `true` / `false` |
| `list` | ordered, mutable list |
| `tuple` | immutable ordered sequence |
| `map` | key/value map |
| `void` | no value |

## Functions

```wynx
fn name(param1: type, param2: type) -> returnType {
    // body
    return value;
}
```

A function is exported with `export name;` so a `.wpf` page can bind an
element event to it (`app.onClick`).

## Control flow

```wynx
if (cond) { ... } elif (cond2) { ... } else { ... }

while (cond) { ... }
for (int i = 0; i < n; i = i + 1) { ... }
for (int x in list) { ... }

switch (value) {
    case(1) { ... }
    default() { ... }
}

try { ... } catch (str error) { ... }
```

## Built-ins (planned)

| Function | Meaning |
| --- | --- |
| `alert(str)` | show a browser alert |
| `println(...)` | print to the console |
| `strOf(value)` | convert a value to a string |

## Macros

File-scoped macros, `pub` exports, token arguments and codeblock parameters
are borrowed from Lynxer.

## See also

- [wpf.md](wpf.md) — the page format
- [elements.md](elements.md) — the element API
- [stdlib.md](stdlib.md) — the web standard library
