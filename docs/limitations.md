# Known limitations

`todo.md` marks most work as complete. This page records the places where the
code, the tests, or the docs are still behind that claim, so nobody has to
rediscover the gap by reading the interpreter.

Every entry below was verified against the source.

---

## Compiler optimization — constant folding only

`todo.md` ("Compiler improvements") claims safe optimization passes. Constant
folding is now implemented: `_optimize_program()` in `lynxer/bytecode.py`
replaces an arithmetic or comparison expression with a literal when every
operand is a literal, and it does so by asking the interpreter itself to
evaluate the expression. An expression that the interpreter evaluates with an
error is left alone, so `1 / 0` still raises "Division by zero" at run time
instead of being folded away.

Still missing:

- **Dead-code elimination and unused-variable removal.** There is no purity or
  effect analysis anywhere in the compiler, and `ProgramNode.globals_list` is
  iterated when a module is imported, so dropping an apparently unused global
  would silently break importers.
- **Deeper passes** (strength reduction, copy propagation, inlining) have no
  framework to plug into — the pass list is a single function call.

The `--no-opt` flag is real now: it selects an unfolded AST and is recorded in
the bytecode metadata.

## C ABI: bit fields — not implemented

`todo.md` ("C ABI completeness") claims packing, padding, and bit fields.

**Packing is implemented.** Every `memoryStruct*` function that takes a layout
string accepts an optional trailing alignment argument that clamps the
alignment of every field and of any nested aggregate. See
[native-memory.md](native-memory.md#packed-layouts).

**Bit fields are not.** `StructField.offset` in `lynxer/cpp.cpp` is
byte-granular, and `memoryStructGet`/`memoryStructSet` dispatch on the field
type to whole-byte readers and writers that reject aggregates outright.
Bit fields need bit-offset and bit-width metadata plus new bit-level read and
write paths — a rewrite of the field accessors, not a tweak.

What is real: nested and inline structs/unions, fixed arrays, dynamically
sized arrays via `memoryBlockAllocate`, function-pointer fields, host-derived
alignment and padding, signedness, and packed/alignment-overridden layouts.

## Native concurrency: no cancellation, no high-level API

`todo.md` ("Concurrency API") claims cancellation, shutdown, and a high-level
thread API.

- **Cancellation is still cooperative only** — there is no thread-cancellation
  builtin. (`asyncTimerCancel` cancels a *timer*, not a thread.)
- **There is no `lynxer/stdlib/` concurrency module.** The API is the raw
  `nativeThread*`, `nativeMutex*`, `nativeCondition*`, and `nativeSemaphore*`
  builtins documented in [native-memory.md](native-memory.md).

Fixed since this page was written: `nativeThreadJoinAll()` is now exposed as a
Lynxer builtin, so a program can join every thread it left running instead of
relying on the interpreter's exit-time safety net.

## async I/O: `poll`, not `epoll`

`todo.md` ("async I/O") asks for epoll. The implementation is a `select.poll`
event loop (`lynxer/builtins.py`), which on Linux is a `poll(2)` wrapper.
[async.md](async.md) describes it honestly as an event poller and never claims
epoll. The `asyncPoll*` API and the syscall wrappers `syscallCreateEventPoll`,
`syscallControlEventPoll`, and `syscallWaitForEvents` do expose real epoll when
you need it.

This is unlikely to change: `asyncPoll*` accepts arbitrary file descriptors
including regular files, which epoll cannot watch (`EPERM`) but `poll` can, and
the syscall layer is Linux-only.

## Enums: braced body is inert

`todo.md` ("Rust-style enums") describes the braced section as
"enum-associated code". The block is parsed but **never executed**, and it may
not contain function declarations (a `global` function there is a parse error).
See [enums.md](enums.md#the-braced-body). There is no precedent to follow —
`class` and `struct` bodies do not execute statements either.

Fixed since this page was written: duplicate enum names and enum/function name
collisions are both diagnosed at parse time, with the original declaration's
line in the message.

## Switch patterns: no duplicate or unreachable-pattern diagnostics

`todo.md` asks for diagnostics for duplicate and unreachable patterns. Neither
exists.

- Duplicate detection would need a structural comparator for `PatternNode`,
  and "duplicate" is ill-defined for literal patterns because they hold an
  *unevaluated* expression — `case(1+1)` and `case(2)` mean the same thing but
  have different trees.
- Unreachable detection (subsumption) is a lattice problem with no
  infrastructure behind it.

One case has been closed: a binding pattern whose name already refers to a real
variable used to silently degrade into an equality comparison. It is now a
runtime error. Reusing a name that an earlier `switch` bound as a pattern is
still allowed, so two switches may both bind `v`.

## Ownership state is not serialized in bytecode

`todo.md` asks for ownership metadata to survive bytecode serialization. The
`.lynxc` payload stores the AST, version, hash, and native dependencies only.
Ownership, borrow, and runtime-type state are **recomputed at run time** from
the same program, so the semantics match, but a serialized snapshot of the
state is not part of the format.

This is not a small gap to close. Ownership is a purely runtime notion: the
state lives in side tables keyed by live `(SymbolTable, name)` tuples and by
native reference pointers, none of which exist before execution. A payload key
for it would always serialize empty. Closing it would mean writing a static
borrow checker first.

## Bundling: no real end-to-end bundle test

`todo.md` ("Bundling 3") leaves one item unchecked — running a release bundle
on physical ARM64 hardware — which this checkout cannot do.

Beyond that, `test_bundle_smoke_and_diagnostics` in `test/validate.py` replaces
`subprocess.run` with a stub, so PyInstaller is never actually invoked and no
executable is produced by the suite. The launcher, architecture guard, syscall
self-check, and diagnostics are all covered, and other tests in that file do
run the real `lynxer` CLI; it is specifically the packaging step that is not
exercised.

---

## Related pages

- [enums.md](enums.md) — enum reference, including its current limitations
- [native-memory.md](native-memory.md) — native memory, FFI, threads, sync
- [async.md](async.md) — async functions and the event poller
- [bytecode.md](bytecode.md) — bytecode format, cache, and CLI flags
- [CLI.md](CLI.md) — command-line reference
