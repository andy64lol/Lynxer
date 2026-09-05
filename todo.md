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
* `syscallOpenAt()`
* `syscallClose()`
* `syscallReadVector()`
* `syscallWriteVector()`
* `syscallSeekFile()`
* `syscallGetFileStatus()`
* `syscallGetFileStatusAt()`
* `syscallTruncateFile()`
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
* `syscallGetSystemInformation()`
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
- [x] Cover packing, padding, bit fields, signedness, and platform ABI differences with compiler-backed tests.

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

- [x] expose threads and synchronization cleanly to Lynxer:
- [x] add a high-level thread lifecycle and result/error propagation API
- [x] add mutexes, condition variables, semaphores, and safe ownership
- [x] define cancellation, shutdown, and deadlock-resistant cleanup rules.
- [x] test contention, wakeups, failures, and interpreter shutdown

## async I/O

- [x] Add epoll and event-driven APIs for serious servers:
- [x] Provide registration, modification, removal, and event waiting.
- [x] Integrate file, socket, timer, and wakeup events.
- [x] Define callback/task scheduling, cancellation, and backpressure.
- [x] Add high-concurrency and graceful-shutdown tests.

## Compiler improvements

- [x] Improve compiler performance and developer feedback:
- [x] Add safe optimization passes and benchmark representative programs.
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

- [ ] Add explicit ownership transfer and borrowing operations inspired by Rust:
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
  - Preserve ownership metadata through function parameters, return values,
    lists, tuples, structs, modules, and bytecode serialization. Runtime errors
    must identify the variable and the conflicting ownership operation.
  - Ensure failed operations do not partially update either variable or the
    ownership table.

- [ ] Add ownership-aware swapping operations:
  - Implement `varSwapAll(firstVar, secondVar)` to exchange both values and
    their complete runtime type/ownership metadata. The operation must work
    across compatible variable declarations and must define what happens when
    either variable is moved, borrowed, const, or otherwise unavailable.
  - Implement `varSwapVal(firstVar, secondVar)` to exchange only values while
    retaining each variable's declared/runtime type identity. Reject the swap
    when either value cannot be assigned to the other variable's type.
  - Define atomic failure behavior: type incompatibility, active-borrow conflicts,
    and invalid ownership states must leave both variables unchanged.
  - Add clear diagnostics and tests for primitive values, lists, tuples, structs,
    `any`/numeric types, moved values, and active borrows.

- [ ] Add type-changing ownership operations:
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

- [ ] Add ownership inspection helpers:
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

- [ ] Expand `switch` pattern matching to support lists and tuples:
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

- [ ] Add a standalone `sound` standard-library module:
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

- [ ] Update the bytecode format for the new language features:
  - Bump the bytecode format version and serialize the AST/runtime metadata
    required for `func` declarations, ownership states, borrow relationships,
    mutable type transitions, list/tuple switch patterns, enums, and the sound
    module's import metadata.
  - Update compiler output, bytecode loading, inspection tools, imports, cache
    invalidation, and bundled executables together so source and `.lynxc`
    programs have the same semantics.
  - Reject incompatible or incomplete bytecode with a clear recompile message;
    do not silently interpret new nodes using old semantics or discard ownership
    metadata.
  - Add compatibility tests for current-version files, intentionally stale
    versions, corrupted payloads, imported bytecode modules, and bundled
    bytecode programs.

- [ ] Add comprehensive regression tests for the language and runtime changes:
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

- [ ] Add Rust-style enums with variable-like declarations and separate values
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
