# Language Reference

## Program structure

Every Lynxer program must contain exactly two required top-level declarations, **in strict order**:

1. **`global setup(){}`** — must be the **very first** declaration. The only place to declare global variables and call `import()`. Required even when empty.
2. **`global main(){}`** — must be the **very last** declaration. The entry point; runs last.

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
    def getMax() { return this.maxRetries; }
}

global main(){
    greet(APP);
}
```

**Rules:**
- `global setup(){}` is **mandatory** and must be first — before all other globals, classes, and main.
- `global main(){}` is **mandatory** and must be last — no declarations may follow it.
- Global function and class declarations are only allowed between `setup` and `main`.
- Executable code outside a function body is a syntax error.
- `import()` may only appear inside `setup()`.

---

## Comments

```c
// single-line comment

///
  multi-line comment
  spans as many lines as needed
///
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
| `str`      | `"hello"`, `"line\n"`     | double-quoted; supports `\n \t \\ \r \e` escapes |
| `bool`     | `true`, `false`           | displays as `true`/`false`; truthy when non-zero |
| `list`     | `range(5)`                | ordered sequence of values; declare with `any` |
| `vargroup` | `vargroup p = [...]`      | named typed record with dot-accessed fields; see [vargroups.md](vargroups.md) |
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

### The `list` type

Lists are first-class values. `returnType()` returns `"list"` for them. They are created with `range()` or `seqFromTo()`, or built up with `listPush()`. Because Lynxer uses value semantics, mutating built-ins like `listPush` and `listSet` return a **new** list — always reassign the result:

```c
any lst = range(5);              // [0, 1, 2, 3, 4]
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
str msg = "hi";
bool alive = true;
any thing = none;

const str VERSION = "1.0";   // immutable — reassignment is a runtime error
```

All variables must be declared with a type before use. The declaration initialises them.

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

### Compound assignment

```c
x += 5;   // x = x + 5
x -= 3;   // x = x - 3
x *= 2;   // x = x * 2
x /= 4;   // x = x / 4
x %= 3;   // x = x % 3
```

### Comparison

| Op | Description |
|----|-------------|
| `<` | less than |
| `>` | greater than |
| `<=` | less than or equal |
| `>=` | greater than or equal |
| `is` | equal (works for `int`, `float`, `str`, `bool`) |
| `not is` | not equal |

### Logic

```c
x > 0 and x < 10
x < 0 or x > 100
not alive
```

### Bitwise

| Op | Description |
|----|-------------|
| `&` | AND |
| `\|` | OR |
| `^` | XOR |
| `~` | NOT (unary) |
| `<<` | left shift |
| `>>` | right shift |

### Operator precedence (high → low)

`~` → `* / %` → `+ -` → `<< >>` → `&` → `^` → `|` → comparisons → `not` → `and` → `or`

---

## Control flow

### if / else

```c
if(x > 0){
    print("positive\n");
} else {
    if(x is 0){
        print("zero\n");
    } else {
        print("negative\n");
    }
}
```

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

### break / continue / restart

`break;` exits the nearest enclosing loop immediately.
`continue;` and `restart;` (both work identically) skip the rest of the current loop body and jump to the next iteration. In a `for` loop, the update step still runs before the next iteration.

```c
// break
for(int i = 0; i < 10; i = i + 1){
    if(i is 5){ break; }
    print(i); print(" ");
}
// prints: 0 1 2 3 4

// continue
for(int i = 0; i < 6; i = i + 1){
    if(i % 2 is 0){ continue; }
    print(i); print(" ");
}
// prints: 1 3 5

// restart (same as continue)
int w = 0;
while(w < 5){
    w += 1;
    if(w is 3){ restart; }
    print(w); print(" ");
}
// prints: 1 2 4 5
```

`break` and `continue`/`restart` work in `for`, `while`, and `iterate` loops. They are **not** valid outside a loop.

---

## Functions

Lynxer has two kinds of functions with distinct scopes:

| Kind | Keyword | Where | Visible from |
|------|---------|-------|--------------|
| Global | `global` | Top-level only (between `setup` and `main`) | Any function in the file |
| Local | `def` | Inside any function body | Only inside the declaring function |

### Global functions (`global`)

`global` functions must be declared at the **top level of the file** — between `setup` and `main`, never inside another function. Attempting to define a `global` inside another function is a syntax error.

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

### Local functions (`def`)

`def` is the keyword for **local** helpers declared inside a function body. They are not accessible outside their declaring scope.

```c
global main(){
    def square(int n){
        return n * n;
    }
    print(square(5)); print("\n");   // 25
}
```

`def` functions can be nested (a `def` inside a `def`).

**Scope rules for `def`:**
- Visible only after the line it is declared.
- A `def` shadows any outer name of the same name within its declaring scope.
- Variables are looked up through the parent context chain at call time.

### Typed parameters

Parameters may be typed (enforced at call time) or left untyped (treated as `any`):

```c
global typed(int a, str b){ ... }
global untyped(a, b){ ... }         // any type accepted
global mixed(int n, any x){ ... }
```

A call that passes a value of the wrong type is a runtime error.

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

## Classes

A **class** is a named, static singleton that groups typed fields and methods. There is one instance per class (no `new` keyword). See [classes.md](classes.md) for a quick-reference summary.

```c
global setup(){}

class Counter {
    int count = 0;

    def init() {
        int this.count = 0;
    }

    def increment() {
        int this.count = this.count + 1;
    }

    def value() {
        return this.count;
    }
}

global main(){
    global.class.Counter();          // runs init()
    global.class.Counter.increment();
    global.class.Counter.increment();
    print(global.class.Counter.value()); print("\n");  // 2
}
```

---

## rawPy and rawPyx

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
| `list` | **not visible** — use built-in functions instead |
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
- `list` values are not visible in rawPy blocks.
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

`import()` loads a `.lynx` file as a module. It may only be called inside `setup()`.

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

### Calling module functions

```c
global.<module>.<function>(args)
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
vargroup player = [
    str  username = "Andy",
    int  coins    = 250,
    bool online   = true,
    vargroup stats = [
        int   level = 5,
        float speed = 3.5
    ]
];

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
    vargroup config = [str host = "localhost", int port = 8080];
}
global main(){
    print(config.host);   // localhost
    int config.port = 9000;
}
```
