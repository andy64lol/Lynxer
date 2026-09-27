# Lynxer — TODO and non-goals

Open work for the standalone C++ implementation in `lynxer/`. Completed work is
in git; this file stays forward-looking. The behaviour register is
[docs/limitations.md](docs/limitations.md) and the named-syscall reference is
[docs/syscalls.md](docs/syscalls.md).

Any name still registered in `unsupportedTable()` reports
`<name>() is not supported in Lynxer yet`.

## Not planned (will not be implemented)

Permanent non-goals — do **not** start work on these. Kept here so the decision
is visible next to the work that *is* open.

| Feature | Why |
| --- | --- |
| `venv` module | A virtual-environment manager is a Python concept with no equivalent in a standalone runtime. |
| `rawPy`, `rawPyx`, `cleanRawPyxCache`, `embedPy` | **Removed fully.** Python/Cython embedding; Lynxer does not ship or link a Python runtime. Not a reopen. |
| `tkinter`, `tkinterPlus` | No Python GUI toolkit. Any future GUI would be Rust-backed (see the `graphics` decision). |
| `turtle` | Rust's `turtle` crate has not been maintained since 2019. |
| Python runtime introspection (`sys.path`, `addPath`, `prependPath`, `removeFromPath`, `getModules`, `isModuleLoaded`, `getRecursionLimit`, `setRecursionLimit`, `os.getPythonVersion`, `os.getPythonImplementation`) | There is no Python runtime to introspect. |
| Bytecode (`.lynxc`, `--view-bytecode`, `--benchmark-compile`, `--no-cache`) | Removed; `--compile` produces a standalone ELF executable instead. |
| `\x` / `\u` string escapes | Only `\n`, `\r`, `\t`, `\\`, `\"` and `\e` are accepted. |

## Open work

### Modules

- [ ] `tkinter` / `tkinterPlus` / `turtle` — **reopen**. Candidate replacement:
      a Rust-backed `graphics` module (see the decision below).

## Named syscalls — policy for new wrappers

Every name the original documented, plus the extended and Stretch sets, is
implemented; there are no open syscall items. New wrappers follow these rules:

**Call syntax.** Named syscalls are architecture-gated: a program calls
`syscalls("amd64")` (or `"arm64"`; `x86-64` / `aarch64` are aliases) once to
select the host architecture, then reaches every wrapper through that namespace
— `amd64.syscallRead(...)`. Only one architecture can be selected, the keyword
must name the machine, and a misspelled keyword or namespace prefix answers with
`You meant: <closest>?`. `global.sys.architecture()` returns the keyword to pass.

**Portability rule.** A new wrapper must work on **both** Linux `amd64` and
`aarch64` from the same source: resolve the number in `syscallNumberFor`, guard
each mapping with `#ifdef SYS_<name>` so an older header set degrades to the
"not available on this architecture" error instead of the wrong table, and keep
every name available on both arches — where the raw call differs, split only the
number and argument fix-up, not the name (the portable `poll`/`epoll` wrappers
are the model). No `syscall*` name is architecture-exclusive. Prefer a libc
wrapper when the raw call is an unstable ABI (e.g. `clone`/`clone3`). Because
call sites name an architecture, an architecture-agnostic fixture carries a
`__ARCH__` token that the Makefile substitutes per host (`SYSCALL_ARCH`).

## Open decisions

Work that needs a decision before it can be built:

- [ ] **`ffi*` calling convention.** `ffiCall` has to describe and perform
  arbitrary native calls. `libffi` is the usual answer and is a **new build
  dependency** for the interpreter; hand-rolling covers only a few fixed
  signatures. It is also the largest security surface, since it turns a Lynxer
  program into arbitrary native code. Decide: take the dependency, hand-roll
  fixed shapes, or move the family to "not planned".
- [ ] **`async*` semantics.** The family is implemented (`asyncRun`,
  `asyncGather`, `asyncSleep`, the `asyncPoll*` set, timers and wakeups) and
  `await` yields cooperatively, but there is no `async` language support.
  Decide whether to keep it as-is, expand it, or freeze the surface.
- [ ] **`nativeThread*` interleaving.** Threads are cooperative: a worker cannot
  make progress while the main body is running, which is the opposite of a
  pre-emptive runtime. Decide whether real interleaving is needed.
- [ ] **`graphics` module.** A Rust `iced`-backed GUI replacement for the
  not-planned `tkinter` / `turtle` surface — decide whether to build it.
- [ ] **`tui` real backend.** The API surface is complete but the backend is a
  placeholder: rendering draws fixed output, the prompt operations return empty
  defaults, and the stateful families keep no state. Decide whether to build a
  real terminal UI.

## Already incorporated

- The rest of the original `docs-legacy` surface: pointers/raw addresses,
  atomics and volatile access, native synchronization, `memoryProtect`, the
  `nativeModule*` handles, `ffi*`, typed `memory*` accessors, the managed
  `filesystem*` / `process*` / `networking*` / `sound*` families, and
  `nativeThread*`.
- Language: ownership/borrowing (`varTransfer`/`varBorrow`/…, `shared`,
  `unshare()`), the `async*` family with cooperative `await`, and bracket-literal
  tuple rebinding (accepted, with a once-per-location deprecation warning).
- Stdlib modules ported from `docs-legacy`: `typing`, `text`, `csv`, `regex`,
  `image`, `game`, `server`, `network`, `cli` (native Click/Typer builders), and
  `mathPlus` (merged into `math`).
- `sys` system information: `architecture`, `cpuCount`, `pageSize`,
  `memoryTotal`, `memoryAvailable`, `uptime`, `bootTime`, `loadAverage`.
- Named syscalls: the original set plus the extended and Stretch families
  (io_uring, Landlock/seccomp), architecture-gated and available on both arches.
- Range `for` loops: `for (int i = start (.. | ..=) end [.. step])` beside the
  C-style form (`..` excludes the end, `..=` includes it; the third value is the
  step). See [docs/loops.md](docs/loops.md) and
  `lynxer/examples/range_for.lynx`.
- `cli` builders: `click*` / `typer*` over integer handles — with `--help`/`-h`,
  `--no-<flag>` negation, `noArgsHelp`, repeated options and env-var defaults —
  and removal of the Python availability probes.
- Absorbing the legacy docs: the remaining `docs-legacy` reference pages
  (`async`, `filesystem`, `networking`, `process`, `native-memory`,
  `native-modules`) now live under `docs/`, bytecode/`rawPy`/`venv`/`tkinter` and
  the Python migration are consolidated in
  [docs/removed-features.md](docs/removed-features.md), and every claim was
  verified against the interpreter. That audit corrected
  [docs/legacy-surface.md](docs/legacy-surface.md): the `async` language syntax
  **does** exist (it is eager, not a coroutine).

## Ground rules

- Keep everything under `lynxer/`; documentation in `docs/`, the website in
  `site/`.
- C++17 and the standard library only, unless a native dependency is explicitly
  chosen. The Rust backends are optional — a missing `cargo` only skips them.
- Prefer explicit, source-located errors over partial support.
- Add a focused `.lynx` fixture under `lynxer/examples/` for every user-visible
  feature, with a sibling `.expected`, and keep `make testLynxer` green.
- Regenerate the website with `python3 site/build.py` after editing `docs/`.
