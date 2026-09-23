# Clynxer Syntax Guide (C++ Implementation)

This document provides a **summarized syntax guide** for the **C++-based `clynxer`** implementation of the Lynxer language, including key differences from the Python `lynxer`.

> Updated 2026-09-23. The examples below are the Clynxer form and all parse in
> Clynxer; the intended parity scope with Python is `clynxer/docs/parity.md` and
> the divergence register is `clynxer/docs/limitations.md`.

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
// File-level function (`global name(...)` is the global form; `global func` is invalid)
func add(int a, int b) -> int {
    return a + b;
}

// Entry point
global main() {
    println(add(5, 3));
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
    x -= 1;
}

for (int i = 0; i < 5) {   // the update is implicit: i = i + 1
    println(i);
}

// Switch-case (`case(x)`, no colon and no `break`)
switch (x) {
    case(1){ println("One"); }
    case(2){ println("Two"); }
    default(){ println("Other"); }
}
```

### Modules and Imports
```lynx
global setup() {
    // Imports are statements, so they run from setup() or main().
    importAs("stdlib/os", "os");
}

global main() {
    println(os.getcwd());
}
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
- No `venv` support, and no language-level `async`/`await` concurrency: the
  `async*` built-ins and the `await` expression evaluate cooperatively inline.
- FFI is available as the `ffi*` built-ins (`dlopen`/`dlsym` plus
  signature-string dispatch), not through libffi.
- The full list is `clynxer/docs/limitations.md`; the parity scope is
  `clynxer/docs/parity.md`.

---

### References
- [clynxer Documentation](clynxer/docs/README.md)
- [clynxer Limitations](clynxer/docs/limitations.md)
- [Parity scope](clynxer/docs/parity.md)