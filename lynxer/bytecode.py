"""Lynxer bytecode compilation, loading, and execution.

The payload in a ``.lynxc`` file is a zlib-compressed stack-machine instruction
stream.  It is not a pickle and it does not contain a Python object graph.
Instructions push constants, build containers, and construct nodes from a fixed
class table.  The resulting program node is then handed to the existing Lynxer
runtime.  Runtime classes are imported lazily so this module can be imported by
``lynxer.py`` without creating a circular import during interpreter startup.
"""

from __future__ import annotations

import hashlib
import os
import re
import struct
import time
import zlib
from typing import Any

BYTECODE_MAGIC = b"LYNXC\x00"
BYTECODE_VERSION = 8
MAX_BYTECODE_FILE_SIZE = 64 * 1024 * 1024
MAX_BYTECODE_PAYLOAD_SIZE = 256 * 1024 * 1024
_NATIVE_IMPORT_RE = re.compile(
    r"""(?:import|importAs)\s*\(\s*["']([^"']+\.(?:so|dylib|dll))["']""",
    re.IGNORECASE,
)

# ---------------------------------------------------------------------------
# Binary payload format
# ---------------------------------------------------------------------------

_TAG_NONE = 0x00
_TAG_FALSE = 0x01
_TAG_TRUE = 0x02
_TAG_INT = 0x03
_TAG_FLOAT = 0x04
_TAG_COMPLEX = 0x05
_TAG_STR = 0x06
_TAG_BYTES = 0x07
_TAG_LIST = 0x08
_TAG_TUPLE = 0x09
_TAG_DICT = 0x0A
_TAG_SET = 0x0B
_TAG_FROZENSET = 0x0C
_TAG_REF = 0x0D
_TAG_OBJECT = 0x0E
_TAG_POSITION = 0x0F

_DOUBLE = struct.Struct("<d")
# Integer literals are arbitrary-precision, so the cap only has to be large
# enough to stay well clear of Python's own int-parsing limit; running off the
# end of the payload is caught by the read itself.
_MAX_VARINT_BITS = 1 << 16
_MAX_OBJECT_ATTRS = 4096
_REGISTRY_CACHE: Any = None

# The compiled-program portion of the payload is a postfix stack machine.  The
# metadata dictionary still uses the small tagged-value encoder below, but AST
# objects are never passed to that encoder.  Keeping the instruction opcodes
# separate makes it possible to inspect a .lynxc file without treating it as a
# Python serialization format.
_OP_NONE = 0x20
_OP_FALSE = 0x21
_OP_TRUE = 0x22
_OP_INT = 0x23
_OP_FLOAT = 0x24
_OP_COMPLEX = 0x25
_OP_STR = 0x26
_OP_BYTES = 0x27
_OP_BUILD_LIST = 0x28
_OP_BUILD_TUPLE = 0x29
_OP_BUILD_DICT = 0x2A
_OP_BUILD_SET = 0x2B
_OP_BUILD_FROZENSET = 0x2C
_OP_BUILD_POSITION = 0x2D
_OP_BUILD_NODE = 0x2E
_MAX_INSTRUCTIONS = 16 * 1024 * 1024


def _source_hash(text: str) -> str:
    return hashlib.sha256(text.encode("utf-8")).hexdigest()


def _compile_options(optimize: bool) -> dict[str, Any]:
    return {"optimize": bool(optimize)}


def _is_cache_current(data: dict[str, Any], fn: str, text: str, optimize: bool) -> bool:
    return (
        data.get("source") == os.path.abspath(fn)
        and data.get("source_hash") == _source_hash(text)
        and data.get("compiler_options") == _compile_options(optimize)
    )


def _optimize_program(node: Any) -> Any:
    """Return an optimized AST.

    This is intentionally conservative for now. The hook makes optimized and
    unoptimized compilation explicit without risking behavior changes in the
    interpreter's mutable AST nodes.
    """
    return node


def _runtime() -> Any:
    """Return the interpreter module only when a bytecode operation needs it."""
    from . import lynxer

    return lynxer


def _registry() -> tuple[list[type[Any]], dict[type, int]]:
    """Return ``([classes], {class: id})`` for every encodable AST type.

    Ids are assigned over the sorted class names, so a given Lynxer runtime
    derives the same table whether it is writing or reading a payload.
    ``Position`` is excluded: it carries the whole source text and is encoded
    by its own tag instead.
    """
    global _REGISTRY_CACHE
    runtime = _runtime()
    if _REGISTRY_CACHE is None or _REGISTRY_CACHE[0] is not runtime:
        names = sorted(
            name
            for name, value in vars(runtime).items()
            if isinstance(value, type)
            and name != "Position"
            and (name.endswith("Node") or name == "Token")
        )
        classes = [getattr(runtime, name) for name in names]
        _REGISTRY_CACHE = (runtime, classes, {cls: i for i, cls in enumerate(classes)})
    return _REGISTRY_CACHE[1], _REGISTRY_CACHE[2]


def _write_varint(out: bytearray, value: int) -> None:
    while True:
        byte = value & 0x7F
        value >>= 7
        if value:
            out.append(byte | 0x80)
        else:
            out.append(byte)
            return


def _write_string(out: bytearray, value: str) -> None:
    raw = value.encode("utf-8")
    out.append(_TAG_STR)
    _write_varint(out, len(raw))
    out += raw


class _Encoder:
    """Serialise bytecode metadata into the tagged metadata stream."""

    def __init__(self) -> None:
        self.out = bytearray()
        self.memo: dict[int, int] = {}
        self._interpreter: Any = None

    def encode(self, value: Any) -> bytes:
        self.write(value)
        return bytes(self.out)

    def _reference(self, value: Any) -> int | None:
        """Reserve a memo slot for *value*; return an existing index if it has one."""
        key = id(value)
        index = self.memo.get(key)
        if index is not None:
            self.out.append(_TAG_REF)
            _write_varint(self.out, index)
            return index
        self.memo[key] = len(self.memo)
        return None

    def write(self, value: Any) -> None:
        out = self.out

        if value is None:
            out.append(_TAG_NONE)
            return

        value_type = type(value)

        if value_type is bool:
            out.append(_TAG_TRUE if value else _TAG_FALSE)
            return
        if value_type is int:
            out.append(_TAG_INT)
            _write_varint(out, value << 1 if value >= 0 else (~value << 1) | 1)
            return
        if value_type is float:
            out.append(_TAG_FLOAT)
            out += _DOUBLE.pack(value)
            return
        if value_type is complex:
            out.append(_TAG_COMPLEX)
            out += _DOUBLE.pack(value.real)
            out += _DOUBLE.pack(value.imag)
            return
        if value_type is str:
            _write_string(out, value)
            return
        if value_type is bytes:
            out.append(_TAG_BYTES)
            _write_varint(out, len(value))
            out += value
            return

        if value_type is list or value_type is tuple or value_type is dict:
            if self._reference(value) is not None:
                return
            if value_type is list:
                out.append(_TAG_LIST)
            elif value_type is tuple:
                out.append(_TAG_TUPLE)
            else:
                out.append(_TAG_DICT)
            _write_varint(out, len(value))
            if value_type is dict:
                for key, item in value.items():
                    self.write(key)
                    self.write(item)
            else:
                for item in value:
                    self.write(item)
            return

        if value_type is set or value_type is frozenset:
            if self._reference(value) is not None:
                return
            out.append(_TAG_SET if value_type is set else _TAG_FROZENSET)
            _write_varint(out, len(value))
            for item in value:
                self.write(item)
            return

        if value_type is self.interpreter().Position:
            # The source text is deliberately left out; only the location is
            # kept, so a compiled program does not embed its own source.
            out.append(_TAG_POSITION)
            self.write(value.idx)
            self.write(value.ln)
            self.write(value.col)
            self.write(value.fn)
            return

        class_id = _registry()[1].get(value_type)
        if class_id is not None:
            if self._reference(value) is not None:
                return
            out.append(_TAG_OBJECT)
            _write_varint(out, class_id)
            state = vars(value)
            _write_varint(out, len(state))
            for name, attribute in state.items():
                _write_string(out, name)
                self.write(attribute)
            return

        raise ValueError(
            f"cannot encode a value of type {value_type.__name__!r} into Lynxer bytecode"
        )

    def interpreter(self) -> Any:
        if self._interpreter is None:
            self._interpreter = _runtime()
        return self._interpreter


class _Decoder:
    """Read the tagged metadata stream back into Python values."""

    def __init__(self, data: bytes) -> None:
        self.data = data
        self.pos = 0
        self.memo: list[Any] = []
        self._interpreter: Any = None

    def decode(self) -> Any:
        value = self.read()
        if self.pos != len(self.data):
            raise ValueError("trailing data in bytecode payload")
        return value

    def _read(self, count: int) -> bytes:
        end = self.pos + count
        if count < 0 or end > len(self.data):
            raise ValueError("truncated bytecode payload")
        chunk = self.data[self.pos:end]
        self.pos = end
        return chunk

    def _byte(self) -> int:
        if self.pos >= len(self.data):
            raise ValueError("truncated bytecode payload")
        byte = self.data[self.pos]
        self.pos += 1
        return byte

    def _varint(self) -> int:
        result = 0
        shift = 0
        while True:
            byte = self._byte()
            result |= (byte & 0x7F) << shift
            if not byte & 0x80:
                return result
            shift += 7
            if shift > _MAX_VARINT_BITS:
                raise ValueError("integer in bytecode payload is too large")

    def _count(self) -> int:
        count = self._varint()
        if count > len(self.data):
            raise ValueError("container declares more items than the payload holds")
        return count

    def read(self) -> Any:
        tag = self._byte()

        if tag == _TAG_NONE:
            return None
        if tag == _TAG_TRUE:
            return True
        if tag == _TAG_FALSE:
            return False
        if tag == _TAG_INT:
            number = self._varint()
            return -(number >> 1) - 1 if number & 1 else number >> 1
        if tag == _TAG_FLOAT:
            return _DOUBLE.unpack(self._read(8))[0]
        if tag == _TAG_COMPLEX:
            return complex(
                _DOUBLE.unpack(self._read(8))[0], _DOUBLE.unpack(self._read(8))[0]
            )
        if tag == _TAG_STR:
            return self._read(self._varint()).decode("utf-8")
        if tag == _TAG_BYTES:
            return self._read(self._varint())

        if tag == _TAG_LIST:
            items: list[Any] = []
            self.memo.append(items)
            items.extend(self.read() for _ in range(self._count()))
            return items
        if tag == _TAG_TUPLE:
            index = len(self.memo)
            self.memo.append(None)
            result = tuple(self.read() for _ in range(self._count()))
            self.memo[index] = result
            return result
        if tag == _TAG_SET:
            items_set: set[Any] = set()
            self.memo.append(items_set)
            for _ in range(self._count()):
                items_set.add(self.read())
            return items_set
        if tag == _TAG_FROZENSET:
            index = len(self.memo)
            self.memo.append(None)
            result_frozen = frozenset(self.read() for _ in range(self._count()))
            self.memo[index] = result_frozen
            return result_frozen
        if tag == _TAG_DICT:
            mapping: dict[Any, Any] = {}
            self.memo.append(mapping)
            for _ in range(self._count()):
                key = self.read()
                mapping[key] = self.read()
            return mapping
        if tag == _TAG_REF:
            index = self._varint()
            if index >= len(self.memo):
                raise ValueError("invalid back-reference in bytecode payload")
            return self.memo[index]

        if tag == _TAG_POSITION:
            runtime = self.interpreter()
            idx = self.read()
            ln = self.read()
            col = self.read()
            fn = self.read()
            return runtime.Position(idx, ln, col, fn, "")

        if tag == _TAG_OBJECT:
            classes = _registry()[0]
            class_id = self._varint()
            if class_id >= len(classes):
                raise ValueError(
                    f"unknown AST node type id {class_id} in bytecode payload"
                )
            node_class = classes[class_id]
            node: Any = object.__new__(node_class)
            self.memo.append(node)
            count = self._varint()
            if count > _MAX_OBJECT_ATTRS:
                raise ValueError("AST node declares too many attributes")
            state = {}
            for _ in range(count):
                name = self.read()
                if not isinstance(name, str):
                    raise ValueError("malformed AST node attribute name")  # noqa: TRY004
                state[name] = self.read()
            node.__dict__.update(state)
            return node

        raise ValueError(f"unknown bytecode tag 0x{tag:02x}")

    def interpreter(self) -> Any:
        if self._interpreter is None:
            self._interpreter = _runtime()
        return self._interpreter


def _emit_instruction_string(out: bytearray, value: str) -> None:
    raw = value.encode("utf-8")
    out.append(_OP_STR)
    _write_varint(out, len(raw))
    out += raw


def _emit_instruction_value(out: bytearray, value: Any) -> None:
    """Compile one AST value into postfix stack-machine instructions."""
    value_type = type(value)

    if value is None:
        out.append(_OP_NONE)
        return
    if value_type is bool:
        out.append(_OP_TRUE if value else _OP_FALSE)
        return
    if value_type is int:
        out.append(_OP_INT)
        _write_varint(out, value << 1 if value >= 0 else (~value << 1) | 1)
        return
    if value_type is float:
        out.append(_OP_FLOAT)
        out += _DOUBLE.pack(value)
        return
    if value_type is complex:
        out.append(_OP_COMPLEX)
        out += _DOUBLE.pack(value.real)
        out += _DOUBLE.pack(value.imag)
        return
    if value_type is str:
        _emit_instruction_string(out, value)
        return
    if value_type is bytes:
        out.append(_OP_BYTES)
        _write_varint(out, len(value))
        out += value
        return

    if value_type is _runtime().Position:
        _emit_instruction_value(out, value.idx)
        _emit_instruction_value(out, value.ln)
        _emit_instruction_value(out, value.col)
        _emit_instruction_value(out, value.fn)
        out.append(_OP_BUILD_POSITION)
        return

    if value_type is list or value_type is tuple:
        for item in value:
            _emit_instruction_value(out, item)
        out.append(_OP_BUILD_LIST if value_type is list else _OP_BUILD_TUPLE)
        _write_varint(out, len(value))
        return

    if value_type is dict:
        for key, item in value.items():
            _emit_instruction_value(out, key)
            _emit_instruction_value(out, item)
        out.append(_OP_BUILD_DICT)
        _write_varint(out, len(value))
        return

    if value_type is set or value_type is frozenset:
        # AST values are deterministic apart from sets.  Sorting their
        # instruction representation keeps cache artifacts reproducible.
        items = list(value)
        items.sort(key=repr)
        for item in items:
            _emit_instruction_value(out, item)
        out.append(_OP_BUILD_SET if value_type is set else _OP_BUILD_FROZENSET)
        _write_varint(out, len(value))
        return

    class_id = _registry()[1].get(value_type)
    if class_id is not None:
        state = vars(value)
        for name, attribute in state.items():
            _emit_instruction_string(out, name)
            _emit_instruction_value(out, attribute)
        out.append(_OP_BUILD_NODE)
        _write_varint(out, class_id)
        _write_varint(out, len(state))
        return

    raise ValueError(
        f"cannot compile a value of type {value_type.__name__!r} into Lynxer bytecode"
    )


def _compile_instruction_stream(node: Any) -> bytes:
    """Compile the parsed program into a real postfix instruction stream."""
    instructions = bytearray()
    _emit_instruction_value(instructions, node)
    if len(instructions) > _MAX_INSTRUCTIONS:
        raise ValueError("compiled Lynxer instruction stream is too large")
    return bytes(instructions)


class _InstructionReader:
    """Execute the serialized instruction stream into safe runtime values."""

    def __init__(self, code: bytes) -> None:
        self.code = code
        self.pos = 0
        self.stack: list[Any] = []
        self.instruction_count = 0

    def _read(self, count: int) -> bytes:
        end = self.pos + count
        if count < 0 or end > len(self.code):
            raise ValueError("truncated bytecode instruction stream")
        chunk = self.code[self.pos:end]
        self.pos = end
        return chunk

    def _byte(self) -> int:
        if self.pos >= len(self.code):
            raise ValueError("truncated bytecode instruction stream")
        byte = self.code[self.pos]
        self.pos += 1
        return byte

    def _varint(self) -> int:
        result = 0
        shift = 0
        while True:
            byte = self._byte()
            result |= (byte & 0x7F) << shift
            if not byte & 0x80:
                return result
            shift += 7
            if shift > _MAX_VARINT_BITS:
                raise ValueError("integer in bytecode instruction stream is too large")

    def _count(self) -> int:
        count = self._varint()
        if count > len(self.code):
            raise ValueError(
                "instruction declares more values than the bytecode holds"
            )
        return count

    def _pop_values(self, count: int) -> list[Any]:
        if count > len(self.stack):
            raise ValueError("bytecode instruction stack underflow")
        if not count:
            return []
        values = self.stack[-count:]
        del self.stack[-count:]
        return values

    def _read_string(self) -> str:
        try:
            return self._read(self._varint()).decode("utf-8")
        except UnicodeDecodeError as exc:
            raise ValueError("bytecode instruction contains invalid UTF-8") from exc

    def run(self) -> Any:
        while self.pos < len(self.code):
            self.instruction_count += 1
            if self.instruction_count > _MAX_INSTRUCTIONS:
                raise ValueError("bytecode instruction stream contains too many instructions")
            opcode = self._byte()

            if opcode == _OP_NONE:
                self.stack.append(None)
            elif opcode == _OP_FALSE:
                self.stack.append(False)
            elif opcode == _OP_TRUE:
                self.stack.append(True)
            elif opcode == _OP_INT:
                number = self._varint()
                self.stack.append(-(number >> 1) - 1 if number & 1 else number >> 1)
            elif opcode == _OP_FLOAT:
                self.stack.append(_DOUBLE.unpack(self._read(8))[0])
            elif opcode == _OP_COMPLEX:
                self.stack.append(
                    complex(
                        _DOUBLE.unpack(self._read(8))[0],
                        _DOUBLE.unpack(self._read(8))[0],
                    )
                )
            elif opcode == _OP_STR:
                self.stack.append(self._read_string())
            elif opcode == _OP_BYTES:
                self.stack.append(self._read(self._varint()))
            elif opcode in (
                _OP_BUILD_LIST,
                _OP_BUILD_TUPLE,
                _OP_BUILD_SET,
                _OP_BUILD_FROZENSET,
            ):
                count = self._count()
                values = self._pop_values(count)
                if opcode == _OP_BUILD_LIST:
                    self.stack.append(values)
                elif opcode == _OP_BUILD_TUPLE:
                    self.stack.append(tuple(values))
                elif opcode == _OP_BUILD_SET:
                    try:
                        self.stack.append(set(values))
                    except TypeError as exc:
                        raise ValueError("bytecode built an invalid set") from exc
                else:
                    try:
                        self.stack.append(frozenset(values))
                    except TypeError as exc:
                        raise ValueError("bytecode built an invalid frozenset") from exc
            elif opcode == _OP_BUILD_DICT:
                count = self._count()
                values = self._pop_values(count * 2)
                mapping: dict[Any, Any] = {}
                try:
                    for index in range(0, len(values), 2):
                        mapping[values[index]] = values[index + 1]
                except (IndexError, TypeError) as exc:
                    raise ValueError("bytecode built an invalid dictionary") from exc
                self.stack.append(mapping)
            elif opcode == _OP_BUILD_POSITION:
                values = self._pop_values(4)
                if (
                    not all(isinstance(value, int) for value in values[:3])
                    or not isinstance(values[3], str)
                ):
                    raise ValueError("bytecode built an invalid source position")
                runtime = _runtime()
                self.stack.append(runtime.Position(*values, ""))
            elif opcode == _OP_BUILD_NODE:
                classes = _registry()[0]
                class_id = self._varint()
                if class_id >= len(classes):
                    raise ValueError(
                        f"unknown AST node type id {class_id} in bytecode instruction stream"
                    )
                count = self._varint()
                if count > _MAX_OBJECT_ATTRS:
                    raise ValueError("AST node declares too many attributes")
                values = self._pop_values(count * 2)
                node_class = classes[class_id]
                node: Any = object.__new__(node_class)
                state = {}
                try:
                    for index in range(0, len(values), 2):
                        name = values[index]
                        if not isinstance(name, str):
                            raise ValueError("malformed AST node attribute name")  # noqa: TRY004
                        state[name] = values[index + 1]
                except IndexError as exc:
                    raise ValueError("truncated AST node instruction") from exc
                node.__dict__.update(state)
                self.stack.append(node)
            else:
                raise ValueError(f"unknown bytecode instruction 0x{opcode:02x}")

        if len(self.stack) != 1:
            raise ValueError("bytecode instruction stream did not produce one program")
        return self.stack[0]


def _decode_instruction_stream(code: bytes) -> Any:
    try:
        return _InstructionReader(code).run()
    except RecursionError as exc:
        raise ValueError("bytecode instruction stream is nested too deeply") from exc


def _encode_payload(data: dict[str, Any]) -> bytes:
    return _Encoder().encode(data)


def _decode_payload(raw: bytes) -> Any:
    try:
        return _Decoder(raw).decode()
    except RecursionError as exc:
        raise ValueError("bytecode payload is nested too deeply") from exc


def _read_bytecode(fn: str) -> tuple[dict[str, Any], int, int]:
    """Read, decompress, validate, and decode a ``.lynxc`` file."""
    try:
        file_size = os.path.getsize(fn)
    except OSError as exc:
        raise ValueError(f"could not read '{fn}': {exc}") from exc
    if file_size > MAX_BYTECODE_FILE_SIZE:
        raise ValueError(
            f"'{fn}' is too large ({file_size:,} bytes); "
            f"the maximum supported bytecode file is {MAX_BYTECODE_FILE_SIZE:,} bytes"
        )

    try:
        with open(fn, "rb") as bytecode_file:
            magic = bytecode_file.read(len(BYTECODE_MAGIC))
            if magic != BYTECODE_MAGIC:
                raise ValueError(
                    f"'{fn}' is not a valid Lynxer bytecode file "
                    f"(bad magic bytes: {magic!r})"
                )
            compressed = bytecode_file.read()
    except OSError as exc:
        raise ValueError(f"could not read '{fn}': {exc}") from exc

    try:
        decompressor = zlib.decompressobj()
        raw = decompressor.decompress(compressed, MAX_BYTECODE_PAYLOAD_SIZE + 1)
        if len(raw) > MAX_BYTECODE_PAYLOAD_SIZE or decompressor.unconsumed_tail:
            raise ValueError(
                f"'{fn}' expands beyond the maximum supported bytecode payload "
                f"of {MAX_BYTECODE_PAYLOAD_SIZE:,} bytes"
            )
        raw += decompressor.flush()
        if len(raw) > MAX_BYTECODE_PAYLOAD_SIZE:
            raise ValueError(
                f"'{fn}' expands beyond the maximum supported bytecode payload "
                f"of {MAX_BYTECODE_PAYLOAD_SIZE:,} bytes"
            )
        if decompressor.unused_data:
            raise ValueError(f"'{fn}' contains trailing or oversized compressed data")
    except (ValueError, zlib.error) as exc:
        raise ValueError(
            f"'{fn}' bytecode is corrupt or was compiled with an older "
            f"Lynxer version (decompression failed: {exc}). "
            "Recompile the source file to fix this."
        ) from exc

    try:
        data = _decode_payload(raw)
    except (
        ValueError,
        struct.error,
        UnicodeDecodeError,
        AttributeError,
        IndexError,
        KeyError,
        OverflowError,
        TypeError,
    ) as exc:
        raise ValueError(
            f"'{fn}' does not contain a valid Lynxer bytecode payload: {exc}"
        ) from exc
    if not isinstance(data, dict):
        raise ValueError(f"'{fn}' does not contain a valid Lynxer bytecode payload")  # noqa: TRY004

    file_version = data.get("version")
    if file_version != BYTECODE_VERSION:
        raise ValueError(
            f"'{fn}' was compiled with bytecode version {file_version} but this "
            f"Lynxer runtime expects version {BYTECODE_VERSION}.  "
            "Recompile the source file with "
            "'lynxer --compile <source.lynx>' to generate an up-to-date .lynxc file."
        )

    code = data.get("code")
    if not isinstance(code, bytes):
        raise ValueError(f"'{fn}' does not contain a compiled Lynxer instruction stream")  # noqa: TRY004
    try:
        data["node"] = _decode_instruction_stream(code)
    except (
        ValueError,
        struct.error,
        UnicodeDecodeError,
        AttributeError,
        IndexError,
        KeyError,
        OverflowError,
        TypeError,
    ) as exc:
        raise ValueError(
            f"'{fn}' does not contain a valid Lynxer instruction stream: {exc}"
        ) from exc

    if "node" not in data:
        raise ValueError(f"'{fn}' does not contain a compiled Lynxer program")
    runtime = _runtime()
    if not isinstance(data["node"], runtime.ProgramNode):
        raise ValueError(f"'{fn}' does not contain a valid compiled Lynxer program")  # noqa: TRY004

    return data, len(raw), len(compressed)


def load_bytecode(fn: str) -> dict[str, Any]:
    """Load and validate a bytecode file, returning its payload."""
    data, _, _ = _read_bytecode(fn)
    return data


def read_bytecode(fn: str) -> tuple[dict[str, Any], int, int]:
    """Load bytecode and return ``(payload, decompressed_size, stored_size)``."""
    return _read_bytecode(fn)


def compile_to_bytecode(
    fn: str,
    text: str,
    *,
    optimize: bool = True,
    use_cache: bool = True,
) -> tuple[str | None, Any]:
    """Parse and compile *text* to a ``.lynxc`` bytecode file."""
    started = time.perf_counter()
    out_path = os.path.splitext(os.path.abspath(fn))[0] + ".lynxc"
    if use_cache and os.path.exists(out_path):
        try:
            cached = load_bytecode(out_path)
            if _is_cache_current(cached, fn, text, optimize):
                return out_path, None
        except ValueError:
            pass

    runtime = _runtime()
    lexer = runtime.Lexer(fn, text)
    tokens, error = lexer.make_tokens()
    if error:
        return None, error

    parser = runtime.Parser(tokens)
    ast = parser.parse()
    if ast.error:
        return None, ast.error

    node = _optimize_program(ast.node) if optimize else ast.node
    elapsed_ms = int((time.perf_counter() - started) * 1000)
    data = {
        "version": BYTECODE_VERSION,
        "source": os.path.abspath(fn),
        "source_hash": _source_hash(text),
        "compiler_options": _compile_options(optimize),
        "compile_stats": {
            "token_count": len(tokens),
            "optimized": bool(optimize),
            "elapsed_ms": elapsed_ms,
        },
        "native_dependencies": sorted(set(_NATIVE_IMPORT_RE.findall(text))),
        "code": _compile_instruction_stream(node),
    }
    payload = zlib.compress(
        _encode_payload(data),
        level=zlib.Z_BEST_COMPRESSION,
    )
    with open(out_path, "wb") as bytecode_file:
        bytecode_file.write(BYTECODE_MAGIC)
        bytecode_file.write(payload)

    return out_path, None


def run_bytecode(fn: str, suppress_deprecation_warnings=False) -> tuple[Any, Any]:
    """Load and execute a pre-compiled ``.lynxc`` file."""
    runtime = _runtime()
    runtime.reset_runtime_state()
    runtime._main_override = None
    runtime._forever_delay = 0.02
    runtime._forever_warning_suppressed = False
    runtime._deprecation_warning_suppressed = bool(suppress_deprecation_warnings)
    runtime._pending_deprecation_warnings.clear()
    runtime._setup_in_progress = False

    try:
        data = load_bytecode(fn)
    except Exception as exc:
        raise RuntimeError(str(exc)) from exc

    node = data["node"]
    interpreter = runtime.SHARED_INTERPRETER
    context = runtime.Context("<program>")
    context.symbol_table = runtime.global_symbol_table
    runtime.global_symbol_table.set("__file__", runtime.String(os.path.abspath(fn)))
    runtime.global_symbol_table.set(
        "global", runtime.Namespace(runtime.global_symbol_table)
    )
    runtime.global_symbol_table.set("class", runtime.ClassRegistry())

    result = interpreter.visit(node, context)
    return result.value, result.error


def run_bytecode_file(fn: str, symbol_table: Any) -> Any:
    """Load and execute a compiled module in an existing symbol table."""
    runtime = _runtime()
    try:
        data = load_bytecode(fn)
    except Exception as exc:
        raise RuntimeError(str(exc)) from exc

    node = data["node"]
    interpreter = runtime.SHARED_INTERPRETER
    context = runtime.Context(f"<import:{os.path.basename(fn)}>")
    context.symbol_table = symbol_table
    symbol_table.set("__file__", runtime.String(os.path.abspath(fn)))

    for decl in node.globals_list:
        result = runtime.RTResult()
        result.register(interpreter.visit(decl, context))
        if result.error:
            return result.error

    if node.setup_func:
        previous_setup_state = runtime._setup_in_progress
        runtime._setup_in_progress = True
        try:
            result = interpreter.run_setup(node.setup_func, context)
            if result.error:
                return result.error
        finally:
            runtime._setup_in_progress = previous_setup_state

    return None
