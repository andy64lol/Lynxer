# Removed features

Features the original `docs-legacy/` reference documented that the standalone
runtime does not implement. Each entry records the current behaviour and the
replacement.

## Bytecode (`.lynxc`)

The bytecode backend is gone. `--compile` (`-c`, `--bundle`) produces a
standalone ELF executable instead, and there is no separate cache format.

| Input | Behaviour |
| --- | --- |
| `lynxer file.lynxc` | `lynxer: bytecode files are no longer supported; compile the .lynx source with --compile instead` |
| `--view-bytecode`, `--inspect-bytecode`, `--disasm` | `lynxer: '<flag>' was removed with the bytecode backend; use lynxer --compile instead` |
| `--benchmark-compile`, `--no-cache` | Same "removed with the bytecode backend" error |

See [CLI.md](CLI.md) for the current flags.

## Python bridging (`rawPy`, `rawPyx`, `embedPy`)

Lynxer does not embed or link a Python runtime, and these are **removed fully**
— not a planned reopen.

| Name | Behaviour |
| --- | --- |
| `rawPy` / `rawPyx` | `<name>() is not supported in Lynxer yet` |
| `cleanRawPyxCache` | Same as above |
| `embedPy` | `Python bridging (embedPy) is not supported in Lynxer` |

`rawPy { ... }` is not special syntax: it parses as an ordinary call with an
inline codeblock and then fails as above. `embedPy.x(...)` is rejected at the
member-call level. See [builtins.md](builtins.md).

## Python introspection getters

`os.getPythonVersion()`, `os.getPythonImplementation()` and the `python`,
`pythonImplementation` and `pythonExecutable` fields of `os.getSystemInfo()`
are **removed**. They existed only to mimic a Python host — `""` and `"Lynxer"`
are not values any program should branch on — and there is no Python runtime to
report. The fields are simply absent from `getSystemInfo()` now; the `sys`
introspection surface (`sys.path`, `addPath`, `getModules`, …) was never
implemented and stays unplanned.

## `http` and `net` modules

The legacy `http` and `net` modules are **not provided**, and no compatibility
shim is planned. `network` is the client — HTTP, WebSocket, and URL handling in
one module — and `server` covers serving, so a program has one API surface
rather than three overlapping ones. The legacy names (`httpGet`, `netSocket`, …)
are not aliases; port a program to `global.network.*` / `global.server.*`. See
[stdlib/network.md](stdlib/network.md).

## `venv`

A virtual-environment manager is a Python concept with no equivalent in a
standalone runtime; the module is not provided.

## `tkinter` / `turtle`

There is no Python GUI toolkit binding. The [graphics](stdlib/graphics.md)
module provides a Rust-backed (`macroquad`) immediate-mode drawing, window,
input and UI toolkit instead; it is not a `tkinter` clone. `turtle`'s Rust
crate has been unmaintained since 2019.

## Python → C++ migration

There is no longer a Python implementation to migrate from. The former
`clynxer/` tree, the `lynxer.py` facade, the `lynxer-py` reference build and the
v9 bytecode contract are all gone; `lynxer/` (C++17) is the only implementation.

| Removed | Replacement |
| --- | --- |
| Python implementation and facade | `lynxer/` (C++), built with `make buildLynxer` |
| Python-only build paths (`clynxer/`, `make buildCpp`) | The root `Makefile` |
| Bytecode migration/version contract | `--compile` to a standalone ELF executable |

See [legacy-surface.md](legacy-surface.md) for the full list of original features
and their replacements.
