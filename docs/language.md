# Language Reference

## Program structure

Every program needs `void main()`.  
`void setup()` is optional and runs first — it is the only place for global variables and `import()` calls.

```c
void setup(){
    import("math");
    const str APP = "MyApp";
    int counter = 0;
}

void main(){
    print(APP); print("\n");
}
```

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

## Types

| Type    | Example values            | Notes |
|---------|---------------------------|-------|
| `int`   | `42`, `-7`, `0`           | integer |
| `float` | `3.14`, `-0.5`            | floating-point |
| `str`   | `"hello"`, `"line\n"`     | double-quoted, `\n \t \\` escapes |
| `bool`  | `true`, `false`           | stored as 1 / 0 |
| `any`   | anything, including `none`| no type check |

`none` is a null-like literal assignable to `any`:

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

`int` and `float` are interchangeable (numeric types) to avoid errors from division always returning float.

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

All variables must be declared with a type before use.  
Global variables must be declared inside `setup()`.

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
| `is` | equal |
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

```c
int flags = 6 & 3;    // 2
int mask  = 1 << 4;   // 16
int inv   = ~0;       // -1
```

### Precedence (high → low)

`~` (unary) → `* / %` → `+ -` → `<< >>` → `& ` → `^` → `|` → comparisons → `not` → `and` → `or`

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
while(true){
    if(i > 5){ break; }
    i += 1;
    if(i is 3){ continue; }
    print(i); print("\n");
}
```

---

## Functions

### Global functions (`void`)

Declared at the top level, callable from anywhere.  
Return a value with `return`.

```c
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

### Local functions (`def`)

Declared inside another function, only visible there.

```c
void main(){
    def square(int n){
        return n * n;
    }
    print(square(5)); print("\n");   // 25
}
```

### Typed parameters

Parameters can be typed (enforced at call time) or untyped:

```c
void typed(int a, str b){ ... }
void untyped(a, b){ ... }         // any type accepted
void mixed(int n, any x){ ... }
```

### Return values

Any function can return a value regardless of its keyword:

```c
void compute(int n){
    return n * 2;
}

void main(){
    int r = compute(5);   // 10
}
```
