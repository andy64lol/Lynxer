# lowLevel (experimental)

`lowLevel` exposes a small set of unmanaged C memory operations through a
native C++ Python extension. It is intentionally kept outside the stable
stdlib because incorrect addresses or sizes can crash the Python process.

## Build the extension

The extension is optional and is not built automatically when Lynxer is
installed:

```bash
python lynxer/stdlib/experimental/setup.py build_ext --inplace
# or
make buildExperimental
```

This creates a platform-specific `c` extension beside
`lynxer/stdlib/experimental/lowLevel.lynx`. The Lynxer loader adds that
directory to Python's import path when `importPy("c")` runs.

The module can be listed with:

```bash
python lynxer/shell.py --list-stdlibs
```

## Import

```c
global setup(){
    import("lowLevel");
}
```

Functions are accessed through `global.lowLevel`.

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
    import("lowLevel");
}

global main(){
    int address = global.lowLevel.calloc(4, 1);
    global.lowLevel.writeByte(address, 0, 65);
    global.lowLevel.writeByte(address, 1, 66);
    println(global.lowLevel.readByte(address, 0));
    println(global.lowLevel.readByte(address, 1));
    global.lowLevel.free(address);
}
```

## Safety

Addresses are represented as Lynxer integers and memory is unmanaged. Always
free allocations exactly once, never read or write beyond the allocated
region, and do not use an address after `realloc` or `free`. These functions
perform no ownership tracking or bounds checking.