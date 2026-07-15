# Lynxer

A statically-flavoured, C-style scripting language that runs on Python.  
Files use the `.lynx` extension.

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

Types are declared but not enforced at runtime in v0.1.

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

| Function          | Description                          |
|-------------------|--------------------------------------|
| `print(v)`        | Print any value (no automatic newline) |
| `input("prompt")` | Read a line from stdin, return `str` |
| `str_of(v)`       | Convert any value to string          |
| `int_of(v)`       | Parse value to int                   |
| `float_of(v)`     | Parse value to float                 |
| `rawpy("code")`   | Execute Python code string           |

### rawpy — embed Python

```c
void main(){
    // Block form: variables bridge in and out
    int x = 0;
    rawpy(){
        x = 7 * 6          // x is now 42 in Lynxer
    }

    str s = "";
    rawpy(){
        s = "hello".upper()  // s is now "HELLO"
    }

    // String form: quick one-liners (stdout only, no bridging)
    rawpy("import math; print(math.pi)");
}
```

### Modules and imports

`import()` may only appear inside `setup()`.  
The `.lynx` extension is optional.

```c
void setup(){
    import("mylib");        // loads mylib.lynx from same directory
    import("math");         // loads from stdlib if not found locally
}

void main(){
    mylib.global.sayHi();
    print(math.global.sqrt(144));
    print("\n");
}
```

---

## Standard Library

Import with `import("math")` or `import("typing")`.

### `math`

| Function                    | Description              |
|-----------------------------|--------------------------|
| `math.global.abs(n)`        | Absolute value           |
| `math.global.max(a, b)`     | Larger of two values     |
| `math.global.min(a, b)`     | Smaller of two values    |
| `math.global.clamp(v,lo,hi)`| Clamp value to range     |
| `math.global.pow(base, exp)`| Integer exponentiation   |
| `math.global.sqrt(n)`       | Square root              |
| `math.global.floor(n)`      | Floor (round down)       |
| `math.global.ceil(n)`       | Ceiling (round up)       |
| `math.global.roundNum(n)`   | Round to nearest integer |
| `math.global.PI()`          | π ≈ 3.14159…             |

### `typing`

| Function                          | Description                        |
|-----------------------------------|------------------------------------|
| `typing.global.toStr(n)`          | Number → string                    |
| `typing.global.toInt(s)`          | String → int (0 on failure)        |
| `typing.global.toFloat(s)`        | String → float (0.0 on failure)    |
| `typing.global.toBool(n)`         | 0 → false, nonzero → true          |
| `typing.global.isNumeric(s)`      | 1 if parseable as number, else 0   |
| `typing.global.lenStr(s)`         | Length of string                   |
| `typing.global.repeat(s, n)`      | Repeat string n times              |
| `typing.global.contains(hay,needle)` | 1 if needle in haystack, else 0 |

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
