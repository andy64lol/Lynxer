# Clynxer Implementation Comparison

This skill provides a structured comparison between the **Python `clynxer`** and **C++ `lynxer`** implementations of the Clynxer language, along with syntax guidelines and usage examples.

> **Updated 2026-09-23.** Lynxer is the primary implementation. It has
> deliberately diverged from the Python reference (no bytecode, an ELF
> `--compile`, Rust stdlib backends, cooperative threads), so the intended
> parity scope is `lynxer/docs/parity.md` and the full divergence register is
> `lynxer/docs/limitations.md`. The syntax below is the **Lynxer** form, which
> is what new code should use.

---

## Overview

| Feature                     | Python `clynxer`                          | C++ `lynxer`                          | Notes                                                                 |
|-----------------------------|----------------------------------------|---------------------------------------|----------------------------------------------------------------------|
| **Language**                | Python                                  | C++                                   | `lynxer` is faster and more efficient.                                |
| **Performance**             | Slower (Python overhead)                | Faster (C++ optimized)                 | `lynxer` excels in production environments.                        |
| **Standalone Execution**    | ❌ No (requires Python)                 | ✅ Yes (ELF executables)               | `lynxer` can compile to standalone binaries.                       |
| **Native Modules**          | ❌ Limited (Python-based)               | ✅ Yes (Rust/C++ `.so` libraries)      | `lynxer` supports Rust and C++ native modules.                     |
| **Comment Syntax**          | ✅ `//`, `/// ... ///`                   | ✅ `//`, `/// ... ///`                 | Neither supports `/* ... */`.                                            |
| **String Escapes**          | ✅ Basic (`\n`, `\t`, etc.)           | ✅ Basic (`\n`, `\t`, etc.)         | Neither supports `\x` or `\u`.                                          |
| **Bytecode Support**        | ✅ `.lynxc` files                      | ❌ No (removed)                        | `lynxer` focuses on standalone executables.                         |
| **FFI Support**             | ✅ Yes (ctypes)                         | ✅ Yes (`ffi*` built-ins)              | `lynxer` dispatches by signature string over `dlopen`/`dlsym`; no libffi. |
| **Async Support**           | ✅ Yes (`async`/`await`)                | ⚠️ Partial                             | `lynxer` has `async*` built-ins and an `await` expression, evaluated cooperatively inline (no event loop). |
| **Debugging Tools**         | ✅ Full Python debugger integration    | ❌ Limited (C++ debugging)             | Python `clynxer` integrates better with Python tools.                |
| **`venv` Support**           | ✅ Yes                                   | ❌ No (excluded)                       | Python `clynxer` supports virtual environments.                      |
| **Multiprocessing**         | ✅ Preemptive threading                | ✅ Cooperative threading               | `lynxer` uses a cooperative model.                                     |

---

## When to Use Which

### Use **Python `clynxer`** if:
- You need **rapid prototyping** or **debugging**.
- You rely on **Python-specific features** (e.g., `venv`, FFI, async).
- You want **easier development** (no compilation needed).

### Use **C++ `lynxer`** if:
- You need **high performance** and **standalone executables**.
- You want to **deploy Clynxer programs** without Python dependencies.
- You rely on **native modules** (Rust/C++).

---

## Clynxer Syntax Summary

Clynxer is a **statically-typed, imperative** language with support for functions, loops, and modules. Below is a **summarized syntax guide** for both implementations.

### Basic Syntax

#### Variables and Types
```lynx
int x = 42;
float y = 3.14;
str name = "Clynxer";
bool flag = true;
any value = x; // `any` type
```

#### Functions
```lynx
// File-level function (use `global name(...)` for a global function)
func add(int a, int b) -> int {
    return a + b;
}

// Entry point
global main() {
    println(add(5, 3));
}
```

#### Control Flow
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

// Switch-case
switch (x) {
    case(1){ println("One"); }
    case(2){ println("Two"); }
    default(){ println("Other"); }
}
```

#### Modules and Imports
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

## Key Syntax Differences Between `clynxer` and `lynxer`

While the **core syntax** of Clynxer is identical between `clynxer` and `lynxer`, there are **key differences** in behavior and supported features.

### 1. **Logical NOT Operator**
- **Python `clynxer`**: Supports `!value`.
- **`lynxer`**: **Does not support `!`**. Use `not value` or `!!value`.

### 2. **String Escapes**
- Both support basic escapes (`\n`, `\t`, etc.), but **neither supports `\x` or `\u`**.

### 3. **Comments**
- Both support `//` and `/// ... ///`, but **neither supports `/* ... */`**.

### 4. **Bytecode and Compilation**
- **Python `clynxer`**: Supports `.lynxc` bytecode files.
- **`lynxer`**: **No bytecode support**. Compiles directly to standalone executables.

### 5. **Module Self-Calls**
- In `lynxer`, `global.name(...)` resolves to **core builtins**, not module functions. Use **bare names** for module functions.

### 6. **Error Handling**
- **Python `clynxer`**: Uses Python’s exception system.
- **`lynxer`**: Uses Clynxer’s `try/catch` blocks, but some error messages may differ.

---

## Example: Writing a Simple Program

### Python `clynxer` Example
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

### `lynxer` Example
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

## Key Takeaways

1. **`lynxer` is the preferred choice for production** due to its performance, portability, and native module support.
2. **Python `clynxer` is better for development and debugging** due to its Python tooling; `lynxer` provides FFI built-ins and cooperative `async*` built-ins instead.
3. **Neither supports `/* ... */` comments or `\x`/`\u` escapes**—both use basic escape sequences.
4. **`lynxer` has stricter syntax rules** (e.g., no `!` operator, different module resolution).

---

## Next Steps

- For **production use**, prefer `lynxer`.
- For **development and prototyping**, use Python `clynxer`.
- Check the [limitations](lynxer/docs/limitations.md) for `lynxer` to understand unsupported features.

---

### References
- [lynxer Documentation](lynxer/docs/README.md)
- [lynxer Limitations](lynxer/docs/limitations.md)
- [Parity scope](lynxer/docs/parity.md)
- [Clynxer Language Guide](lynxer/docs/language.md)