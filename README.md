# Lynxer

A statically-flavoured C-style scripting language that runs on Python.  
Files use the `.lynx` extension.
[Open syntax.lynx](syntax.lynx)

```c
void setup(){
    str name = input("What's your name? ");
}

void main(){
    print("Hello, ");
    print(name);
    print("!\n");
}
```

---

## Installation

### Option A — System-wide (`make install`)

```bash
git clone https://github.com/andy64lol/Lynxer.git
cd Lynxer

# installs to /usr/local/bin/lynxer  (may need sudo)
make install

# or install to a user-local prefix (no sudo)
make install PREFIX=~/.local
```

### Option B — run straight from the repo (no install)

```bash
python -m lynxer examples/example.lynx
# or
python lynxer/shell.py examples/example.lynx
```

### Uninstall

```bash
make uninstall # may need sudo
```

---

## Running a program

```bash
lynxer my_program.lynx
lynxer --version
lynxer --help
```

---

## Language Reference

### Program structure

Every Lynxer program must have a `void main()` function.  
`void setup()` is optional and runs before `main()`.  
**Global variables must be declared inside `setup()`.**

```c
void setup(){
    // imports go here
    import("mylib");

    // global variables go here
    int counter = 0;
    str greeting = "Hello";
}

void main(){
    print(greeting);
    print("\n");
}
```

### Comments

```c
// single-line comment          (trailing // is decorative, ignored)
// section ─────────────── //  ← same thing, both styles work

///
  multi-line comment
  spans as many lines as needed
///
```

### Types

| Type    | Example value        |
|---------|----------------------|
| `int`   | `42`, `-7`           |
| `float` | `3.14`, `-0.5`       |
| `str`   | `"hello\n"`          |
| `bool`  | `true`, `false`      |
| `any`   | any of the above, or `none` |

Types are declared but not enforced at runtime in v0.1.

`any` accepts a value of any type, including the `none` literal (a null-like
empty value):

```c
any nothing = none;
any x = 5;
x = "now a string";   // fine — any never type-checks
```

### Variables

```c
int x = 10;
float pi = 3.14159;
str msg = "hi";
bool alive = true;
const str VERSION = "1.0";  // immutable — reassignment is a runtime error
```

### Operators

| Category   | Operators                           |
|------------|-------------------------------------|
| Arithmetic | `+`, `-`, `*`, `/`, `%`             |
| Comparison | `<`, `>`, `<=`, `>=`                |
| Equality   | `is`, `not is`                      |
| Logic      | `and`, `or`, `not`                  |
| Assign     | `=` (plain assignment, no `+=` yet) |

### Control flow

```c
if(x > 0){
    print("positive\n");
}
else{
    print("non-positive\n");
}

while(x > 0){
    x = x - 1;
}

for(int i = 0; i < 10; i = i + 1){
    print(i);
    print("\n");
}
```

### Functions

```c
// Global function (callable from anywhere)
void greet(str name){
    print("Hello, ");
    print(name);
    print("!\n");
}

void main(){
    // Local function (only visible inside main)
    def square(int n){
        return n * n;
    }

    int result = square(5);   // 25
    greet("World");
}
```

`void` = global function, `def` = local/nested function.

### Built-in functions

| Function                    | Description                                   |
|-----------------------------|------------------------------------------------|
| `print(v)`                  | Print any value (no automatic newline)        |
| `input("prompt")`           | Read a line from stdin, return `str`          |
| `str_of(v)`                 | Convert any value to string                   |
| `int_of(v)`                 | Parse value to int                             |
| `float_of(v)`               | Parse value to float                           |
| `rawPy("code")`             | Execute Python code string                     |
| `rawPyx("code")`         | Compile and execute a Cython code string       |
| `returnType(v)`             | Name of `v`'s type: `"int"`, `"float"`, `"str"`, `"bool"`, `"none"`, `"list"`, `"function"`, or `"any"` |
| `returnLength(v)`           | Length of a `str` or the `list` from `seqFromTo` |
| `seqFromTo(start, stop, step)` | Build a list of ints, like Python's `range()` |

### rawPy — embed Python

```c
void main(){
    // Block form: variables bridge in and out
    int x = 0;
    rawPy(){
        x = 7 * 6          // x is now 42 in Lynxer
    }

    str s = "";
    rawPy(){
        s = "hello".upper()  // s is now "HELLO"
    }

    // String form: quick one-liners (stdout only, no bridging)
    rawPy("import math; print(math.pi)");
}
```

### rawPyx — embed compiled Cython

Same shape as `rawPy`, but the code is compiled with Cython before running —
use it for numeric hot loops that need to run at native speed. The first call
for a given snippet pays a one-time compile cost (cached under
`.cache/cython/`); repeat calls reuse the compiled extension.

```c
void main(){
    // Block form: variables bridge in and out
    int result = 0;
    rawPyx(){
        result = 6 * 7        // result is now 42 in Lynxer
    }

    // String form: quick one-liners (stdout only, no bridging)
    rawPyx("print('compiled with Cython')");
}
```

Requires the `cython` package (declared in `setup.py`'s `install_requires`)
and a C compiler (`gcc`/`cc`) on the system `PATH`.

### Modules and imports

`import()` may only appear inside `setup()`.  
The `.lynx` extension is optional.

```c
void setup(){
    import("mylib");        // loads mylib.lynx from same directory
    import("math");         // loads from stdlib if not found locally
}

void main(){
    global.mylib.sayHi();
    print(global.math.sqrt(144));
    print("\n");
}
```

---

## Standard Library

Import with `import("math")` or `import("typing")`.

### `math`

| Function                    | Description              |
|-----------------------------|--------------------------|
| `global.math.abs(n)`        | Absolute value           |
| `global.math.max(a, b)`     | Larger of two values     |
| `global.math.min(a, b)`     | Smaller of two values    |
| `global.math.clamp(v,lo,hi)`| Clamp value to range     |
| `global.math.pow(base, exp)`| Integer exponentiation   |
| `global.math.sqrt(n)`       | Square root              |
| `global.math.floor(n)`      | Floor (round down)       |
| `global.math.ceil(n)`       | Ceiling (round up)       |
| `global.math.roundNum(n)`   | Round to nearest integer |
| `global.math.PI()`          | π ≈ 3.14159…             |

### `typing`

| Function                          | Description                        |
|-----------------------------------|------------------------------------|
| `global.typing.toStr(n)`          | Number → string                    |
| `global.typing.toInt(s)`          | String → int (0 on failure)        |
| `global.typing.toFloat(s)`        | String → float (0.0 on failure)    |
| `global.typing.toBool(n)`         | 0 → false, nonzero → true          |
| `global.typing.isNumeric(s)`      | 1 if parseable as number, else 0   |
| `global.typing.lenStr(s)`         | Length of string                   |
| `global.typing.repeat(s, n)`      | Repeat string n times              |
| `global.typing.contains(hay,needle)` | 1 if needle in haystack, else 0 |

---

## Project layout

```
lynxer/
  __main__.py          CLI entry point  (`python -m lynxer`)
  lynxer.py            Interpreter core (lexer + parser + runtime)
  shell.py             Dev shim         (`python shell.py file.lynx`)
  strings_with_arrows.py
  stdlib/
    math.lynx
    typing.lynx
  examples/
    example.lynx
    hello.lynx
  tests/
    test.lynx
    import_test.lynx
    rawpy_test.lynx
    greetlib.lynx
bin/
  lynxer               Installed shell wrapper
Makefile
setup.py
README.md
```

---

## License

MIT
