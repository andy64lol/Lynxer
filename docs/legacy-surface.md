# Original Lynxer surface not carried over

The original interpreter exposed a larger built-in and module surface than the
standalone C++ runtime implements — much of it existed to bridge into CPython,
raw C memory, or a pre-emptive threading model. This page catalogues that
surface so programs and notes written against the original have somewhere to
look. [limitations.md](limitations.md) remains the normative register; this page
adds the *what did it do* and *what replaces it* detail.

Two statuses are used:

- **Not implemented** — the name is recognised and fails with
  `<name>() is not supported in Lynxer yet`. Some of these could be built later.
- **Not planned** — a deliberate, permanent non-goal.

## Pointers and raw addresses

The original had C-style address and function-pointer built-ins.

| Name | Status | Notes |
| --- | --- | --- |
| `getAddress(variable)` | Not implemented | Address of a variable's storage |
| `getAddressValue(address)` | Not implemented | Read through a raw address |
| `modifyAddressValue(address, value)` | Not implemented | Write through a raw address |
| `functionAddress(name)` | Not implemented | Address of a Lynxer function |
| `nativeFunctionAddress(name)` | Not implemented | Address of a native function |
| `nativeCall(address, args...)` | Not implemented | Call an address directly |
| `nativeHandleAllocate/Address/Free/IsAlive` | Not planned | Owned native handles |

**Why:** exposing raw addresses across the native ABI is the largest possible
security surface, and it conflicts with the interpreter's value-ownership model
(see [ownership and borrowing](language.md#ownership-and-borrowing)). **Use
instead:** typed byte buffers via the [`memory*` built-ins](builtins.md#native-memory)
(`memoryAllocate`, `memoryReadInt64`, `memoryWriteFloat64`, `memoryReadEndian`,
`sizeOf`, …) for storage you control, the [native-module ABI](native-module-abi.md)
to call native code, and the [`ffi*` family](builtins.md#ffi) for dynamic calls.

## Advanced memory layout

The original could model C structs and arrays directly in the allocator.

| Family | Status | Notes |
| --- | --- | --- |
| `memoryProtect` | Not implemented | Change page protection |
| `memoryBlockAllocate`, `memoryBlockView/Get/Set/Length` | Not implemented | Untyped blocks |
| `memoryArrayAllocate`, `memoryArrayView/Get/Set/Length` | Not implemented | Indexed arrays |
| `memoryViewGet/Set/Length` | Not implemented | Views into a block |
| `memoryStructSize/FieldOffset/FieldSize/Alignment/FieldCount/FieldType/Allocate/Get/Set` | Not implemented | C-struct layout helpers |
| `nativeStruct*`, `nativeTypeAlignment` | Not implemented | Native-layout mirrors |

**Use instead:** the flat [`memory*`](builtins.md#native-memory) accessors with
explicit offsets, plus `sizeOf(type)` / `memoryTypeSize(type)` /
`memoryTypeAlignment(type)` for layout queries. A native module is the right
tool when you need real struct layout.

## Native synchronization

| Family | Status | Notes |
| --- | --- | --- |
| `nativeMutexCreate/Lock/TryLock/Unlock/Close` | Not planned | No pre-emptive threads to protect |
| `nativeConditionCreate/Wait/Notify/NotifyAll/Close` | Not planned | — |
| `nativeSemaphoreCreate/Wait/TryWait/Post/Close` | Not planned | — |

**Why:** Lynxer evaluates on one interpreter thread under a single lock, so there
is no shared mutable state to guard. The cooperative
[`nativeThread*`](builtins.md#native-threads) family is the supported model.

## Async

| Family | Status | Notes |
| --- | --- | --- |
| `asyncRun`, `asyncGather`, `asyncSleep` | Not planned | No event loop |
| `asyncPollCreate/Register/Modify/Remove/Wait/Dispatch/Close` | Not planned | — |
| `asyncTimerCreate/Cancel`, `asyncWakeupCreate/Signal/Close` | Not planned | — |

**Why:** the language has no `async` support, so there is nothing for a runtime
to schedule. **Use instead:** `sleep(seconds)` and `foreverDelay(seconds)` for
blocking waits; `nativeThread*` for concurrent work.

## Python bridging

| Name | Status | Notes |
| --- | --- | --- |
| `rawPy`, `rawPyx`, `cleanRawPyxCache`, `embedPy` | Not planned | Embedded CPython/Cython |

**Why:** Lynxer does not ship or link a Python runtime. Anything that depended on
`rawPy { }` — including the original `tkinter`/`turtle` modules and tuple
interop — has no direct equivalent.

## FFI and native-module handles

| Family | Status | Notes |
| --- | --- | --- |
| `nativeModuleLoad/Name/Function/Constant/Type/Error/Dependencies/Close` | Not planned | Superseded by the import ABI |
| `ffiLoadLibrary`, `ffiLookup`, `ffiCloseLibrary`, `ffiCall`, `ffiCallback`, `ffiFreeCallback` | Implemented | In C++ (`lynxer/builtins.cpp`) |

Native code is reached through the documented
[native-module ABI](native-module-abi.md): an `.so` exporting
`lynxer_module_init_v1` is imported by name (`importAs("libx.so", "x")`), so the
`nativeModule*` handle built-ins are unnecessary. The `ffi*` family is the
dynamic alternative.

## Modules not ported

| Module | Status | Replacement |
| --- | --- | --- |
| `venv` | Not planned | None — a virtual environment is a CPython concept |
| `tkinter`, `tkinterPlus`, `turtle` | Not planned | None; a future GUI would be Rust-backed (see `todo.md`) |
| `http`, `net` | Superseded | [`network`](stdlib/network.md) + [`server`](stdlib/server.md) |
| `mathPlus` | Merged | [`math`](stdlib/math.md) (the float `sign` is `signFloat`) |

## Toolchain surface removed

| Feature | Status | Replacement |
| --- | --- | --- |
| Bytecode (`.lynxc`, `--view-bytecode`, `--benchmark-compile`, `--no-cache`) | Removed | `--compile` produces a standalone ELF executable |
| Python `migration/` guide | Removed | The language reference in [language.md](language.md) |
| Python-only build paths (`clynxer/`, PyInstaller targets) | Removed | `make buildLynxer` and the root `Makefile` |

## Quick map: original → now

| Original | Now |
| --- | --- |
| `getAddress` / `getAddressValue` / `modifyAddressValue` | `memoryAllocate` + `memoryRead*` / `memoryWrite*` |
| `nativeFunctionAddress` / `nativeCall` | The [native-module ABI](native-module-abi.md) or `ffiCall` |
| `memoryStruct*` / `nativeStruct*` | Flat `memory*` offsets + `sizeOf` / `memoryTypeSize` |
| `nativeMutex*` / `Condition*` / `Semaphore*` | `nativeThread*` (cooperative) |
| `async*` | `sleep`, `foreverDelay`, `nativeThread*` |
| `rawPy` / `embedPy` | None (no CPython) |
| `nativeModule*` handles | `importAs("<name>.so", …)` |
| `tkinter` / `turtle` / `venv` | None |

## See also

- [limitations.md](limitations.md) — the normative constraints and non-goals.
- [builtins.md](builtins.md) — every built-in the interpreter does implement.
- [native-module-abi.md](native-module-abi.md) — calling native code.
