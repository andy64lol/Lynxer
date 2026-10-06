# Lynxer

![Lynxer logo](assets/lynxer.png)
![](https://img.shields.io/badge/-Custom%20programming%20language-blue?style=for-the-badge)
[![Build Linux AMD64](https://github.com/andy64lol/Lynxer/actions/workflows/build-lynxer-linux-amd64.yml/badge.svg)](https://github.com/andy64lol/Lynxer/actions/workflows/build-lynxer-linux-amd64.yml)
[![Build Linux ARM64](https://github.com/andy64lol/Lynxer/actions/workflows/build-lynxer-linux-arm64.yml/badge.svg)](https://github.com/andy64lol/Lynxer/actions/workflows/build-lynxer-linux-arm64.yml)
[![Build Windows AMD64](https://github.com/andy64lol/Lynxer/actions/workflows/build-lynxer-windows-amd64.yml/badge.svg)](https://github.com/andy64lol/Lynxer/actions/workflows/build-lynxer-windows-amd64.yml)
[![Build Windows ARM64](https://github.com/andy64lol/Lynxer/actions/workflows/build-lynxer-windows-arm64.yml/badge.svg)](https://github.com/andy64lol/Lynxer/actions/workflows/build-lynxer-windows-arm64.yml)
[![Build Bob Linux AMD64](https://github.com/andy64lol/Lynxer/actions/workflows/build-bob-linux-amd64.yml/badge.svg)](https://github.com/andy64lol/Lynxer/actions/workflows/build-bob-linux-amd64.yml)
[![Build Bob Linux ARM64](https://github.com/andy64lol/Lynxer/actions/workflows/build-bob-linux-arm64.yml/badge.svg)](https://github.com/andy64lol/Lynxer/actions/workflows/build-bob-linux-arm64.yml)
[![Build Bob Windows AMD64](https://github.com/andy64lol/Lynxer/actions/workflows/build-bob-windows-amd64.yml/badge.svg)](https://github.com/andy64lol/Lynxer/actions/workflows/build-bob-windows-amd64.yml)
[![Build Bob Windows ARM64](https://github.com/andy64lol/Lynxer/actions/workflows/build-bob-windows-arm64.yml/badge.svg)](https://github.com/andy64lol/Lynxer/actions/workflows/build-bob-windows-arm64.yml)

Lynxer is a small, statically-flavoured, C-style scripting language. Programs use
the `.lynx` extension and run on the standalone native interpreter in `lynxer/` —
no VM, no bytecode. The same interpreter can also compile a program into a single
executable.

```c
global setup(){
    str name = input("What's your name? ");
}

global main(){
    println("Hello, ", name, "!");
}
```

→ **[Website](https://lynxer.onrender.com)** | **[Documentation](https://lynxer.onrender.com/docs/)** | **[Installation](docs/install.md)** | **[Language reference](docs/language.md)** | **[Functions](docs/functions.md)** | **[Macros](docs/macros.md)** | **[Standard library](docs/stdlib/)**

---

## Platforms

Lynxer currently supports **64-bit Linux** on `amd64` and `arm64`. The standalone
bundler and the Linux-specific system calls in `os` require Linux. Builds on any
other operating system or architecture **fail early** rather than mix syscall
tables.

**Windows** is a work in progress: the host layer exists, and the Linux-only
surface (the named syscalls and the `sys` module) is excluded for now. See
[docs/windows.md](docs/windows.md).

---

## Status

**v0.1.8.3** — a standalone C++ implementation with:

- a native interpreter and an AST optimizer;
- 38 standard-library modules, native (C++/Rust) backends included;
- a frozen native-module ABI for third-party backends;
- single-file executables via `--compile`;
- a full test suite on `amd64` and `arm64`.

Planned gaps are tracked in [docs/limitations.md](docs/limitations.md).

---

## Quick start

```bash
make build                                       # build the interpreter and every stdlib module
./lynxer/lynxer syntax.lynx                      # run a source file
./lynxer/lynxer --compile syntax.lynx -o syntax  # compile to a standalone executable
./lynxer/lynxer --version                        # print the version
./lynxer/lynxer --help                           # list every command

make test                                        # build and run the full test suite
```

To install the interpreter system-wide (default prefix `/usr`, override with
`LYNXER_PREFIX`):

```bash
sudo ./lynxer/lynxer --install
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

## Bob — the package manager

**Bob** (`Bob/`) is Lynxer's package manager: a separate Rust component with its
own Makefile and CI. Build it with `make buildBob`; the examples below use `bob`,
which is `Bob/target/release/bob` (or install it onto your `PATH` first with
`--install-exec`):

```bash
bob --ver            # Bob's version and the Lynxer version it supports
bob --init           # scaffold a project (bob/bob.toml, lock file, packages/)
bob --init-module    # scaffold a reusable module
bob --install-exec   # install the bob executable (prefix: BOB_PREFIX)
bob publish          # publish the current module to a GitHub Release
bob install <name> <version>   # install a module through the hosted registry
```

Published modules are listed on the [Bob Index](https://bobi-index.onrender.com).
See [Bob/README.md](Bob/README.md) for the full command set.

---

## Documentation

The documentation lives in [`docs/`](docs/README.md) and is published online:

- **Home page** — <https://lynxer.onrender.com>
- **Documentation** — <https://lynxer.onrender.com/docs/>

Both are built from `docs/` by [`site/build.py`](site/build.py) and served as a
Render static site.

| Page | Contents |
|------|----------|
| [Installation](docs/install.md) | How to build and run Lynxer |
| [CLI reference](docs/CLI.md) | Complete command-line usage |
| [Language reference](docs/language.md) | Types, variables, control flow, functions |
| [Functions](docs/functions.md) | `global`, `func` and `local` declaration forms and caller-supplied codeblocks |
| [Operators](docs/operators.md) | Arithmetic, comparison, boolean and bitwise operators, precedence, deprecated spellings |
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
| [Limitations](docs/limitations.md) | Planned implementation gaps |

---

## Links

| Resource | Link |
|----------|------|
| Website (home page) | <https://lynxer.onrender.com> |
| Documentation | <https://lynxer.onrender.com/docs/> |
| Package index (Bob Index) | <https://bobi-index.onrender.com> |
| Registry API | <https://lynxer.netlify.app> |
| GitHub repository | <https://github.com/andy64lol/Lynxer> |
| Bob (package manager) | [Bob/README.md](Bob/README.md) |

---

## Project layout

```
lynxer/        The interpreter and its standard library
  *.cpp, *.hpp   Lexer, parser, interpreter, optimizer, formatter, CLI
  stdlib/        Native and pure stdlib modules
  rust/          Rust-backed native modules
Bob/           Bob — the package manager (a separate Rust component)
BobI/          Bob Index — a read-only web index of published modules
docs/          Documentation (Markdown source)
site/          Static website: home page plus docs/ rendered to HTML
packaging/     Distribution packaging (AUR)
supabase/      Registry database migrations
syntax.lynx    Full syntax showcase
Makefile
README.md
```

`Bob/` builds separately with `make buildBob` (see above). `BobI/` is its own
Node service; `packaging/` and `supabase/` support distribution and the registry.

---

## License

MIT — [see LICENSE](LICENSE).
