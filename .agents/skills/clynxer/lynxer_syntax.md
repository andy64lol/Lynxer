# Lynxer Syntax Guide (C++ Implementation)

This document provides a **summarized syntax guide** for the **C++-based `lynxer`** implementation of the Clynxer language, including key differences from the Python `clynxer`.

> Updated 2026-09-23. The examples below are the Lynxer form and all parse in
> Lynxer; the intended parity scope with Python is `lynxer/docs/parity.md` and
> the divergence register is `lynxer/docs/limitations.md`.

---

## Basic Syntax

Lynxer shares **most syntax** with Python `clynxer`, but has **key differences** in behavior and supported features. Below is a summary of the core syntax.

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
- **Python `clynxer`**: Supports `!value`.
- **`lynxer`**: **Does not support `!`**. Use `not value` or `!!value`.

### 2. **Module Self-Calls**
- In `lynxer`, `global.name(...)` resolves to **core builtins**, not module functions.
- Use **bare names** for module functions.

### 3. **Bytecode and Compilation**
- **Python `clynxer`**: Supports `.lynxc` bytecode files.
- **`lynxer`**: **No bytecode support**. Compiles directly to standalone executables.

### 4. **Error Handling**
- **Python `clynxer`**: Uses Python’s exception system.
- **`lynxer`**: Uses Clynxer’s `try/catch` blocks, but some error messages may differ.

---

## Example: Writing a Simple Program

```lynx
// hello.lynx
global main() {
    println("Hello, Clynxer!");
}
```

Compile and run with:
```bash
make
./lynxer hello.lynx
```

Or compile to a standalone executable:
```bash
make
./lynxer --compile hello.lynx -o hello
./hello
```

---

## Key Features

- **Standalone executables**: Compile Clynxer programs to ELF binaries.
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
- The full list is `lynxer/docs/limitations.md`; the parity scope is
  `lynxer/docs/parity.md`.

---

### References
- [lynxer Documentation](lynxer/docs/README.md)
- [lynxer Limitations](lynxer/docs/limitations.md)
- [Parity scope](lynxer/docs/parity.md)