# Lynxer

![Lynxer logo](assets/lynxer.png)
![](https://img.shields.io/badge/-Custom%20programming%20language-blue?style=for-the-badge)

A statically-flavoured, C-style scripting language that runs on Python.
Files use the `.lynx` extension.

> **Linux only:** Lynxer is currently supported for Linux users and Linux
> distributions. The native C++ extension, standalone bundler, and Linux
> system-level `os` calls require Linux. ARM64 (`aarch64`) and x86-64 Linux
> hosts are supported; other operating systems are not supported.

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
lynxer syntax.lynx           # run a source file
lynxer --compile syntax.lynx # compile to bytecode (syntax.lynxc)
lynxer syntax.lynxc          # run compiled bytecode directly
lynxer --version             # print version
lynxer --help                # print help
```

---

## Language at a glance

```c
global setup(){
    import("math");
    const str LANG = "Lynxer";
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
| [Installation](docs/install.md) | How to install and run Lynxer |
| [CLI reference](docs/CLI.md) | Complete command-line usage |
| [Language reference](docs/language.md) | Types, variables, operators, control flow, functions |
| [Type reference](docs/types.md) | Primitive, fixed-width integer, and fixed-width float types |
| [Built-ins](docs/builtins.md) | `print`, `input`, `strOf`, `returnType`, `seqFromTo`, … |
| [Tuples](docs/tuples.md) | `tuple` type, built-in tuple functions |
| [importAs](docs/importAs.md) | `importAs("module", "alias")` — import under a custom name |
| [Standard library](docs/stdlib.md) | All stdlib modules — overview and function tables |
| [stdlib/ reference](docs/stdlib/README.md) | Per-module documentation pages |
| [Built-ins](docs/builtins.md) | Core language functions, including unmanaged memory operations |
| [rawPy / rawPyx](docs/rawpy.md) | Embedding Python and Cython |
| [Module system](docs/modules.md) | `import()`, `importAs()`, namespaces, writing your own modules |
| [Bytecode (.lynxc)](docs/bytecode.md) | Compiling to bytecode, running `.lynxc` files |
| [Vargroups](docs/vargroups.md) | Named typed records (struct-like) |
| [Structs](docs/structs.md) | Data-only named types with positional constructors |
| [Classes](docs/classes.md) | Instances, constructors, fields, and methods |
| [Async](docs/async.md) | Async functions |
| [Lists](docs/lists.md) | List operations |

---

## Project layout

```
lynxer/
  lynxer.py         Lexer + parser + interpreter + bytecode compiler
  builtins.py       Language builtin implementations and registry
  shell.py          CLI entry point
  stdlib/           Standard library modules (.lynx files; native memory is built in)
  cpp.cpp           C++ implementation of core memory built-ins
  setup.py          C++ extension build script
docs/               Documentation
syntax.lynx         Full syntax showcase
main.py             Launcher (delegates to shell.py)
Makefile
README.md
```

---

## License

MIT — [see LICENSE](LICENSE).
