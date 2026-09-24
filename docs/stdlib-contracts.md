# Standard library module contracts

**Frozen at Lynxer 0.1.8 — 2026-09-20.**

Every `stdlib/<name>.lynx` wrapper and its `stdlib/<name>.so` backend are two
halves of one contract. This page states that contract once, so a second backend
for an existing module — or a new module — can be written against it without
re-deriving the conventions from the code.

The conventions below apply to all 27 bundled modules. The per-module table
records only the dimensions that actually vary between them. For an individual
operation's argument list and return value, the authoritative sources are the
wrapper itself (`stdlib/<name>.lynx`), `docs/stdlib/<name>.md`, and
`--list-stdlibs`, which prints each wrapper's docstring.

## The module pair

A module is always two files with the same stem:

| File | Role |
| --- | --- |
| `stdlib/<name>.lynx` | The Lynxer-facing module. Defines `setup()` and one `global` function per operation, and forwards each one to the namespace its `setup()` imported. |
| `stdlib/<name>.so` | The native backend. Built from `stdlib/<name>.cpp` (C++) or `rust/<name>/` (Rust), and registers its operations through `lynxer_module_init_v1`. |

The wrapper is the contract's public surface; the backend is an implementation
detail behind it. **A wrapper must never expose a crate- or library-specific
type.** Everything crossing the boundary is a number, a string, or an integer
handle — see [native-module-abi.md](native-module-abi.md) for why.

## Conventions that apply to every module

### Operation names

- The backend registers each operation under the name the wrapper calls; the
  Lynxer-facing name is a lowerCamelCase `global` function.
- The wrapper imports its backend once, in `setup()`, with
  `importAs("<name>.so", "native<Name>")`. Nothing else in the wrapper refers to
  the backend.
- `setup()` is the only place a module may import. Importing from a program
  function is not part of the contract.

### Argument order and types

- A Lynxer `str` crosses as `cstring`, and `int`, `float` and `bool` cross as
  numbers. Booleans are `0`/`1`; the wrapper converts back with `!= 0`.
- **A Rust backend must register the packed `cdecl:<ret>(...)` signature.** The
  packed argument view indexes per kind, so the *i*-th numeric argument is
  `args.int(i)` and the *i*-th string argument is `args.string(i)`, independent
  of the order they appeared in the call.
- A C++ backend may register one of the fixed shapes instead; the interpreter
  type-checks those per argument at call time.

### Handles

- A handle is an **integer index into a registry owned by the backend**, not a
  pointer. `-1` means "no handle".
- Handles are stable for the life of the entry: an index is never reused while
  the entry is live, and released entries leave a gap.
- The backend owns the underlying resource. The wrapper never sees it, and the
  Lynxer program cannot reach it except through operations.
- Passing an out-of-range or released handle is **not** an error condition: the
  operation returns its failure sentinel.

### Strings

- Returned strings stay valid only until the interpreter copies them, which
  happens immediately after the call. A backend therefore keeps at most one live
  string result per call (a `thread_local` buffer on the Rust side).
- String arguments are borrowed for the duration of the call only; a backend
  must copy anything it retains.
- Structured data crosses as a **JSON string**, never as a native object.

### Error sentinels

There is no error channel across the ABI, so failures are in-band. Each module
picks one family and applies it consistently:

| Family | Shape | Modules |
| --- | --- | --- |
| Scalar sentinel | `-1` for indices and sizes, `0` / `0.0` for numbers, `false` for predicates, `""` for strings | `cli`, `debug`, `fileIO`, `game`, `image`, `json`, `math`, `multiprocessing`, `os`, `path`, `random`, `re`, `regex`, `sound`, `sys`, `time` |
| Process exit code | the command's exit status; a negative value when it did not run | `shell` |
| Status string | `"ok"` on success; `"ERROR: <message>"` on failure | `csv`, `sqldb`, `network`, `server` |
| Error string | `"Error: <message>"` | `js`, `lua`, `tui` |

`csv` mixes the first two: its table-returning operations answer with the status
string (`"ok"` / `"ERROR: ..."`), and the rest use scalar sentinels.
`colorlib`, `text` and `typing` are pure Lynxer and have no native boundary to
report across.

Operations returning a JSON document return `[]` or `{}` for an empty result
and an error string for a failure — never an exception. `jsonParse` on malformed
input yields the **empty string** (`""`), not `[]`; test the result for `""`.

A failure sentinel is a result, not a diagnostic: a program checks it and
decides what to do. No bundled module aborts the process, and none panics across
the C boundary (Rust backends wrap every operation in a panic guard that yields
the module's sentinel).

### Callbacks

One module currently registers a callback into Lynxer: `game`, through the
optional `lynxer_module_attach_v1` host API. The contract for it is:

- Callbacks are **named Lynxer functions**, resolved against the top-level
  program, not closures or function pointers.
- The name is supplied by the program (`setUpdateCallback("onUpdate")`) and
  resolved when the callback fires, so a missing function is reported at that
  point rather than at registration.
- A failing callback surfaces through the interpreter after the native call
  returns; the backend does not swallow it.
- Frame callbacks only fire from `run()`, which blocks until the program is
  closed or interrupted.

Any future callback must be declared here before the module ships.

### Interruption

- Native operations that can run long must poll `interrupted` from the host API
  and return their sentinel once SIGINT has been seen, rather than holding the
  interpreter past the interrupt.
- `game.run()` is the module that implements this today: Ctrl-C exits with
  status 130.

### Cleanup

- Whatever the program allocates with an *open*/*load*/*create* operation, it
  releases with the matching *close*/*release* operation, or by loading the
  module's `setup` state again. Where a wrapper can do this automatically it
  does, and that is recorded in the table below.
- Releasing twice, or releasing an invalid handle, returns the failure sentinel
  rather than trapping. It is never a fatal error.
- A backend holds no file, socket or device open beyond the operations that
  need it, except where the module's own documentation says otherwise
  (`server` holds a listener; `sound` holds one output stream; `network` holds
  named WebSocket connections).

## What each module commits to

| Module | Backend | Identity model | Cleanup owner |
| --- | --- | --- | --- |
| `cli` | C++ | none | none |
| `colorlib` | pure Lynxer | none | none |
| `csv` | C++ | none — paths are strings | none |
| `debug` | C++ + pure | none | none |
| `fileIO` | C++ | none — paths are strings | none |
| `game` | Rust | integer indices: cached textures and sprites | none (caches live for the run; `close()` ends the loop) |
| `image` | Rust | **integer handles** into a backend registry | caller: `close(handle)` |
| `js` | C++ | none | none |
| `json` | Rust | none — documents are strings | none |
| `lua` | Rust | none | none |
| `math` | C++ | none | none |
| `multiprocessing` | C++ | **integer handles** for result sets | wrapper releases automatically |
| `network` | Rust | WebSocket connections keyed by **name**; HTTP is stateless | caller: `wsClose(name)` |
| `os` | C++ | none | none |
| `path` | C++ | none — paths are strings | none |
| `random` | C++ | none — seeded generator lives in the backend | none |
| `re` | C++ | none | none |
| `regex` | C++ | none — the named-pattern cache lives in the backend | none |
| `server` | Rust | routes keyed by path, in registration order | caller: `clearRoutes()` / `stop()` |
| `shell` | C++ | none | none |
| `sound` | Rust | **integer handles** into a backend registry | caller: `releaseSound(handle)` |
| `sqldb` | Rust | **no handle** — every call names a database path and gets its own connection | none (the connection is closed per call) |
| `sys` | C++ | none | none |
| `text` | pure Lynxer | none | none |
| `time` | C++ | none | none |
| `tui` | Rust | placeholder indices only; no state is kept | none |
| `typing` | pure Lynxer | none | none |

Per-module constraints are recorded in
[limitations.md](limitations.md), not here: this page fixes what Lynxer's own
contract *is*, and that page records where it deliberately differs.

## Changing a contract

The contract is frozen as of the version above. Changing any part of it — an
operation name, an argument's type or order, which argument is the handle, a
sentinel value, or cleanup ownership — is a breaking change and needs:

1. the wrapper and **every** backend for that module updated together;
2. the module's `docs/stdlib/<name>.md` and this page updated;
3. its fixture and `.expected` updated, and the change noted in
   [limitations.md](limitations.md) if it diverges further from the reference.

An operation may be **added** without unfreezing anything, provided it uses the
existing conventions and does not need a new ABI shape.

## How it is enforced

| Mechanism | What it checks |
| --- | --- |
| `scripts/check_module_contracts.py`, run by `make test` | Every `global.native<Alias>.<op>(...)` call in a wrapper names a registered op; a Rust backend's packed reads are in range for the arguments that wrapper passes; and any registered op no wrapper calls is reported. |
| Fixture suite (`examples/stdlib_*.lynx` + `.expected`) | Each module's observable behaviour, per operation, including failure paths. |
| Compiled/bundled parity (`make test`) | The same fixtures behave identically when compiled into a standalone executable, with the module sources and `.so` files embedded. |
| `stdlib/*.so` registration | The interpreter rejects a module whose initialiser fails, whose name is a duplicate, or whose symbol does not resolve. |

The contract check covers the mechanical half — names and argument arity. The
semantic half — which argument *means* what — is what this page freezes, and it
is the part a reviewer must check against a new backend by hand.
