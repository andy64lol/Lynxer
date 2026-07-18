# Lynxer

A statically-flavoured, C-style scripting language that runs on Python.  
Files use the `.lynx` extension.

```c
void setup(){
    str name = input("What's your name? ");
}

void main(){
    print("Hello, ");
    print(name);
    print("!\n");
}
```

---

## Installation

### System-wide

```bash
git clone https://github.com/andy64lol/Lynxer.git
cd Lynxer
make install          # installs to /usr/local/bin (may need sudo)
make install PREFIX=~/.local  # no sudo needed
```

`make install` also installs required Python packages (`cython`, `setuptools`).

### Run without installing

```bash
python lynxer/shell.py my_program.lynx
python -m lynxer my_program.lynx
```

### Uninstall

```bash
make uninstall
```

---

## Quick start

```bash
lynxer syntax.lynx      # run a file
lynxer --version        # print version
lynxer --help           # print help
```

---

## Language at a glance

```c
void setup(){
    import("math");
    const str LANG = "Lynxer";
}

void greet(str name){
    print("Hello, "); print(name); print("!\n");
}

void main(){
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
| [Standard library](docs/stdlib.md) | `math`, `typing`, `fileIO`, `shell` modules |
| [rawPy / rawPyx](docs/rawpy.md) | Embedding Python and Cython |
| [Module system](docs/modules.md) | `import()`, namespaces, writing your own modules |
| [Installation](docs/install.md) | Detailed install, Makefile targets, requirements |

---

## Project layout

```
lynxer/
  __main__.py       CLI entry point
  lynxer.py         Lexer + parser + interpreter
  shell.py          Dev shim (python shell.py file.lynx)
  stdlib/
    math.lynx
    typing.lynx
    fileIO.lynx
    shell.lynx
  tests/
    test.lynx
    import_test.lynx
    rawPy_test.lynx
    newfeatures_test.lynx
docs/
  language.md
  builtins.md
  stdlib.md
  rawpy.md
  modules.md
  install.md
syntax.lynx         Full syntax showcase
Makefile
setup.py
README.md
```

---

## License

MIT — [see LICENSE](LICENSE).
