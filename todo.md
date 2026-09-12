# TODO

## Native execution & FFI

- [x] Native function calling from an address
- [x] Function-address values
- [x] C/C++ FFI
- [x] Dynamic library loading and symbol lookup
- [x] Native callbacks
- [x] ABI and calling-convention support

## Native memory

- [x] C++-backed raw allocation and typed memory
- [x] Native struct allocation and field access
- [x] Alignment and layout introspection
- [x] Explicit native byte-order operations
- [x] Atomic memory operations
- [x] Volatile memory operations
- [x] Memory protection
- [x] Safe native handles with ownership and lifetime checks

## Native concurrency

- [x] Native thread primitives

## Runtime / portability

- [x] Native error/status values instead of silent failures
- [x] Portable native-extension builds

## Bundling 1

- [x] --bundle command producing a host-native Linux executable for x86-64 with the Lynxer interpreter bundled
- [x] Make sure --bundle command works
- [x] Make --bundle generate: build/ and dist/
- [x] Make Lynxer to bundle the compiled bytecode stored in build/bytecode
- [x] Test if bundled projects works

## Syscalls

- [x] adding following syscalls:

* `syscallRead()`
* `syscallWrite()`
* `syscallGetCurrentDirectory()`
* `syscallChangeDirectory()`
* `syscallControlInputOutput()`
* `syscallPositionedRead64()`
* `syscallPositionedWrite64()`
* `syscallOpenAt()`
* `syscallClose()`
* `syscallReadVector()`
* `syscallWriteVector()`
* `syscallSeekFile()`
* `syscallGetFileStatus()`
* `syscallGetFileStatusAt()`
* `syscallTruncateFile()`
* `syscallCheckFileAccessAt()`
* `syscallSynchronizeFile()`
* `syscallSynchronizeFileData()`
* `syscallDuplicateFileDescriptor()`
* `syscallDuplicateFileDescriptorAt()`
* `syscallCreatePipe()`
* `syscallControlFileDescriptor()`
* `syscallGetDirectoryEntries()`
* `syscallReadSymbolicLink()`
* `syscallCreateDirectoryAt()`
* `syscallRemoveFileAt()`
* `syscallRenameFileAt()`
* `syscallCreateHardLinkAt()`
* `syscallCreateSymbolicLinkAt()`
* `syscallChangeFilePermissions()`
* `syscallChangeFileDescriptorPermissions()`
* `syscallChangeFileOwner()`
* `syscallChangeFileDescriptorOwner()`
* `syscallMemoryMap()`
* `syscallMemoryUnmap()`
* `syscallMemoryProtect()`
* `syscallMemoryAdvise()`
* `syscallMemoryRemap()`
* `syscallAdjustProgramBreak()`
* `syscallExecuteProgram()`
* `syscallExecuteProgramAt()`
* `syscallExitProcess()`
* `syscallExitAllThreads()`
* `syscallWaitForProcess()`
* `syscallGetProcessId()`
* `syscallGetParentProcessId()`
* `syscallSendSignal()`
* `syscallCreateThread()`
* `syscallGetThreadId()`
* `syscallWaitOnMemory()`
* `syscallSetThreadIdAddress()`
* `syscallSetRobustThreadList()`
* `syscallGetRobustThreadList()`
* `syscallYieldProcessor()`
* `syscallGetClockTime()`
* `syscallGetClockResolution()`
* `syscallSleep()`
* `syscallGetRandomBytes()`
* `syscallCreateSocket()`
* `syscallCreateSocketPair()`
* `syscallBindSocket()`
* `syscallListenSocket()`
* `syscallAcceptConnection()`
* `syscallConnectSocket()`
* `syscallSendData()`
* `syscallReceiveData()`
* `syscallSendMessage()`
* `syscallReceiveMessage()`
* `syscallShutdownSocket()`
* `syscallGetSocketAddress()`
* `syscallGetPeerAddress()`
* `syscallSetSocketOption()`
* `syscallGetSocketOption()`
* `syscallPollFileDescriptors()`
* `syscallCreateEventPoll()`
* `syscallControlEventPoll()`
* `syscallWaitForEvents()`
* `syscallInitializeInodeNotifications()`
* `syscallAddInodeNotificationWatch()`
* `syscallRemoveInodeNotificationWatch()`
* `syscallGetSystemInformation()`
* `syscallGetUnixSystemName()`
* `syscallGetExtendedFileStatus()`
* `syscallGetResourceUsage()`
* `syscallGetResourceLimit()`
* `syscallSetResourceLimit()`
* `syscallControlProcess()`

### Syscall layer extensions

- [x] Finish the Linux x86-64/ARM64 layer:
- [x] Verify every wrapper against both architectures' syscall tables and calling conventions.
- [x] Add architecture-aware tests for return values, errno failures, pointer arguments, and ABI-specific structures.
- [x] Document supported flags, structures, and portability limitations.

## C ABI completeness

- [x] Complete C ABI support for native interop:
- [x] Add nested structs and unions with exact native size, alignment, and field-offset calculations.
- [x] Add fixed-size and dynamically sized native arrays.
- [x] Add function-pointer fields, values, callbacks, and typed invocation.
- [x] Cover packing, padding, signedness, and platform ABI differences with compiler-backed tests.
- [x] Implement deterministic integer bit-field layout and accessors with
  regression coverage; compiler-specific allocation-order compatibility remains
  documented as a portability limitation.

## Native module system

- [x] Make `.so` extensions first-class Lynxer modules:
- [x] Define module discovery, naming, loading, and lifecycle rules.
- [x] Expose a stable registration ABI for functions, types, and constants.
- [x] Add module-local error handling, unload safety, and dependency reporting.
- [x] Support modules in source execution, bytecode, and bundled programs.

## Process API

- [x] Build a high-level process API over the syscall layer:
- [x] Spawn programs with argument and environment configuration.
- [x] Provide stdin/stdout/stderr pipes and safe descriptor cleanup.
- [x] Add signal delivery, wait, timeout, and exit-status inspection.
- [x] Cover failures, interrupted waits, orphaned processes, and cleanup.

## Filesystem API

- [x] Build a safe, ergonomic filesystem abstraction over the syscall layer:
- [x] Add path, file, directory, metadata, rename, link, and permission operations.
- [x] Use managed file handles with deterministic close behavior.
- [x] Normalize errors and path behavior without hiding errno details.
- [x] Test regular files, directories, symbolic links, and edge cases.

## Networking API

- [x] Add TCP, UDP, and Unix-domain socket APIs over the syscall layer:
- [x] Provide address parsing, bind/listen/connect, accept, send, and receive operations.
- [x] Add blocking, non-blocking, socket options, shutdown, and cleanup.
- [x] Cover IPv4, IPv6, Unix sockets, connection failures, and timeouts.

## Concurrency API

- [ ] expose threads and synchronization cleanly to Lynxer:
- [ ] add a high-level thread lifecycle and result/error propagation API
- [x] add mutexes, condition variables, semaphores, and safe ownership
- [ ] define cancellation, shutdown, and deadlock-resistant cleanup rules.
- [x] test contention, wakeups, failures, and interpreter shutdown

## async I/O

- [x] Add event-driven APIs for serious servers:
- [ ] Make the high-level async poller epoll-backed where the platform permits.
- [x] Provide registration, modification, removal, and event waiting.
- [x] Integrate file, socket, timer, and wakeup events.
- [x] Define callback/task scheduling, cancellation, and backpressure.
- [x] Add high-concurrency and graceful-shutdown tests.

## Compiler improvements

- [x] Improve compiler performance and developer feedback:
- [x] Add safe optimization passes and benchmark representative programs.
- [ ] Add dead-code elimination and a pluggable optimization-pass pipeline.
- [x] Improve source locations, type errors, runtime diagnostics, and actionable suggestions.
- [x] Reduce compilation overhead through caching and incremental work.
- [x] Add regression tests for optimized and unoptimized output.

## bundling 2

- [x] Make `--bundle` absolutely bulletproof:
- [x] Test bundled source and bytecode programs on Linux x86-64.
- [x] Verify native extensions, standard-library assets, imports, and resource paths in clean environments.
- [x] Improve failure diagnostics and reproducibility.
- [x] Add smoke, compatibility, and repeatable release-build checks.

## Bundling 3

- [x] Make bundling host-architecture aware for both x86-64 and ARM64:
  - Resolve the Linux architecture from the host ABI before compiling, and
    record the selecxed architecture in the generated launcher.
  - Refuse to run a copied executable on a different architecture instead of
    silently using the wrong syscall table.
- [x] Make syscalls inside bundled executables self-contained:
  - Include `lynxer.syscalls`, the `system_calls` hidden imports, submodules,
    and package data in both the standalone bundle command and the Makefile
    release build.
  - Validate the platform and syscall-table availability before launching the
    bundled bytecode, with a clear rebuild error when the tables are missing.
- [x] Verify bundling behavior and failure reporting:
  - Cover launcher architecture checks, syscall-table checks, runtime-hook
    staging, successful command generation, and preservation of PyInstaller
    stdout/stderr in regression tests.
- [ ] Run a release bundle and execute syscall smoke tests on a physical ARM64
  Linux runner; host-native code can guard the target correctly, but this
  checkout cannot honestly claim hardware execution on ARM64 without that
  runner.
- [x] Make bundle failures actionable:
  - Reject unsupported platforms, missing source files, invalid output names,
    missing native dependencies, stale/invalid compilation, missing output
    executables, and PyInstaller failures with explicit diagnostics.

## v0.1.8

- [x] Add file-wide `func` declarations for uniquely named functions:
  - Introduce `func functionName(){}` as a declaration form for functions that
    are callable directly as `functionName()` without a namespace qualifier.
  - Register every `func` declaration in a file-wide function table, regardless
    of where the declaration appears, so a name can be referenced before its
    textual declaration when the language's normal declaration rules allow it.
  - Reject duplicate `func` declarations anywhere in the same Lynxer file with
    a compile-time error that points to both the original and conflicting
    declarations. The uniqueness check must cover functions declared in
    different blocks or sections, not only adjacent declarations.
  - Disallow local functions from declaring another `func` function. A local
    function may still use ordinary local-function syntax where supported, but
    it must not enter the file-wide `func` registry.
  - Keep imported functions module-scoped at the import boundary: if a file
    declares `func myFunction(){}`, another Lynxer file calls it as
    `global.moduleName.myFunction()`, while code inside the declaring file may
    call it directly as `myFunction()`.
  - Define collision behavior between `func` names, existing global functions,
    imported names, built-ins, and reserved names, and produce actionable
    diagnostics rather than silently shadowing a callable.
  - Cover direct calls, recursion, forward references, imports, duplicate
    declarations, and attempts to declare a `func` inside a local function.

- [x] Add explicit ownership transfer and borrowing operations inspired by Rust:
  - Implement `varTransfer(oldVar, newVar)` as a move operation. The destination
    receives the value and ownership metadata, while the source becomes moved
    and cannot be read, written, transferred, borrowed, or passed to a function
    until it is reinitialized according to the ownership rules.
  - Implement `varBorrow(oldVar, newVar)` as a borrow operation that gives
    `newVar` a tracked reference to `oldVar` without duplicating ownership.
    Track the source, borrower, lifetime/state, and mutability so a source
    cannot be destroyed or moved while an active borrow exists.
  - Implement `varEndBorrow(newVar)` to explicitly end the borrow represented by
    `newVar`. It must validate that the variable is an active borrow and report
    an error for moved, already-ended, or non-borrowed values.
  - Define the behavior for read-only versus mutable borrows, including whether
    multiple read-only borrows may coexist and when a mutable borrow must be
    exclusive. Enforce these rules consistently in assignments, calls, returns,
    collection operations, and scope cleanup.
  - [ ] Preserve ownership metadata through function parameters, return values,
    lists, tuples, structs, modules, and bytecode serialization. Runtime errors
    must identify the variable and the conflicting ownership operation.
  - Ensure failed operations do not partially update either variable or the
    ownership table.

- [x] Add ownership-aware swapping operations:
  - [x] Implement `varSwapAll(firstVar, secondVar)` to exchange both values and
    their complete runtime type/ownership metadata. The operation must work
    across compatible variable declarations and must define what happens when
    either variable is moved, borrowed, const, or otherwise unavailable.
  - [x] Implement `varSwapVal(firstVar, secondVar)` to exchange only values while
    retaining each variable's declared/runtime type identity. Reject the swap
    when either value cannot be assigned to the other variable's type.
  - [x] Define atomic failure behavior: type incompatibility, active-borrow conflicts,
    and invalid ownership states must leave both variables unchanged.
  - [x] Add clear diagnostics and tests for primitive values, lists, tuples, structs,
    `any`/numeric types, moved values, and active borrows.

- [x] Add type-changing ownership operations:
  - Implement `varTransferMutate(oldVar, newVar)` as a transfer that also allows
    the destination's runtime type to change to the moved value's type, subject
    to the destination declaration and ownership rules.
  - Implement `varBorrowMutate(oldVar, newVar)` as a borrow that permits the
    borrowed value's type to be changed through the tracked borrower only when
    the borrow is mutable and exclusive. Define whether the source's visible type
    changes immediately or when the borrow ends, and keep that behavior
    consistent for all aliases.
  - Reject type mutation through read-only borrows, stale borrows, moved values,
    or aliases that would make the ownership state ambiguous.
  - Serialize and restore the resulting type and borrow metadata in bytecode,
    including failures for invalid mutation sequences.

- [x] Add ownership inspection helpers:
  - Implement `borrowing(variable)` returning a boolean indicating whether the
    variable currently represents an active borrow or has an active borrowing
    relationship, using one documented and consistent interpretation.
  - Implement `beingBorrowed(variable)` returning a boolean indicating whether
    another live variable currently borrows from the supplied variable.
  - Define behavior for uninitialized, moved, ended-borrow, const, and
    non-reference values, and make the helpers safe to call during error
    handling and cleanup.
  - Add tests proving that both helpers change at the correct points during
    transfer, borrow, mutation, explicit `varEndBorrow`, and scope exit.

- [x] Expand `switch` pattern matching to support lists and tuples:
  - Allow list and tuple literals as switch patterns, including nested lists and
    tuples, so a case can match sequence shape and element values rather than
    only scalar expressions.
  - Add documented wildcard and binding behavior for sequence patterns,
    including length checks, nested patterns, and bindings that can be used by
    the case body.
  - Define whether lists use exact length matching, whether tuple arity is
    always required, and how mutable list values are compared without
    accidentally changing them.
  - Produce compile-time or runtime diagnostics for malformed patterns,
    incompatible scrutinee types, duplicate/unreachable patterns, and bindings
    that conflict with existing names.
  - Cover scalar, list, tuple, nested, empty, wildcard, and no-match cases,
    then verify identical behavior from source execution and compiled bytecode.

- [x] Add a standalone `sound` standard-library module:
  - Make `import("sound")` expose a documented `global.sound` namespace for
    loading and reproducing sound files independently of the game module.
  - Provide lifecycle operations for loading a file, playing it once, looping
    playback, stopping, pausing/resuming, changing volume, querying playback
    state, and releasing resources. Return stable Lynxer values and explicit
    errors for missing files, unsupported formats, invalid handles, and audio
    backend failures.
  - Define supported audio formats, relative-path resolution, resource
    ownership, concurrent playback behavior, cleanup on interpreter shutdown,
    and behavior in headless or unavailable-audio environments.
  - Keep the API usable from source, imported modules, bytecode, and bundled
    programs, and document the distinction between `sound` and
    `global.game` audio helpers.
  - Add regression coverage for successful loading/playback control, invalid
    paths, repeated cleanup, volume boundaries, and backend failure reporting.

- [x] Update the bytecode format for the new language features:
  - Bump the bytecode format version and serialize the AST metadata required
    for `func` declarations, mutable type transitions, list/tuple switch
    patterns, enums, and the sound module's import metadata.
  - [ ] Serialize ownership states and borrow relationships, once a stable
    static/runtime ownership representation exists.
  - Update compiler output, bytecode loading, inspection tools, imports, cache
    invalidation, and bundled executables together so source and `.lynxc`
    programs have the same semantics.
  - Reject incompatible or incomplete bytecode with a clear recompile message;
    do not silently interpret new nodes using old semantics or discard ownership
    metadata.
  - Add compatibility tests for current-version files, intentionally stale
    versions, corrupted payloads, imported bytecode modules, and bundled
    bytecode programs.

- [x] Add comprehensive regression tests for the language and runtime changes:
  - Add positive and negative tests for every new syntax and operation, with
    assertions for values, types, ownership states, diagnostics, and source
    locations.
  - Exercise interactions between transfers, borrows, mutable type changes,
    swaps, functions, lists, tuples, structs, modules, and enums rather than
    testing each feature only in isolation.
  - Run the same representative programs through source execution, optimized
    bytecode, unoptimized bytecode, imported modules, and bundled output.
  - Include tests for duplicate `func` declarations, use-after-move,
    conflicting borrows, invalid borrow termination, incompatible swaps,
    malformed sequence patterns, invalid enum construction, and stale
    bytecode.
  - Keep regression fixtures deterministic and suitable for environments with
    no audio device; audio tests must use controlled fixtures or explicitly
    verify a documented headless error path.

- [x] Add Rust-style enums with variable-like declarations and separate values
  and implementation code:
  - Introduce an enum declaration whose bracketed section describes the
    variants and their optional payload values, while the braced section
    contains enum-associated code. For example:
    ```
    enum result = [
        Ok(int value),
        Err(str message)
    ]{
        // enum-associated functions and behavior
    }
    ```
  - Treat enums as tagged, type-safe unions rather than plain integer
    constants. Each enum value must carry its enum identity, variant identity,
    and any declared payload values; construction and payload types must be
    checked at runtime.
  - Define constructor syntax, field names/positional payloads, equality,
    copying/moving, ownership interaction, display/conversion behavior, and
    whether variants may be empty, data-bearing, or recursive.
  - Allow `switch` patterns to match enum variants and bind their payloads,
    including nested enum/list/tuple patterns, with useful diagnostics for
    missing or incompatible cases.
  - Restrict enum-associated code to the documented scope, prevent duplicate
    variant names, and define how enum names and variants are exposed through
    modules without conflicting with `func` declarations or existing types.
  - Add documentation and regression fixtures for declaration, construction,
    payload access, matching, invalid payloads, duplicate variants, imports,
    bytecode, and bundling.

## Python → C/C++ toolchain migration

Two-stage plan: first rewrite the Python toolchain as clean, decoupled
Python modules; once that is done, rewrite the whole project
module-by-module to C/C++ instead of Python. Most of the ~18.5k lines
of `.lynx` stdlib carries over, but modules that are thin wrappers
around Python-hosted backends are not coming (see stdlib triage).
Already native: `cpp.cpp`
(memory/threads/FFI) and `bytecode_vm.cpp` (v9 AST decoder).
PyInstaller disappears in the final stage; the build moves to CMake.

Rationale: clean Python module boundaries make every later C++ port a
mechanical translation of one unit behind a fixed API, instead of
surgery on a monolith.

### Strategic decision (needed up front)

- [x] Decide the fate of Python-interop features (EmbedPy, raw `py`/`pyx`
  blocks, `exec` blocks, Cython inline, pyglet-backed games):
  - Selected **(b)**: drop them in the native build, keep the Python
    toolchain as `lynxer-py` for a transition period. Native replacements
    must report explicit unavailable-feature/module errors rather than
    silently falling back to Python. See `docs/migration/README.md`.

### Stdlib triage

The stdlib structure changes in the new versions: the current single
flat `.lynx` layout will not carry over as-is; CLynxer defines its own
stdlib layout (module set and directory structure are re-decided
there). The triage below decides which modules survive, not the final
structure.

Nearly every stdlib module currently reaches the Python host through
`embedPy` bridges (measured: all 32 modules contain embedPy calls), so
each module needs one of three outcomes before Stage 2 finishes.

- Not coming (decided):
  - [ ] Drop `tkinter.lynx` (1,242 lines) — pure Tk wrapper
  - [ ] Drop `tkinterPlus.lynx` (2,198 lines) — customtkinter wrapper
  - [ ] Native build raises a clear import-time error for dropped
    modules: "module X is not available in the native build" (no
    silent failure), with a pointer to the Python build for compat
- Decide per module (Python-hosted backends, tied to the interop
  decision):
  - [ ] `turtle.lynx` (648) — wraps Python's `turtle`/Tk; drop or
    native canvas reimplementation
  - [ ] `venv.lynx` (164) — wraps Python venv; drop or reimplement
  - [ ] `js.lynx` (133) / `lua.lynx` (129) — Python-hosted JS/Lua
    runtimes; drop or embed native interpreters
  - [ ] `game.lynx` (2,809) / `sound.lynx` (281) / `image.lynx`
    (1,423) — pyglet/audio backends; native replacement is the
    SDL/sfml path from the interop decision
- Coming (replace the embedPy bridge with a native backend, keep the
  `.lynx` API identical): `path`, `os`, `net`, `server`, `http`, `csv`,
  `json`, `math`, `mathPlus`, `text`, `regex`, `re`, `time`, `sys`,
  `cli`, `tui`, `fileIO`, `sqldb`, `random`, `colorlib`, `typing`,
  `debug`, `multiprocessing`, `shell` — many map directly onto the
  existing syscall layer and `cpp.cpp` primitives
- [ ] Update the golden corpus to exclude dropped modules and add
  fixtures asserting the import error for each
- [ ] Document dropped modules in the stdlib docs and the bundle
  failure diagnostics

### Stage 1 — Python module rewrite (no behavior change)

Goal: every Python module becomes a small, focused unit with a
documented public API, zero circular imports, and a dependency graph
that maps 1:1 onto future C++ translation units.

- [x] Golden-output corpus first: run every `test/*.lynx` fixture +
  stdlib through the current implementation, capture stdout/stderr/
  error text; `scripts/golden_corpus.py` provides the update/check command,
  and interactive fixtures are recorded as explicit skips.
- [x] Define the target module layout and strict dependency direction,
  e.g. error → lexer → ast → parser → bytecode (encode/decode) →
  values → interpreter → builtins → stdlib glue → CLI. See
  `docs/migration/README.md`.
- [ ] Break the existing circular imports (`lynxer.py` ↔ `runtime.py`
  try/except cycle; values/builtins/runtime entanglement)
- [ ] Split the monoliths into cohesive modules:
  - [ ] `builtins.py` (4,816) → per-category modules (strings, lists,
    dicts, io, ...)
  - [ ] `parser.py` (4,760) → expression / statement / declaration /
    pattern modules
  - [ ] `runtime.py` (3,299) → interpreter core, async, embed-py,
    native callbacks
  - [ ] `values.py` (2,914) → value hierarchy vs type registry vs
    Python conversions
- [ ] Stop cross-module use of private (`_underscore`) names; promote
  shared internals into a documented internal API module (warning and
  error helpers, shared parameters logic)
- [ ] Make runtime singletons explicit (class registry, warning state,
  native VM cache) — passed-in context objects, not module globals
- [ ] Keep the bytecode v9 encoder/decoder byte-identical (format
  unchanged during Stage 1)
- [ ] Gate: full test suite + golden corpus pass with identical
  output; no API change visible outside the package

### Stage 2 — C/C++ rewrite (after Stage 1)

**Location: Stage 2 is built in a separate directory, `CLynxer` —
not inside the Lynxer tree.** The Lynxer Python tree stays untouched
as the reference implementation; CLynxer grows the native one side
by side so both remain runnable and diffable.

Each Stage-1 module is one porting unit with a defined API. Port in
dependency order; keep both implementations runnable and diffable the
whole time.

- [ ] Port error / formatting / strings_with_arrows (exact message
  parity)
- [ ] Port lexer — gate: token-stream parity over the corpus
- [ ] Port the AST as a C++ node hierarchy
- [ ] Port parser — gate: structural AST parity
- [ ] Port the bytecode encoder; reuse the existing `bytecode_vm.cpp`
  decoder — gate: `.lynxc` output byte-identical
- [ ] Port the value module → refcounted C++ `Value` hierarchy
  (arbitrary-precision integers, Python-float `repr` parity,
  insertion-ordered dicts)
- [ ] Port the interpreter core; async → C++20 coroutines or explicit
  state machines; multiprocessing workers → threads/fork — gate:
  golden-identical output on all non-embedpy fixtures
- [ ] Port the builtins modules; re-target `cpp.cpp` to the new Value
  model
- [ ] Stdlib: keep the "coming" `.lynx` modules as-is, replace each of
  their `embedPy` bridges with a native backend (syscall layer /
  `cpp.cpp`), restructure the stdlib layout for CLynxer (the current
  flat structure does not carry over), and remove the dropped modules
  (tkinter, tkinterPlus/customtkinter) per the triage above
- [ ] Port syscalls → direct syscalls (harness:
  `scripts/testARM64Syscall.py`)
- [ ] Port the CLI (`shell.py` → `main()`); bundle/install shrink to
  static-binary logic (no PyInstaller, no bootstrap download); switch
  the build to CMake
- [ ] Python-interop features: `embedPy`, `rawPy` blocks and `rawPyx`
  blocks **do not exist in the new versions** — dropped outright, no
  libpython embedding, no native replacement, no silent fallback;
  native builds raise explicit unavailable-feature errors. Game
  fixtures (test37-40) stay blocked/dropped accordingly
- [ ] Flip the default to native; keep the Python implementation for
  one release cycle as the reference; then delete the Python
  toolchain and PyInstaller targets

### Top risks

1. `values.py` is the keystone — the whole runtime is typed against
   Python's object model; Stage 1 must isolate it behind a narrow API
   before the C++ hierarchy can be designed
2. Error-message and repr parity (exact strings, `strOf`, float
   formatting, arrows in error display)
3. Async/coroutine semantics (80+ touch points in runtime.py)
4. Bigint literals — easy to miss until a fixture fails
5. Game/interactive fixtures blocked on the interop decision

Roughly 60% of the port (lexer, parser, AST, encoder, syscalls, CLI)
is mechanical translation; the object model and interpreter are the
hard 40%. Stage 1 exists to move as much of the hard 40% as possible
into the mechanical column before C++ enters the picture.
