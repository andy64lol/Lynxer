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
