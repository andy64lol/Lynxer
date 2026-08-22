# Native memory built-ins

Native memory is part of Lynxer’s built-in API, not a standard-library module.
The functions below are available directly in every program; they never need
`import()`. The implementation uses the optional C++ extension in
`lynxer/cpp.cpp`.

## Build the extension

```bash
make buildCpp
```

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

## Safety

The extension does not provide automatic ownership, bounds tracking, or
garbage collection for raw pointers. Do not use an address after
`memoryFree` or after `memoryReallocate`, keep source allocations alive while
using views, and track allocation sizes and offsets in the caller.