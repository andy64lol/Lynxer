# Lynxer

A statically-flavoured, C-style scripting language that runs on Python.  
Files use the `.lynx` extension.

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

    // compound assignment
    x += 5;

    // control flow
    if(x > 10){
        greet(LANG);
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
    print(math.global.sqrt(144)); print("\n");
}
```

---

## Documentation

| Page | Contents |
|------|----------|
| [Language reference](docs/language.md) | Types, variables, operators, control flow, functions |
| [Built-ins](docs/builtins.md) | `print`, `input`, `strOf`, `returnType`, `seqFromTo`, … |
| [Standard library](docs/stdlib.md) | All stdlib modules — overview and function tables |
| [stdlib/ reference](docs/stdlib/README.md) | Individual per-module documentation pages |
| [rawPy / rawPyx](docs/rawpy.md) | Embedding Python and Cython |
| [Module system](docs/modules.md) | `import()`, namespaces, writing your own modules |
| [Bytecode (.lynxc)](docs/bytecode.md) | Compiling to bytecode, running `.lynxc` files, importing bytecode modules |
| [Vargroups](docs/vargroups.md) | Named typed records (struct-like) |
| [Classes](docs/classes.md) | Static singleton classes |
| [Async](docs/async.md) | Async functions |
| [Lists](docs/lists.md) | List operations |
| [Installation](docs/install.md) | Install, Makefile targets, requirements |

---

## Project layout

```
lynxer/
  lynxer.py         Lexer + parser + interpreter + bytecode compiler
  shell.py          CLI entry point (dev shim: python shell.py file.lynx)
  stdlib/
    math.lynx
    typing.lynx
    fileIO.lynx
    shell.lynx
docs/               Documentation
syntax.lynx         Full syntax showcase
Makefile
README.md
```

---

## License

MIT — [see LICENSE](LICENSE).
