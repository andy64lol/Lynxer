# Clynxer Implementation Comparison

This document compares the **Python `clynxer`** and **C++ `lynxer`** implementations of the Clynxer language.

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
| **Async Support**           | ✅ Yes (Python `async/await`)           | ⚠️ Partial                             | `lynxer` has `async*` built-ins and an `await` expression, evaluated cooperatively inline (no event loop). |
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

## Key Takeaways

1. **`lynxer` is the preferred choice for production** due to its performance, portability, and native module support.
2. **Python `clynxer` is better for development and debugging** due to its Python tooling; `lynxer` provides FFI built-ins and cooperative `async*` built-ins instead.

---

### References
- [lynxer Documentation](lynxer/docs/README.md)
- [lynxer Limitations](lynxer/docs/limitations.md)
- [Parity scope](lynxer/docs/parity.md)