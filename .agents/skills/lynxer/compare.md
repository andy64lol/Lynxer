# Lynxer Implementation Comparison

This document compares the **Python `lynxer`** and **C++ `clynxer`** implementations of the Lynxer language.

---

## Overview

| Feature                     | Python `lynxer`                          | C++ `clynxer`                          | Notes                                                                 |
|-----------------------------|----------------------------------------|---------------------------------------|----------------------------------------------------------------------|
| **Language**                | Python                                  | C++                                   | `clynxer` is faster and more efficient.                                |
| **Performance**             | Slower (Python overhead)                | Faster (C++ optimized)                 | `clynxer` excels in production environments.                        |
| **Standalone Execution**    | ❌ No (requires Python)                 | ✅ Yes (ELF executables)               | `clynxer` can compile to standalone binaries.                       |
| **Native Modules**          | ❌ Limited (Python-based)               | ✅ Yes (Rust/C++ `.so` libraries)      | `clynxer` supports Rust and C++ native modules.                     |
| **Comment Syntax**          | ✅ `//`, `/// ... ///`                   | ✅ `//`, `/// ... ///`                 | Neither supports `/* ... */`.                                            |
| **String Escapes**          | ✅ Basic (`\n`, `\t`, etc.)           | ✅ Basic (`\n`, `\t`, etc.)         | Neither supports `\x` or `\u`.                                          |
| **Bytecode Support**        | ✅ `.lynxc` files                      | ❌ No (removed)                        | `clynxer` focuses on standalone executables.                         |
| **FFI Support**             | ✅ Yes (ctypes)                         | ✅ Yes (`ffi*` built-ins)              | `clynxer` dispatches by signature string over `dlopen`/`dlsym`; no libffi. |
| **Async Support**           | ✅ Yes (Python `async/await`)           | ⚠️ Partial                             | `clynxer` has `async*` built-ins and an `await` expression, evaluated cooperatively inline (no event loop). |
| **Debugging Tools**         | ✅ Full Python debugger integration    | ❌ Limited (C++ debugging)             | Python `lynxer` integrates better with Python tools.                |
| **`venv` Support**           | ✅ Yes                                   | ❌ No (excluded)                       | Python `lynxer` supports virtual environments.                      |
| **Multiprocessing**         | ✅ Preemptive threading                | ✅ Cooperative threading               | `clynxer` uses a cooperative model.                                     |

---

## When to Use Which

### Use **Python `lynxer`** if:
- You need **rapid prototyping** or **debugging**.
- You rely on **Python-specific features** (e.g., `venv`, FFI, async).
- You want **easier development** (no compilation needed).

### Use **C++ `clynxer`** if:
- You need **high performance** and **standalone executables**.
- You want to **deploy Lynxer programs** without Python dependencies.
- You rely on **native modules** (Rust/C++).

---

## Key Takeaways

1. **`clynxer` is the preferred choice for production** due to its performance, portability, and native module support.
2. **Python `lynxer` is better for development and debugging** due to its Python tooling; `clynxer` provides FFI built-ins and cooperative `async*` built-ins instead.

---

### References
- [clynxer Documentation](clynxer/docs/README.md)
- [clynxer Limitations](clynxer/docs/limitations.md)
- [Parity scope](clynxer/docs/parity.md)