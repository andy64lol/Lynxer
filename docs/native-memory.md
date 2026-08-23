# Native memory built-ins

Native memory is part of Lynxer’s built-in API, not a standard-library module.
The functions below are available directly in every program; they never need
`import()`. The implementation uses the optional C++ extension in
`lynxer/cpp.cpp`.

## Build the extension

```bash
make buildCpp
```

The extension build selects the appropriate C++17 and thread-linker flags for
the host compiler: MSVC on Windows, and POSIX-compatible compiler flags on
Unix-like systems.

The normal `make build` and `make buildLite` targets run this step
automatically before packaging Lynxer. A C++ compiler and the active Python
development headers are required. Native addresses are
represented as Lynxer integers and are unmanaged: callers own allocations and
must release them exactly once.

## Raw allocation

| Function | Description |
|----------|-------------|
| `memoryAllocate(size)` | Allocate `size` bytes and return its address |
| `memoryCallocate(count, size)` | Allocate zero-initialized memory |
| `memoryReallocate(address, size)` | Resize an allocation |
| `memoryFree(address)` | Release an allocation |
| `memorySet(address, value, size)` | Fill memory with a byte value |
| `memoryCopy(destination, source, size)` | Copy bytes between allocations |

Short C-style aliases (`malloc`, `calloc`, `realloc`, `free`, `memset`, and
`memcpy`) remain available for compatibility. New code should use the
camelCase names above.

## Native function calls

`nativeCall(address, signature, arguments)` invokes a native function pointer
using a deliberately small integer ABI:

```c
// address is supplied by a native integration or extension.
int result = nativeCall(address, "int32(int32,int32)", [int 2, int 3]);
```

Signatures use `returnType(parameterType,...)`. The return type may be `void`,
`int8`, `uint8`, `int16`, `uint16`, `int32`, `uint32`, `int64`, `uint64`, or
`uintptr`; parameters use the integer types above or `uintptr`. At most six
parameters are accepted, and arguments must be Lynxer integers compatible with
their declared parameter types.
The caller must provide a valid function address with the exact compatible ABI;
invalid addresses can crash the process.

The address may be a raw integer, an `Address` obtained with `getAddress()` when
its target contains an integer pointer, or an explicit typed function address:

```lynx
int raw = /* supplied by an extension */;
functionAddress fn = functionAddress(raw);
int result = nativeCall(fn, "int32(int32)", [int 7]);
```

`nativeFunctionAddress(address)` is an alias for `functionAddress(address)`.
The wrapper is useful for declarations and APIs that should reject ordinary
integers and data addresses. It does not validate that the pointer is
executable; the caller remains responsible for supplying a valid ABI.

## C/C++ FFI

The FFI helpers load shared libraries and expose symbols as function addresses:

```lynx
int libc = ffiLoadLibrary("libc.so.6");
functionAddress strlen = ffiLookup(libc, "strlen");
println(ffiCall(strlen, "cdecl:uintptr(cstring)", [str "hello"]));
ffiCloseLibrary(libc);
```

FFI signatures support `void`, all signed and unsigned integer widths,
`uintptr`, `float32`, `float64`, and `cstring`. Calls accept `cdecl` (the
default) and `stdcall` on Windows. All arguments must match the declared
signature. `ffiCallback(signature, function)` creates a native callback
address and keeps it alive until `ffiFreeCallback(callback)` is called.
Callbacks use the same types and calling conventions. Native code must not
retain a callback after it has been freed.

## Native threads

Native threads run a Lynxer function on a C++ `std::thread` while acquiring the
interpreter lock for each callback. Start a thread with a function and a list of
arguments, then join it:

```lynx
global worker(int value){ println(value); }
int thread = nativeThreadStart(global.worker, [int 42]);
nativeThreadJoin(thread);
```

`nativeThreadJoin(handle)` returns `completed` on success or the callback's
error message on failure. `nativeThreadIsAlive(handle)` reports whether the
thread is still running.
`nativeThreadStatus(handle)` returns the current status while the thread is
running.
`nativeThreadDetach(handle)` releases ownership so it cleans itself up when it
finishes. A thread handle must be joined or detached exactly once.

## Typed memory

`memoryTypeSize(type)` returns the byte size of a supported type:
`byte`, `int8`, `uint8`, `int16`, `uint16`, `int32`, `uint32`, `int64`,
`uint64`, `float32`, or `float64`.
`memoryTypeAlignment(type)` returns that type's native alignment in bytes.

For data formats whose byte order is part of the protocol, use the explicit
endian operations instead of relying on the host machine:

```c
int address = memoryAllocate(8);
memoryWriteEndian(address, 0, "uint32", "big", 305419896);
println(memoryReadEndian(address, 0, "uint32", "big"));
memoryFree(address);
```

`memoryReadEndian(address, offset, type, order)` and
`memoryWriteEndian(address, offset, type, order, value)` support every typed
memory type and accept `"little"`/`"le"` or `"big"`/`"be"`. The bytes in memory
are always written and interpreted according to the requested order.

| Function | Description |
|----------|-------------|
| `memoryBlockAllocate(type, count)` | Allocate a typed block |
| `memoryBlockView(address, type, count)` | View an existing allocation |
| `memoryBlockGet(address, index)` | Read an element |
| `memoryBlockSet(address, index, value)` | Write an element |
| `memoryBlockLength(address)` | Return the element count |

`memoryArrayAllocate`, `memoryArrayView`, `memoryArrayGet`,
`memoryArraySet`, and `memoryArrayLength` are aliases for the block API.
`memoryViewGet`, `memoryViewSet`, and `memoryViewLength` are also aliases.

Typed read/write functions are available for signed and unsigned 8/16/32/64
bit integers and `float32`/`float64`, for example
`memoryReadInt32(address, offset)` and
`memoryWriteInt32(address, offset, value)`. The shorter `readInt32` and
`writeInt32` forms are compatibility aliases.

`sizeOf(typeName)` returns the size of a native C type. This is the canonical
camelCase spelling; `sizeof` is no longer part of the built-in API.

## Native structs

A layout is a comma-separated list of numeric fields:

```c
global main(){
    str layout = "int32 id, float64 score";
    int address = nativeStructAllocate(layout);
    nativeStructSet(address, "id", 7);
    nativeStructSet(address, "score", 12.5);
    println(nativeStructGet(address, "id"));
    println(nativeStructFieldOffset(layout, "score"));
    println(nativeStructSize(layout));
    memoryFree(address);
}
```

The `nativeStruct*` API uses native alignment and byte order. The equivalent
`memoryStructSize`, `memoryStructFieldOffset`, `memoryStructFieldSize`,
`memoryStructAllocate`, `memoryStructGet`, and `memoryStructSet` names are
also available.

Layout introspection is available without allocating a struct:

| Function | Description |
|----------|-------------|
| `memoryStructAlignment(layout)` | Maximum native field alignment in bytes |
| `memoryStructFieldCount(layout)` | Number of fields |
| `memoryStructFieldType(layout, field)` | Declared type name of a field |

The same queries are available as `nativeStructAlignment`,
`nativeStructFieldCount`, and `nativeStructFieldType`; `nativeTypeAlignment`
is the alias for `memoryTypeAlignment`.

## Atomic and volatile access

Atomic operations use sequential consistency and support `int32`, `uint32`,
`int64`, and `uint64`:

```c
int address = memoryAllocate(8);
atomicStore(address, 0, "int64", 10);
println(atomicAdd(address, 0, "int64", 5));
println(atomicLoad(address, 0, "int64"));
memoryFree(address);
```

`volatileRead(address, offset, type)` and
`volatileWrite(address, offset, type, value)` provide compiler-volatile byte
access for supported native memory types. Volatile access is not atomic and
does not provide thread synchronization.

## Memory protection

`memoryProtect(address, size, mode)` changes page protection for an allocation.
Modes are `read`, `readwrite`, `execute`, and `none`. Protection is page based,
so the system may change surrounding bytes in the same pages. Unsupported
platforms return an explicit error.

## Owned native handles

For allocations that should not be passed around as unmanaged integers, use
`nativeHandleAllocate(size)`, `nativeHandleAddress(handle)`,
`nativeHandleIsAlive(handle)`, and `nativeHandleFree(handle)`. Copies of a
handle share ownership state, so freeing one copy marks every copy as freed
and later use is rejected.

## Safety

The extension does not provide automatic ownership, bounds tracking, or
garbage collection for raw pointers. Do not use an address after
`memoryFree` or after `memoryReallocate`, keep source allocations alive while
using views, and track allocation sizes and offsets in the caller.