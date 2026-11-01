# The Lynxer language

This page describes the Lynxer language **as the interpreter actually parses and
runs it**, with every example verified against the interpreter. For the
behaviour that is deliberately constrained or not implemented, see
[limitations.md](limitations.md).

Related pages: [functions.md](functions.md), [types.md](types.md),
[lists.md](lists.md), [structs.md](structs.md), [templates.md](templates.md),
[classes.md](classes.md), [enums.md](enums.md), [vargroups.md](vargroups.md),
[modules.md](modules.md), and [builtins.md](builtins.md).

## A first program

```lynx
fn setup() {
    println("ready");
}

fn main() {
    println("Hello, Lynxer!");
}
```

Requires the interpreter (see [install.md](install.md)) and runs with
`lynxer hello.lynx`. `setup()` is optional and its absence produces a warning;
`main()` is required and its absence is an error.

## Program structure

A file is a sequence of top-level declarations. Functions use `fn`, and may
appear before or after either entry point:

```lynx
fn calculate(int value) -> int {
    return value * 2;
}

fn main() {
    println(calculate(21));
}
```

`fn setup()` is an optional initialization entry point; `fn main()` is
mandatory. Other declarations, including functions, may follow `main()`. A
template's or class's parent must still be declared before the child.
The legacy `global`, `local` and `func` function keywords continue to parse
with deprecation warnings; use `fn` for new code.

Executable code outside a function body is a syntax error
(`expected top-level function declaration`).

`setup()` and `main()` may take **defaulted parameters**; they are filled in
before the body runs:

```lynx
fn setup(int retries = 3) {
    println("retries ", retries);   // retries 3
}

fn main() {
    println("main");
}
```

### `overrideMain`

When `setup()` calls `overrideMain("name")`, the named function runs instead
of `main`. The program must still declare `main()`:

```lynx
fn setup() {
    overrideMain("start");
}

fn start() {
    println("started");   // printed
}

fn main() {
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
fn setup() {
    int count = 0;
    const str APP = "demo";
    println(APP, " ", count);   // demo 0
}

fn main() {
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

## Ownership and borrowing

Variables own their value. A few built-ins make moves and temporary aliases
explicit, which matters when a value is expensive to copy.

```lynx
int source = 42;
int destination = 0;
varTransfer(source, destination);   // source is moved; destination owns 42

int borrower = 0;
varBorrow(destination, borrower);   // borrower is a read-only alias
println(borrowing(borrower));        // true
println(beingBorrowed(destination)); // true
varEndBorrow(borrower);              // borrower becomes an independent copy
```

- `varTransfer(source, destination)` moves the value into an already-declared
  destination whose declared type must accept it. A moved source cannot be read,
  passed to a function, borrowed or moved again; a plain assignment reinitialises
  it. Transfers are rejected while either side has an active borrow, and
  constants cannot be moved.
- `varTransferMutate(source, destination)` is the move form for an `any` or
  `num` destination: it accepts a value of any runtime type, where
  `varTransfer` requires the declared type to match.
- `varBorrow(source, borrower)` makes a read-only tracked alias. The source
  stays readable and several read-only borrowers may coexist, but neither side
  may be written while the borrow is active. `varEndBorrow(borrower)` ends it
  and gives the borrower an independent copy of the source's current value.
  Borrowing a moved value, borrowing into a constant, and ending a borrow that
  is not active are errors.
- `borrowing(variable)` reports whether a variable is currently a borrower, and
  `beingBorrowed(variable)` whether another variable is borrowing from it. Both
  accept moved variables, so ownership state can be inspected while handling an
  error.
- `varBorrowMutate(source, borrower)` makes an exclusive mutable borrow: the
  borrower aliases the same storage, so writes through either name are visible
  through the other.
  Another active borrow makes it fail, and ending it with `varEndBorrow` leaves
  the borrower independent.
- `varSwapAll(first, second)` exchanges values **and** declared type metadata;
  `varSwapVal(first, second)` exchanges only values and keeps each declared type,
  so both cross-assignments must be type-compatible. Both require independent,
  live, mutable variables and validate everything before changing either one.

### Shared variables

Prefix a typed declaration with `shared` to make it a mutable alias of an
existing variable. Assignments through either name update the same storage:

```lynx
int x = 42;
shared int y = x;
y = 100;       // x is now 100 too
x = 7;         // y observes the same value
unshare(y);    // y becomes independent, keeping its current value
y = 200;       // x stays 7, y is 200
```

The initializer must be a **variable name**, the declared type must accept the
source's value, and the source must be readable (not moved). `unshare(name)`
detaches the alias; the detached variable keeps its current value and declared
type. `shared` is a declaration form of `varBorrowMutate`, and `unshare` is an
alias of `varEndBorrow` — so a shared variable reports `borrowing(alias)` and
`beingBorrowed(source)` as `true` until it is detached.

The arguments to these built-ins are **variable names**, not values. A runnable
example is `lynxer/examples/ownership.lynx`.

## Types

The complete type list, ranges, and conversion rules live in
[types.md](types.md). The scalar names are `int`, `float`, `num`, `bool`,
`str`, `char`, `numBool`, `bit`, `byte`, `int8`/`int16`/`int32`/`int64`,
`uint8`/`uint16`/`uint32`/`uint64`, `float32`/`float64`, plus `any`, `none`,
`list`, `tuple`, `codeblock`, and the named `struct`/`template`/`class`/`enum`
types.

## Operators

Lynxer uses keyword operators: `is` / `isnt`, `and` / `or` / `not`, `bitand` /
`bitor` / `bitxor`, `bitleft` / `bitright`, and so on. The symbolic C-style
spellings (`==`, `!=`, `&&`, `||`, `&`, `|`, `^`, `<<`, `>>`, …) still parse but
are **deprecated** and print a warning; a bare `!` is a syntax error.

```lynx
int n = 6;
if((n % 2 is 0) and n > 4){
    println("even and greater than four");
}
println(n bitand 3);   // 2
```

→ **[Full operator reference, precedence and the deprecation table](operators.md)**

## Control flow

- [conditionals.md](conditionals.md) — `if` / `elif` / `else` and
  `switch` / `case` / `default`.
- [loops.md](loops.md) — `while`, `for` (both the C style and the range form),
  `doWhile`, `iterate`, `forever`, and `break` / `continue` / `restart`.
- [try-catch.md](try-catch.md) — `try` / `catch`, `raise`, and `exceptionInfo()`.

### `try` / `catch`

```lynx
try {
    println(1 /% 0);
} catch (str error) {
    println("caught ", error);   // caught division by zero
}
```

`try` handles runtime errors. The binding clause is optional; the caught value
is the message string. `raise(message)` raises one, and `exceptionInfo()` returns
the traceback inside a handler.

→ **[Full `try` / `catch` reference: `raise`, `exceptionInfo()`, nesting, and what is not caught](try-catch.md)**

### `return`

`return expr;` returns a value; a bare `return;` returns nothing. The value is
checked against the function's return annotation, if any.

## Functions

See [functions.md](functions.md) for the full reference. `fn` is used for
top-level and nested functions; named lambdas remain available:

| Form | Written | Where | Called from |
|------|---------|-------|-------------|
| Function | `fn name(...)` | file or nested function scope | bare name, module namespace, or nested dot path |
| Named lambda | `lambda name(...)` | inside a function body | its name in the declaring scope |

`global`, `local` and `func` remain accepted as deprecated declaration
keywords; use `fn` instead.

```lynx
fn double(int n) -> int {
    return n * 2;
}

fn add(int a, int b = 1) -> int {
    return a + b;
}

fn main() {
    fn square(int n) -> int {
        return n * n;
    }
    println(double(4)); // 8
    println(add(2));    // 3
    println(square(3)); // 9
}
```

Named lambdas are local to their declaring function scope and are not exported;
nested function scopes cannot see them. See
[functions.md](functions.md#named-lambdas) for an example.
Functions and lambdas can also be stored as `function` values, returned, and
passed as arguments; see [function values](functions.md#function-values).

### Parameters and defaults

- Parameters may be typed (`int n`) or untyped (treated as `any`).
- A parameter may have a default (`int b = 1`).
- A required parameter may **not** follow one with a default:
  `required parameters cannot follow a parameter with a default value`.
- `type T of value` declares an inferred type parameter for an ordinary
  function. Reuse `T` in later parameter types, local declarations, or the
  return annotation; each call infers `T` from `value`. See
  [functions.md](functions.md#parameters-and-defaults) for an example and
  restrictions.
- Template and class methods are the exception: their parameters are typed but
  may **not** have defaults (see [templates.md](templates.md) and
  [classes.md](classes.md)).

### Return annotations

Only the arrow form is accepted:

```lynx
fn f(int x) -> int { return x + 1; }
```

- `: int` is **not** valid syntax (`unexpected character ':'`).
- Omit the annotation, or use `-> any`, to leave the result unchecked.
- `-> none` is accepted: the function must return nothing, and a returned value
  fails with `value cannot be assigned to type 'none'`.
- A mismatched return is a source-located runtime error.
- Named types (a `struct`, `template`, `class`, or `enum`) may be used as the
  annotation.

### Codeblocks

A function may declare caller-supplied blocks after its parameters and invoke
them with `exec()`. See **[codeblocks.md](codeblocks.md)** for the full
reference.

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
fn setup() {
    int total = 0;
}

fn add(int n) {
    total += n;        // updates the top-level variable
    int scratch = n;   // local to add(); not visible outside
}

fn main() {
    add(2);
    add(3);
    println(total);    // 5
}
```

## String interpolation

`inter"..."` interpolates `{expr}` inside a string, with `\{`, `\}`, and `\\`
as escapes:

```lynx
fn main() {
    int n = 3;
    str message = inter"n is {n}";
    println(message);          // n is 3
    println(inter"also {n}"); // also 3
}
```

`inter"..."` is a string expression, so it can be used wherever a string value
is accepted, including output calls, assignments, and return values:

```lynx
fn describe(int n) -> str {
    return inter"value: {n}\n";
}
```

The embedded expressions run when the string is evaluated. Each result is
converted to text and inserted between the literal parts.

### Interpolating output calls

`printf(...)` and `printlnf(...)` are the interpolating counterparts of
`print(...)` and `println(...)`. A double-quoted string literal written directly
as an argument is treated as if it had an `inter` prefix, so `{expr}` is
evaluated without constructing an explicit `inter"..."` value:

```lynx
fn main() {
    int n = 3;
    str name = "Ada";
    printf("n is {n} and name is {name}\n");
    printlnf("next year: {n + 1}"); // next year: 4
}
```

`printf` writes the result without a trailing newline; `printlnf` adds one.
The escape rules match `inter"..."`: `\{` and `\}` insert literal braces and
`\\` inserts a backslash.

Only string literals written directly in the call are interpolated. A string
held in a variable is printed unchanged:

```lynx
str pattern = "n is {n}";
printlnf(pattern); // n is {n}
```

## Modules

`import("name")` and `importAs("name", "alias")` load `.lynx` source modules or
native `.so` libraries. They are **statements**, so they run from any function
body — most often `setup()` — not at file scope. Both arguments must be string
**literals**. See [modules.md](modules.md).

## Aggregate types

- [structs.md](structs.md) — data-only records built with `new`.
- [templates.md](templates.md) — header constructors, `build ... named`, fields,
  `fn` methods, and `extending`.
- [classes.md](classes.md) — deprecated class form.
- [enums.md](enums.md) — tagged unions with payloads and `switch` patterns.
- [vargroups.md](vargroups.md) — typed records with defaults and dot access.
- [lists.md](lists.md) — `list` and `tuple` values and their builtins.

## Not supported

See [limitations.md](limitations.md) for the complete list. The most common
surprises are `\x`/`\u` escapes, the `.lynxc` bytecode format, and the
not-planned modules (`tkinter`, `tkinterPlus`, `turtle`, `venv`).
