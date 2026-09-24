"""Runtime type names, ranges, and declaration matching.

The registry deliberately uses the value protocol instead of importing the
value implementation.  That keeps type checking independent from both the
interpreter and builtin layers, and gives the future native runtime one small
API to implement.
"""

from __future__ import annotations

import math
from typing import Any


NUMERIC_TYPES = frozenset({"int", "float"})
INTEGER_RANGES = {
    "int8": (-128, 127),
    "int16": (-32768, 32767),
    "int32": (-2147483648, 2147483647),
    "int64": (-9223372036854775808, 9223372036854775807),
    "uint8": (0, 255),
    "uint16": (0, 65535),
    "uint32": (0, 4294967295),
    "uint64": (0, 18446744073709551615),
    "bit": (0, 1),
    "numBool": (0, 1),
    "byte": (0, 255),
}
FLOAT_RANGES = {
    "float32": 3.4028234663852886e38,
    "float64": 1.7976931348623157e308,
}


def _kind(value: Any) -> str:
    return type(value).__name__


def value_type_name(value: Any) -> str:
    """Return the Lynxer declaration type for a runtime value."""
    kind = _kind(value)
    if kind == "Null":
        return "none"
    if kind == "Number":
        if getattr(value, "is_bool", False):
            return "bool"
        return "float" if isinstance(getattr(value, "value", None), float) else "int"
    if kind == "Char":
        return "char"
    if kind == "String":
        return "str"
    if kind == "LynxTuple":
        return "tuple"
    if kind == "List":
        return "list"
    if kind == "Sentinel":
        return "sentinel"
    if kind == "ObjectValue":
        return "object"
    if kind in {"ClassInstance", "StructInstance"}:
        return getattr(value, "class_name", "any")
    if kind == "CodeBlockValue":
        return "codeblock"
    if kind == "Address":
        return "address"
    if kind == "FunctionAddress":
        return "functionAddress"
    if kind == "NativeHandle":
        return "nativeHandle"
    if kind == "EnumValue":
        return getattr(value, "enum_name", "any")
    if kind == "VarGroup":
        return "vargroup"
    if kind in {"Function", "AsyncFunction", "BuiltInFunction"}:
        return "function"
    return "any"


def type_matches(declared_type: str | None, value: Any) -> bool:
    """Return whether a runtime value satisfies a Lynxer declaration."""
    if declared_type in (None, "any"):
        return True

    actual = value_type_name(value)
    if _kind(value) == "EnumValue":
        return declared_type == getattr(value, "enum_name", None)
    if declared_type == "num":
        return actual in NUMERIC_TYPES
    if declared_type in NUMERIC_TYPES:
        return actual in NUMERIC_TYPES
    if declared_type in INTEGER_RANGES:
        raw = getattr(value, "value", None)
        return (
            _kind(value) == "Number"
            and not getattr(value, "is_bool", False)
            and isinstance(raw, int)
            and INTEGER_RANGES[declared_type][0] <= raw <= INTEGER_RANGES[declared_type][1]
        )
    if declared_type in FLOAT_RANGES:
        raw = getattr(value, "value", None)
        return (
            _kind(value) == "Number"
            and not getattr(value, "is_bool", False)
            and isinstance(raw, (int, float))
            and (not isinstance(raw, float) or not math.isnan(raw))
            and abs(raw) <= FLOAT_RANGES[declared_type]
        )
    if declared_type == "char":
        return _kind(value) == "Char"
    if declared_type == "functionAddress":
        return _kind(value) == "FunctionAddress"
    if declared_type == "nativeHandle":
        return _kind(value) == "NativeHandle"
    if declared_type == "codeblock":
        return _kind(value) == "CodeBlockValue"
    if declared_type in {"vargroup", "struct"}:
        return actual == "vargroup"
    return actual == declared_type
