# Language Reference

## Program structure

Every Lynxer program has one required top-level declaration and one customisable entry point:

1. **`global setup(){}`** — must be the **very first** declaration. The only place to declare global variables and call `import()`. Required even when empty.
2. **Entry point** — by default this is `global main(){}`, which must be the last declaration. You can replace it with any global function using [`overrideMain()`](#overriding-the-entry-point).

Additional global functions and class definitions may be declared **between** `setup` and `main`:

```c
global setup(){
    import("math");
    const str APP = "MyApp";
    int counter = 0;
}

// global helpers go here, between setup and main
global greet(str name){
    print("Hello, "); print(name); print("!\n");
}

class Config {
    int maxRetries = 3;
    local getMax() { return this.maxRetries; }
}

global main(){
    global.greet(APP);
}
```

**Rules:**
- `global setup(){}` is **mandatory** and must be first — before all other globals, classes, and main.
- `global main(){}` must be the **last** declaration when present — no declarations may follow it.
- Global function and class declarations are only allowed between `setup` and `main`.
- Executable code outside a function body is a syntax error.
- `import()` may only appear inside `setup()`.

---

## Overriding the entry point

By default Lynxer calls `global main(){}` after setup finishes. You can redirect this to any other global function with `overrideMain("funcName")` inside `setup()`:

```c
global setup(){
    overrideMain("start");   // "start" runs instead of main
}

global start(){
    print("Hello from start!\n");
}
```

When `overrideMain` is set, `global main(){}` is **not required**. Both can be present — the named function wins and `main` is never called.

```c
global setup(){
    overrideMain("app");
}

global app(){
    print("app is the entry point\n");
}

global main(){
    // this is NEVER called when overrideMain is active
    print("main\n");
}
```

If the named function does not exist, a runtime error is raised with the missing name clearly shown.

---

## Comments

```c
// single-line comment

///
  multi-line comment
  spans as many lines as needed
///

////
  This is a docstring
  It should be called before global setup(){}
////
```

---

## The `global` namespace

`global` is a built-in namespace that exposes every built-in function, every imported module, and every global constant. You can always qualify a name with `global.` even when it is also accessible directly.

```c
// these are equivalent
print("hello\n");
global.print("hello\n");

// module functions must use global.<module>.<function>()
global.math.sqrt(144)
global.typing.toStr(99)
```

**What lives under `global`:**
- All built-in functions: `print`, `input`, `strOf`, `intOf`, `floatOf`, `returnType`, `returnLength`, `seqFromTo`, `range`, all list built-ins, `rawPy`, `rawPyx`, etc.
- All imported modules: after `import("math")`, accessible as `global.math`.
- Global constants and variables declared in `setup()`.
- The class registry: `global.class.ClassName` accesses a defined class.

Built-in functions are conventionally called **directly** (without `global.`). Module functions are always called as **`global.<module>.<function>()`**.

---

## Types

| Type       | Example values            | Notes |
|------------|---------------------------|-------|
| `int`      | `42`, `-7`, `0`           | integer |
| `float`    | `3.14`, `-0.5`            | floating-point; `int` and `float` are interchangeable in expressions |
| `num`      | `42`, `3.14`              | flexible numeric — accepts both `int` and `float`; `returnType()` reflects the actual stored kind |
| `char`     | `'a'`, `'\n'`             | single Unicode character; single-quote literal; concatenates with `str` |
| `str`      | `"hello"`, `"line\n"`     | double-quoted; supports `\n \t \\ \r \e` escapes |
| `bool`     | `true`, `false`           | displays as `true`/`false`; truthy when non-zero |
| `numBool`  | integer `0` or `1`        | numeric boolean; displays as `0`/`1` |
| `bit`      | integer `0` or `1`        | one-bit numeric value |
| `byte`     | integer `0` through `255` | unsigned 8-bit numeric value |
| `int8`     | signed 8-bit integer      | range `-128..127` |
| `int16`    | signed 16-bit integer     | range `-32768..32767` |
| `int32`    | signed 32-bit integer     | range `-2147483648..2147483647` |
| `int64`    | signed 64-bit integer     | range `-9223372036854775808..9223372036854775807` |
| `uint8`    | unsigned 8-bit integer    | range `0..255` |
| `uint16`   | unsigned 16-bit integer   | range `0..65535` |
| `uint32`   | unsigned 32-bit integer   | range `0..4294967295` |
| `uint64`   | unsigned 64-bit integer   | range `0..18446744073709551615` |
| `float32`  | single-precision float    | finite range up to approximately `3.4028235e38` |
| `float64`  | double-precision float    | finite range up to approximately `1.7976931e308` |
| `tuple`    | `(int 1, int 2, int 3)`   | immutable, fixed-length sequence; see [tuples.md](tuples.md) |
| `sentinel` | `sentinel("MISSING")`     | unique identity marker; compare with `is` / `not is` |
| `codeblock`| `{ println("hi"); }`      | stored Lynxer statements; execute with `exec(){{name}}` |
| `list`     | `range(5)`                | ordered mutable sequence; declare with `list` keyword |
| `vargroup` | `vargroup p = {...}`      | named typed record with dot-accessed fields; see [vargroups.md](vargroups.md) |
| `any`      | anything, including `none`| no type check at assignment |

`none` is a null-like literal, assignable to `any`:

```c
any x = none;
any y = 42;
y = "now a string";   // fine — any skips type checks
```

Type declarations are enforced at runtime. Assigning the wrong type is a runtime error:

```c
int n = 42;
n = "oops";   // Runtime Error: Type mismatch
```

Use `num` when a variable needs to hold either an integer or a floating-point value without knowing which ahead of time:

```c
num x = 10;      // holds an int
x = 3.14;        // now holds a float — no error
x = x + 1;       // arithmetic works normally
print(returnType(x));  // "float" — reflects the actual stored kind
```

`num` enforces that the value is always numeric — assigning a string or other type is still a runtime error.

### The `char` type

`char` holds exactly one Unicode character. Use single-quote literals:

```c
char c = 'A';
char newline = '\n';
char tab     = '\t';
```

You can also assign a single-character string — the interpreter auto-converts it:

```c
char c = "B";    // fine — single-char string converted to char
char bad = "hi"; // Runtime Error — string length 2 is not a char
```

Concatenating a `char` with another `char` or a `str` produces a `str`:

```c
char a = 'H';
char b = 'i';
str s = a + b;           // "Hi"
str t = a + "ello";      // "Hello"
print(returnType(a));    // char
print(returnType(s));    // str
```

`char` values support `==` / `!=` equality:

```c
char x = 'z';
if(x == 'z'){ print("yes\n"); }
```

Use `typing` helpers for char-specific operations:
- `global.typing.charCodeOf(c)` — Unicode code point of a char
- `global.typing.toChar(n)` — char from a code point integer
- `global.typing.charAt(s, idx)` — extract the char at position `idx` from a string

### The `list` type

Lists are first-class values. `returnType()` returns `"list"` for them. They are created with `range()` or `seqFromTo()`, or built up with `listPush()`. Because Lynxer uses value semantics, mutating built-ins like `listPush` and `listSet` return a **new** list — always reassign the result:

```c
list lst = range(5);             // [0, 1, 2, 3, 4]
lst = listPush(lst, 10);         // [0, 1, 2, 3, 4, 10]
int n = listGet(lst, 0);         // 0
print(returnType(lst));          // list
print(returnLength(lst));        // 6
```

See [builtins.md](builtins.md#list-operations) for the full list API.

### Booleans

Boolean literals are `true` and `false`. All comparison and logic operators produce `bool`. Boolean values print as `true` or `false`:

```c
bool alive = true;
print(alive);           // true
print(5 > 3);           // true
print(true and false);  // false
print(not true);        // false
```

---

## Variables

```c
int x = 10;
float pi = 3.14159;
num score = 0;        // accepts int or float freely
str msg = "hi";
bool alive = true;
any thing = none;

const str VERSION = "1.0";   // immutable — reassignment is a runtime error
```

All variables must be declared with a type keyword before use. The declaration initialises them.

**Reassignment** — after a variable has been declared, assign a new value using just the variable name, **without the type keyword**:

```c
int x = 10;   // declaration — type keyword required
x = 20;       // reassignment — no type keyword
x += 5;       // compound assignment: x is now 25
```

Using the type keyword a second time on the same name re-declares the variable — this is also how you **change a variable's type**:

```c
int x = 10;
x = 20;         // reassignment — still an int, type enforced
str x = "hi";   // re-declaration — x is now a str
x = "world";    // fine — x is a str
```

Re-declaration replaces both the value and the recorded type, so subsequent bare assignments are checked against the new type. The normal idiom is bare assignment for same-type updates and re-declaration only when you intentionally change the type.

> **List variables** use the `list` keyword: `list nums = range(5);`

### Shared variables

Prefix a typed declaration with `shared` to create an alias to another
variable. Assignments through either name update the same stored value:

```c
int x = 42;
shared int y = x;

y = 100;       // x is now 100 too
unshare(y);    // y becomes independent, keeping its current value
y = 200;       // x remains 100, while y is 200
```

The initializer for a shared declaration must be a variable name, and its
value must satisfy the declared type. `unshare(name)` takes exactly one
variable name and detaches that alias. The detached variable keeps its
current value and retains its declared type.

**Global variables** must be declared inside `setup()`. They are accessible from any function in the file.

---

## Operators

### Arithmetic

| Op | Description |
|----|-------------|
| `+` | add (also concatenates strings) |
| `-` | subtract |
| `*` | multiply |
| `/` | divide (always returns float) |
| `%` | modulo |
| `**` | exponentiation |
| `/*` | root (`a /* b` is the b-th root of a) |
| `/%` | floor division (division without a fractional result) |

### Compound assignment

```c
x += 5;   // x = x + 5
x -= 3;   // x = x - 3
x *= 2;   // x = x * 2
x /= 4;   // x = x / 4
x %= 3;   // x = x % 3
x **= 2;  // x = x ** 2
x /*= 2;  // x = x /* 2
x /%= 2;  // x = x /% 2
```

### Comparison

| Op | Description |
|----|-------------|
| `<` | less than |
| `>` | greater than |
| `<=` | less than or equal |
| `>=` | greater than or equal |
| `==` | equal (works for `int`, `float`, `str`, `bool`) |
| `!=` | not equal |
| `is` | legacy spelling for `==` (deprecated) |
| `not is` | legacy spelling for `!=` (deprecated) |

### Logic

```c
x > 0 && x < 10
x < 0 || x > 100
!!alive

// NAND and NOR are the negated logical combinations.
x > 0 !&& x < 10
x < 0 !|| x > 100
```

### Bitwise

| Op | Description |
|----|-------------|
| `&` | AND |
| `\|` | OR |
| `^` | XOR |
| `!&` | NAND |
| `!^` | XNOR |
| `!|` | NOR |
| `~` | NOT (unary) |
| `<<` | left shift |
| `>>` | right shift |

### Operator precedence (high → low)

`~` → `** /*` → `* / % /%` → `+ -` → `<< >>` → `& !&` → `^ !^` → `| !|`
→ comparisons → `!!` → `&& !&&` → `|| !||`

The word forms `and`, `or`, and `not` remain supported. The legacy equality
forms `is` and `not is` are accepted for compatibility but emit a deprecation
warning; use `==` and `!=` in new code.

---

## Control flow

### if / elif / else

```c
if(x > 0){
    print("positive\n");
} elif(x == 0){
    print("zero\n");
} else {
    print("negative\n");
}
```

`elif(condition){}` chains another condition after an `if` or previous
`elif`. At most one matching branch runs; the final `else{}` is optional.

### switch / case / default

`switch(value){}` selects the first `case(value){}` block whose value equals
the switch value. If no case matches, an optional `default(){}` block runs.
Cases do not fall through to one another: after a matching case finishes, the
switch exits automatically. `break;` is not required, and omitting it never
continues into the next case as it would in C.

```c
int status = 2;

switch(status){
    case(1){
        println("pending");
    }
    case(2){
        println("complete");
    }
    case(3){
        println("failed");
    }
    default(){
        println("unknown");
    }
}
```

`case` and `default` blocks are only valid directly inside a `switch` block.
A switch may contain any number of cases and at most one `default()` block.

### while

```c
int i = 0;
while(i < 5){
    print(i); print("\n");
    i += 1;
}
```

### for

```c
for(int i = 0; i < 10; i = i + 1){
    print(i); print("\n");
}
```

The update clause may be omitted; it defaults to incrementing the loop
variable by one:

```c
for(int i = 0; i < 3){
    print(i); print("\n");
}
```

Compound assignment operators are also supported in the update clause:
`+=`, `-=`, `*=`, `/=`, `%=`, `**=`, `/*=`, and `/%=`:

```c
for(int i = 1; i < 20; i *= 2){
    print(i); print("\n");
}

for(int i = 2; i < 100; i **= 2){
    print(i); print("\n");
}

for(int i = 20; i > 1; i /%= 3){
    print(i); print("\n");
}
```

### doWhile

`doWhile(condition){}` runs its body once before checking the condition, then
repeats the body while the condition remains true. The condition is optional,
so `doWhile(){}` repeats until it reaches a `break;`:

```c
int i = 0;
doWhile(i < 5){
    print(i); print("\n");
    i += 1;
}
```

`break` exits the loop and `continue` proceeds to the condition check.

### iterate

Run a block a fixed number of times without a loop variable:

```c
iterate(5) {
    println("hello");
}

// the count can be any expression
int n = 3;
iterate(n * 2) {
    println("again");
}
```

`break` and `continue` work inside `iterate` the same as in `while` and `for`.

### forever

Run a block repeatedly with a short delay between iterations. A `forever`
loop should normally contain a `break;` condition:

```c
global setup(){
    foreverDelay(0.02);
}

global main(){
    int i = 0;
    forever(){
        i += 1;
        if(i == 5){ break; }
    }
}
```

The default delay is `0.02` seconds. Use `suppressForeverWarning()` in
`setup()` when a loop is intentionally unbounded. `break`, `continue`, and
`restart` work in `forever` loops.

### break / continue / restart

`break;` exits the nearest enclosing loop immediately.
`continue;` and `restart;` (both work identically) skip the rest of the current loop body and jump to the next iteration. In a `for` loop, the update step still runs before the next iteration.

```c
// break
for(int i = 0; i < 10; i = i + 1){
    if(i == 5){ break; }
    print(i); print(" ");
}
// prints: 0 1 2 3 4

// continue
for(int i = 0; i < 6; i = i + 1){
    if(i % 2 == 0){ continue; }
    print(i); print(" ");
}
// prints: 1 3 5

// restart (same as continue)
int w = 0;
while(w < 5){
    w += 1;
    if(w == 3){ restart; }
    print(w); print(" ");
}
// prints: 1 2 4 5
```

`break` and `continue`/`restart` work in `for`, `while`, `doWhile`, `iterate`,
and `forever` loops. They are **not** valid outside a loop.

---

## Functions

Lynxer has two kinds of functions with distinct scopes:

| Kind | Keyword | Where | Visible from |
|------|---------|-------|--------------|
| Global | `global` | Top-level only (between `setup` and `main`) | Any function in the file |
| Local | `local` | Inside any function body | Only inside the declaring function |

### Global functions (`global`)

`global` functions must be declared at the **top level of the file** — between `setup` and `main`. They may also be declared inside another `global` function body (the parser permits nested `global` definitions inside an enclosing `global`). Attempting to define a `global` inside a `local` function is a syntax error.

```c
global setup(){}

global add(int a, int b){
    return a + b;
}

global greet(str name){
    print("Hello, "); print(name); print("!\n");
}

global main(){
    int sum = global.add(3, 4);   // 7
    global.greet("World");
}
```

All `global` functions must be called with the `global.` prefix when invoked from inside another global function.

### Local functions (`local`)

`local` is the keyword for **local** helpers declared inside a function body. They are not accessible outside their declaring scope.

```c
global main(){
    local square(int n){
        return n * n;
    }
    print(local.square(5)); print("\n");   // 25
}
```

`local` functions can be nested (a `local` inside a `local`). Calling a nested local uses the chained `local.outer.inner()` syntax:

```c
global main(){
    local outer(){
        local inner(){
            return 42;
        }
        return local.inner();
    }
    print(local.outer()); print("\n");  // 42
}
```

**Scope rules for `local`:**
- Visible only after the line it is declared.
- A `local` shadows any outer name of the same name within its declaring scope.
- Variables are looked up through the parent context chain at call time.
- Always call a local function with the `local.` prefix: `local.funcName()`.

### Typed parameters

Parameters may be typed (enforced at call time) or left untyped (treated as `any`):

```c
global typed(int a, str b){ ... }
global untyped(a, b){ ... }         // any type accepted
global mixed(int n, any x){ ... }
```

A call that passes a value of the wrong type is a runtime error.

### Default parameters

Parameters may provide a default expression. The default is used when the
caller omits that trailing argument, and an explicit argument overrides it:

```c
global foo(int x, str y = "hello"){
    println(x, y);
}

global main(){
    global.foo(42);           // prints: 42hello
    global.foo(42, "world");  // prints: 42world
}
```

Required parameters must come before parameters with defaults. Defaults are
evaluated at call time, so expressions are evaluated for each call. This also
applies to parameters on `global setup(){}`; setup still runs in the program
or module's global scope.

### Caller-supplied code blocks

Global, local, and class-method functions may declare named parameters for
code supplied by the caller. Put each code-block name in braces after the
normal parameters, then write the function body:

```c
global repeat(str label){body}{
    print(label); print(": ");
    exec(){{body}}
    print("\n");
}

global main(){
    global.repeat("message"){
        print("hello from the caller");
    }
}
```

`exec(){{body}}` runs the supplied Lynxer code at that point in the function.
The code runs in the callee's context, so it can use the function's parameters
and local variables. It can call existing functions, use control flow, and
return from the callee.

### Multiple code blocks

Declare multiple blocks in the order in which they should be bound:

```c
global runTwice(){first}{second}{
    exec(){{first}}
    exec(){{second}}
}

global main(){
    global.runTwice(){
        print("one\n");
    }{
        print("two\n");
    }
}
```

The comma form `{first, second}` is also accepted. A call must provide exactly
the number of blocks declared by the function, and blocks are bound in
declaration order.

The semicolon after a call is optional when the call ends with one or more
code blocks:

```c
global.runTwice(){ print("one\n"); }{ print("two\n"); }
```

Ordinary function calls still require a semicolon:

```c
global.runTwice();   // syntax error: no code blocks were supplied
global.greet("Hi");  // normal call; semicolon required
```

Code-block parameters are not allowed on `setup()` or the program-entry
`main()`. Supplied code cannot define `local`, `global`, or `async` functions,
and cannot attach another code block to a call. It may call functions that were
defined elsewhere.

### Return values

`return` exits the function and optionally produces a value:

```c
global compute(int n){
    return n * 2;
}

global main(){
    int r = global.compute(5);   // 10
}
```

A function without an explicit `return` produces `none`.

---

### Named codeblock values and `exec`

Use the `codeblock` type to store Lynxer statements in a variable:

```c
codeblock helloWorld = {
    println("Hello world!");
};

exec(){{helloWorld}}
```

Stored codeblocks may declare the names and types of the values they receive
after the body:

```c
codeblock example = {
    println(name, age);
}[str name, int age]

exec("Alice", 30){{example}}
```

The declared list is positional. Each name used by the body must appear in the
list, and each supplied value is checked against its declared type. The
semicolon after the closing `]` is optional.

The semicolon after a stored codeblock body or parameter list is optional.

`exec(){...}` executes an inline block. Values used by the block are declared
in the `exec` parameter list, using the same typed parameter syntax as a
function:

```c
str text = "left";
exec(str text){
    println(text);
}
```

Each declared value is looked up in the surrounding scope, checked against its
declared type, and is available under its declared name while the block runs.
The declaration is temporary and does not create magic `exec` variables.

When the parameter list is omitted, names are inferred from the user variables
referenced by the codeblock:

```c
codeblock example = {
    println(text);
};

exec("Hello!"){{example}}
```

The first value is bound to `text`, the next value to the next referenced
variable, and so on. The number of provided values must match the number of
variables required by the codeblock. These bindings are temporary and are
restored after execution. This inferred form is equivalent to being prompted
for `text` in the example; a block using `name` and `age` would require those
two positional values.

The old `exec({name})` form is replaced by `exec(){{name}}`.

Code-block identifiers are unique across the complete source file, regardless
of where they are declared. A `codeblock` variable and a caller-supplied
code-block parameter therefore cannot reuse the same identifier. Named blocks
can be passed to functions from another level with the double-brace form:

```c
global setup(){
    codeblock code1 = {
        println("A");
    };
}

global something(){handler}{
    exec(){{handler}}
}

global main(){
    global.something(){{code1}}
}
```

The block keeps its unique identity when copied into a function parameter, so
it can be referenced through `exec(){{name}}` wherever that named block is
available.

---

## Classes

A **class** is a reusable object definition that groups typed fields and
methods. `new` creates a separate instance, and each method call is bound to
the instance used as its receiver. See [classes.md](classes.md) for a
quick-reference summary.

```c
global setup(){}

class Counter {
    int value = 0;

    local init(int initial) {
        this.value = initial;
    }

    local increment() {
        this.value = this.value + 1;
    }

    local get() {
        return this.value;
    }
}

global main(){
    Counter first = new Counter(1);
    Counter second = new Counter(10);
    first.increment();
    second.increment();
    print(first.get()); print("\n");   // 2
    print(second.get()); print("\n");  // 11
}
```

---

## rawPy and rawPyx

### exec block

`exec(){...}` parses and runs the Lynxer statements inside the block directly
in the current function context. Variables, control flow, `return`, and calls
therefore behave as if the statements had been written at the `exec` location.

```c
global greet(str name){
    exec(){
        print("Hello, ");
        print(name);
        print("!\n");
    }
}
```

An `exec` block cannot define `local`, `global`, or `async` functions,
including inside nested control-flow blocks. It can call functions that were
defined elsewhere:

```c
global add(int a, int b){
    return a + b;
}

global main(){
    int result = 0;
    exec(){
        result = global.add(20, 22);
    }
    print(result); print("\n"); // 42
}
```

### rawPy block

Embeds a Python code block inside a Lynxer function. Variables from the surrounding Lynxer scope are bridged into and out of the block.

```c
global main(){
    int total = 0;
    rawPy(){
        total = sum(range(1, 11))
    }
    print(total); print("\n");   // 55
}
```

#### Bridging rules

**Into Python (what Python can see):**

| Lynxer type | Python value |
|-------------|--------------|
| `int` | `int` |
| `float` | `float` |
| `str` | `str` |
| `bool` | `int` (0 or 1) |
| `list` | `list` (read-only — changes inside the block are not written back) |
| `none` | **not visible** |
| function/module | **not visible** |

**Out of Python (what gets written back):**

| Python type | Lynxer value |
|-------------|--------------|
| `bool` | `bool` (`true` / `false`) |
| `int` | `int` |
| `float` | `float` |
| `str` | `str` |
| anything else | ignored |

**Rules:**
- Only variables that already exist in Lynxer scope can be updated.
- Python code can read and update `int`, `float`, and `str` Lynxer variables.
- `list` values are visible as Python `list` inside rawPy blocks but are read-only (changes are not written back).
- Any `import` inside a rawPy block uses Python's import system.

### rawPyx block

Like `rawPy` but compiles code with Cython for potential speed gains. Requires Cython to be installed; falls back to Python exec if unavailable.

```c
int result = 0;
rawPyx(){
    result = 6 * 7
}
print(result); print("\n");   // 42
```

---

## Errors

Any unhandled runtime error terminates the program immediately with a message showing the error type, details, file, line, column, and a code snippet.

Common error types:
- `Type mismatch` — assigning wrong type to a typed variable
- `Runtime Error` — division by zero, index out of range, undefined variable, etc.
- `Unexpected Character` / `Missing Character` — lexer/syntax errors

---

## try / catch

`try/catch` lets you handle runtime errors instead of letting them terminate the program.

```c
// Form 1 — catch and bind the error message
try {
    // code that might fail
} catch(str err) {
    print(err); print("\n");
}

// Form 2 — catch without binding
try {
    // code that might fail
} catch {
    print("something went wrong\n");
}
```

- If a runtime error occurs in the `try` block, the `catch` block runs.
- If no error occurs, the `catch` block is skipped.
- `return`, `break`, and `continue` inside try/catch behave normally.
- Syntax and lexer errors are not catchable.

---

## Module system

### Importing

`import();` loads a `.lynx` file as a module. It may only be called inside `setup()`.

```c
global setup(){
    import("math");
    import("mylib");   // looks for mylib.lynx
}
```

**Search order:**
1. Same directory as the running script
2. The `stdlib/` folder bundled with Lynxer

**Idempotency:** Importing the same module twice is safe and has no effect the second time.

### Nested and path-based imports

An import path is resolved relative to the file that contains the import. This
allows a module to load another module from a lower directory:

```c
// app.lynx
global setup(){ import("lib/features.lynx"); }
global main(){ global.features.run(); }
```

If `lib/features.lynx` contains `import("helpers/format.lynx");`, Lynxer
looks for `helpers/format.lynx` relative to the `lib/` directory, not relative
to `app.lynx`. The module name is the filename without its extension
(`features` and `format` in this example).

Two different files that produce the same module name are rejected:

```c
global setup(){
    import("one/config.lynx");
    import("two/config.lynx");  // error: both modules are named "config"
}
```

Importing the exact same file more than once remains safe and reuses the
already-loaded module.

### Calling module functions

```c
global.<module>.<function>(args);
```

```c
global setup(){ import("math"); }

global main(){
    print(global.math.sqrt(144));  // 12
    print(global.math.pi());       // 3.141592653589793
    print("\n");
}
```

### Accessing module globals

Constants and variables declared in a module's `setup()` are accessible via `global.<module>.<name>`:

```c
/// config.lynx ///
global setup(){
    const str HOST = "localhost";
    const int PORT = 8080;
}
global main(){}
```

```c
global setup(){ import("config"); }
global main(){
    print(global.config.HOST); print("\n");   // localhost
    print(global.config.PORT); print("\n");   // 8080
}
```

---

## VarGroups

A **vargroup** is a named, typed record with dot-accessed fields — similar to a C struct. See [vargroups.md](vargroups.md) for a quick-reference summary.

```c
vargroup player = {
    str  username = "Andy",
    int  coins    = 250,
    bool online   = true,
    vargroup stats = {
        int   level = 5,
        float speed = 3.5
    }
};

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
    vargroup config = {str host = "localhost", int port = 8080};
}
global main(){
    print(config.host);   // localhost
    int config.port = 9000;
}
```

## Structs

A **struct** is a data-only named type. Its fields have no defaults and are
initialized in declaration order through `new`:

```c
global setup(){}

struct Player {
    int health;
    str name;
}

global main(){
    Player player = new Player(100, "Ada");
    println(player.name);
    player.health = 90;
}
```

Struct construction requires exactly one argument for every field, and each
argument must match that field's declared type. Structs support field access
and assignment, but cannot contain methods, inheritance, or uninitialized
fields. Use a `class` when behavior or default field values are needed.
