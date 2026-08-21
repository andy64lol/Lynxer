# Native memory built-ins

Native memory is part of Lynxer’s built-in API, not a standard-library module.
The functions below are available directly in every program; they never need
`import()`. The implementation uses the optional C++ extension in
`lynxer/cpp.cpp`.

## Build the extension

```bash
python lynxer/setup.py build_ext --inplace
# or
make buildCpp
```

The extension must be built before using native memory. Native addresses are
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

## Typed memory

`memoryTypeSize(type)` returns the byte size of a supported type:
`byte`, `int8`, `uint8`, `int16`, `uint16`, `int32`, `uint32`, `int64`,
`uint64`, `float32`, or `float64`.

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

## Safety

The extension does not provide automatic ownership, bounds tracking, or
garbage collection for raw pointers. Do not use an address after
`memoryFree` or after `memoryReallocate`, keep source allocations alive while
using views, and track allocation sizes and offsets in the caller.