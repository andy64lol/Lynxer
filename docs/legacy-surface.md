# Original Lynxer surface: status

The original interpreter exposed a larger built-in and module surface than the
standalone C++ runtime. This page catalogues it so programs and notes written
against the original have somewhere to look, and records what has since been
brought across. [limitations.md](limitations.md) remains the normative register.

Three statuses are used:

- **Implemented** — the name works today; the notes point at the current
  built-ins.
- **Not implemented** — the name is recognised and fails with
  `<name>() is not supported in Lynxer yet`.
- **Not planned** — a deliberate, permanent non-goal.

## Pointers and raw addresses

The original had C-style address and function-pointer built-ins.

| Name | Status | Notes |
| --- | --- | --- |
| `getAddress(value)` | Implemented | Validates an integer as a live native allocation |
| `getAddressValue(address)` | Implemented | Reads an `int64` from the address |
| `modifyAddressValue(address, value)` | Implemented | Writes an `int64` to the address |
| `functionAddress` / `nativeFunctionAddress` | Implemented | Typed non-zero function address (`functionAddress` is a declared type) |
| `nativeCall(address, signature, arguments)` | Implemented | Calls a function pointer through the small integer ABI |
| `nativeHandleAllocate/Address/Free/IsAlive` | Implemented | Owned native handles (see below) |

Addresses are plain integers, as the original documented, and the raw forms
are implemented. `nativeCall` restricts signatures to the shapes the runtime
supports and cannot make an arbitrary ABI safe — an invalid address can still
crash the process. Prefer the structured built-ins when they fit — typed blocks
(`memoryBlockAllocate`/`memoryBlockGet`/`memoryBlockSet`), native structs
(`nativeStructAllocate`/`nativeStructGet`/`nativeStructSet` over a layout
string), and owned handles (`nativeHandleAllocate`/`nativeHandleAddress`/
`nativeHandleFree`); see [builtins.md](builtins.md#native-memory).

## Advanced memory layout

The original could model typed blocks and C structs directly in the allocator.
All of it is implemented, including page protection and atomic/volatile access.

| Family | Status | Notes |
| --- | --- | --- |
| `memoryBlockAllocate/View/Get/Set/Length` | Implemented | Typed blocks: an element type and count |
| `memoryArrayAllocate/View/Get/Set/Length`, `memoryViewGet/Set/Length` | Implemented | Aliases of the block API |
| `memoryStructSize/FieldOffset/FieldSize/Alignment/FieldCount/FieldType/Allocate/Get/Set` | Implemented | Layout-string struct helpers |
| `nativeStruct*`, `nativeTypeAlignment` | Implemented | Aliases of the `memoryStruct*` / `memoryTypeAlignment` names |
| `memoryProtect` | Implemented | Change page protection (POSIX, page-granular) |

A layout is a comma-separated `type name` list, e.g. `"int32 id, float64
score"`; see [builtins.md](builtins.md#native-memory).

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

The `async*` family is **implemented**: `asyncRun`, `asyncGather`, `asyncSleep`,
the `asyncPoll*` set, and the timer/wakeup built-ins. `await` in the caller
yields cooperatively. There is no `async` language support — the family is
driven through explicit handles. See [builtins.md](builtins.md#async).

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
| `getAddress` / `getAddressValue` / `modifyAddressValue` | Implemented (addresses are integers) |
| `nativeFunctionAddress` / `nativeCall` | Implemented (`nativeCall` over the small integer ABI) |
| `nativeHandle*` | Implemented under the same names |
| `nativeMutex*` / `Condition*` / `Semaphore*` | `nativeThread*` (cooperative) |
| `async*` | Implemented (cooperative handles over `sleep`) |
| `rawPy` / `embedPy` | None (no CPython) |
| `nativeModule*` handles | `importAs("<name>.so", …)` |
| `tkinter` / `turtle` / `venv` | None |

## See also

- [limitations.md](limitations.md) — the normative constraints and non-goals.
- [builtins.md](builtins.md) — every built-in the interpreter does implement.
- [native-module-abi.md](native-module-abi.md) — calling native code.
