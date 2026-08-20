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

| Function | Description |
|----------|-------------|
| `malloc(size)` | Allocate `size` bytes and return the address as an integer |
| `calloc(count, size)` | Allocate zero-initialized memory |
| `realloc(address, size)` | Resize an allocation and return its new address |
| `free(address)` | Release an allocation |
| `memset(address, value, size)` | Fill `size` bytes with a byte value from 0 to 255 |
| `memcpy(destination, source, size)` | Copy `size` bytes between allocations |
| `readByte(address, offset)` | Read one byte at an offset |
| `writeByte(address, offset, value)` | Write one byte at an offset; value must be 0–255 |
| `readInt8` / `writeInt8` | Read or write signed 8-bit integers |
| `readInt16` / `writeInt16` | Read or write signed 16-bit integers |
| `readInt32` / `writeInt32` | Read or write signed 32-bit integers |
| `readInt64` / `writeInt64` | Read or write signed 64-bit integers |
| `readUInt8` / `writeUInt8` | Read or write unsigned 8-bit integers |
| `readUInt16` / `writeUInt16` | Read or write unsigned 16-bit integers |
| `readUInt32` / `writeUInt32` | Read or write unsigned 32-bit integers |
| `readUInt64` / `writeUInt64` | Read or write unsigned 64-bit integers |
| `readFloat32` / `writeFloat32` | Read or write a 32-bit IEEE floating-point value |
| `readFloat64` / `writeFloat64` | Read or write a 64-bit IEEE floating-point value |
| `sizeof(typeName)` | Return the platform size of supported C types |

Supported `sizeof` names include `char`, `short`, `int`, `long`, `long long`,
`float`, `double`, `void*`, `size_t`, and `uintptr_t`.
It also accepts `int8`, `int16`, `int32`, `int64`, `uint8`, `uint16`,
`uint32`, and `uint64`. `unit32` is accepted as a compatibility alias for
`uint32`. `float32` and `float64` are also accepted.

Typed functions use native machine byte order and accept byte offsets. For
example, `writeInt32(address, 0, -123)` writes four bytes starting at offset
zero.

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

## Safety

Native memory addresses are represented as Lynxer integers and are different
from the built-in `address` type. The built-in type points to a Lynxer
variable:

```c
global setup(){}
global main(){
    int x = 42;
    address p = getAddress(x);
    modifyAddressValue(p, 100);
    println(getAddressValue(p)); // 100
}
```

Memory is intentionally unmanaged. The C++ extension does not track
allocation ownership, allocation sizes, or pointer lifetimes. Therefore:

- the caller owns every successful allocation and must call `free` exactly once;
- using a pointer after `free`, or using the old pointer after `realloc`, is
  invalid;
- reads and writes are not bounds-checked by Lynxer;
- callers must keep allocation sizes and offsets themselves.

This design keeps the built-ins a thin native-memory interface rather than
introducing a second managed heap. Invalid pointers and out-of-range offsets
can crash the process, so use these functions only when native-memory behavior
is required.