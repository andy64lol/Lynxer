# Language Reference

## Program structure

Every Lynxer program must contain exactly two required top-level declarations, in this order:

1. **`void setup(){}`** — runs before `main`. The only place to declare global variables and call `import()`. Required even when empty.
2. **`void main(){}`** — the entry point. Runs last.

Additional global functions may be declared between `setup` and `main`:

```c
void setup(){
    import("math");
    const str APP = "MyApp";
    int counter = 0;
}

// global helpers go here, between setup and main
void greet(str name){
    print("Hello, "); print(name); print("!\n");
}

void main(){
    greet(APP);
}
```

**Rules:**
- `void setup(){}` is **mandatory**, even if its body is empty.
- Global `void` function declarations are only allowed between `setup` and `main`.
- Executable code outside a function body is a runtime error.
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
- All built-in functions: `print`, `input`, `strOf`, `intOf`, `floatOf`, `returnType`, `returnLength`, `seqFromTo`, all list built-ins, `rawPy`, `rawPyx`, etc.
- All imported modules: after `import("math")`, accessible as `global.math`.
- Global constants and variables declared in `setup()`.

Built-in functions are conventionally called **directly** (without `global.`). Module functions are always called as **`global.<module>.<function>()`**.

---

## Types

| Type       | Example values            | Notes |
|------------|---------------------------|-------|
| `int`      | `42`, `-7`, `0`           | integer |
| `float`    | `3.14`, `-0.5`            | floating-point; `int` and `float` are interchangeable in expressions |
| `str`      | `"hello"`, `"line\n"`     | double-quoted; supports `\n \t \\` escapes |
| `bool`     | `true`, `false`           | displays as `true`/`false`; truthy when non-zero |
| `list`     | `seqFromTo(0,3,1)`        | ordered sequence of values; use `any` to declare |
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

Lists are first-class values. `returnType()` returns `"list"` for them. They are created with `seqFromTo()` or built up with `listPush()`. Because Lynxer uses value semantics (every variable read is a copy), mutating built-ins like `listPush` and `listSet` return a **new** list — always reassign the result:

```c
any lst = seqFromTo(0, 0, 1);   // []
lst = listPush(lst, 10);         // [10]
lst = listPush(lst, 20);         // [10, 20]
int n = listGet(lst, 0);         // 10
print(returnType(lst));          // list
print(returnLength(lst));        // 2
```

See [lists.md](lists.md) for the full list API.

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

### break / continue

```c
int i = 0;
while(true){
    if(i > 5){ break; }
    i += 1;
    if(i is 3){ continue; }
    print(i); print("\n");
}
```

---

## Functions

Lynxer has two kinds of functions with distinct scopes:

| Kind | Keyword | Where | Visible from |
|------|---------|-------|--------------|
| Global | `void` | Top-level only (between `setup` and `main`) | Any function in the file |
| Local | `def` | Inside any function body | Only inside the declaring function |

### Global functions (`void`)

`void` functions must be declared at the **top level of the file** — never inside another function. Attempting to define a `void` inside another function is a syntax error.

```c
void setup(){}

// ✓ correct — top-level void helpers
void add(int a, int b){
    return a + b;
}

void greet(str name){
    print("Hello, "); print(name); print("!\n");
}

void main(){
    int sum = add(3, 4);   // 7
    greet("World");
}
```

```c
// ✗ WRONG — void inside another void is forbidden
void main(){
    void helper(){ ... }   // Syntax Error
}
```

All `void` functions can return a value with `return`, regardless of the signature name. A `void` without an explicit `return` produces `none`.

### Local functions (`def`)

`def` is the keyword for **local**, value-returning helpers declared inside a function body. They are not accessible outside their declaring scope.

```c
void main(){
    def square(int n){
        return n * n;
    }
    print(square(5)); print("\n");   // 25
}
```

`def` functions can be nested (a `def` inside a `def`).

**Scope rules for `def`:**
- Visible only after the line it is declared.
- `def` functions can be nested (a `def` inside a `def`).
- A `def` shadows any outer name of the same name within its declaring scope.
- Variables are looked up through the parent context chain at call time, not captured by closure copy.

### Typed parameters

Parameters may be typed (enforced at call time) or left untyped (treated as `any`):

```c
void typed(int a, str b){ ... }
void untyped(a, b){ ... }         // any type accepted
void mixed(int n, any x){ ... }
```

A call that passes a value of the wrong type is a runtime error.

### Return values

`return` exits the function and optionally produces a value:

```c
void compute(int n){
    return n * 2;
}

void main(){
    int r = compute(5);   // 10
}
```

A function without an explicit `return` produces `none`.

---

## rawPy and rawPyx

### rawPy block

Embeds a Python code block inside a Lynxer function. Variables from the surrounding Lynxer scope are bridged into and out of the block.

```c
void main(){
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
| `bool` | `int` (0 or 1) — **not** Python `bool` |
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
- Only variables that already exist in Lynxer scope can be updated. You cannot create new Lynxer variables from inside a rawPy block.
- Python code can read and update `int`, `float`, and `str` Lynxer variables. For `bool` variables, write Python `True`/`False`; they come back as Lynxer `true`/`false`.
- `list` values are not visible in rawPy blocks. Use built-in list functions (`listPush`, `sortList`, etc.) instead.
- Any `import` inside a rawPy block uses Python's import system and does not affect the Lynxer module namespace.

```c
void main(){
    str msg = "hello";
    bool flag = false;
    rawPy(){
        msg = msg.upper()   // "HELLO"
        flag = True         // becomes Lynxer true
    }
    print(msg);  print("\n");   // HELLO
    print(flag); print("\n");   // true
}
```

### rawPy string form

`rawPy("code")` executes a Python one-liner. No variable bridging — stdout only.

```c
rawPy("print('hello from Python')");
```

### rawPyx block

Like `rawPy` but compiles Python code with Cython for potential speed gains. Requires Cython to be installed. Falls back to an error if Cython is not available.

```c
int result = 0;
rawPyx(){
    result = 6 * 7
}
print(result); print("\n");   // 42
```

---

## Errors

Any unhandled runtime error **terminates the program immediately** with a message showing:
- The error type (e.g. `Runtime Error`, `Type mismatch`)
- The details
- The file, line, and column
- A snippet with a caret pointing at the problem

```
[Lynxer] Runtime Error
  Type mismatch: 'n' is declared as 'int' but got a 'str' value
  --> myfile.lynx, line 5, column 5
```

Common error types:
- `Type mismatch` — assigning wrong type to a typed variable, or passing wrong type to a typed parameter
- `Runtime Error` — division by zero, index out of range, undefined variable, etc.
- `Unexpected Character` / `Missing Character` — lexer/syntax errors

---

## try / catch

`try/catch` lets you handle runtime errors instead of letting them terminate the program.

### Syntax

```c
// Form 1 — catch and bind the error message
try {
    // code that might fail
} catch(str err) {
    // err holds the error message as a string
    print(err); print("\n");
}

// Form 2 — catch without binding the message
try {
    // code that might fail
} catch {
    print("something went wrong\n");
}
```

### How it works

- The **`try` block** runs normally.
- If a **runtime error** occurs anywhere inside the `try` block, execution jumps immediately to the **`catch` block**. The rest of the `try` body is skipped.
- If **no error** occurs, the `catch` block is **never executed**.
- `catch(str varname)` binds the error message as a `str` in the catch block's scope. The variable is available only inside that block.
- **Syntax and lexer errors are not catchable** — they occur before execution begins.

### Examples

**Catching division by zero:**

```c
void main(){
    int result = 0;
    try {
        result = 10 / 0;
    } catch(str err) {
        print("Caught: "); print(err); print("\n");
        result = -1;
    }
    print(result); print("\n");   // -1
}
```

**Safe integer conversion:**

```c
void setup(){
    str userInput = "abc";
}

void main(){
    int n = 0;
    try {
        n = intOf(userInput);
    } catch {
        print("Not a valid integer\n");
    }
}
```

**Nested try/catch:**

```c
void main(){
    try {
        try {
            int bad = 1 / 0;
        } catch(str inner) {
            print("Inner caught: "); print(inner); print("\n");
            // this second error propagates to the outer catch
            any x = undefinedVariable;
        }
    } catch(str outer) {
        print("Outer caught: "); print(outer); print("\n");
    }
}
```

**`return` and loop signals propagate through try/catch:**

`return`, `break`, and `continue` inside a `try` or `catch` block behave exactly as they would outside — they exit the block normally and are not treated as errors.

```c
void findFirst(any lst){
    for(int i = 0; i < returnLength(lst); i = i + 1){
        try {
            int v = intOf(listGet(lst, i));
            return v;
        } catch {
            // not an integer — skip
        }
    }
    return -1;
}
```

### Scoping rules for the catch variable

The catch variable (`str err` in `catch(str err)`) is bound in the **same scope** as the surrounding code — not in an isolated inner scope. This means:

- After the catch block finishes, the variable is still accessible.
- If a variable with the same name already exists, the following rules apply:
  - **Same type (`str`) or `any`:** the existing variable is rebound to the error message — this is valid.
  - **Different type (e.g. `int err`):** a runtime error is raised — type guarantees are preserved.
  - **`const` variable:** a runtime error is raised — const guarantees are preserved.

```c
void main(){
    int score = 0;
    try {
        int bad = 1 / 0;
    } catch(str score) {
        // Runtime Error: 'score' is declared as 'int', cannot bind as 'str'
    }
}
```

```c
void main(){
    str msg = "ok";
    try {
        int bad = 1 / 0;
    } catch(str msg) {
        // fine — msg is already str; it is now rebound to the error text
        print(msg); print("\n");
    }
    // msg is still in scope here (contains the error message)
}
```

---

## Module system

### Importing

`import()` loads a `.lynx` file as a module. It may only be called inside `setup()`.

```c
void setup(){
    import("math");
    import("mylib");   // looks for mylib.lynx
}
```

**Search order:**
1. Same directory as the running script
2. The `stdlib/` folder bundled with Lynxer

The `.lynx` extension is optional.

**Idempotency:** Importing the same module twice is safe and has no effect the second time — the module is loaded once and cached.

### Calling module functions

```c
global.<module>.<function>(args)
```

```c
void setup(){ import("math"); }

void main(){
    print(global.math.sqrt(144));  // 12
    print(global.math.pi());       // 3.141592653589793
    print("\n");
}
```

### Accessing module globals

Constants and variables declared in a module's `setup()` are accessible via `global.<module>.<name>`:

```c
/// config.lynx ///
void setup(){
    const str HOST = "localhost";
    const int PORT = 8080;
}
void main(){}
```

```c
void setup(){ import("config"); }
void main(){
    print(global.config.HOST); print("\n");   // localhost
    print(global.config.PORT); print("\n");   // 8080
}
```

---

## VarGroups

A **vargroup** is a named, typed record with dot-accessed fields — similar to a C struct.

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

player.coins       = 500;      // dot-assignment
player.stats.level = 10;       // nested dot-assignment

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
void setup(){
    vargroup config = [str host = "localhost", int port = 8080];
}
void main(){
    print(config.host);   // localhost
    config.port = 9000;
}
```

See [vargroups.md](vargroups.md) for the full reference.
