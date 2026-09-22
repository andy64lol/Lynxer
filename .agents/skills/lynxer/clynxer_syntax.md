# Clynxer Syntax Guide (C++ Implementation)

This document provides a **summarized syntax guide** for the **C++-based `clynxer`** implementation of the Lynxer language, including key differences from the Python `lynxer`.

---

## Basic Syntax

Clynxer shares **most syntax** with Python `lynxer`, but has **key differences** in behavior and supported features. Below is a summary of the core syntax.

### Variables and Types
```lynx
int x = 42;
float y = 3.14;
str name = "Lynxer";
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

## Key Syntax Differences

### 1. **Logical NOT Operator**
- **Python `lynxer`**: Supports `!value`.
- **`clynxer`**: **Does not support `!`**. Use `not value` or `!!value`.

### 2. **Module Self-Calls**
- In `clynxer`, `global.name(...)` resolves to **core builtins**, not module functions.
- Use **bare names** for module functions.

### 3. **Bytecode and Compilation**
- **Python `lynxer`**: Supports `.lynxc` bytecode files.
- **`clynxer`**: **No bytecode support**. Compiles directly to standalone executables.

### 4. **Error Handling**
- **Python `lynxer`**: Uses Python’s exception system.
- **`clynxer`**: Uses Lynxer’s `try/catch` blocks, but some error messages may differ.

---

## Example: Writing a Simple Program

```lynx
// hello.lynx
global main() {
    println("Hello, Lynxer!");
}
```

Compile and run with:
```bash
make
./clynxer hello.lynx
```

Or compile to a standalone executable:
```bash
make
./clynxer --compile hello.lynx -o hello
./hello
```

---

## Key Features

- **Standalone executables**: Compile Lynxer programs to ELF binaries.
- **Native modules**: Supports Rust and C++ `.so` libraries.
- **Bundling**: Embed multiple `.lynx` files and `.so` libraries into a single executable.
- **Cooperative threading**: Supports `multiprocessing` but uses a cooperative model.

---

## Limitations

- No support for `/* ... */` comments.
- No support for hex (`\x`) or Unicode (`\u`) escapes.
- No FFI or async support.
- No `venv` support.

---

### References
- [clynxer Documentation](clynxer/docs/README.md)
- [clynxer Limitations](clynxer/docs/limitations.md)