# Clynxer Syntax Guide (Python Implementation)

This document provides a **summarized syntax guide** for the **Python-based `clynxer`** implementation of the Clynxer language.

---

## Basic Syntax

### Variables and Types
```lynx
int x = 42;
float y = 3.14;
str name = "Clynxer";
bool flag = true;
any value = x; // `any` type
```

### Functions
```lynx
// Global function
global func add(int a, int b) -> int {
    return a + b;
}

// Local function
func subtract(int a, int b) -> int {
    return a - b;
}

// Main entry point
global main() {
    println(add(5, 3));
    println(subtract(5, 3));
}
```

### Control Flow
```lynx
// If-else
if (x > 10) {
    println("Large");
} else if (x > 5) {
    println("Medium");
} else {
    println("Small");
}

// Loops
while (x > 0) {
    println(x);
    x--;
}

for (int i = 0; i < 5; i++) {
    println(i);
}

// Switch-case
switch (x) {
    case 1: println("One"); break;
    case 2: println("Two"); break;
    default: println("Other");
}
```

### Modules and Imports
```lynx
// Import a module
import math;

// Use a function from the module
println(math.sqrt(16));

// Import with an alias
importAs("stdlib/os", "os");
println(os.getcwd());
```

---

## Example: Writing a Simple Program

```lynx
// hello.lynx
global main() {
    println("Hello, Clynxer!");
}
```

Run with:
```bash
python -m clynxer hello.lynx
```

---

## Key Features

- **Statically-typed**: Variables must be declared with a type.
- **Global and local functions**: Use `global func` for global functions and `func` for local ones.
- **Modules**: Import standard library modules or custom modules using `import` or `importAs`.
- **Error handling**: Use `try/catch` blocks for exception handling.

---

## Limitations

- No support for `/* ... */` comments.
- No support for hex (`\x`) or Unicode (`\u`) escapes.
- No standalone executables (requires Python runtime).

---

### References
- [Clynxer Language Guide](lynxer/docs/language.md)