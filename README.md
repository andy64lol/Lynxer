# Lynxer

> **Status (2026-09-24): Lynxer is a standalone C++ implementation.** It ships
> standalone ELF executables, 27 natively backed stdlib modules, an AST
> optimizer, a frozen native-module ABI, and a full test suite on both amd64 and
> arm64. See [docs/limitations.md](docs/limitations.md) for the behaviour that
> is deliberately constrained or not implemented.

![Lynxer logo](assets/lynxer.png)
![](https://img.shields.io/badge/-Custom%20programming%20language-blue?style=for-the-badge)

A statically-flavoured, C-style scripting language. Files use the `.lynx`
extension and run on the standalone C++ interpreter in `lynxer/`.

> **Linux only:** Lynxer is currently supported for Linux users and Linux
> distributions. The standalone bundler and the Linux system-level `os` calls
> require Linux. Native builds support 64-bit x86-64 (`amd64`) and ARM64
> (`aarch64`) hosts. Builds fail early on other operating systems or
> architectures rather than mixing syscall tables.

```c
global setup(){
    str name = input("What's your name? ");
}

global main(){
    println("Hello, ", name, "!");
}
```

→ **[Installation](docs/install.md)** | **[Language reference](docs/language.md)** | **[Standard library](docs/stdlib/)**

---

## Quick start

```bash
make                         # build the interpreter and every native module
./lynxer/lynxer syntax.lynx  # run a source file
./lynxer/lynxer --compile syntax.lynx -o syntax   # build a standalone executable
./lynxer/lynxer --version    # print version
./lynxer/lynxer --help       # print help
```

---

## Language at a glance

```c
global setup(){
    importAs("math", "m");
    const str LANG = "Lynxer";
}

global greet(str name){
    println("Hello, ", name, "!");
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
        println(i);
    }

    // stdlib
    println(global.m.sqrt(144));
}
```

---

## Documentation

The documentation lives in [`docs/`](docs/README.md).

| Page | Contents |
|------|----------|
| [Installation](docs/install.md) | How to build and run Lynxer |
| [CLI reference](docs/CLI.md) | Complete command-line usage |
| [Language reference](docs/language.md) | Types, variables, operators, control flow, functions |
| [Type reference](docs/types.md) | Primitive, fixed-width integer, and fixed-width float types |
| [Built-ins](docs/builtins.md) | Core language functions and unmanaged memory operations |
| [Lists](docs/lists.md) | `list` and `tuple` values and their builtins |
| [importAs](docs/importAs.md) | `importAs("module", "alias")` — import under a custom name |
| [Module system](docs/modules.md) | `import()`, `importAs()`, namespaces, writing your own modules |
| [Standard library](docs/stdlib/) | Per-module documentation pages |
| [Native-module ABI](docs/native-module-abi.md) | The shared C ABI for C++ and Rust backends |
| [Extending](docs/extending.md) | Adding a module: wrapper, backend, fixture, contract |
| [Vargroups](docs/vargroups.md) | Named typed records (struct-like) |
| [Structs](docs/structs.md) | Data-only named types with positional constructors |
| [Classes](docs/classes.md) | Instances, constructors, fields, and methods |
| [Enums](docs/enums.md) | Rust-style tagged unions, payloads, and pattern matching |
| [Limitations](docs/limitations.md) | Deliberate constraints and what is not implemented |

---

## Project layout

```
lynxer/             The interpreter and its standard library
  *.cpp, *.hpp      Lexer, parser, interpreter, optimizer, formatter, CLI
  stdlib/           Native and pure stdlib modules
  rust/             Rust-backed native modules
docs/               Documentation
syntax.lynx         Full syntax showcase
Makefile
README.md
```

---

## License

MIT — [see LICENSE](LICENSE).
