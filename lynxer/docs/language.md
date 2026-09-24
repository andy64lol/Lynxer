# The Lynxer language

Lynxer is the standalone C++ implementation of Lynxer. This page describes the
language **as Lynxer actually parses and runs it**, with every example verified
against the interpreter. For the differences from the older Python
implementation, see [limitations.md](limitations.md) and
[parity.md](parity.md).

Related pages: [types.md](types.md), [lists.md](lists.md),
[structs.md](structs.md), [classes.md](classes.md), [enums.md](enums.md),
[vargroups.md](vargroups.md), [modules.md](modules.md), and
[builtins.md](builtins.md).

## A first program

```lynx
global setup() {
    println("ready");
}

global main() {
    println("Hello, Lynxer!");
}
```

Requires the interpreter (see [install.md](install.md)) and runs with
`lynxer hello.lynx`. If `setup` or `main` is missing, the parser reports it
with a source location, for example
`lynxer: hello.lynx:2:2: program must define global setup()`.

## Program structure

A file is a sequence of top-level declarations, in this order:

1. **`global setup()`** — must be the **first** declaration. The interpreter
   raises `global setup() must be the first declaration` otherwise.
2. **Anything else** — file-level `func`s, `global` functions, `struct`,
   `class` and `enum` definitions, in any order before `main`.
3. **`global main()`** — the default entry point, and the last declaration.
   A declaration after `main` fails with
   `declarations may not follow global main()`.

Executable code outside a function body is a syntax error
(`expected top-level function declaration`).

`setup()` and `main()` may take **defaulted parameters**; they are filled in
before the body runs:

```lynx
global setup(int retries = 3) {
    println("retries ", retries);   // retries 3
}

global main() {
    println("main");
}
```

### `overrideMain`

When `setup()` calls `overrideMain("name")`, the named global function runs
instead of `main`, and `main` becomes optional:

```lynx
global setup() {
    overrideMain("start");
}

global start() {
    println("started");   // printed
}

global main() {
    println("unused");
}
```

## Comments and docstrings

```lynx
// a single-line comment

///
  a multi-line delimiter comment;
  the closing delimiter is the same slash run
///

////
  a file docstring. `--list-stdlibs` prints this block for a module.
////
```

- `//` runs to end of line.
- `/// ... ///` and `//// ... ////` are multi-line blocks opened and closed by
  the *same* slash run.
- `/* ... */` is **not** a comment — it fails to parse
  (`expected a declaration, assignment, or print call`).
- The docstring form recognised by `--list-stdlibs` is a line that is exactly
  `////`; a module that opens with `//// text` on the same line will not have
  its docstring extracted.

Comment markers inside string literals are ordinary text.

## Variables and declarations

```lynx
global setup() {
    int count = 0;
    const str APP = "demo";
    println(APP, " ", count);   // demo 0
}

global main() {
    float ratio = 1.5;
    ratio += 0.5;
    println(ratio);             // 2
}
```

- A declaration is `type name = value;`.
- `const` makes later assignment a runtime error:
  `variable 'frozen' is constant and cannot be reassigned`.
- Fixed-width types reject out-of-range values at the declaration, parameter,
  assignment, and return boundaries (see [types.md](types.md)).
- A declaration inside a loop or `if` body belongs to the enclosing top-level
  scope, not a nested one (see [Scoping](#scoping)).

## Types

The complete type list, ranges, and conversion rules live in
[types.md](types.md). The scalar names are `int`, `float`, `num`, `bool`,
`str`, `char`, `numBool`, `bit`, `byte`, `int8`/`int16`/`int32`/`int64`,
`uint8`/`uint16`/`uint32`/`uint64`, `float32`/`float64`, plus `any`, `none`,
`list`, `tuple`, `codeblock`, and the named `struct`/`class`/`enum` types.

## Operators

### Arithmetic and text

| Operator | Meaning |
|----------|---------|
| `+` | Addition, or string/char concatenation |
| `-` `*` `/` | Subtraction, multiplication, division |
| `%` | Modulo (integers only) |
| `/%` | Floor division (integers only) |
| `**` | Exponentiation (right-associative) |
| unary `-` | Negation |

`//` is a comment, not division — integer division is `/%`.

### Comparison and equality

| Operator | Meaning |
|----------|---------|
| `is` | Equal (preferred) |
| `isnt` | Not equal (preferred) |
| `<` `<=` `>` `>=` | Ordering (numbers, or two strings) |

`==`, `!=` and `not is` still work but are **deprecated** and print a warning
naming `is`/`isnt`.

### Boolean logic

| Operator | Meaning |
|----------|---------|
| `and` | Short-circuit AND |
| `or` | Short-circuit OR |
| `not` | Unary NOT |
| `xor` `xnor` | Logical XOR / XNOR (no short-circuit) |
| `nand` `nor` | Logical NAND / NOR (no short-circuit) |

A bare `!` is invalid. `&&`, `||`, `!!`, `!&&` and `!||` are deprecated
spellings of `and`, `or`, `not`, `nand` and `nor`.

### Bitwise and shifts

| Operator | Meaning |
|----------|---------|
| `bitand` `bitor` `bitxor` | Bitwise AND / OR / XOR |
| `bitnand` `bitnor` `bitxnor` | Bitwise NAND / NOR / XNOR |
| `bitnot` | Bitwise NOT |
| `bitleft` `bitright` | Shifts |

These require `int64` operands. The symbolic spellings `&`, `|`, `^`, `!&`,
`!|`, `!^`, `~`, `<<`, `>>` are deprecated.

### Compound assignment

`+=`, `-=`, `*=`, `/=`, `%=`, `**=` are accepted. The result is validated
against the variable's type, so `int x = 5; x /= 2;` is an error rather than a
silent truncation.

### Precedence

Lowest to highest: `or`/`nor` → `xor`/`xnor` → `and`/`nand` → `not` → equality
and ordering → `bitor` → `bitxor` → `bitand` → shifts → `+`/`-` → `*` `/` `%`
`/%` → `**` → unary → postfix/primary. Parenthesise when in doubt; the parser
follows this chain exactly.

## Control flow

### `if` / `elif` / `else`

```lynx
if (n > 0) {
    println("positive");
} elif (n is 0) {
    println("zero");
} else {
    println("negative");
}
```

`else if` is also accepted and behaves like `elif`.

### `switch` / `case` / `default`

Cases are written `case(pattern){ ... }`; there is **no colon form and no
`break`** — the first matching case runs, then the `switch` ends.

```lynx
switch (x) {
    case(1){ println("one"); }
    case(2){ println("two"); }
    default(){ println("other"); }
}
```

A pattern may destructure an enum payload and binds names for the body (see
[enums.md](enums.md)):

```lynx
switch (result) {
    case(status.Ok(value)){ println("ok ", value); }
    case(status.Failed(reason)){ println("failed ", reason); }
}
```

### `while`, `for`, `doWhile`, `iterate`, `forever`

```lynx
while (n > 0) {
    n -= 1;
}

for (int i = 0; i < 3) {   // the update is implicit: i = i + 1
    println(i);
}

doWhile (n < 3) {          // tests before each pass after the first
    n += 1;
}

doWhile(){                 // no-condition form: loops until `break`
    n += 1;
    if (n > 9) { break; }
}

iterate (4) {              // exactly four iterations
    println("tick");
}

forever(){                 // loops until `break`
    if (done) { break; }
}
```

- Every loop keyword takes parentheses. `forever` and the condition-less
  `doWhile` are written `forever(){ ... }` and `doWhile(){ ... }`.
- `for` takes an optional update clause; **`i++` and `i--` do not exist**. Use
  the implicit update, `i = i + 1`, or `i += 1`.
- `iterate(count)` requires an integer count.

### `break`, `continue`, `restart`

- `break` leaves the innermost loop or runs the `switch` default.
- `continue` jumps to the next iteration.
- `restart` re-runs the current iteration from the top.

### `try` / `catch`

```lynx
try {
    println(1 /% 0);
} catch (str error) {
    println("caught ", error);   // caught division by zero
}
```

The catch clause requires a type, normally `(str error)`. The caught value is
the message string.

### `return`

`return expr;` returns a value; a bare `return;` returns nothing. The value is
checked against the function's return annotation, if any.

## Functions

There are three declaration forms:

| Form | Written | Where | Called from |
|------|---------|-------|-------------|
| Global function | `global name(...)` | between `setup` and `main` | `global.name(...)` or a bare name in the same file |
| File-level function | `func name(...)` | between `setup` and `main` | a bare name in the same file; `global.module.name(...)` from an importer |
| Local function | `local name(...)` | inside another function body | a bare name, or `local.name(...)` |

`global func name(...)` is **not** valid syntax; use `func name(...)` for a
file-level function.

```lynx
func double(int n) -> int {
    return n * 2;
}

global add(int a, int b = 1) -> int {
    return a + b;
}

global main() {
    local square(int n) -> int {
        return n * n;
    }
    println(double(4));        // 8
    println(global.add(2));    // 3
    println(square(3));        // 9
}
```

### Parameters and defaults

- Parameters may be typed (`int n`) or untyped (treated as `any`).
- A parameter may have a default (`int b = 1`).
- A required parameter may **not** follow one with a default:
  `required parameters cannot follow a parameter with a default value`.
- Class methods are the exception: their parameters are typed but may **not**
  have defaults (see [classes.md](classes.md)).

### Return annotations

Only the arrow form is accepted:

```lynx
global f(int x) -> int { return x + 1; }
```

- `: int` is **not** valid syntax (`unexpected character ':'`).
- Omit the annotation, or use `-> any`, to leave the result unchecked.
- `-> none` is accepted: the function must return nothing, and a returned value
  fails with `value cannot be assigned to type 'none'`.
- A mismatched return is a source-located runtime error.
- Named types (a `struct`, `class`, or `enum`) may be used as the annotation.

### Codeblocks

A function may declare caller-supplied blocks after its parameters, and invoke
them with `exec()`:

```lynx
global repeat(str label){body} {
    print(label);
    print(": ");
    exec(){{body}}
    println("");
}

global main() {
    global.repeat("msg") {
        print("hello");
    }
}
```

`exec({{name}})` runs a previously declared named codeblock. `setup` and `main`
cannot take codeblock parameters.

## Scoping

Lynxer has **no nested block scopes**, and blocks do not shadow:

- `setup()` and `main()` share one top-level scope. A variable declared in
  either — including inside an `if`, `while`, `for`, `switch`, or `try` body —
  is visible in the other and to any function called later.
- Declaring the same name again overwrites the previous value rather than
  shadowing it.
- A function or method call gets a fresh scope for its parameters and locals.
  Those do not leak out, but the function can read and update the top-level
  variables it closes over.

```lynx
global setup() {
    int total = 0;
}

func add(int n) {
    total += n;        // updates the top-level variable
    int scratch = n;   // local to add(); not visible outside
}

global main() {
    add(2);
    add(3);
    println(total);    // 5
}
```

## String interpolation

`inter"..."` interpolates `{expr}` inside a string, with `\{`, `\}`, and `\\`
as escapes:

```lynx
global main() {
    int n = 3;
    println(inter"n is {n}");   // n is 3
}
```

Interpolation is available in the arguments of `print`, `println`, `input`, and
`inputln`; elsewhere it is a syntax error.

## Modules

`import("name")` and `importAs("name", "alias")` load `.lynx` source modules or
native `.so` libraries. They are **statements**, so they run from any function
body — most often `setup()` — not at file scope. Both arguments must be string
**literals**. See [modules.md](modules.md).

## Aggregate types

- [structs.md](structs.md) — data-only records built with `new`.
- [classes.md](classes.md) — fields plus `local` methods.
- [enums.md](enums.md) — tagged unions with payloads and `switch` patterns.
- [vargroups.md](vargroups.md) — typed records with defaults and dot access.
- [lists.md](lists.md) — `list` and `tuple` values and their builtins.

## Not supported

See [limitations.md](limitations.md) for the complete list. The most common
surprises are a bare `!`, `/* ... */` comments, `\x`/`\u` escapes, the `.lynxc`
bytecode format, and the Python-only modules (`tkinter`, `tkinterPlus`,
`turtle`, `venv`).
