# Native memory built-ins

Lynxer exposes unmanaged native-memory operations as core built-ins. They use
the bundled C++ extension in `lynxer/cpp.cpp`.

## Build the extension

The extension is optional and is not built automatically when Lynxer is
installed:

```bash
python lynxer/setup.py build_ext --inplace
# or
make buildCpp
```

This creates a platform-specific `cpp` extension beside `lynxer/cpp.cpp`.

The module can be listed with:

```bash
python lynxer/shell.py --list-stdlibs
```

No import is required. Functions are available directly in every Lynxer
program.

## API

### Raw allocation

| Function | Description |
|----------|-------------|
| `malloc(size)` | Allocate `size` bytes and return the address as an integer |
| `calloc(count, size)` | Allocate zero-initialized memory |
| `realloc(address, size)` | Resize an allocation and return its new address |
| `free(address)` | Release an allocation |
| `memset(address, value, size)` | Fill `size` bytes with a byte value from 0 to 255 |
| `memcpy(destination, source, size)` | Copy `size` bytes between allocations |

### Typed reads and writes

| Function | Description |
|----------|-------------|
| `readByte(address, offset)` / `writeByte(address, offset, value)` | Read/write an unsigned byte |
| `readInt8` / `writeInt8` | Read/write signed 8-bit integers |
| `readInt16` / `writeInt16` | Read/write signed 16-bit integers |
| `readInt32` / `writeInt32` | Read/write signed 32-bit integers |
| `readInt64` / `writeInt64` | Read/write signed 64-bit integers |
| `readUInt8` / `writeUInt8` | Read/write unsigned 8-bit integers |
| `readUInt16` / `writeUInt16` | Read/write unsigned 16-bit integers |
| `readUInt32` / `writeUInt32` | Read/write unsigned 32-bit integers |
| `readUInt64` / `writeUInt64` | Read/write unsigned 64-bit integers |
| `readFloat32` / `writeFloat32` | Read/write a 32-bit IEEE floating-point value |
| `readFloat64` / `writeFloat64` | Read/write a 64-bit IEEE floating-point value |

Typed functions use native machine byte order and accept byte offsets. For
example, `writeInt32(address, 0, -123)` writes four bytes starting at offset
zero.

## Typed memory blocks and views

Typed memory blocks provide indexed access to native allocations. The type
must be one of `byte`, `int8`, `uint8`, `int16`, `uint16`, `int32`, `uint32`,
`int64`, `uint64`, `float32`, or `float64`.

| Function | Description |
|----------|-------------|
| `memoryTypeSize(type)` | Return the size in bytes of a supported Lynxer native memory type |
| `memoryBlockAllocate(type, count)` | Allocate a typed block containing `count` elements; returns its address |
| `memoryBlockView(address, type, count)` | Create a typed view over an existing native allocation; does not own the allocation |
| `memoryBlockGet(address, index)` | Read an element from a typed block/view |
| `memoryBlockSet(address, index, value)` | Write an element to a typed block/view |
| `memoryBlockLength(address)` | Return the element count of a typed block/view |

`memoryArrayAllocate`, `memoryArrayView`, `memoryArrayGet`,
`memoryArraySet`, and `memoryArrayLength` are aliases for the corresponding
`memoryBlock*` operations. `memoryViewGet`, `memoryViewSet`, and
`memoryViewLength` are aliases for the corresponding block operations as well.

A view does not own the underlying allocation. Keep the original allocation
alive while using the view, and free the allocation only after the view is no
longer used.

Example:

```c
global setup(){}

global main(){
    int address = malloc(4 * memoryTypeSize("int32"));
    int view = memoryBlockView(address, "int32", 4);

    memoryBlockSet(view, 0, 42);
    memoryBlockSet(view, 1, 99);
    println(memoryBlockGet(view, 0));
    println(memoryBlockLength(view));

    free(address);
}
```

## Native struct layouts

Lynxer can describe a native struct layout with a comma-separated string of
`type field` declarations:

```c
str layout = "int32 id, float64 x, float64 y, int16 flags";
```

Struct fields use native-style alignment. Each field is aligned to its natural
size (capped at 8 bytes), and the final structure size is rounded up to the
maximum alignment. This makes these layouts suitable for buffers exchanged
with native C/C++ code.

| Function | Description |
|----------|-------------|
| `memoryStructSize(layout)` | Return the native size of the layout in bytes |
| `memoryStructFieldOffset(layout, field)` | Return a field's byte offset |
| `memoryStructFieldSize(layout, field)` | Return a field's size in bytes |
| `memoryStructAllocate(layout)` | Allocate one native struct using the layout |
| `memoryStructGet(address, field)` | Read a numeric field from a native struct |
| `memoryStructSet(address, field, value)` | Write a numeric field in a native struct |

The `nativeStruct*` names are explicit FFI aliases for the same operations:
`nativeStructSize`, `nativeStructAllocate`, `nativeStructFieldOffset`,
`nativeStructFieldSize`, `nativeStructGet`, and `nativeStructSet`.

Example:

```c
global setup(){}

global main(){
    str layout = "int32 id, float64 x, float64 y";
    int address = nativeStructAllocate(layout);

    nativeStructSet(address, "id", 7);
    nativeStructSet(address, "x", 12);
    nativeStructSet(address, "y", 34);

    println(nativeStructGet(address, "id"));
    println(nativeStructFieldOffset(layout, "y"));
    println(nativeStructSize(layout));

    free(address);
}
```

Struct layouts currently contain numeric native types only. Field names must
be valid identifiers and must not be duplicated.

## Example

```c
global setup(){
}

global main(){
    int address = calloc(4, 1);
    writeByte(address, 0, 65);
    writeByte(address, 1, 66);
    println(readByte(address, 0));
    println(readByte(address, 1));
    free(address);
}
```

## Variable addresses vs native addresses

Native memory addresses are represented as Lynxer integers and are different
from the built-in `address` type. The built-in type is backed by a native C++
reference-cell pointer to a Lynxer variable:

```c
global setup(){}
global main(){
    int x = 42;
    address p = getAddress(x);
    modifyAddressValue(p, 100);
    println(getAddressValue(p)); // 100
}
```

`shared` aliases use the same native reference-cell pointer internally.
`unshare(name)` detaches the alias and gives it a new independent cell while
keeping its current value:

```c
global setup(){}
global main(){
    int x = 42;
    shared int y = x;
    address p = getAddress(y);
    modifyAddressValue(p, 100);
    println(x); // 100
    unshare(y);
    modifyAddressValue(p, 200);
    println(x); // 200
    println(y); // 100
}
```

## Safety

Memory is intentionally unmanaged. The C++ extension does not provide a
managed heap or automatic ownership for raw allocations. Therefore:

- the caller owns every successful `malloc`, `calloc`, `realloc`, and struct/block allocation and must release owned memory appropriately;
- using a pointer after `free`, or using an old pointer after `realloc`, is invalid;
- a `memoryBlockView` does not own its source allocation;
- callers must keep allocation sizes and offsets themselves;
- native memory operations can fail with runtime errors for invalid typed
  blocks, layouts, fields, or values, but raw pointer misuse can still crash
the process.

Use these functions only when native-memory behavior is required.
