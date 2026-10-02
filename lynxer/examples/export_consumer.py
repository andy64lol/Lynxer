#!/usr/bin/env python3
"""ctypes consumer for the library built from export_basic.lynx.

Proves the emitted C ABI is callable from another language, not just C++.
Usage: export_consumer.py <path-to-emitted.so>
"""
import ctypes
import struct
import sys


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: export_consumer.py <library.so>", file=sys.stderr)
        return 2
    library = ctypes.CDLL(sys.argv[1])

    library.add.restype = ctypes.c_int64
    library.add.argtypes = [ctypes.c_int64, ctypes.c_int64]
    library.negate.restype = ctypes.c_int64
    library.negate.argtypes = [ctypes.c_int64]
    library.scale.restype = ctypes.c_double
    library.scale.argtypes = [ctypes.c_double]
    library.greet.restype = ctypes.c_char_p
    library.greet.argtypes = [ctypes.c_char_p]
    library.echoBytes.restype = ctypes.POINTER(ctypes.c_uint8)
    library.echoBytes.argtypes = [ctypes.POINTER(ctypes.c_uint8), ctypes.c_int64]
    library.sink.restype = None
    library.sink.argtypes = [ctypes.c_int64]

    failures = 0

    def check(ok: bool, name: str) -> None:
        nonlocal failures
        if not ok:
            failures += 1
            print(f"export consumer FAIL: {name}", file=sys.stderr)

    check(library.add(2, 3) == 5, "add")
    check(library.negate(7) == -7, "negate (int32 alias)")
    check(library.scale(1.5) == 3.0, "scale")
    check(library.greet(b"bob") == b"hi bob", "greet")

    payload = bytes([0x00, 0x01, 0xFF]) + b"hi"
    buffer = (ctypes.c_uint8 * len(payload)).from_buffer_copy(payload)
    framed = library.echoBytes(buffer, len(payload))
    length = struct.unpack("<q", bytes(framed[i] for i in range(8)))[0]
    echoed = bytes(framed[8:8 + length])
    check(length == len(payload) and echoed == payload, "echoBytes")

    library.sink(1)

    if failures == 0:
        print("export_consumer.py: ok")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
