# Native memory

Typed access to raw memory from `.lynx` programs, with no import required —
these are builtins, not a module. Every `*` address is an integer handle; the
interpreter owns the allocation table.

## Raw allocation

| Function | Notes |
| --- | --- |
| `memoryAllocate(bytes)` | Allocates `bytes` and returns an address handle. |
| `memoryAllocateZeroed(bytes)` | As above, zero-initialised. |
| `memoryReallocate(address, bytes)` | Resizes an allocation. |
| `memoryFree(address)` | Releases an allocation. |
| `memorySet(address, byte, count)` | Fills `count` bytes. |
| `memoryCopy(source, target, count)` | Copies `count` bytes. |

```lynx
global setup(){}

global main(){
    int buffer = memoryAllocate(16);
    memoryWriteInt32(buffer, 0, 1234);
    println(memoryReadInt32(buffer, 0));   // 1234
    memoryFree(buffer);
}
```

## Typed accessors

`memoryRead<Type>(address, offset)` and `memoryWrite<Type>(address, offset, value)`
exist for `Byte`, `Int8`, `UInt8`, `Int16`, `UInt16`, `Int32`, `UInt32`,
`Int64`, `UInt64`, `Float32` and `Float64`. `memoryReadEndian` /
`memoryWriteEndian` take an explicit byte order, and `memoryTypeSize(type)` /
`memoryTypeAlignment(type)` / `sizeOf(type)` report layout facts. The builtin is
`sizeOf`, **not** `sizeof`.

## Typed blocks, arrays and views

| Family | Functions |
| --- | --- |
| Blocks | `memoryBlockAllocate`, `memoryBlockView`, `memoryBlockGet`, `memoryBlockSet`, `memoryBlockLength` |
| Arrays | `memoryArrayAllocate`, `memoryArrayView`, `memoryArrayGet`, `memoryArraySet`, `memoryArrayLength` |
| Views | `memoryViewGet`, `memoryViewSet`, `memoryViewLength` |

`memoryArray*` are aliases of the block operations.

## Native structs over a layout string

A layout is a comma-separated list of `"<type> <name>"` fields, e.g.
`"int32 x, int32 y"`. Both `memoryStruct*` and `nativeStruct*` families provide
`Size`, `Alignment`, `FieldCount`, `FieldOffset`, `FieldSize`, `FieldType`,
`Allocate`, `Get` and `Set`; `nativeTypeAlignment(type)` returns a type's
alignment.

Fields use **natural alignment**, and only scalar field types are understood:
`byte`, `int8`, `uint8`, `int16`, `uint16`, `int32`, `uint32`, `int64`,
`uint64`, `float32`, `float64`.

```lynx
println(memoryStructSize("int32 x, int32 y"));            // 8
println(memoryStructFieldOffset("int32 x, int32 y", "y")); // 4
```

Limits, all verified against the current build:

- A `pointer` / `uintptr` / `functionPointer` field is rejected.
- The optional packed-alignment argument is **ignored**:
  `memoryStructSize("int32 x, int32 y", 1)` still returns `8`.
- Fixed-size array syntax is not understood: `"int32 a[2]"` is parsed as a
  single `int32` field literally named `a[2]`, not as an array.
- Nested structs/unions and bit-fields are not supported.

## Owned handles

`nativeHandleAllocate(bytes)`, `nativeHandleAddress(handle)`,
`nativeHandleIsAlive(handle)` and `nativeHandleFree(handle)` give an owned,
liveness-checked block: `nativeHandleIsAlive` is true after allocation and false
after free. `memoryProtect(address, length, protection)` changes page protection
(POSIX, page-granular).

## Related

- Raw function addresses and `nativeCall` live in [native-modules.md](native-modules.md)
  and [builtins.md](builtins.md); `nativeCall` uses a fixed-shape table with at
  most four typed parameters (or the packed `ret(...)` form), not `libffi`.
- There is no separate C++ extension to build — native memory is part of
  `lynxer/builtins.cpp` (there is no `lynxer/cpp.cpp` and no `make buildCpp`).
