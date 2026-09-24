# Clynxer

> **Status (2026-09-23): this Python implementation is the frozen behaviour
> reference.** Lynxer — the standalone C++ implementation under `lynxer/` —
> is the primary implementation and has surpassed it for real use: it ships
> standalone ELF executables, 27 natively backed stdlib modules, an AST
> optimizer, a frozen native-module ABI, and a full test suite on both amd64
> and arm64. The files under `clynxer/` and `test/` are kept for reference and
> receive no new features; parity fixes are made in Lynxer. See
> `lynxer/docs/parity.md` (what is a parity target) and
> `lynxer/docs/limitations.md` (the divergence register).

![Clynxer logo](assets/lynxer.png)
![](https://img.shields.io/badge/-Custom%20programming%20language-blue?style=for-the-badge)

A statically-flavoured, C-style scripting language that runs on Python.
Files use the `.lynx` extension.

> **Linux only:** Clynxer is currently supported for Linux users and Linux
> distributions. The native C++ extension, standalone bundler, and Linux
> system-level `os` calls require Linux. Native builds support 64-bit
> x86-64 (`amd64`) and ARM64 (`aarch64`) hosts. Builds fail early on other
> operating systems, architectures, or Python ABIs rather than mixing syscall
> tables or native extensions.

```c
global setup(){
    str name = input("What's your name? ");
}

global main(){
    print("Hello, ");
    print(name);
    print("!\n");
}
```

→ **[Installation](docs/install.md)** | **[Language reference](docs/language.md)** | **[Standard library](docs/stdlib.md)**

---

## Quick start

```bash
clynxer syntax.lynx           # run a source file
clynxer --compile syntax.lynx # compile to bytecode (syntax.lynxc)
clynxer syntax.lynxc          # run compiled bytecode directly
clynxer --version             # print version
clynxer --help                # print help
```

---

## Language at a glance

```c
global setup(){
    import("math");
    const str LANG = "Clynxer";
}

global greet(str name){
    print("Hello, "); print(name); print("!\n");
}

global main(){
    int x = 10;
    float pi = 3.14;
    bool ok = true;

    x += 5;

    if(x > 10){
        global.greet(LANG);
    }

    for(int i = 0; i < 3; i = i + 1){
        print(i); print("\n");
    }

    // inline Python
    int result = 0;
    rawPy(){
        result = sum(range(1, 11))
    }
    print(result); print("\n");

    // stdlib
    print(global.math.sqrt(144)); print("\n");
}
```

---

## Documentation

| Page | Contents |
|------|----------|
| [Installation](docs/install.md) | How to install and run Clynxer |
| [CLI reference](docs/CLI.md) | Complete command-line usage |
| [Language reference](docs/language.md) | Types, variables, operators, control flow, functions |
| [Type reference](docs/types.md) | Primitive, fixed-width integer, and fixed-width float types |
| [Built-ins](docs/builtins.md) | `print`, `input`, `strOf`, `returnType`, `seqFromTo`, … |
| [Tuples](docs/tuples.md) | `tuple` type, built-in tuple functions |
| [importAs](docs/importAs.md) | `importAs("module", "alias")` — import under a custom name |
| [Standard library](docs/stdlib.md) | All stdlib modules — overview and function tables |
| [stdlib/ reference](docs/stdlib/README.md) | Per-module documentation pages |
| [Built-ins](docs/builtins.md) | Core language functions, including unmanaged memory operations |
| [Filesystem API](docs/filesystem.md) | Safe handle-based file and directory operations |
| [Networking API](docs/networking.md) | Managed TCP, UDP, and Unix-domain sockets |
| [rawPy / rawPyx](docs/rawpy.md) | Embedding Python and Cython |
| [Module system](docs/modules.md) | `import()`, `importAs()`, namespaces, writing your own modules |
| [Bytecode (.lynxc)](docs/bytecode.md) | Compiling to bytecode, running `.lynxc` files |
| [Vargroups](docs/vargroups.md) | Named typed records (struct-like) |
| [Structs](docs/structs.md) | Data-only named types with positional constructors |
| [Classes](docs/classes.md) | Instances, constructors, fields, and methods |
| [Enums](docs/enums.md) | Rust-style tagged unions, payloads, and pattern matching |
| [Known limitations](docs/limitations.md) | Documented gaps between `todo.md` and the implementation |
| [Async](docs/async.md) | Async functions |
| [Lists](docs/lists.md) | List operations |

---

## Project layout

```
clynxer/
  clynxer.py         Lexer + parser + interpreter + bytecode compiler
  builtins.py       Language builtin implementations and registry
  shell.py          CLI entry point
  stdlib/           Standard library modules (.lynx files; native memory is built in)
  cpp.cpp           C++ implementation of core memory built-ins
  bytecode_vm.cpp    C++ bytecode stack-machine executor
  setup.py          Native extension build script
docs/               Documentation
syntax.lynx         Full syntax showcase
main.py             Launcher (delegates to shell.py)
Makefile
README.md
```

---

## License

MIT — [see LICENSE](LICENSE).
