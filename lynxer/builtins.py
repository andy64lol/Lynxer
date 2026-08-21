"""Lynxer built-in functions, implementations, and runtime registry.

The interpreter value types live in :mod:`lynxer.lynxer`.  This module is
imported after those types have been defined, so it can own the complete
implementation of every built-in without making the runtime import cycle
fragile.
"""

from __future__ import annotations

import importlib
import sys
from collections.abc import Callable
from typing import Any

_runtime = importlib.import_module(".lynxer", package=__package__)
_MEMORY_LIB = importlib.import_module(".cpp", package=__package__)


BaseFunction = _runtime.BaseFunction
CoroutineValue = _runtime.CoroutineValue
List = _runtime.List
LynxTuple = _runtime.LynxTuple
VarGroup = _runtime.VarGroup
Sentinel = _runtime.Sentinel
ObjectValue = _runtime.ObjectValue
Number = _runtime.Number
Address = _runtime.Address
RTError = _runtime.RTError
RTResult = _runtime.RTResult
String = _runtime.String
type_matches = _runtime.type_matches
value_type_name = _runtime.value_type_name
_get_cython_inline = _runtime._get_cython_inline

_MEMORY_TYPES = {
    "byte": (1, _MEMORY_LIB.readByte, _MEMORY_LIB.writeByte, 0, 255),
    "int8": (1, _MEMORY_LIB.readInt8, _MEMORY_LIB.writeInt8, -(2**7), 2**7 - 1),
    "uint8": (1, _MEMORY_LIB.readUInt8, _MEMORY_LIB.writeUInt8, 0, 2**8 - 1),
    "int16": (2, _MEMORY_LIB.readInt16, _MEMORY_LIB.writeInt16, -(2**15), 2**15 - 1),
    "uint16": (2, _MEMORY_LIB.readUInt16, _MEMORY_LIB.writeUInt16, 0, 2**16 - 1),
    "int32": (4, _MEMORY_LIB.readInt32, _MEMORY_LIB.writeInt32, -(2**31), 2**31 - 1),
    "uint32": (4, _MEMORY_LIB.readUInt32, _MEMORY_LIB.writeUInt32, 0, 2**32 - 1),
    "int64": (8, _MEMORY_LIB.readInt64, _MEMORY_LIB.writeInt64, -(2**63), 2**63 - 1),
    "uint64": (8, _MEMORY_LIB.readUInt64, _MEMORY_LIB.writeUInt64, 0, 2**64 - 1),
    "float32": (4, _MEMORY_LIB.readFloat32, _MEMORY_LIB.writeFloat32, None, None),
    "float64": (8, _MEMORY_LIB.readFloat64, _MEMORY_LIB.writeFloat64, None, None),
}


def _memory_type(value):
    return value.value.lower() if isinstance(value, String) else None


def _native_int(value):
    return (
        isinstance(value, Number)
        and not value.is_bool
        and isinstance(value.value, int)
    )


def _native_nonnegative(value):
    return _native_int(value) and value.value >= 0


def _json_value(value):
    """Convert a Lynxer value into a JSON-compatible Python value."""
    if isinstance(value, Number):
        return bool(value.value) if value.is_bool else value.value
    if isinstance(value, String):
        return value.value
    if isinstance(value, _runtime.Char):
        return value.value
    if isinstance(value, _runtime.Null):
        return None
    if isinstance(value, List):
        return [_json_value(element) for element in value.elements]
    if isinstance(value, LynxTuple):
        return [_json_value(element) for element in value.elements]
    if isinstance(value, VarGroup):
        return {
            name: _json_value(info["value"])
            for name, info in value._fields.items()
        }
    if isinstance(value, (Sentinel, ObjectValue)):
        return str(value)
    return str(value)


class BuiltInFunction(BaseFunction):
    """A callable implemented by Python and exposed to Lynxer programs."""

    def execute(self, args):
        res = RTResult()
        exec_ctx = self.generate_new_context()

        method_name = f"execute_{self.name}"
        method = getattr(self, method_name, self.no_visit_method)
        return_value = res.register(method(args, exec_ctx))

        if res.should_return():
            return res
        return res.success(return_value)

    def no_visit_method(self, node, context):
        raise Exception(f"No execute_{self.name} method defined")

    def _failure(self, exec_ctx, message):
        return RTResult().failure(
            RTError(self.pos_start, self.pos_end, message, exec_ctx)
        )

    def _cpp(self, method, values, exec_ctx):
        """Call a C++ memory primitive and translate its exception to Lynxer."""
        try:
            return method(*values)
        except (RuntimeError, ValueError, OverflowError, MemoryError) as exc:
            return self._failure(exec_ctx, str(exc))

    def copy(self):
        c = BuiltInFunction(self.name)
        c.set_context(self.context)
        c.set_pos(self.pos_start, self.pos_end)
        return c

    def __repr__(self):
        return f"<built-in {self.name}>"

    def execute_print(self, args, exec_ctx):
        output = "".join(str(a) for a in args)
        sys.stdout.write(output)
        sys.stdout.flush()
        return RTResult().success(Number.null)

    def execute_println(self, args, exec_ctx):
        output = "".join(str(a) for a in args)
        sys.stdout.write(output + "\n")
        sys.stdout.flush()
        return RTResult().success(Number.null)

    def execute_unshare(self, args, exec_ctx):
        if len(args) != 1:
            return self._failure(exec_ctx, "unshare() expects exactly one variable")
        # The AST-level variable name is attached by the interpreter before
        # calling this built-in; values alone are intentionally not enough to
        # identify an alias.
        name = getattr(args[0], "_lynxer_name", None)
        if not isinstance(name, str) or not exec_ctx.symbol_table.unshare(name):
            return self._failure(exec_ctx, "unshare() expects a shared variable name")
        return RTResult().success(Number.null)

    def execute_getAddress(self, args, exec_ctx):
        """Return an address pointing at a variable argument."""
        if len(args) != 1:
            return self._failure(exec_ctx, "getAddress() expects exactly one variable")
        reference = getattr(args[0], "_lynxer_ref", None)
        if reference is None:
            return self._failure(
                exec_ctx,
                "getAddress() expects a variable name, not a computed value",
            )
        table, name = reference
        pointer = table.get_reference(name)
        if pointer is None:
            return self._failure(exec_ctx, "getAddress() points to an undefined variable")
        address = Address(pointer, table, name)
        address.set_context(exec_ctx)
        return RTResult().success(address)

    def execute_getAddressValue(self, args, exec_ctx):
        """Read the value currently stored at an address."""
        if len(args) != 1 or not isinstance(args[0], Address):
            return self._failure(
                exec_ctx,
                "getAddressValue() expects exactly one address",
            )
        value = args[0].get_value()
        if value is None:
            return self._failure(exec_ctx, "getAddressValue() points to an undefined variable")
        return RTResult().success(value)

    def execute_modifyAddressValue(self, args, exec_ctx):
        """Write a value through an address while enforcing its target type."""
        if len(args) != 2 or not isinstance(args[0], Address):
            return self._failure(
                exec_ctx,
                "modifyAddressValue() expects an address and a value",
            )
        address = args[0]
        table, name = address._target()
        if table is None:
            return self._failure(exec_ctx, "modifyAddressValue() points to an undefined variable")
        if table.is_const(name):
            return self._failure(
                exec_ctx,
                f"Cannot modify constant '{name}' through an address",
            )
        declared_type = table.types.get(name)
        if not type_matches(declared_type, args[1]):
            return self._failure(
                exec_ctx,
                f"Cannot store '{value_type_name(args[1])}' in address to "
                f"'{declared_type}' variable '{name}'",
            )
        if not address.set_value(args[1]):
            return self._failure(exec_ctx, "modifyAddressValue() could not update its target")
        return RTResult().success(Number.null)

    def execute_memoryTypeSize(self, args, exec_ctx):
        if len(args) != 1 or _memory_type(args[0]) not in _MEMORY_TYPES:
            return self._failure(exec_ctx, "memoryTypeSize(type) expects a supported memory type")
        result = self._cpp(_MEMORY_LIB.memoryTypeSize, [_memory_type(args[0])], exec_ctx)
        return result if isinstance(result, RTResult) else RTResult().success(Number(result))

    def execute_memoryBlockAllocate(self, args, exec_ctx):
        if (
            len(args) != 2
            or _memory_type(args[0]) not in _MEMORY_TYPES
            or not _native_nonnegative(args[1])
        ):
            return self._failure(
                exec_ctx,
                "memoryBlockAllocate(type, count) expects a supported type and non-negative count",
            )
        type_name = _memory_type(args[0])
        assert type_name is not None
        count = args[1].value
        size = _MEMORY_TYPES[type_name][0] * count
        result = self._cpp(_MEMORY_LIB.memoryBlockAllocate, [type_name, count], exec_ctx)
        return result if isinstance(result, RTResult) else RTResult().success(Number(result))

    def execute_memoryBlockView(self, args, exec_ctx):
        """Describe an existing native allocation as a typed array view.

        Views deliberately do not own the allocation.  The caller must keep
        the source allocation alive and free it only after the view is gone.
        """
        if (
            len(args) != 3
            or not _native_int(args[0]) or args[0].value < 0
            or _memory_type(args[1]) not in _MEMORY_TYPES
            or not _native_nonnegative(args[2])
        ):
            return self._failure(
                exec_ctx,
                "memoryBlockView(address, type, count) expects an address, "
                "supported type, and non-negative count",
            )
        address, type_name, count = args[0].value, _memory_type(args[1]), args[2].value
        error = self._check_memory_address(address, exec_ctx)
        if error:
            return error
        result = self._cpp(_MEMORY_LIB.memoryBlockView, [address, type_name, count], exec_ctx)
        return result if isinstance(result, RTResult) else RTResult().success(Number(result))

    def execute_memoryArrayAllocate(self, args, exec_ctx):
        return self.execute_memoryBlockAllocate(args, exec_ctx)

    def execute_memoryArrayView(self, args, exec_ctx):
        return self.execute_memoryBlockView(args, exec_ctx)

    def execute_memoryArrayGet(self, args, exec_ctx):
        return self.execute_memoryBlockGet(args, exec_ctx)

    def execute_memoryArraySet(self, args, exec_ctx):
        return self.execute_memoryBlockSet(args, exec_ctx)

    def execute_memoryArrayLength(self, args, exec_ctx):
        return self.execute_memoryBlockLength(args, exec_ctx)

    def execute_memoryViewGet(self, args, exec_ctx):
        return self.execute_memoryBlockGet(args, exec_ctx)

    def execute_memoryViewSet(self, args, exec_ctx):
        return self.execute_memoryBlockSet(args, exec_ctx)

    def execute_memoryViewLength(self, args, exec_ctx):
        return self.execute_memoryBlockLength(args, exec_ctx)

    def execute_memoryBlockGet(self, args, exec_ctx):
        if len(args) != 2 or not _native_nonnegative(args[0]) or not _native_nonnegative(args[1]):
            return self._failure(exec_ctx, "memoryBlockGet(address, index) expects non-negative integers")
        index = args[1].value
        result = self._cpp(_MEMORY_LIB.memoryBlockGet, [args[0].value, index], exec_ctx)
        return result if isinstance(result, RTResult) else RTResult().success(Number(result))

    def execute_memoryBlockSet(self, args, exec_ctx):
        if (
            len(args) != 3
            or not _native_nonnegative(args[0])
            or not _native_nonnegative(args[1])
            or not isinstance(args[2], Number)
            or args[2].is_bool
        ):
            return self._failure(exec_ctx, "memoryBlockSet(address, index, value) expects an address, index, and number")
        index = args[1].value
        value = args[2].value
        result = self._cpp(_MEMORY_LIB.memoryBlockSet, [args[0].value, index, value], exec_ctx)
        return result if isinstance(result, RTResult) else RTResult().success(Number.null)

    def execute_memoryBlockLength(self, args, exec_ctx):
        if len(args) != 1 or not _native_nonnegative(args[0]):
            return self._failure(exec_ctx, "memoryBlockLength(address) expects a non-negative integer address")
        result = self._cpp(_MEMORY_LIB.memoryBlockLength, [args[0].value], exec_ctx)
        return result if isinstance(result, RTResult) else RTResult().success(Number(result))

    def execute_memoryStructSize(self, args, exec_ctx):
        if len(args) != 1 or not isinstance(args[0], String):
            return self._failure(exec_ctx, "memoryStructSize(layout) expects fields like \"int32 id, float32 x\"")
        result = self._cpp(_MEMORY_LIB.memoryStructSize, [args[0].value], exec_ctx)
        return result if isinstance(result, RTResult) else RTResult().success(Number(result))

    def execute_memoryStructFieldOffset(self, args, exec_ctx):
        if len(args) != 2 or not isinstance(args[0], String) or not isinstance(args[1], String):
            return self._failure(
                exec_ctx,
                "memoryStructFieldOffset(layout, field) expects a layout and field name",
            )
        result = self._cpp(_MEMORY_LIB.memoryStructFieldOffset, [args[0].value, args[1].value], exec_ctx)
        return result if isinstance(result, RTResult) else RTResult().success(Number(result))

    def execute_memoryStructFieldSize(self, args, exec_ctx):
        if len(args) != 2 or not isinstance(args[0], String) or not isinstance(args[1], String):
            return self._failure(
                exec_ctx,
                "memoryStructFieldSize(layout, field) expects a layout and field name",
            )
        result = self._cpp(_MEMORY_LIB.memoryStructFieldSize, [args[0].value, args[1].value], exec_ctx)
        return result if isinstance(result, RTResult) else RTResult().success(Number(result))

    # Explicit names for FFI callers.  The memoryStruct implementation uses
    # native alignment and native-endian primitive access, so these aliases
    # make that intent clear without creating a second layout format.
    def execute_nativeStructSize(self, args, exec_ctx):
        return self.execute_memoryStructSize(args, exec_ctx)

    def execute_nativeStructAllocate(self, args, exec_ctx):
        return self.execute_memoryStructAllocate(args, exec_ctx)

    def execute_nativeStructFieldOffset(self, args, exec_ctx):
        return self.execute_memoryStructFieldOffset(args, exec_ctx)

    def execute_nativeStructFieldSize(self, args, exec_ctx):
        return self.execute_memoryStructFieldSize(args, exec_ctx)

    def execute_nativeStructGet(self, args, exec_ctx):
        return self.execute_memoryStructGet(args, exec_ctx)

    def execute_nativeStructSet(self, args, exec_ctx):
        return self.execute_memoryStructSet(args, exec_ctx)

    def execute_memoryStructAllocate(self, args, exec_ctx):
        if len(args) != 1 or not isinstance(args[0], String):
            return self._failure(exec_ctx, "memoryStructAllocate(layout) expects fields like \"int32 id, float32 x\"")
        result = self._cpp(_MEMORY_LIB.memoryStructAllocate, [args[0].value], exec_ctx)
        return result if isinstance(result, RTResult) else RTResult().success(Number(result))

    def execute_memoryStructGet(self, args, exec_ctx):
        if len(args) != 2 or not _native_nonnegative(args[0]) or not isinstance(args[1], String):
            return self._failure(exec_ctx, "memoryStructGet(address, field) expects an address and field name")
        result = self._cpp(_MEMORY_LIB.memoryStructGet, [args[0].value, args[1].value], exec_ctx)
        return result if isinstance(result, RTResult) else RTResult().success(Number(result))

    def execute_memoryStructSet(self, args, exec_ctx):
        if (
            len(args) != 3
            or not _native_nonnegative(args[0])
            or not isinstance(args[1], String)
            or not isinstance(args[2], Number)
            or args[2].is_bool
        ):
            return self._failure(exec_ctx, "memoryStructSet(address, field, value) expects an address, field, and number")
        result = self._cpp(_MEMORY_LIB.memoryStructSet, [args[0].value, args[1].value, args[2].value], exec_ctx)
        return result if isinstance(result, RTResult) else RTResult().success(Number.null)

    def execute_memoryAllocate(self, args, exec_ctx):
        if len(args) != 1 or not _native_nonnegative(args[0]):
            return self._failure(exec_ctx, "memoryAllocate(size) expects a non-negative integer size")
        address = _MEMORY_LIB.malloc(args[0].value)
        return RTResult().success(Number(address))

    def execute_memoryCallocate(self, args, exec_ctx):
        if len(args) != 2 or not all(_native_nonnegative(arg) for arg in args):
            return self._failure(
                exec_ctx,
                "memoryCallocate(count, size) expects non-negative integer arguments",
            )
        address = _MEMORY_LIB.calloc(args[0].value, args[1].value)
        return RTResult().success(Number(address))

    def execute_memoryReallocate(self, args, exec_ctx):
        if (
            len(args) != 2
            or not _native_nonnegative(args[1])
            or not _native_int(args[0])
            or args[0].value < 0
        ):
            return self._failure(
                exec_ctx,
                "memoryReallocate(address, size) expects an address and non-negative integer size",
            )
        error = self._check_memory_address(args[0].value, exec_ctx)
        if error:
            return error
        old_address = args[0].value
        new_address = _MEMORY_LIB.realloc(old_address, args[1].value)
        return RTResult().success(Number(new_address))

    def execute_memoryFree(self, args, exec_ctx):
        if len(args) != 1 or not _native_int(args[0]) or args[0].value < 0:
            return self._failure(exec_ctx, "memoryFree(address) expects an integer address")
        address = args[0].value
        error = self._check_memory_address(address, exec_ctx)
        if error:
            return error
        _MEMORY_LIB.free(address)
        return RTResult().success(Number.null)

    def _check_memory_address(self, address, exec_ctx):
        # Allocation ownership and lifetime are tracked by cpp.cpp.  Keeping
        # a second Python-side address set is incorrect because malloc may
        # legally reuse an address after free.
        return None

    def execute_memorySet(self, args, exec_ctx):
        if (
            len(args) != 3
            or not all(_native_nonnegative(arg) for arg in (args[0], args[2]))
            or not _native_int(args[1])
            or not 0 <= args[1].value <= 255
        ):
            return self._failure(
                exec_ctx,
                "memorySet(address, value, size) expects a non-negative address and size "
                "and a byte value from 0 to 255",
            )
        error = self._check_memory_address(args[0].value, exec_ctx)
        if error:
            return error
        _MEMORY_LIB.memset(args[0].value, args[1].value, args[2].value)
        return RTResult().success(Number.null)

    def execute_memoryCopy(self, args, exec_ctx):
        if (
            len(args) != 3
            or not all(_native_nonnegative(arg) for arg in args)
        ):
            return self._failure(
                exec_ctx,
                "memoryCopy(destination, source, size) expects non-negative integer arguments",
            )
        for address in (args[0].value, args[1].value):
            error = self._check_memory_address(address, exec_ctx)
            if error:
                return error
        _MEMORY_LIB.memcpy(args[0].value, args[1].value, args[2].value)
        return RTResult().success(Number.null)

    def _memory_read_builtin(self, args, exec_ctx, name, native_function):
        if (
            len(args) != 2
            or not _native_nonnegative(args[0])
            or not _native_nonnegative(args[1])
        ):
            return self._failure(
                exec_ctx,
                f"{name}(address, offset) expects non-negative integer arguments",
            )
        error = self._check_memory_address(args[0].value, exec_ctx)
        if error:
            return error
        return RTResult().success(
            Number(native_function(args[0].value, args[1].value))
        )

    def _memory_write_builtin(
        self, args, exec_ctx, name, native_function, minimum, maximum
    ):
        if (
            len(args) != 3
            or not _native_nonnegative(args[0])
            or not _native_nonnegative(args[1])
            or not _native_int(args[2])
            or not minimum <= args[2].value <= maximum
        ):
            return self._failure(
                exec_ctx,
                f"{name}(address, offset, value) expects non-negative address "
                f"and offset and a value from {minimum} to {maximum}",
            )
        error = self._check_memory_address(args[0].value, exec_ctx)
        if error:
            return error
        native_function(args[0].value, args[1].value, args[2].value)
        return RTResult().success(Number.null)

    def execute_memoryReadInt8(self, args, exec_ctx):
        return self._memory_read_builtin(
            args, exec_ctx, "memoryReadInt8", _MEMORY_LIB.readInt8
        )

    def execute_memoryWriteInt8(self, args, exec_ctx):
        return self._memory_write_builtin(
            args, exec_ctx, "memoryWriteInt8", _MEMORY_LIB.writeInt8, -(2**7), 2**7 - 1
        )

    def execute_memoryReadInt16(self, args, exec_ctx):
        return self._memory_read_builtin(
            args, exec_ctx, "memoryReadInt16", _MEMORY_LIB.readInt16
        )

    def execute_memoryWriteInt16(self, args, exec_ctx):
        return self._memory_write_builtin(
            args, exec_ctx, "memoryWriteInt16", _MEMORY_LIB.writeInt16, -(2**15), 2**15 - 1
        )

    def execute_memoryReadInt32(self, args, exec_ctx):
        return self._memory_read_builtin(
            args, exec_ctx, "memoryReadInt32", _MEMORY_LIB.readInt32
        )

    def execute_memoryWriteInt32(self, args, exec_ctx):
        return self._memory_write_builtin(
            args, exec_ctx, "memoryWriteInt32", _MEMORY_LIB.writeInt32, -(2**31), 2**31 - 1
        )

    def execute_memoryReadInt64(self, args, exec_ctx):
        return self._memory_read_builtin(
            args, exec_ctx, "memoryReadInt64", _MEMORY_LIB.readInt64
        )

    def execute_memoryWriteInt64(self, args, exec_ctx):
        return self._memory_write_builtin(
            args, exec_ctx, "memoryWriteInt64", _MEMORY_LIB.writeInt64, -(2**63), 2**63 - 1
        )

    def execute_memoryReadUInt8(self, args, exec_ctx):
        return self._memory_read_builtin(
            args, exec_ctx, "memoryReadUInt8", _MEMORY_LIB.readUInt8
        )

    def execute_memoryWriteUInt8(self, args, exec_ctx):
        return self._memory_write_builtin(
            args, exec_ctx, "memoryWriteUInt8", _MEMORY_LIB.writeUInt8, 0, 2**8 - 1
        )

    def execute_memoryReadUInt16(self, args, exec_ctx):
        return self._memory_read_builtin(
            args, exec_ctx, "memoryReadUInt16", _MEMORY_LIB.readUInt16
        )

    def execute_memoryWriteUInt16(self, args, exec_ctx):
        return self._memory_write_builtin(
            args, exec_ctx, "memoryWriteUInt16", _MEMORY_LIB.writeUInt16, 0, 2**16 - 1
        )

    def execute_memoryReadUInt32(self, args, exec_ctx):
        return self._memory_read_builtin(
            args, exec_ctx, "memoryReadUInt32", _MEMORY_LIB.readUInt32
        )

    def execute_memoryWriteUInt32(self, args, exec_ctx):
        return self._memory_write_builtin(
            args, exec_ctx, "memoryWriteUInt32", _MEMORY_LIB.writeUInt32, 0, 2**32 - 1
        )

    def execute_memoryReadUInt64(self, args, exec_ctx):
        return self._memory_read_builtin(
            args, exec_ctx, "memoryReadUInt64", _MEMORY_LIB.readUInt64
        )

    def execute_memoryWriteUInt64(self, args, exec_ctx):
        return self._memory_write_builtin(
            args, exec_ctx, "memoryWriteUInt64", _MEMORY_LIB.writeUInt64, 0, 2**64 - 1
        )

    def _memory_read_float_builtin(self, args, exec_ctx, name, native_function):
        return self._memory_read_builtin(args, exec_ctx, name, native_function)

    def _memory_write_float_builtin(self, args, exec_ctx, name, native_function):
        if (
            len(args) != 3
            or not _native_nonnegative(args[0])
            or not _native_nonnegative(args[1])
            or not isinstance(args[2], Number)
            or args[2].is_bool
        ):
            return self._failure(
                exec_ctx,
                f"{name}(address, offset, value) expects an address, offset, and number",
            )
        native_function(args[0].value, args[1].value, args[2].value)
        return RTResult().success(Number.null)

    def execute_memoryReadFloat32(self, args, exec_ctx):
        return self._memory_read_float_builtin(
            args, exec_ctx, "memoryReadFloat32", _MEMORY_LIB.readFloat32
        )

    def execute_memoryWriteFloat32(self, args, exec_ctx):
        return self._memory_write_float_builtin(
            args, exec_ctx, "memoryWriteFloat32", _MEMORY_LIB.writeFloat32
        )

    def execute_memoryReadFloat64(self, args, exec_ctx):
        return self._memory_read_float_builtin(
            args, exec_ctx, "memoryReadFloat64", _MEMORY_LIB.readFloat64
        )

    def execute_memoryWriteFloat64(self, args, exec_ctx):
        return self._memory_write_float_builtin(
            args, exec_ctx, "memoryWriteFloat64", _MEMORY_LIB.writeFloat64
        )

    # Short native-memory names exposed alongside the descriptive memory* API.
    def execute_malloc(self, args, exec_ctx):
        return self.execute_memoryAllocate(args, exec_ctx)

    def execute_calloc(self, args, exec_ctx):
        return self.execute_memoryCallocate(args, exec_ctx)

    def execute_realloc(self, args, exec_ctx):
        return self.execute_memoryReallocate(args, exec_ctx)

    def execute_free(self, args, exec_ctx):
        return self.execute_memoryFree(args, exec_ctx)

    def execute_memset(self, args, exec_ctx):
        return self.execute_memorySet(args, exec_ctx)

    def execute_memcpy(self, args, exec_ctx):
        return self.execute_memoryCopy(args, exec_ctx)

    def execute_readByte(self, args, exec_ctx):
        return self._memory_read_builtin(
            args, exec_ctx, "readByte", _MEMORY_LIB.readByte
        )

    def execute_writeByte(self, args, exec_ctx):
        return self._memory_write_builtin(
            args, exec_ctx, "writeByte", _MEMORY_LIB.writeByte, 0, 255
        )

    def execute_readInt8(self, args, exec_ctx):
        return self.execute_memoryReadInt8(args, exec_ctx)

    def execute_writeInt8(self, args, exec_ctx):
        return self.execute_memoryWriteInt8(args, exec_ctx)

    def execute_readInt16(self, args, exec_ctx):
        return self.execute_memoryReadInt16(args, exec_ctx)

    def execute_writeInt16(self, args, exec_ctx):
        return self.execute_memoryWriteInt16(args, exec_ctx)

    def execute_readInt32(self, args, exec_ctx):
        return self.execute_memoryReadInt32(args, exec_ctx)

    def execute_writeInt32(self, args, exec_ctx):
        return self.execute_memoryWriteInt32(args, exec_ctx)

    def execute_readInt64(self, args, exec_ctx):
        return self.execute_memoryReadInt64(args, exec_ctx)

    def execute_writeInt64(self, args, exec_ctx):
        return self.execute_memoryWriteInt64(args, exec_ctx)

    def execute_readUInt8(self, args, exec_ctx):
        return self.execute_memoryReadUInt8(args, exec_ctx)

    def execute_writeUInt8(self, args, exec_ctx):
        return self.execute_memoryWriteUInt8(args, exec_ctx)

    def execute_readUInt16(self, args, exec_ctx):
        return self.execute_memoryReadUInt16(args, exec_ctx)

    def execute_writeUInt16(self, args, exec_ctx):
        return self.execute_memoryWriteUInt16(args, exec_ctx)

    def execute_readUInt32(self, args, exec_ctx):
        return self.execute_memoryReadUInt32(args, exec_ctx)

    def execute_writeUInt32(self, args, exec_ctx):
        return self.execute_memoryWriteUInt32(args, exec_ctx)

    def execute_readUInt64(self, args, exec_ctx):
        return self.execute_memoryReadUInt64(args, exec_ctx)

    def execute_writeUInt64(self, args, exec_ctx):
        return self.execute_memoryWriteUInt64(args, exec_ctx)

    def execute_readFloat32(self, args, exec_ctx):
        return self.execute_memoryReadFloat32(args, exec_ctx)

    def execute_writeFloat32(self, args, exec_ctx):
        return self.execute_memoryWriteFloat32(args, exec_ctx)

    def execute_readFloat64(self, args, exec_ctx):
        return self.execute_memoryReadFloat64(args, exec_ctx)

    def execute_writeFloat64(self, args, exec_ctx):
        return self.execute_memoryWriteFloat64(args, exec_ctx)

    def execute_sizeOf(self, args, exec_ctx):
        if len(args) != 1 or not isinstance(args[0], String):
            return self._failure(exec_ctx, "sizeOf(typeName) expects one string")
        try:
            size = _MEMORY_LIB.sizeof(args[0].value)
        except (TypeError, ValueError) as exc:
            return self._failure(exec_ctx, str(exc))
        return RTResult().success(Number(size))

    def execute_input(self, args, exec_ctx):
        if len(args) > 1:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "input() takes 0 or 1 arguments",
                    exec_ctx,
                )
            )
        prompt = str(args[0]) if args else ""
        text = input(prompt)
        return RTResult().success(String(text))

    def execute_inputln(self, args, exec_ctx):
        if len(args) > 1:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "inputln() takes 0 or 1 arguments",
                    exec_ctx,
                )
            )
        prompt = str(args[0]) if args else ""
        text = input(prompt)
        return RTResult().success(String(text + "\n"))

    def execute_rawPy(self, args, exec_ctx):
        if len(args) != 1 or not isinstance(args[0], String):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    'rawPy() expects exactly one string argument — rawPy("python code")',
                    exec_ctx,
                )
            )
        try:
            exec(args[0].value, {"__builtins__": __builtins__})
        except Exception as e:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    f"Python error in rawPy(): {e}",
                    exec_ctx,
                )
            )
        return RTResult().success(Number.null)

    def execute_strOf(self, args, exec_ctx):
        if len(args) != 1:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "strOf() takes exactly 1 argument",
                    exec_ctx,
                )
            )
        return RTResult().success(String(str(args[0])))

    def execute_intOf(self, args, exec_ctx):
        if len(args) != 1:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "intOf() takes exactly 1 argument",
                    exec_ctx,
                )
            )
        v = args[0]
        try:
            return RTResult().success(Number(int(float(v.value))))
        except Exception:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    f"Cannot convert '{v}' to int",
                    exec_ctx,
                )
            )

    def execute_floatOf(self, args, exec_ctx):
        if len(args) != 1:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "floatOf() takes exactly 1 argument",
                    exec_ctx,
                )
            )
        v = args[0]
        try:
            return RTResult().success(Number(float(v.value)))
        except Exception:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    f"Cannot convert '{v}' to float",
                    exec_ctx,
                )
            )

    def execute_sentinel(self, args, exec_ctx):
        """sentinel([name]) — create a unique, optionally named sentinel."""
        if len(args) > 1 or (args and not isinstance(args[0], String)):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    'sentinel() expects zero or one string argument — sentinel("NAME")',
                    exec_ctx,
                )
            )
        name = args[0].value if args else None
        return RTResult().success(Sentinel(name).set_context(exec_ctx))

    def execute_object(self, args, exec_ctx):
        """object() — create a unique unnamed opaque object value."""
        if args:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "object() takes no arguments",
                    exec_ctx,
                )
            )
        return RTResult().success(ObjectValue().set_context(exec_ctx))

    def execute_rawPyx(self, args, exec_ctx):
        if len(args) != 1 or not isinstance(args[0], String):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    'rawPyx() expects exactly one string argument — rawPyx("cython code")',
                    exec_ctx,
                )
            )
        try:
            cython_inline = _get_cython_inline()
            cy_locals = {}
            cython_inline(args[0].value, locals=cy_locals, globals=cy_locals, quiet=True)
        except Exception as e:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    f"Cython error in rawPyx(): {type(e).__name__}: {e}",
                    exec_ctx,
                )
            )
        return RTResult().success(Number.null)

    def execute_returnType(self, args, exec_ctx):
        if len(args) != 1:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "returnType() takes exactly 1 argument",
                    exec_ctx,
                )
            )
        return RTResult().success(String(_runtime.value_type_name(args[0])))

    def execute_returnLength(self, args, exec_ctx):
        if len(args) != 1:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "returnLength() takes exactly 1 argument",
                    exec_ctx,
                )
            )
        v = args[0]
        if isinstance(v, String):
            return RTResult().success(Number(len(v.value)))
        if isinstance(v, (List, LynxTuple)):
            return RTResult().success(Number(len(v.elements)))
        return RTResult().failure(
            RTError(
                self.pos_start,
                self.pos_end,
                f"returnLength() does not support values of type '{type(v).__name__}'",
                exec_ctx,
            )
        )

    def execute_seqFromTo(self, args, exec_ctx):
        if len(args) != 3 or not all(isinstance(a, Number) for a in args):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "seqFromTo() expects exactly 3 numeric arguments — seqFromTo(start, stop, step)",
                    exec_ctx,
                )
            )
        start, stop, step = (int(a.value) for a in args)
        if step == 0:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "seqFromTo() step cannot be 0",
                    exec_ctx,
                )
            )
        elements = [Number(n).set_context(exec_ctx) for n in range(start, stop, step)]
        return RTResult().success(List(elements))

    def execute_range(self, args, exec_ctx):
        """range(stop), range(start, stop), or range(start, stop, step)."""
        if not args or len(args) > 3 or not all(isinstance(a, Number) for a in args):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "range() expects 1, 2, or 3 integer arguments: "
                    "range(stop), range(start, stop), or range(start, stop, step)",
                    exec_ctx,
                )
            )
        if len(args) == 1:
            start, stop, step = 0, int(args[0].value), 1
        elif len(args) == 2:
            start, stop, step = int(args[0].value), int(args[1].value), 1
        else:
            start, stop, step = (
                int(args[0].value),
                int(args[1].value),
                int(args[2].value),
            )
        if step == 0:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "range() step cannot be 0",
                    exec_ctx,
                )
            )
        elements = [Number(n).set_context(exec_ctx) for n in range(start, stop, step)]
        return RTResult().success(List(elements))

    def execute_cleanRawPyxCache(self, args, exec_ctx):
        import os
        import shutil

        cache_dir = os.path.expanduser("~/.cython/inline")
        try:
            if os.path.isdir(cache_dir):
                shutil.rmtree(cache_dir)
        except Exception as e:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    f"cleanRawPyxCache() failed: {e}",
                    exec_ctx,
                )
            )
        return RTResult().success(Number.null)

    # list built-ins

    def execute_listJsonArray(self, args, exec_ctx):
        import json as _json

        if len(args) != 1 or not isinstance(args[0], List):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "listJsonArray(list) expects a list",
                    exec_ctx,
                )
            )
        try:
            items = [_json_value(element) for element in args[0].elements]
            return RTResult().success(String(_json.dumps(items)))
        except Exception as e:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    f"listJsonArray() failed: {e}",
                    exec_ctx,
                )
            )

    def execute_listJsonObject(self, args, exec_ctx):
        import json as _json

        if len(args) != 1 or not isinstance(args[0], List):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "listJsonObject(list) expects a flat key/value list",
                    exec_ctx,
                )
            )
        els = args[0].elements
        if len(els) % 2 != 0:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "listJsonObject() requires an even-length list (key, value, key, value, ...)",
                    exec_ctx,
                )
            )
        try:
            obj = {}
            for i in range(0, len(els), 2):
                k = _json_value(els[i])
                v = _json_value(els[i + 1])
                obj[str(k)] = v
            return RTResult().success(String(_json.dumps(obj)))
        except Exception as e:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    f"listJsonObject() failed: {e}",
                    exec_ctx,
                )
            )

    def execute_splitStr(self, args, exec_ctx):
        if (
            len(args) != 2
            or not isinstance(args[0], String)
            or not isinstance(args[1], String)
        ):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "splitStr(str, sep) expects two string arguments",
                    exec_ctx,
                )
            )
        parts = args[0].value.split(args[1].value)
        elements = [String(p).set_context(exec_ctx) for p in parts]
        return RTResult().success(List(elements))

    def execute_listFlatten(self, args, exec_ctx):
        if len(args) != 1 or not isinstance(args[0], List):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "listFlatten(list) expects a list",
                    exec_ctx,
                )
            )
        flat = []
        for el in args[0].elements:
            if isinstance(el, List):
                flat.extend(el.elements)
            else:
                flat.append(el)
        return RTResult().success(List(flat))

    def execute_listUnique(self, args, exec_ctx):
        if len(args) != 1 or not isinstance(args[0], List):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "listUnique(list) expects a list",
                    exec_ctx,
                )
            )
        seen_strs: list[str] = []
        unique_els = []
        for el in args[0].elements:
            s = str(el)
            if s not in seen_strs:
                seen_strs.append(s)
                unique_els.append(el)
        return RTResult().success(List(unique_els))

    def execute_listPush(self, args, exec_ctx):
        if len(args) != 2 or not isinstance(args[0], List):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "listPush(list, item) expects a list and a value",
                    exec_ctx,
                )
            )
        new_elements = list(args[0].elements) + [args[1]]
        return RTResult().success(List(new_elements))

    def execute_listPop(self, args, exec_ctx):
        if len(args) != 1 or not isinstance(args[0], List):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "listPop(list) expects a list",
                    exec_ctx,
                )
            )
        if not args[0].elements:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "listPop() called on an empty list",
                    exec_ctx,
                )
            )
        return RTResult().success(args[0].elements.pop())

    def execute_listGet(self, args, exec_ctx):
        if (
            len(args) != 2
            or not isinstance(args[0], List)
            or not isinstance(args[1], Number)
        ):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "listGet(list, idx) expects a list and an integer index",
                    exec_ctx,
                )
            )
        lst = args[0]
        idx = int(args[1].value)
        if idx < -len(lst.elements) or idx >= len(lst.elements):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    f"listGet() index {idx} out of range for list of length {len(lst.elements)}",
                    exec_ctx,
                )
            )
        return RTResult().success(lst.elements[idx])

    def execute_listSet(self, args, exec_ctx):
        if (
            len(args) != 3
            or not isinstance(args[0], List)
            or not isinstance(args[1], Number)
        ):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "listSet(list, idx, val) expects a list, an integer index, and a value",
                    exec_ctx,
                )
            )
        lst = args[0]
        idx = int(args[1].value)
        if idx < -len(lst.elements) or idx >= len(lst.elements):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    f"listSet() index {idx} out of range for list of length {len(lst.elements)}",
                    exec_ctx,
                )
            )
        new_elements = list(lst.elements)
        new_elements[idx] = args[2]
        return RTResult().success(List(new_elements))

    def execute_listSlice(self, args, exec_ctx):
        if (
            len(args) != 3
            or not isinstance(args[0], List)
            or not isinstance(args[1], Number)
            or not isinstance(args[2], Number)
        ):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "listSlice(list, start, stop) expects a list and two integer indices",
                    exec_ctx,
                )
            )
        start = int(args[1].value)
        stop = int(args[2].value)
        return RTResult().success(List(args[0].elements[start:stop]))

    def execute_listContains(self, args, exec_ctx):
        if len(args) != 2 or not isinstance(args[0], List):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "listContains(list, item) expects a list and a value",
                    exec_ctx,
                )
            )
        target = str(args[1])
        found = any(str(e) == target for e in args[0].elements)
        return RTResult().success(Number(1 if found else 0, is_bool=True))

    def execute_contains(self, args, exec_ctx):
        """contains(sequence, value) — membership for lists and tuples."""
        if len(args) != 2 or not isinstance(args[0], (List, LynxTuple)):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "contains(list_or_tuple, value) expects a list or tuple and a value",
                    exec_ctx,
                )
            )
        target = str(args[1])
        found = any(str(element) == target for element in args[0].elements)
        return RTResult().success(Number(1 if found else 0, is_bool=True))

    def execute_listJoin(self, args, exec_ctx):
        if (
            len(args) != 2
            or not isinstance(args[0], List)
            or not isinstance(args[1], String)
        ):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "listJoin(list, sep) expects a list and a string separator",
                    exec_ctx,
                )
            )
        sep = args[1].value
        result = sep.join(str(e) for e in args[0].elements)
        return RTResult().success(String(result))

    def execute_listIndex(self, args, exec_ctx):
        if len(args) != 2 or not isinstance(args[0], List):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "listIndex(list, item) expects a list and a value",
                    exec_ctx,
                )
            )
        target = str(args[1])
        for i, e in enumerate(args[0].elements):
            if str(e) == target:
                return RTResult().success(Number(i))
        return RTResult().success(Number(-1))

    def execute_listRemove(self, args, exec_ctx):
        if (
            len(args) != 2
            or not isinstance(args[0], List)
            or not isinstance(args[1], Number)
        ):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "listRemove(list, idx) expects a list and an integer index",
                    exec_ctx,
                )
            )
        lst = args[0]
        idx = int(args[1].value)
        if idx < -len(lst.elements) or idx >= len(lst.elements):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    f"listRemove() index {idx} out of range for list of length {len(lst.elements)}",
                    exec_ctx,
                )
            )
        new_elements = list(lst.elements)
        new_elements.pop(idx)
        return RTResult().success(List(new_elements))

    def execute_anyOf(self, args, exec_ctx):
        if len(args) != 1 or not isinstance(args[0], List):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "anyOf(list) expects a list",
                    exec_ctx,
                )
            )
        result = any(e.is_true() for e in args[0].elements)
        return RTResult().success(Number(1 if result else 0, is_bool=True))

    def execute_allOf(self, args, exec_ctx):
        if len(args) != 1 or not isinstance(args[0], List):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "allOf(list) expects a list",
                    exec_ctx,
                )
            )
        result = all(e.is_true() for e in args[0].elements)
        return RTResult().success(Number(1 if result else 0, is_bool=True))

    def execute_sumOf(self, args, exec_ctx):
        if len(args) != 1 or not isinstance(args[0], List):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "sumOf(list) expects a list",
                    exec_ctx,
                )
            )
        try:
            total = sum(e.value for e in args[0].elements if isinstance(e, Number))
            return RTResult().success(Number(total))
        except Exception as e:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    f"sumOf() failed: {e}",
                    exec_ctx,
                )
            )

    def _list_sort_key(self, e):
        if isinstance(e, (Number, String)):
            return e.value
        return str(e)

    def execute_sortList(self, args, exec_ctx):
        if len(args) not in (1, 2) or not isinstance(args[0], List):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "sortList(list) or sortList(list, reverse) expects a list",
                    exec_ctx,
                )
            )
        reverse = args[1].is_true() if len(args) == 2 else False
        try:
            sorted_els = sorted(
                args[0].elements, key=self._list_sort_key, reverse=reverse
            )
            return RTResult().success(List(sorted_els))
        except Exception as e:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    f"sortList() failed: {e}",
                    exec_ctx,
                )
            )

    def execute_reverseList(self, args, exec_ctx):
        if len(args) != 1 or not isinstance(args[0], List):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "reverseList(list) expects a list",
                    exec_ctx,
                )
            )
        return RTResult().success(List(list(reversed(args[0].elements))))

    def execute_listMin(self, args, exec_ctx):
        if len(args) != 1 or not isinstance(args[0], List):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "listMin(list) expects a list",
                    exec_ctx,
                )
            )
        if not args[0].elements:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "listMin() called on an empty list",
                    exec_ctx,
                )
            )
        try:
            return RTResult().success(
                min(args[0].elements, key=self._list_sort_key)
            )
        except Exception as e:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    f"listMin() failed: {e}",
                    exec_ctx,
                )
            )

    def execute_listMax(self, args, exec_ctx):
        if len(args) != 1 or not isinstance(args[0], List):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "listMax(list) expects a list",
                    exec_ctx,
                )
            )
        if not args[0].elements:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "listMax() called on an empty list",
                    exec_ctx,
                )
            )
        try:
            return RTResult().success(
                max(args[0].elements, key=self._list_sort_key)
            )
        except Exception as e:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    f"listMax() failed: {e}",
                    exec_ctx,
                )
            )

    def execute_listFirst(self, args, exec_ctx):
        if len(args) != 1 or not isinstance(args[0], List):
            return self._failure(exec_ctx, "listFirst(list) expects a list")
        if not args[0].elements:
            return self._failure(exec_ctx, "listFirst() called on an empty list")
        return RTResult().success(args[0].elements[0])

    def execute_listLast(self, args, exec_ctx):
        if len(args) != 1 or not isinstance(args[0], List):
            return self._failure(exec_ctx, "listLast(list) expects a list")
        if not args[0].elements:
            return self._failure(exec_ctx, "listLast() called on an empty list")
        return RTResult().success(args[0].elements[-1])

    def execute_listHead(self, args, exec_ctx):
        if (
            len(args) != 2
            or not isinstance(args[0], List)
            or not isinstance(args[1], Number)
        ):
            return self._failure(
                exec_ctx, "listHead(list, count) expects a list and an integer count"
            )
        count = int(args[1].value)
        if count < 0:
            return self._failure(exec_ctx, "listHead() count cannot be negative")
        return RTResult().success(List(args[0].elements[:count]))

    def execute_listTail(self, args, exec_ctx):
        if (
            len(args) != 2
            or not isinstance(args[0], List)
            or not isinstance(args[1], Number)
        ):
            return self._failure(
                exec_ctx, "listTail(list, count) expects a list and an integer count"
            )
        count = int(args[1].value)
        if count < 0:
            return self._failure(exec_ctx, "listTail() count cannot be negative")
        return RTResult().success(List(args[0].elements[-count:] if count else []))

    def execute_listCount(self, args, exec_ctx):
        if len(args) != 2 or not isinstance(args[0], List):
            return self._failure(
                exec_ctx, "listCount(list, value) expects a list and a value"
            )
        target = str(args[1])
        return RTResult().success(
            Number(sum(1 for element in args[0].elements if str(element) == target))
        )

    def execute_listExtend(self, args, exec_ctx):
        if (
            len(args) != 2
            or not isinstance(args[0], List)
            or not isinstance(args[1], List)
        ):
            return self._failure(exec_ctx, "listExtend(list1, list2) expects two lists")
        return RTResult().success(List(args[0].elements + args[1].elements))

    def execute_listInsert(self, args, exec_ctx):
        if (
            len(args) != 3
            or not isinstance(args[0], List)
            or not isinstance(args[1], Number)
        ):
            return self._failure(
                exec_ctx,
                "listInsert(list, index, value) expects a list, integer index, and value",
            )
        elements = list(args[0].elements)
        elements.insert(int(args[1].value), args[2])
        return RTResult().success(List(elements))

    def execute_listClear(self, args, exec_ctx):
        if len(args) != 1 or not isinstance(args[0], List):
            return self._failure(exec_ctx, "listClear(list) expects a list")
        return RTResult().success(List([]))

    def execute_listRepeat(self, args, exec_ctx):
        if len(args) != 2 or not isinstance(args[1], Number):
            return self._failure(
                exec_ctx, "listRepeat(value, count) expects a value and integer count"
            )
        count = int(args[1].value)
        if count < 0:
            return self._failure(exec_ctx, "listRepeat() count cannot be negative")
        return RTResult().success(List([args[0]] * count))

    def execute_listAvg(self, args, exec_ctx):
        if len(args) != 1 or not isinstance(args[0], List):
            return self._failure(exec_ctx, "listAvg(list) expects a list")
        numbers = [element.value for element in args[0].elements if isinstance(element, Number)]
        if not numbers:
            return RTResult().success(Number(0.0))
        return RTResult().success(Number(sum(numbers) / len(numbers)))

    def execute_listZip(self, args, exec_ctx):
        if (
            len(args) != 2
            or not isinstance(args[0], List)
            or not isinstance(args[1], List)
        ):
            return self._failure(exec_ctx, "listZip(list1, list2) expects two lists")
        import json as _json

        pairs = []
        for left, right in zip(args[0].elements, args[1].elements):
            pairs.append(
                String(
                    _json.dumps(
                        {"a": _json_value(left), "b": _json_value(right)}
                    )
                )
            )
        return RTResult().success(List(pairs))

    # tuple built-ins

    def execute_tupleCreate(self, args, exec_ctx):
        """tupleCreate(v1, v2, ...) — create a tuple from any number of arguments."""
        return RTResult().success(LynxTuple(args))

    def execute_tupleGet(self, args, exec_ctx):
        if (
            len(args) != 2
            or not isinstance(args[0], LynxTuple)
            or not isinstance(args[1], Number)
        ):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "tupleGet(tuple, idx) expects a tuple and an integer index",
                    exec_ctx,
                )
            )
        t = args[0]
        idx = int(args[1].value)
        if idx < -len(t.elements) or idx >= len(t.elements):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    f"tupleGet() index {idx} out of range for tuple of length {len(t.elements)}",
                    exec_ctx,
                )
            )
        return RTResult().success(t.elements[idx])

    def execute_tupleLen(self, args, exec_ctx):
        if len(args) != 1 or not isinstance(args[0], LynxTuple):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "tupleLen(tuple) expects a tuple",
                    exec_ctx,
                )
            )
        return RTResult().success(Number(len(args[0].elements)))

    def execute_tupleContains(self, args, exec_ctx):
        if len(args) != 2 or not isinstance(args[0], LynxTuple):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "tupleContains(tuple, val) expects a tuple and a value",
                    exec_ctx,
                )
            )
        target = str(args[1])
        found = any(str(e) == target for e in args[0].elements)
        return RTResult().success(Number(1 if found else 0, is_bool=True))

    def execute_tupleIndex(self, args, exec_ctx):
        if len(args) != 2 or not isinstance(args[0], LynxTuple):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "tupleIndex(tuple, val) expects a tuple and a value",
                    exec_ctx,
                )
            )
        target = str(args[1])
        for i, e in enumerate(args[0].elements):
            if str(e) == target:
                return RTResult().success(Number(i))
        return RTResult().success(Number(-1))

    def execute_tupleSlice(self, args, exec_ctx):
        if (
            len(args) != 3
            or not isinstance(args[0], LynxTuple)
            or not isinstance(args[1], Number)
            or not isinstance(args[2], Number)
        ):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "tupleSlice(tuple, start, stop) expects a tuple and two integer indices",
                    exec_ctx,
                )
            )
        start = int(args[1].value)
        stop = int(args[2].value)
        return RTResult().success(LynxTuple(args[0].elements[start:stop]))

    def execute_tupleToList(self, args, exec_ctx):
        if len(args) != 1 or not isinstance(args[0], LynxTuple):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "tupleToList(tuple) expects a tuple",
                    exec_ctx,
                )
            )
        return RTResult().success(List(list(args[0].elements)))

    def execute_listToTuple(self, args, exec_ctx):
        if len(args) != 1 or not isinstance(args[0], List):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "listToTuple(list) expects a list",
                    exec_ctx,
                )
            )
        return RTResult().success(LynxTuple(args[0].elements))

    def execute_tupleConcat(self, args, exec_ctx):
        if (
            len(args) != 2
            or not isinstance(args[0], LynxTuple)
            or not isinstance(args[1], LynxTuple)
        ):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "tupleConcat(t1, t2) expects two tuples",
                    exec_ctx,
                )
            )
        return RTResult().success(LynxTuple(args[0].elements + args[1].elements))

    def execute_tupleCount(self, args, exec_ctx):
        if len(args) != 2 or not isinstance(args[0], LynxTuple):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "tupleCount(tuple, val) expects a tuple and a value",
                    exec_ctx,
                )
            )
        target = str(args[1])
        count = sum(1 for e in args[0].elements if str(e) == target)
        return RTResult().success(Number(count))

    def execute_tupleFirst(self, args, exec_ctx):
        if len(args) != 1 or not isinstance(args[0], LynxTuple):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "tupleFirst(tuple) expects a tuple",
                    exec_ctx,
                )
            )
        if not args[0].elements:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "tupleFirst() called on an empty tuple",
                    exec_ctx,
                )
            )
        return RTResult().success(args[0].elements[0])

    def execute_tupleLast(self, args, exec_ctx):
        if len(args) != 1 or not isinstance(args[0], LynxTuple):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "tupleLast(tuple) expects a tuple",
                    exec_ctx,
                )
            )
        if not args[0].elements:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "tupleLast() called on an empty tuple",
                    exec_ctx,
                )
            )
        return RTResult().success(args[0].elements[-1])

    def execute_tupleJsonArray(self, args, exec_ctx):
        import json as _json

        if len(args) != 1 or not isinstance(args[0], LynxTuple):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "tupleJsonArray(tuple) expects a tuple",
                    exec_ctx,
                )
            )
        try:
            items = [_json_value(element) for element in args[0].elements]
            return RTResult().success(String(_json.dumps(items)))
        except Exception as e:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    f"tupleJsonArray() failed: {e}",
                    exec_ctx,
                )
            )

    def _tuple_values(
        self, args: list[Any], exec_ctx: Any, name: str
    ) -> tuple[tuple[Any, ...] | None, Any]:
        if len(args) != 1 or not isinstance(args[0], LynxTuple):
            return None, self._failure(exec_ctx, f"{name}(tuple) expects a tuple")
        return args[0].elements, None

    def execute_tupleReverse(self, args, exec_ctx):
        values, error = self._tuple_values(args, exec_ctx, "tupleReverse")
        if error:
            return error
        assert values is not None
        return RTResult().success(LynxTuple(reversed(values)))

    def execute_tupleSort(self, args, exec_ctx):
        if len(args) not in (1, 2) or not isinstance(args[0], LynxTuple):
            return self._failure(
                exec_ctx,
                "tupleSort(tuple) or tupleSort(tuple, reverse) expects a tuple",
            )
        reverse = args[1].is_true() if len(args) == 2 else False
        try:
            return RTResult().success(
                LynxTuple(sorted(args[0].elements, key=self._list_sort_key, reverse=reverse))
            )
        except Exception as exc:
            return self._failure(exec_ctx, f"tupleSort() failed: {exc}")

    def execute_tupleSortDesc(self, args, exec_ctx):
        if len(args) != 1:
            return self._failure(exec_ctx, "tupleSortDesc(tuple) expects a tuple")
        return self.execute_tupleSort([args[0], Number(1, is_bool=True)], exec_ctx)

    def execute_tupleMin(self, args, exec_ctx):
        values, error = self._tuple_values(args, exec_ctx, "tupleMin")
        if error:
            return error
        if not values:
            return self._failure(exec_ctx, "tupleMin() called on an empty tuple")
        try:
            return RTResult().success(min(values, key=self._list_sort_key))
        except Exception as exc:
            return self._failure(exec_ctx, f"tupleMin() failed: {exc}")

    def execute_tupleMax(self, args, exec_ctx):
        values, error = self._tuple_values(args, exec_ctx, "tupleMax")
        if error:
            return error
        if not values:
            return self._failure(exec_ctx, "tupleMax() called on an empty tuple")
        try:
            return RTResult().success(max(values, key=self._list_sort_key))
        except Exception as exc:
            return self._failure(exec_ctx, f"tupleMax() failed: {exc}")

    def execute_tupleSum(self, args, exec_ctx):
        values, error = self._tuple_values(args, exec_ctx, "tupleSum")
        if error:
            return error
        assert values is not None
        return RTResult().success(
            Number(sum(element.value for element in values if isinstance(element, Number)))
        )

    def execute_tupleAny(self, args, exec_ctx):
        values, error = self._tuple_values(args, exec_ctx, "tupleAny")
        if error:
            return error
        assert values is not None
        return RTResult().success(Number(int(any(value.is_true() for value in values)), is_bool=True))

    def execute_tupleAll(self, args, exec_ctx):
        values, error = self._tuple_values(args, exec_ctx, "tupleAll")
        if error:
            return error
        assert values is not None
        return RTResult().success(Number(int(all(value.is_true() for value in values)), is_bool=True))

    def execute_tupleUnique(self, args, exec_ctx):
        values, error = self._tuple_values(args, exec_ctx, "tupleUnique")
        if error:
            return error
        assert values is not None
        seen = set()
        unique = []
        for value in values:
            key = str(value)
            if key not in seen:
                seen.add(key)
                unique.append(value)
        return RTResult().success(LynxTuple(unique))

    def execute_tupleMean(self, args, exec_ctx):
        values, error = self._tuple_values(args, exec_ctx, "tupleMean")
        if error:
            return error
        assert values is not None
        numbers = [value.value for value in values if isinstance(value, Number)]
        return RTResult().success(Number(sum(numbers) / len(numbers) if numbers else 0.0))

    def execute_tupleFlatten(self, args, exec_ctx):
        values, error = self._tuple_values(args, exec_ctx, "tupleFlatten")
        if error:
            return error
        assert values is not None
        flattened = []
        for value in values:
            if isinstance(value, LynxTuple):
                flattened.extend(value.elements)
            else:
                flattened.append(value)
        return RTResult().success(LynxTuple(flattened))

    def execute_tupleZip(self, args, exec_ctx):
        if (
            len(args) != 2
            or not isinstance(args[0], LynxTuple)
            or not isinstance(args[1], LynxTuple)
        ):
            return self._failure(exec_ctx, "tupleZip(tuple1, tuple2) expects two tuples")
        import json as _json

        pairs = []
        for left, right in zip(args[0].elements, args[1].elements):
            pairs.append(
                String(
                    _json.dumps(
                        {"a": _json_value(left), "b": _json_value(right)}
                    )
                )
            )
        return RTResult().success(List(pairs))

    def execute_tupleJoin(self, args, exec_ctx):
        if (
            len(args) != 2
            or not isinstance(args[0], LynxTuple)
            or not isinstance(args[1], String)
        ):
            return self._failure(
                exec_ctx, "tupleJoin(tuple, separator) expects a tuple and string separator"
            )
        return RTResult().success(
            String(args[1].value.join(str(value) for value in args[0].elements))
        )

    # async built-ins

    def execute_asyncRun(self, args, exec_ctx):
        """asyncRun(coro) — run a coroutine."""
        import asyncio

        if len(args) != 1 or not isinstance(args[0], CoroutineValue):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "asyncRun(coro) expects a single coroutine argument "
                    "(the result of calling an 'async' function)",
                    exec_ctx,
                )
            )
        try:
            coro_res = asyncio.run(args[0].coro)
        except Exception as e:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    f"asyncRun() raised an exception: {type(e).__name__}: {e}",
                    exec_ctx,
                )
            )
        if isinstance(coro_res, RTResult):
            if coro_res.error:
                return RTResult().failure(coro_res.error)
            return RTResult().success(
                coro_res.value if coro_res.value is not None else Number.null
            )
        return RTResult().success(Number.null)

    def execute_asyncGather(self, args, exec_ctx):
        """asyncGather(coro1, coro2, ...) — return a coroutine."""
        for i, arg in enumerate(args):
            if not isinstance(arg, CoroutineValue):
                return RTResult().failure(
                    RTError(
                        self.pos_start,
                        self.pos_end,
                        f"asyncGather() argument {i + 1} is not a coroutine "
                        "(expected the result of calling an 'async' function)",
                        exec_ctx,
                    )
                )

        import asyncio

        coros = [arg.coro for arg in args]

        async def _gather():
            results = await asyncio.gather(*coros)
            elements = []
            for r in results:
                if isinstance(r, RTResult):
                    if r.error:
                        return r
                    elements.append(r.value if r.value is not None else Number.null)
                else:
                    elements.append(Number.null)
            return RTResult().success(List(elements))

        return RTResult().success(CoroutineValue(_gather()))

    def execute_sleep(self, args, exec_ctx):
        """sleep(seconds) — block the current execution for a number of seconds."""
        import time

        if len(args) != 1 or not isinstance(args[0], Number) or args[0].is_bool:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "sleep(num) expects exactly one int or float argument",
                    exec_ctx,
                )
            )

        seconds = float(args[0].value)
        if seconds < 0:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "sleep(num) cannot use a negative number of seconds",
                    exec_ctx,
                )
            )

        time.sleep(seconds)
        return RTResult().success(Number.null)

    def execute_asyncSleep(self, args, exec_ctx):
        """asyncSleep(seconds) — return a coroutine."""
        import asyncio

        if len(args) != 1 or not isinstance(args[0], Number):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "asyncSleep(seconds) expects a single numeric argument",
                    exec_ctx,
                )
            )
        seconds = args[0].value

        async def _sleep():
            await asyncio.sleep(seconds)
            return RTResult().success(Number.null)

        return RTResult().success(CoroutineValue(_sleep()))

    def execute_foreverDelay(self, args, exec_ctx):
        """foreverDelay(seconds) — configure the delay used by forever()."""
        if not _runtime._setup_in_progress:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "foreverDelay() may only be called inside global setup(){}",
                    exec_ctx,
                )
            )
        if len(args) != 1 or not isinstance(args[0], Number):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "foreverDelay(seconds) expects exactly one number",
                    exec_ctx,
                )
            )
        delay = float(args[0].value)
        if delay < 0:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "foreverDelay(seconds) cannot be negative",
                    exec_ctx,
                )
            )
        _runtime._forever_delay = delay
        return RTResult().success(Number.null)

    def execute_suppressForeverWarning(self, args, exec_ctx):
        """Suppress the warning for forever() bodies without break."""
        if not _runtime._setup_in_progress:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "suppressForeverWarning() may only be called inside global setup(){}",
                    exec_ctx,
                )
            )
        if args:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "suppressForeverWarning() takes no arguments",
                    exec_ctx,
                )
            )
        _runtime._forever_warning_suppressed = True
        return RTResult().success(Number.null)

    def execute_suppressDeprecationWarning(self, args, exec_ctx):
        """Suppress legacy syntax deprecation warnings for this run."""
        if not _runtime._setup_in_progress:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "suppressDeprecationWarning() may only be called inside global setup(){}",
                    exec_ctx,
                )
            )
        if args:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "suppressDeprecationWarning() takes no arguments",
                    exec_ctx,
                )
            )
        _runtime._deprecation_warning_suppressed = True
        return RTResult().success(Number.null)

    def execute_overrideMain(self, args, exec_ctx):
        """overrideMain("funcName") — redirect the program."""
        if len(args) != 1 or not isinstance(args[0], String):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "overrideMain() expects exactly one string argument — "
                    "the name of the global function to use as the program entry point.\n"
                    '  Example:  overrideMain("start");',
                    exec_ctx,
                )
            )
        _runtime._main_override = args[0].value
        return RTResult().success(Number.null)

    def execute_assert(self, args, exec_ctx):
        """assert(condition[, message]) — fail when condition is false."""
        if len(args) not in (1, 2) or not isinstance(args[0], Number):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "assert(condition[, message]) expects a boolean or number "
                    "condition and an optional string message",
                    exec_ctx,
                )
            )
        if len(args) == 2 and not isinstance(args[1], String):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "assert(condition[, message]) expects message to be a string",
                    exec_ctx,
                )
            )
        if args[0].value == 0:
            message = args[1].value if len(args) == 2 else "Assertion failed"
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    message,
                    exec_ctx,
                )
            )
        return RTResult().success(Number.null)


BuiltinHandler = Callable[[BuiltInFunction, list[Any], Any], Any]

# Keep this list as the single source of truth for functions available to both
# programs and imported modules.  Adding a function here and registering its
# handler below is all that is needed to expose it everywhere.
BUILTIN_FUNCTION_NAMES = (
    "print",
    "println",
    "input",
    "inputln",
    "rawPy",
    "rawPyx",
    "strOf",
    "intOf",
    "floatOf",
    "sentinel",
    "object",
    "returnType",
    "returnLength",
    "seqFromTo",
    "range",
    "cleanRawPyxCache",
    "listJsonArray",
    "listJsonObject",
    "splitStr",
    "listFlatten",
    "listUnique",
    "listPush",
    "listPop",
    "listGet",
    "listSet",
    "listSlice",
    "listContains",
    "contains",
    "listJoin",
    "listIndex",
    "listRemove",
    "anyOf",
    "allOf",
    "sumOf",
    "sortList",
    "reverseList",
    "listMin",
    "listMax",
    "listFirst",
    "listLast",
    "listHead",
    "listTail",
    "listCount",
    "listExtend",
    "listInsert",
    "listClear",
    "listRepeat",
    "listAvg",
    "listZip",
    "asyncRun",
    "asyncGather",
    "sleep",
    "asyncSleep",
    "foreverDelay",
    "suppressForeverWarning",
    "suppressDeprecationWarning",
    "tupleCreate",
    "tupleGet",
    "tupleLen",
    "tupleContains",
    "tupleIndex",
    "tupleSlice",
    "tupleToList",
    "listToTuple",
    "tupleConcat",
    "tupleCount",
    "tupleFirst",
    "tupleLast",
    "tupleJsonArray",
    "tupleReverse",
    "tupleSort",
    "tupleSortDesc",
    "tupleMin",
    "tupleMax",
    "tupleSum",
    "tupleAny",
    "tupleAll",
    "tupleUnique",
    "tupleMean",
    "tupleFlatten",
    "tupleZip",
    "tupleJoin",
    "assert",
    "overrideMain",
    "unshare",
    "getAddress",
    "modifyAddressValue",
    "getAddressValue",
    "memoryTypeSize",
    "memoryBlockAllocate",
    "memoryBlockView",
    "memoryBlockGet",
    "memoryBlockSet",
    "memoryBlockLength",
    "memoryArrayAllocate",
    "memoryArrayView",
    "memoryArrayGet",
    "memoryArraySet",
    "memoryArrayLength",
    "memoryViewGet",
    "memoryViewSet",
    "memoryViewLength",
    "memoryStructSize",
    "memoryStructFieldOffset",
    "memoryStructFieldSize",
    "memoryStructAllocate",
    "memoryStructGet",
    "memoryStructSet",
    "nativeStructSize",
    "nativeStructAllocate",
    "nativeStructFieldOffset",
    "nativeStructFieldSize",
    "nativeStructGet",
    "nativeStructSet",
    "memoryAllocate",
    "memoryCallocate",
    "memoryReallocate",
    "memoryFree",
    "memorySet",
    "memoryCopy",
    "memoryReadInt32",
    "memoryWriteInt32",
    "memoryReadInt8",
    "memoryWriteInt8",
    "memoryReadInt16",
    "memoryWriteInt16",
    "memoryReadInt64",
    "memoryWriteInt64",
    "memoryReadUInt8",
    "memoryWriteUInt8",
    "memoryReadUInt16",
    "memoryWriteUInt16",
    "memoryReadUInt32",
    "memoryWriteUInt32",
    "memoryReadUInt64",
    "memoryWriteUInt64",
    "memoryReadFloat32",
    "memoryWriteFloat32",
    "memoryReadFloat64",
    "memoryWriteFloat64",
    "malloc",
    "calloc",
    "realloc",
    "free",
    "memset",
    "memcpy",
    "readByte",
    "writeByte",
    "readInt8",
    "writeInt8",
    "readInt16",
    "writeInt16",
    "readInt32",
    "writeInt32",
    "readInt64",
    "writeInt64",
    "readUInt8",
    "writeUInt8",
    "readUInt16",
    "writeUInt16",
    "readUInt32",
    "writeUInt32",
    "readUInt64",
    "writeUInt64",
    "readFloat32",
    "writeFloat32",
    "readFloat64",
    "writeFloat64",
    "sizeOf",
)


BUILTIN_FUNCTIONS: dict[str, BuiltInFunction] = {}


def register_builtin(name: str, handler: BuiltinHandler | None = None) -> BuiltInFunction:
    """Register and return a built-in function.

    ``handler`` is an optional callable receiving ``(builtin, args,
    exec_ctx)`` and returning an ``RTResult``. The common in-tree case is
    adding a name whose ``execute_<name>`` method is defined above.
    """
    if not name.isidentifier():
        raise ValueError(f"Invalid builtin name: {name!r}")
    if handler is not None:
        setattr(BuiltInFunction, f"execute_{name}", handler)
    function = BuiltInFunction(name)
    setattr(BuiltInFunction, name, function)
    BUILTIN_FUNCTIONS[name] = function
    # The global table is created after this module is imported, so the
    # startup registrations are installed by lynxer.py.  Extensions added
    # later should become available immediately as well.
    global_symbol_table = getattr(_runtime, "global_symbol_table", None)
    if global_symbol_table is not None:
        global_symbol_table.set(name, function)
    return function


def register_builtins(symbol_table: Any) -> None:
    """Install every registered builtin into a Lynxer symbol table."""
    for name, function in BUILTIN_FUNCTIONS.items():
        symbol_table.set(name, function)


def builtin(name: str) -> Callable[[BuiltinHandler], BuiltinHandler]:
    """Decorator for adding a new builtin implementation in this module."""
    def decorator(handler: BuiltinHandler) -> BuiltinHandler:
        register_builtin(name, handler)
        return handler

    return decorator


# Create the public instances from the complete implementation above.
for _name in BUILTIN_FUNCTION_NAMES:
    register_builtin(_name)

# If this module was imported first, lynxer.py had to defer registration
# while this module was still being initialized. Complete it now that all
# BuiltInFunction instances exist.
if getattr(_runtime, "_builtins_registration_deferred", False):
    _runtime._register_builtins(_runtime.global_symbol_table)
