# Lynxer — TODO and non-goals

Open work for the standalone C++ implementation in `lynxer/`. The completed
milestone history is in git; this file stays forward-looking. The full
behaviour register is [docs/limitations.md](docs/limitations.md).

## Not planned (will not be implemented)

Permanent non-goals — do **not** start work on these. Kept here so the decision
is visible next to the work that *is* open.

| Feature | Why |
| --- | --- |
| `venv` module | A virtual-environment manager is a Python concept with no equivalent in a standalone runtime. |
| `rawPy`, `rawPyx`, `cleanRawPyxCache`, `embedPy` | Python/Cython embedding. Lynxer does not ship or link a Python runtime. |
| `tkinter`, `tkinterPlus` | No Python GUI toolkit. Any future GUI would be Rust-backed (see `graphics` below). |
| `turtle` | Rust's `turtle` crate has not been maintained since 2019. |
| `http`, `net` modules | Superseded by `network` + `server`. |
| Click/Typer builders (`click*`, `typer*`) | The `cli` module does not depend on Python's Click or Typer. |
| Python runtime introspection (`sys.path`, `addPath`, `prependPath`, `removeFromPath`, `getModules`, `isModuleLoaded`, `getRecursionLimit`, `setRecursionLimit`, `os.getPythonVersion`, `os.getPythonImplementation`) | There is no Python runtime to introspect. |
| Bytecode (`.lynxc`, `--view-bytecode`, `--benchmark-compile`, `--no-cache`) | Removed; `--compile` produces a standalone ELF executable instead. |
| `varBorrow*` family | Python's borrowed references have no meaning across the native ABI. |
| FFI / native-module handle built-ins | Superseded by importing a native `.so` through the documented ABI. |
| `nativeMutex*`, `nativeCondition*`, `nativeSemaphore*` | Lynxer runs cooperatively on one interpreter thread; there is no shared mutable state to protect. |
| `async*` family (`Run`, `Gather`, `Sleep`, `Poll*`, timers, wakeups) | No `async` language support and no event loop to serve. |
| `/* ... */` block comments | Only `//` and `/// ... ///` / `//// ... ////` are supported. |
| Bare `!` as logical NOT | Use `!!value` or `not value`. Symbolic comparators/bitwise spellings still parse but warn and are on the way out. |
| `\x` / `\u` string escapes | Only `\n`, `\r`, `\t`, `\\`, `\"` and `\e` are accepted. |

## Open decisions

Work that needs a decision before it can be built:

- [ ] **`ffi*` calling convention.** `ffiCall` has to describe and perform
  arbitrary native calls. `libffi` is the usual answer and is a **new build
  dependency** for the interpreter; hand-rolling covers only a few fixed
  signatures. It is also the largest security surface, since it turns a Lynxer
  program into arbitrary native code. Decide: take the dependency, hand-roll
  fixed shapes, or move the family to "not planned".
- [ ] **`async*` scope.** ~370 lines of surface (`Run`, `Gather`, `Sleep`, a
  `Poll*` family, timers, wakeups) needs an event loop, and Lynxer currently
  runs nothing asynchronously — there is no `async` language support to serve.
  Decide whether the family should exist at all. (Currently listed as not
  planned; this item is the decision that could change that.)
- [ ] **`nativeThread*` interleaving.** Threads are cooperative: a worker cannot
  make progress while the main body is running, which is the opposite of a
  pre-emptive runtime. Decide whether real interleaving is needed.
- [ ] **`graphics` module.** A Rust `iced`-backed GUI replacement for the
  not-planned `tkinter` / `turtle` surface — decide whether to build it.
- [ ] **`tui` real backend.** The API surface is complete but the backend is a
  placeholder: rendering draws fixed output, the prompt operations return empty
  defaults, and the stateful families keep no state. Decide whether to build a
  real terminal UI.

## Ground rules

- Keep everything under `lynxer/`; documentation in `docs/`, the website in
  `site/`.
- C++17 and the standard library only, unless a native dependency is explicitly
  chosen. The Rust backends are optional — a missing `cargo` only skips them.
- Prefer explicit, source-located errors over partial support.
- Add a focused `.lynx` fixture under `lynxer/examples/` for every user-visible
  feature, with a sibling `.expected`, and keep `make testLynxer` green.
- Regenerate the website with `python3 site/build.py` after editing `docs/`.
