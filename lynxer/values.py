"""Lynxer runtime values, execution results, contexts, and symbol tables."""

from __future__ import annotations

import itertools
import sys
from typing import Any, ClassVar

from .error import RTError
from .lexer import Token
from .lynxerAst import (
    CallNode,
    CodeBlockRefNode,
    DotAccessNode,
    VarAccessNode,
    VarAssignNode,
)

_cpp_module: Any = None


def _get_cpp():
    """Load the optional native value-reference module once on first use."""
    global _cpp_module
    if _cpp_module is None:
        from . import cpp
        _cpp_module = cpp
    return _cpp_module

def _shared_interpreter():
    """Return the process-wide interpreter without importing runtime at load.

    ``lynxer.runtime`` imports this module, so the reference has to be resolved
    lazily at call time instead of import time.
    """
    from . import runtime

    return runtime.SHARED_INTERPRETER


def _is_builtin_function(v) -> bool:
    """Check for ``BuiltInFunction`` without importing builtins at load time.

    Mirrors the historical ``"BuiltInFunction" in globals()`` check that only
    matched once ``lynxer.runtime._register_builtins`` had loaded the builtins
    module.
    """
    builtins_module = sys.modules.get(f"{__package__}.builtins")
    if builtins_module is None:
        return False
    builtin_function = getattr(builtins_module, "BuiltInFunction", None)
    return builtin_function is not None and isinstance(v, builtin_function)

# runtime result

class RTResult:
    def __init__(self):
        self.reset()

    def reset(self):
        self.value = None
        self.error = None
        self.func_return_value = None
        self.loop_should_continue = False
        self.loop_should_break = False

    def register(self, res):
        self.error = res.error
        self.func_return_value = res.func_return_value
        self.loop_should_continue = res.loop_should_continue
        self.loop_should_break = res.loop_should_break
        return res.value

    def success(self, value):
        self.reset()
        self.value = value
        return self

    def success_return(self, value):
        self.reset()
        self.func_return_value = value
        return self

    def failure(self, error):
        self.reset()
        self.error = error
        return self

    def should_return(self):
        return (
            self.error
            or self.func_return_value is not None
            or self.loop_should_continue
            or self.loop_should_break
        )

# values

class Value:
    def __init__(self):
        self.pos_start: Any = None
        self.pos_end: Any = None
        self.context: Any = None
        self.set_pos()
        self.set_context()

    def set_pos(self, pos_start: Any = None, pos_end: Any = None) -> Value:
        self.pos_start = pos_start
        self.pos_end = pos_end
        return self

    def set_context(self, context: Any = None) -> Value:
        self.context = context
        return self

    def added_to(self, other: Value) -> tuple[Value | None, RTError | None]:
        return None, self.illegal_operation(other)

    def subbed_by(self, other: Value) -> tuple[Value | None, RTError | None]:
        return None, self.illegal_operation(other)

    def multed_by(self, other: Value) -> tuple[Value | None, RTError | None]:
        return None, self.illegal_operation(other)

    def dived_by(self, other: Value) -> tuple[Value | None, RTError | None]:
        return None, self.illegal_operation(other)

    def modded_by(self, other: Value) -> tuple[Value | None, RTError | None]:
        return None, self.illegal_operation(other)

    def floordivided_by(self, other: Value) -> tuple[Value | None, RTError | None]:
        return None, self.illegal_operation(other)

    def powered_by(self, other: Value) -> tuple[Value | None, RTError | None]:
        return None, self.illegal_operation(other)

    def rooted_by(self, other: Value) -> tuple[Value | None, RTError | None]:
        return None, self.illegal_operation(other)

    def get_comparison_eq(self, other: Value) -> tuple[Value | None, RTError | None]:
        return None, self.illegal_operation(other)

    def get_comparison_ne(self, other: Value) -> tuple[Value | None, RTError | None]:
        return None, self.illegal_operation(other)

    def get_comparison_lt(self, other: Value) -> tuple[Value | None, RTError | None]:
        return None, self.illegal_operation(other)

    def get_comparison_gt(self, other: Value) -> tuple[Value | None, RTError | None]:
        return None, self.illegal_operation(other)

    def get_comparison_lte(self, other: Value) -> tuple[Value | None, RTError | None]:
        return None, self.illegal_operation(other)

    def get_comparison_gte(self, other: Value) -> tuple[Value | None, RTError | None]:
        return None, self.illegal_operation(other)

    def anded_by(self, other: Value) -> tuple[Value | None, RTError | None]:
        return None, self.illegal_operation(other)

    def ored_by(self, other: Value) -> tuple[Value | None, RTError | None]:
        return None, self.illegal_operation(other)

    def nanded_by(self, other: Value) -> tuple[Value | None, RTError | None]:
        return None, self.illegal_operation(other)

    def nored_by(self, other: Value) -> tuple[Value | None, RTError | None]:
        return None, self.illegal_operation(other)

    def notted(self) -> tuple[Value | None, RTError | None]:
        return None, self.illegal_operation()

    def bit_anded_by(self, other: Value) -> tuple[Value | None, RTError | None]:
        return None, self.illegal_operation(other)

    def bit_ored_by(self, other: Value) -> tuple[Value | None, RTError | None]:
        return None, self.illegal_operation(other)

    def bit_xored_by(self, other: Value) -> tuple[Value | None, RTError | None]:
        return None, self.illegal_operation(other)

    def bit_nanded_by(self, other: Value) -> tuple[Value | None, RTError | None]:
        return None, self.illegal_operation(other)

    def bit_xnored_by(self, other: Value) -> tuple[Value | None, RTError | None]:
        return None, self.illegal_operation(other)

    def bit_nored_by(self, other: Value) -> tuple[Value | None, RTError | None]:
        return None, self.illegal_operation(other)

    def bit_notted(self) -> tuple[Value | None, RTError | None]:
        return None, self.illegal_operation()

    def shifted_left_by(self, other: Value) -> tuple[Value | None, RTError | None]:
        return None, self.illegal_operation(other)

    def shifted_right_by(self, other: Value) -> tuple[Value | None, RTError | None]:
        return None, self.illegal_operation(other)

    def execute(self, args):
        return RTResult().failure(self.illegal_operation())

    def copy(self) -> Value:
        raise NotImplementedError("No copy method defined")

    def is_true(self) -> bool:
        return False

    def illegal_operation(self, other: Value | None = None) -> RTError:
        if not other:
            other = self
        return RTError(self.pos_start, other.pos_end, "Illegal operation", self.context)

_CODE_BLOCK_IDS = itertools.count(1)


class CodeBlockValue(Value):
    """A code block captured from a function call."""
    def __init__(self, body_node, param_toks=None, block_id=None):
        super().__init__()
        self.body_node = body_node
        self.param_toks = param_toks
        self.block_id = block_id if block_id is not None else next(_CODE_BLOCK_IDS)

    def copy(self):
        c = CodeBlockValue(self.body_node, self.param_toks, self.block_id)
        c.set_pos(self.pos_start, self.pos_end)
        c.set_context(self.context)
        return c

    def __repr__(self):
        return "<code block>"

class Address(Value):
    """A native C++ pointer to a Lynxer reference cell."""

    def __init__(self, pointer, symbol_table=None, name=None):
        super().__init__()
        self.pointer = pointer
        self.symbol_table = symbol_table
        self.name = name

    def _target(self):
        if self.symbol_table is None or self.name is None:
            return None, None
        table, resolved_name = self.symbol_table._resolve(self.name)
        if table is None or resolved_name is None:
            return None, None
        return table, resolved_name

    def get_value(self):
        if self.pointer is None:
            return None
        return _get_cpp().refGet(self.pointer)

    def set_value(self, value):
        if self.pointer is None:
            return False
        _get_cpp().refSet(self.pointer, value)
        return True

    def copy(self):
        c = Address(self.pointer, self.symbol_table, self.name)
        c.set_pos(self.pos_start, self.pos_end)
        c.set_context(self.context)
        return c

    def __str__(self):
        return f"<address 0x{self.pointer:x}>"

    __repr__ = __str__

class FunctionAddress(Value):
    """A typed native function pointer used by nativeCall()."""

    def __init__(self, pointer, module_handle=None):
        super().__init__()
        self.pointer = pointer
        self.module_handle = module_handle

    def copy(self):
        c = FunctionAddress(self.pointer, self.module_handle)
        c.set_pos(self.pos_start, self.pos_end)
        c.set_context(self.context)
        return c

    def __str__(self):
        return f"<function-address 0x{self.pointer:x}>"

    __repr__ = __str__

class NativeHandle(Value):
    """Owned native allocation with explicit, shared lifetime state."""

    def __init__(self, pointer):
        super().__init__()
        self._state = {"pointer": pointer, "active": True}

    @property
    def pointer(self):
        return self._state["pointer"]

    @property
    def active(self):
        return self._state["active"]

    def copy(self):
        c = NativeHandle(self.pointer)
        c._state = self._state
        c.set_pos(self.pos_start, self.pos_end)
        c.set_context(self.context)
        return c

    def __str__(self):
        status = "active" if self.active else "freed"
        return f"<native-handle 0x{self.pointer:x} {status}>"

    __repr__ = __str__

class Number(Value):
    null: ClassVar[Number]
    false: ClassVar[Number]
    true: ClassVar[Number]

    def __init__(self, value, is_bool=False):
        super().__init__()
        self.value = value
        self.is_bool = is_bool

    def added_to(self, other):
        if isinstance(other, Number):
            return Number(self.value + other.value).set_context(self.context), None
        return None, Value.illegal_operation(self, other)

    def subbed_by(self, other):
        if isinstance(other, Number):
            return Number(self.value - other.value).set_context(self.context), None
        return None, Value.illegal_operation(self, other)

    def multed_by(self, other):
        if isinstance(other, Number):
            return Number(self.value * other.value).set_context(self.context), None
        return None, Value.illegal_operation(self, other)

    def dived_by(self, other):
        if isinstance(other, Number):
            if other.value == 0:
                return None, RTError(
                    other.pos_start, other.pos_end, "Division by zero", self.context
                )
            return Number(self.value / other.value).set_context(self.context), None
        return None, Value.illegal_operation(self, other)

    def modded_by(self, other):
        if isinstance(other, Number):
            if other.value == 0:
                return None, RTError(
                    other.pos_start, other.pos_end, "Modulo by zero", self.context
                )
            return Number(self.value % other.value).set_context(self.context), None
        return None, Value.illegal_operation(self, other)

    def floordivided_by(self, other):
        if isinstance(other, Number):
            if other.value == 0:
                return None, RTError(
                    other.pos_start,
                    other.pos_end,
                    "Floor division by zero",
                    self.context,
                )
            return Number(int(self.value // other.value)).set_context(self.context), None
        return None, Value.illegal_operation(self, other)

    def powered_by(self, other):
        if isinstance(other, Number):
            try:
                result = self.value ** other.value
            except (OverflowError, ValueError, ZeroDivisionError):
                return None, RTError(
                    self.pos_start,
                    other.pos_end,
                    "Invalid exponentiation",
                    self.context,
                )
            if isinstance(result, complex):
                return None, RTError(
                    self.pos_start,
                    other.pos_end,
                    "Exponentiation produced a complex number",
                    self.context,
                )
            return Number(result).set_context(self.context), None
        return None, Value.illegal_operation(self, other)

    def rooted_by(self, other):
        if isinstance(other, Number):
            degree = other.value
            if degree == 0:
                return None, RTError(
                    other.pos_start,
                    other.pos_end,
                    "Root degree cannot be zero",
                    self.context,
                )
            try:
                if self.value < 0:
                    if degree != int(degree) or int(degree) % 2 == 0:
                        return None, RTError(
                            self.pos_start,
                            other.pos_end,
                            "Even roots of negative numbers are not real",
                            self.context,
                        )
                    result = -((-self.value) ** (1 / degree))
                else:
                    result = self.value ** (1 / degree)
            except (OverflowError, ValueError, ZeroDivisionError):
                return None, RTError(
                    self.pos_start,
                    other.pos_end,
                    "Invalid root operation",
                    self.context,
                )
            if isinstance(result, complex):
                return None, RTError(
                    self.pos_start,
                    other.pos_end,
                    "Root operation produced a complex number",
                    self.context,
                )
            return Number(result).set_context(self.context), None
        return None, Value.illegal_operation(self, other)

    def get_comparison_eq(self, other):
        if isinstance(other, Number):
            return Number(int(self.value == other.value), is_bool=True).set_context(
                self.context
            ), None
        return None, Value.illegal_operation(self, other)

    def get_comparison_ne(self, other):
        if isinstance(other, Number):
            return Number(int(self.value != other.value), is_bool=True).set_context(
                self.context
            ), None
        return None, Value.illegal_operation(self, other)

    def get_comparison_lt(self, other):
        if isinstance(other, Number):
            return Number(int(self.value < other.value), is_bool=True).set_context(self.context), None
        return None, Value.illegal_operation(self, other)

    def get_comparison_gt(self, other):
        if isinstance(other, Number):
            return Number(int(self.value > other.value), is_bool=True).set_context(self.context), None
        return None, Value.illegal_operation(self, other)

    def get_comparison_lte(self, other):
        if isinstance(other, Number):
            return Number(int(self.value <= other.value), is_bool=True).set_context(
                self.context
            ), None
        return None, Value.illegal_operation(self, other)

    def get_comparison_gte(self, other):
        if isinstance(other, Number):
            return Number(int(self.value >= other.value), is_bool=True).set_context(
                self.context
            ), None
        return None, Value.illegal_operation(self, other)

    def anded_by(self, other):
        if isinstance(other, Number):
            return Number(int(self.value and other.value), is_bool=True).set_context(
                self.context
            ), None
        return None, Value.illegal_operation(self, other)

    def ored_by(self, other):
        if isinstance(other, Number):
            return Number(int(self.value or other.value), is_bool=True).set_context(
                self.context
            ), None
        return None, Value.illegal_operation(self, other)

    def nanded_by(self, other):
        if isinstance(other, Number):
            return Number(int(not (self.value and other.value)), is_bool=True).set_context(
                self.context
            ), None
        return None, Value.illegal_operation(self, other)

    def nored_by(self, other):
        if isinstance(other, Number):
            return Number(int(not (self.value or other.value)), is_bool=True).set_context(
                self.context
            ), None
        return None, Value.illegal_operation(self, other)

    def notted(self):
        return Number(1 if self.value == 0 else 0, is_bool=True).set_context(
            self.context
        ), None

    def bit_anded_by(self, other):
        if isinstance(other, Number):
            return Number(int(self.value) & int(other.value)).set_context(
                self.context
            ), None
        return None, Value.illegal_operation(self, other)

    def bit_ored_by(self, other):
        if isinstance(other, Number):
            return Number(int(self.value) | int(other.value)).set_context(
                self.context
            ), None
        return None, Value.illegal_operation(self, other)

    def bit_xored_by(self, other):
        if isinstance(other, Number):
            return Number(int(self.value) ^ int(other.value)).set_context(
                self.context
            ), None
        return None, Value.illegal_operation(self, other)

    def bit_nanded_by(self, other):
        if isinstance(other, Number):
            return Number(~(int(self.value) & int(other.value))).set_context(
                self.context
            ), None
        return None, Value.illegal_operation(self, other)

    def bit_xnored_by(self, other):
        if isinstance(other, Number):
            return Number(~(int(self.value) ^ int(other.value))).set_context(
                self.context
            ), None
        return None, Value.illegal_operation(self, other)

    def bit_nored_by(self, other):
        if isinstance(other, Number):
            return Number(~(int(self.value) | int(other.value))).set_context(
                self.context
            ), None
        return None, Value.illegal_operation(self, other)

    def bit_notted(self):
        return Number(~int(self.value)).set_context(self.context), None

    def shifted_left_by(self, other):
        if isinstance(other, Number):
            return Number(int(self.value) << int(other.value)).set_context(
                self.context
            ), None
        return None, Value.illegal_operation(self, other)

    def shifted_right_by(self, other):
        if isinstance(other, Number):
            return Number(int(self.value) >> int(other.value)).set_context(
                self.context
            ), None
        return None, Value.illegal_operation(self, other)

    def copy(self):
        c = Number(self.value, is_bool=self.is_bool)
        c.set_pos(self.pos_start, self.pos_end)
        c.set_context(self.context)
        return c

    def is_true(self):
        return self.value != 0

    def __str__(self):
        if self.is_bool:
            return "true" if self.value else "false"
        v = self.value
        if isinstance(v, float) and v == int(v):
            return str(int(v))
        return str(v)

    def __repr__(self):
        return self.__str__()

Number.null = Number(0)
Number.false = Number(0, is_bool=True)
Number.true = Number(1, is_bool=True)

class String(Value):
    def __init__(self, value):
        super().__init__()
        self.value = value

    def added_to(self, other):
        if isinstance(other, String):
            return String(self.value + other.value).set_context(self.context), None
        if isinstance(other, Char):
            return String(self.value + other.value).set_context(self.context), None
        return None, Value.illegal_operation(self, other)

    def get_comparison_eq(self, other):
        if isinstance(other, String):
            return Number(int(self.value == other.value), is_bool=True).set_context(
                self.context
            ), None
        return None, Value.illegal_operation(self, other)

    def get_comparison_ne(self, other):
        if isinstance(other, String):
            return Number(int(self.value != other.value), is_bool=True).set_context(
                self.context
            ), None
        return None, Value.illegal_operation(self, other)

    def is_true(self):
        return len(self.value) > 0

    def copy(self):
        c = String(self.value)
        c.set_pos(self.pos_start, self.pos_end)
        c.set_context(self.context)
        return c

    def __str__(self):
        return self.value

    def __repr__(self):
        return f'"{self.value}"'

class Char(Value):
    """Single Unicode character. Literal syntax: 'a'"""
    def __init__(self, value):
        super().__init__()
        self.value = value[0] if value else "\0"

    def added_to(self, other):
        if isinstance(other, (Char, String)):
            return String(self.value + other.value).set_context(self.context), None
        return None, Value.illegal_operation(self, other)

    def get_comparison_eq(self, other):
        if isinstance(other, Char):
            return Number(int(self.value == other.value), is_bool=True).set_context(self.context), None
        return None, Value.illegal_operation(self, other)

    def get_comparison_ne(self, other):
        if isinstance(other, Char):
            return Number(int(self.value != other.value), is_bool=True).set_context(self.context), None
        return None, Value.illegal_operation(self, other)

    def is_true(self):
        return True

    def copy(self):
        c = Char(self.value)
        c.set_pos(self.pos_start, self.pos_end)
        c.set_context(self.context)
        return c

    def __str__(self):
        return self.value

    def __repr__(self):
        return f"'{self.value}'"

class Null(Value):
    def __init__(self):
        super().__init__()

    def get_comparison_eq(self, other):
        return Number(int(isinstance(other, Null)), is_bool=True).set_context(self.context), None

    def get_comparison_ne(self, other):
        return Number(int(not isinstance(other, Null)), is_bool=True).set_context(
            self.context
        ), None

    def is_true(self):
        return False

    def copy(self):
        c = Null()
        c.set_pos(self.pos_start, self.pos_end)
        c.set_context(self.context)
        return c

    def __str__(self):
        return "none"

    def __repr__(self):
        return "none"

class Sentinel(Value):
    """A unique marker value with an optional human-readable name."""

    def __init__(self, name=None):
        super().__init__()
        self.name = name

    def get_comparison_eq(self, other):
        return Number(int(self is other), is_bool=True).set_context(self.context), None

    def get_comparison_ne(self, other):
        return Number(int(self is not other), is_bool=True).set_context(self.context), None

    def is_true(self):
        return True

    def copy(self):
        # A sentinel's identity is its meaning; variable access must not clone it.
        return self

    def __str__(self):
        return self.name if self.name is not None else "<sentinel>"

    def __repr__(self):
        return f"sentinel({self.name!r})" if self.name is not None else "sentinel()"

class ObjectValue(Value):
    """A unique unnamed opaque value, analogous to Python's object()."""

    def get_comparison_eq(self, other):
        return Number(int(self is other), is_bool=True).set_context(self.context), None

    def get_comparison_ne(self, other):
        return Number(int(self is not other), is_bool=True).set_context(self.context), None

    def is_true(self):
        return True

    def copy(self):
        # Object identity is significant, just like a Python object() instance.
        return self

    def __str__(self):
        return "<object>"

    def __repr__(self):
        return "object()"

class List(Value):
    def __init__(self, elements):
        super().__init__()
        self.elements = elements

    def added_to(self, other):
        if isinstance(other, List):
            return List(self.elements + other.elements).set_context(
                self.context
            ), None
        return None, Value.illegal_operation(self, other)

    def get_comparison_eq(self, other):
        if not isinstance(other, List):
            return None, Value.illegal_operation(self, other)
        if len(self.elements) != len(other.elements):
            return Number(0, is_bool=True).set_context(self.context), None
        for left, right in zip(self.elements, other.elements):
            equal, error = left.get_comparison_eq(right)
            if error or not equal.is_true():
                return Number(0, is_bool=True).set_context(self.context), None
        return Number(1, is_bool=True).set_context(self.context), None

    def get_comparison_ne(self, other):
        equal, error = self.get_comparison_eq(other)
        if error:
            return None, error
        assert isinstance(equal, Number)
        return Number(1 - int(equal.value), is_bool=True).set_context(self.context), None

    def is_true(self):
        return len(self.elements) > 0

    def copy(self):
        c = List(list(self.elements))
        c.set_pos(self.pos_start, self.pos_end)
        c.set_context(self.context)
        return c

    def __str__(self):
        return "[" + ", ".join(str(e) for e in self.elements) + "]"

    def __repr__(self):
        return self.__str__()

class LynxTuple(Value):
    """Immutable, ordered, fixed-length sequence.  Declared with the 'tuple' type keyword."""

    def __init__(self, elements):
        super().__init__()
        self.elements = tuple(elements)   # Python tuple — truly immutable

    def get_comparison_eq(self, other):
        if isinstance(other, LynxTuple):
            if len(self.elements) != len(other.elements):
                return Number(0, is_bool=True).set_context(self.context), None
            for a, b in zip(self.elements, other.elements):
                eq, err = a.get_comparison_eq(b)
                if err or not eq.is_true():
                    return Number(0, is_bool=True).set_context(self.context), None
            return Number(1, is_bool=True).set_context(self.context), None
        return None, Value.illegal_operation(self, other)

    def get_comparison_ne(self, other):
        eq, err = self.get_comparison_eq(other)
        if err:
            return None, err
        if not isinstance(eq, Number):
            return None, Value.illegal_operation(self, other)
        return Number(1 - int(eq.value), is_bool=True).set_context(self.context), None

    def is_true(self):
        return len(self.elements) > 0

    def copy(self):
        c = LynxTuple(self.elements)
        c.set_pos(self.pos_start, self.pos_end)
        c.set_context(self.context)
        return c

    def __str__(self):
        if len(self.elements) == 1:
            return "(" + str(self.elements[0]) + ",)"
        return "(" + ", ".join(str(e) for e in self.elements) + ")"

    def __repr__(self):
        return self.__str__()

def value_type_name(v):
    if isinstance(v, Null):
        return "none"
    if isinstance(v, Number):
        if v.is_bool:
            return "bool"
        return "float" if isinstance(v.value, float) else "int"
    if isinstance(v, Char):
        return "char"
    if isinstance(v, String):
        return "str"
    if isinstance(v, LynxTuple):
        return "tuple"
    if isinstance(v, List):
        return "list"
    if isinstance(v, Sentinel):
        return "sentinel"
    if isinstance(v, ObjectValue):
        return "object"
    if isinstance(v, ClassInstance):
        return v.class_name
    if isinstance(v, CodeBlockValue):
        return "codeblock"
    if isinstance(v, Address):
        return "address"
    if isinstance(v, FunctionAddress):
        return "functionAddress"
    if isinstance(v, NativeHandle):
        return "nativeHandle"
    if isinstance(v, EnumValue):
        return v.enum_name
    if isinstance(v, VarGroup):
        return "vargroup"
    if isinstance(v, Function) or _is_builtin_function(v):
        return "function"
    return "any"

NUMERIC_TYPES = {"int", "float"}
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

def type_matches(declared_type, value):
    if declared_type in (None, "any"):
        return True
    actual = value_type_name(value)
    if isinstance(value, EnumValue):
        return declared_type == value.enum_name
    if declared_type == "num":
        return actual in NUMERIC_TYPES
    if declared_type in NUMERIC_TYPES:
        return actual in NUMERIC_TYPES
    if declared_type in INTEGER_RANGES:
        return (
            isinstance(value, Number)
            and not value.is_bool
            and isinstance(value.value, int)
            and INTEGER_RANGES[declared_type][0] <= value.value
            <= INTEGER_RANGES[declared_type][1]
        )
    if declared_type in FLOAT_RANGES:
        return (
            isinstance(value, Number)
            and not value.is_bool
            and isinstance(value.value, (int, float))
            and value.value == value.value
            and abs(value.value) <= FLOAT_RANGES[declared_type]
        )
    if declared_type == "char":
        return isinstance(value, Char)
    if declared_type == "functionAddress":
        return isinstance(value, FunctionAddress)
    if declared_type == "nativeHandle":
        return isinstance(value, NativeHandle)
    if declared_type in ("vargroup", "struct"):
        return actual == "vargroup"
    return actual == declared_type

def _exec_codeblock_variable_names(node):
    """Return user-variable references in a codeblock in source order."""
    names = []
    seen = set()
    namespace_names = {"global", "local", "class", "async"}

    def visit(value, call_target=False):
        if value is None or isinstance(value, (Token, str, int, float, bool)):
            return
        if isinstance(value, VarAccessNode):
            name = value.var_name_tok.value
            if not call_target and name not in namespace_names and name not in seen:
                seen.add(name)
                names.append(name)
            return
        if isinstance(value, VarAssignNode):
            name = value.var_name_tok.value
            if name not in namespace_names and name not in seen:
                seen.add(name)
                names.append(name)
            visit(value.value_node)
            return
        if isinstance(value, CodeBlockRefNode):
            return
        if isinstance(value, CallNode):
            visit(value.node_to_call, call_target=True)
            for arg_node in value.arg_nodes:
                visit(arg_node)
            for block_node in value.block_arg_nodes:
                visit(block_node)
            return
        if isinstance(value, DotAccessNode):
            if (
                call_target
                and isinstance(value.obj_node, VarAccessNode)
                and value.obj_node.var_name_tok.value in namespace_names
            ):
                return
            visit(value.obj_node)
            return
        if isinstance(value, (list, tuple)):
            for item in value:
                visit(item)
            return
        if hasattr(value, "__dict__"):
            for attr_name, attr_value in vars(value).items():
                if attr_name in {"pos_start", "pos_end"}:
                    continue
                visit(attr_value)

    visit(node)
    return names

def _build_exec_bindings(node, block, args, context):
    """Build ``(name, declared_type, value)`` bindings for one exec call."""
    if block.param_toks is not None:
        if len(args) != len(block.param_toks):
            expected = ", ".join(
                name_tok.value for _, name_tok in block.param_toks
            ) if block.param_toks else "no variables"
            return None, RTError(
                node.pos_start,
                node.pos_end,
                f"exec() expects {len(block.param_toks)} value(s) for declared "
                f"codeblock parameters ({expected}), but got {len(args)}",
                context,
            )

        bindings = []
        for (type_tok, name_tok), value in zip(block.param_toks, args):
            declared_type = type_tok.value if type_tok else None
            if not type_matches(declared_type, value):
                return None, RTError(
                    name_tok.pos_start,
                    name_tok.pos_end,
                    f"Codeblock parameter '{name_tok.value}' expects "
                    f"'{declared_type}' but got a '{value_type_name(value)}' value",
                    context,
                )
            bindings.append((name_tok.value, declared_type, value))
        return bindings, None

    if node.infer_params:
        names = _exec_codeblock_variable_names(block.body_node)
        if len(args) != len(names):
            expected = ", ".join(names) if names else "no variables"
            return None, RTError(
                node.pos_start,
                node.pos_end,
                f"exec() expects {len(names)} value(s) for codeblock variables "
                f"({expected}), but got {len(args)}",
                context,
            )
        return [(name, None, value) for name, value in zip(names, args)], None

    bindings = []
    for type_tok, name_tok in node.param_toks:
        name = name_tok.value
        value = context.symbol_table.get(name)
        if value is None:
            return None, RTError(
                name_tok.pos_start,
                name_tok.pos_end,
                f"Exec parameter '{name}' is not defined in the surrounding scope",
                context,
            )
        declared_type = type_tok.value if type_tok else None
        if not type_matches(declared_type, value):
            return None, RTError(
                name_tok.pos_start,
                name_tok.pos_end,
                f"Exec parameter '{name}' expects '{declared_type}' "
                f"but got a '{value_type_name(value)}' value",
                context,
            )
        bindings.append((name, declared_type, value))
    return bindings, None

class BaseFunction(Value):
    def __init__(self, name):
        super().__init__()
        self.name = name or "<anonymous>"

    def generate_new_context(self):
        new_context = Context(self.name, self.context, self.pos_start)
        parent_table = new_context.parent.symbol_table if new_context.parent else None
        new_context.symbol_table = SymbolTable(parent_table)
        return new_context

    def check_args(self, arg_names, args, arg_defaults=None):
        res = RTResult()
        arg_defaults = arg_defaults or []
        required_count = len(arg_names) - sum(
            default_node is not None for default_node in arg_defaults
        )
        if len(args) > len(arg_names):
            return res.failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    f"{len(args) - len(arg_names)} too many arguments passed into '{self.name}'",
                    self.context,
                )
            )
        if len(args) < required_count:
            return res.failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    f"{required_count - len(args)} too few arguments passed into '{self.name}'",
                    self.context,
                )
            )
        return res.success(None)

    def populate_args(self, arg_names, args, exec_ctx, arg_types=None):
        for i in range(len(args)):
            arg_name = arg_names[i]
            arg_value = args[i]
            arg_type = arg_types[i] if arg_types else None
            if not type_matches(arg_type, arg_value):
                return RTResult().failure(
                    RTError(
                        self.pos_start,
                        self.pos_end,
                        f"Argument '{arg_name}' of '{self.name}' expects '{arg_type}' "
                        f"but got a '{value_type_name(arg_value)}' value",
                        exec_ctx,
                    )
                )
            arg_value.set_context(exec_ctx)
            exec_ctx.symbol_table.set(arg_name, arg_value, decl_type=arg_type)
        return None

    def check_and_populate_args(
        self, arg_names, args, exec_ctx, arg_types=None, arg_defaults=None
    ):
        res = RTResult()
        res.register(self.check_args(arg_names, args, arg_defaults))
        if res.should_return():
            return res
        err = self.populate_args(arg_names, args, exec_ctx, arg_types)
        if err:
            return err
        arg_defaults = arg_defaults or []
        for index in range(len(args), len(arg_names)):
            default_node = arg_defaults[index]
            default_value = res.register(
                _shared_interpreter().visit(default_node, exec_ctx)
            )
            if res.should_return():
                return res
            err = self.populate_args(
                [arg_names[index]],
                [default_value],
                exec_ctx,
                [arg_types[index] if arg_types else None],
            )
            if err:
                return err
        return res.success(None)

class Function(BaseFunction):
    def __init__(
        self, name, body_node, param_names, param_types=None, is_global=False,
        code_block_names=None, param_defaults=None, is_file_func=False
    ):
        super().__init__(name)
        self.body_node = body_node
        self.param_names = param_names
        self.param_types = param_types or [None] * len(param_names)
        self.param_defaults = param_defaults or [None] * len(param_names)
        self.is_global = is_global
        self.is_file_func = is_file_func
        self.code_block_names = code_block_names or []
        self.inner_locals = {}
        self.inner_globals = {}
        self.global_path: list[str] | None = None

    def get_attr(self, name):
        if name in self.inner_locals:
            return self.inner_locals[name], None
        if name in self.inner_globals:
            return self.inner_globals[name], None
        return None, RTError(
            self.pos_start,
            self.pos_end,
            f"Function '{self.name}' has no nested local or nested global '{name}'",
            self.context,
        )

    def execute(self, args, code_blocks=None):
        res = RTResult()
        interpreter = _shared_interpreter()
        exec_ctx = self.generate_new_context()
        exec_ctx.current_function = self  # track for inner-local/inner-global registration
        if self.is_global:
            exec_ctx.current_global_path = self.global_path  # for hierarchy enforcement

        res.register(
            self.check_and_populate_args(
                self.param_names,
                args,
                exec_ctx,
                self.param_types,
                self.param_defaults,
            )
        )
        if res.should_return():
            return res

        code_blocks = code_blocks or []
        if len(code_blocks) != len(self.code_block_names):
            return res.failure(RTError(
                self.pos_start,
                self.pos_end,
                f"Function '{self.name}' expects exactly "
                f"{len(self.code_block_names)} code block(s), but got "
                f"{len(code_blocks)}",
                exec_ctx,
            ))
        for block_name, block_value in zip(self.code_block_names, code_blocks):
            block_value = block_value.copy().set_context(exec_ctx)
            exec_ctx.symbol_table.set(block_name, block_value)
            exec_ctx.code_blocks[block_name] = block_value

        exec_ctx.symbol_table.set("local", LocalNamespace(exec_ctx.symbol_table))

        res.register(interpreter.visit(self.body_node, exec_ctx))
        if res.should_return() and res.func_return_value is None:
            return res

        ret_value = (
            res.func_return_value if res.func_return_value is not None else Number.null
        )
        return res.success(ret_value)

    def copy(self):
        c = Function(
            self.name,
            self.body_node,
            self.param_names,
            self.param_types,
            self.is_global,
            self.code_block_names,
            self.param_defaults,
            self.is_file_func,
        )
        c.set_context(self.context)
        c.set_pos(self.pos_start, self.pos_end)
        c.inner_locals = dict(self.inner_locals)
        c.inner_globals = dict(self.inner_globals)
        c.global_path = self.global_path
        return c

    def __repr__(self):
        return f"<function {self.name}>"

class AsyncFunction(BaseFunction):
    """User-defined async function.  Calling it returns a CoroutineValue."""

    def __init__(
        self, name, body_node, param_names, param_types=None, is_global=False,
        code_block_names=None, param_defaults=None, is_file_func=False
    ):
        super().__init__(name)
        self.body_node = body_node
        self.param_names = param_names
        self.param_types = param_types or [None] * len(param_names)
        self.param_defaults = param_defaults or [None] * len(param_names)
        self.is_global = is_global
        self.is_file_func = is_file_func
        self.code_block_names = code_block_names or []
        self.inner_locals = {}
        self.inner_globals = {}
        self.global_path: list[str] | None = None

    def get_attr(self, name):
        if name in self.inner_locals:
            return self.inner_locals[name], None
        if name in self.inner_globals:
            return self.inner_globals[name], None
        return None, RTError(
            self.pos_start,
            self.pos_end,
            f"Function '{self.name}' has no nested local or nested global '{name}'",
            self.context,
        )

    def execute(self, args, code_blocks=None):
        res = RTResult()
        exec_ctx = self.generate_new_context()
        exec_ctx.current_function = self  # track for inner-local/inner-global registration
        if self.is_global:
            exec_ctx.current_global_path = self.global_path

        res.register(
            self.check_and_populate_args(
                self.param_names,
                args,
                exec_ctx,
                self.param_types,
                self.param_defaults,
            )
        )
        if res.should_return():
            return res

        code_blocks = code_blocks or []
        if len(code_blocks) != len(self.code_block_names):
            return res.failure(RTError(
                self.pos_start,
                self.pos_end,
                f"Function '{self.name}' expects exactly "
                f"{len(self.code_block_names)} code block(s), but got "
                f"{len(code_blocks)}",
                exec_ctx,
            ))
        for block_name, block_value in zip(self.code_block_names, code_blocks):
            block_value = block_value.copy().set_context(exec_ctx)
            exec_ctx.symbol_table.set(block_name, block_value)
            exec_ctx.code_blocks[block_name] = block_value

        exec_ctx.symbol_table.set("local", LocalNamespace(exec_ctx.symbol_table))

        body_node = self.body_node

        async def _coro():
            body_res = await _shared_interpreter().async_visit(body_node, exec_ctx)
            if body_res.should_return() and body_res.func_return_value is None:
                return body_res  # error / loop signal
            ret = (
                body_res.func_return_value
                if body_res.func_return_value is not None
                else Number.null
            )
            return RTResult().success(ret)

        return RTResult().success(CoroutineValue(_coro()))

    def copy(self):
        c = AsyncFunction(
            self.name,
            self.body_node,
            self.param_names,
            self.param_types,
            self.is_global,
            self.code_block_names,
            self.param_defaults,
            self.is_file_func,
        )
        c.set_context(self.context)
        c.set_pos(self.pos_start, self.pos_end)
        c.inner_locals = dict(self.inner_locals)
        c.inner_globals = dict(self.inner_globals)
        c.global_path = self.global_path
        return c

    def __repr__(self):
        return f"<async function {self.name}>"

class CoroutineValue(Value):
    """Wraps a Python coroutine produced by calling an AsyncFunction."""

    def __init__(self, coro):
        super().__init__()
        self.coro = coro

    def copy(self):
        return self

    def __repr__(self):
        return "<coroutine>"

# modules

class Namespace(Value):
    def __init__(self, symbol_table):
        super().__init__()
        self.symbol_table = symbol_table

    def get_attr(self, name):
        val = self.symbol_table.get(name)
        if val is None:
            return None, RTError(
                self.pos_start,
                self.pos_end,
                f"'{name}' is not defined in this namespace",
                self.context,
            )
        return val, None

    def copy(self):
        c = Namespace(self.symbol_table)
        c.set_pos(self.pos_start, self.pos_end)
        c.set_context(self.context)
        return c

    def __repr__(self):
        return "<namespace>"

class EnumValue(Value):
    """Tagged, immutable enum value with optional named payloads."""
    def __init__(self, enum_name, variant_name, payload=None, field_names=None):
        super().__init__()
        self.enum_name = enum_name
        self.variant_name = variant_name
        self.payload = list(payload or [])
        self.field_names = list(field_names or [])

    def get_attr(self, name):
        if name in self.field_names:
            return self.payload[self.field_names.index(name)], None
        return None, RTError(
            self.pos_start, self.pos_end,
            f"Enum variant '{self.variant_name}' has no payload '{name}'",
            self.context,
        )

    def get_comparison_eq(self, other):
        if not isinstance(other, EnumValue):
            return Number(0, is_bool=True), None
        if self.enum_name != other.enum_name or self.variant_name != other.variant_name:
            return Number(0, is_bool=True), None
        if len(self.payload) != len(other.payload):
            return Number(0, is_bool=True), None
        for left, right in zip(self.payload, other.payload):
            equal, error = left.get_comparison_eq(right)
            if error or not equal.is_true():
                return Number(0, is_bool=True), error
        return Number(1, is_bool=True), None

    def get_comparison_ne(self, other):
        equal, error = self.get_comparison_eq(other)
        if error:
            return None, error
        return Number(1 - int(equal.value), is_bool=True), None

    def is_true(self):
        return True

    def copy(self):
        copied = EnumValue(
            self.enum_name, self.variant_name,
            [item.copy() for item in self.payload], self.field_names
        )
        return copied.set_pos(self.pos_start, self.pos_end).set_context(self.context)

    def __str__(self):
        if not self.payload:
            return f"{self.enum_name}.{self.variant_name}"
        fields = ", ".join(str(item) for item in self.payload)
        return f"{self.enum_name}.{self.variant_name}({fields})"

    __repr__ = __str__

class EnumConstructor(BaseFunction):
    def __init__(self, enum_name, variant_name, field_defs):
        super().__init__(f"{enum_name}.{variant_name}")
        self.enum_name = enum_name
        self.variant_name = variant_name
        self.field_defs = field_defs

    def execute(self, args, code_blocks=None):
        res = RTResult()
        if len(args) != len(self.field_defs):
            return res.failure(RTError(
                self.pos_start, self.pos_end,
                f"{self.name} expects {len(self.field_defs)} payload value(s), "
                f"received {len(args)}",
                self.context,
            ))
        for value, (field_type, field_name) in zip(args, self.field_defs):
            if not type_matches(field_type, value):
                return res.failure(RTError(
                    self.pos_start, self.pos_end,
                    f"Enum '{self.enum_name}.{self.variant_name}' field "
                    f"'{field_name}' is declared as '{field_type}' but received "
                    f"'{value_type_name(value)}'",
                    self.context,
                ))
        return res.success(EnumValue(
            self.enum_name, self.variant_name, [item.copy() for item in args],
            [name for _, name in self.field_defs],
        ))

    def copy(self):
        return self

class EnumType(Value):
    def __init__(self, name, variants):
        super().__init__()
        self.name = name
        self.variants = {
            variant: (
                EnumValue(name, variant)
                if not fields else EnumConstructor(name, variant, fields)
            )
            for variant, fields in variants
        }

    def get_attr(self, name):
        value = self.variants.get(name)
        if value is None:
            return None, RTError(
                self.pos_start, self.pos_end,
                f"Enum '{self.name}' has no variant '{name}'",
                self.context,
            )
        return value, None

    def copy(self):
        return self

    def __str__(self):
        return f"<enum {self.name}>"

class LocalNamespace(Value):
    """Namespace for 'local' functions defined."""

    def __init__(self, symbol_table):
        super().__init__()
        self.symbol_table = symbol_table

    def get_attr(self, name):
        val = self.symbol_table.symbols.get(name)
        if val is None:
            return None, RTError(
                self.pos_start,
                self.pos_end,
                f"Local function '{name}' is not defined in this scope "
                f"(locals are only visible inside the function that defines them)",
                self.context,
            )
        if isinstance(val, (Function, AsyncFunction)) and val.is_global:
            return None, RTError(
                self.pos_start,
                self.pos_end,
                f"'{name}' is a global function — call it with 'global.{name}(...)' instead",
                self.context,
            )
        return val, None

    def copy(self):
        c = LocalNamespace(self.symbol_table)
        c.set_pos(self.pos_start, self.pos_end)
        c.set_context(self.context)
        return c

    def __repr__(self):
        return "<local namespace>"

# embedPy — experimental Python bridge
# a rawPy block.  Example:
# embedPy.requests.get("https://example.com")   →  calls requests.get(...)
# embedPy.len("hello")                           →  calls builtins.len("hello")
def _lynx_to_python(val):
    """Convert a Lynxer Value → plain Python value suitable for passing to Python code."""
    if isinstance(val, Number):
        return val.value
    if isinstance(val, String):
        return val.value
    if isinstance(val, List):
        return [_lynx_to_python(e) for e in val.elements]
    if isinstance(val, LynxTuple):
        return tuple(_lynx_to_python(e) for e in val.elements)
    if isinstance(val, EmbedPyObject):
        return val.py_obj
    return None

def _python_to_lynx(py_val, context=None, pos_start=None, pos_end=None):
    """Convert a plain Python value → the nearest Lynxer Value equivalent."""
    if py_val is None:
        return Number.null
    if isinstance(py_val, bool):
        return Number(1 if py_val else 0)
    if isinstance(py_val, int):
        return Number(py_val)
    if isinstance(py_val, float):
        return Number(py_val)
    if isinstance(py_val, str):
        return String(py_val)
    if isinstance(py_val, bytes):
        return String(py_val.decode("utf-8", errors="replace"))
    if isinstance(py_val, (list, tuple)):
        elements = [_python_to_lynx(e, context, pos_start, pos_end) for e in py_val]
        return List(elements)
    if isinstance(py_val, dict):
        import json as _json
        try:
            return String(_json.dumps(py_val))
        except Exception:  # noqa: BLE001
            return String(str(py_val))
    obj = EmbedPyObject(py_val)
    if context:
        obj.set_context(context)
    if pos_start:
        obj.set_pos(pos_start, pos_end)
    return obj

class EmbedPyObject(Value):
    """Wraps an arbitrary Python object."""

    def __init__(self, py_obj):
        super().__init__()
        self.py_obj = py_obj

    def get_attr(self, name):
        import types as _types
        try:
            attr = getattr(self.py_obj, name)
        except AttributeError:
            return None, RTError(
                self.pos_start, self.pos_end,
                f"Python object <{type(self.py_obj).__name__}> has no attribute '{name}'",
                self.context,
            )
        if isinstance(attr, _types.ModuleType):
            mod = EmbedPyModule(attr, type(self.py_obj).__name__ + "." + name)
            mod.set_context(self.context).set_pos(self.pos_start, self.pos_end)
            return mod, None
        if callable(attr):
            fn = EmbedPyCallable(attr, f"<{type(self.py_obj).__name__}>.{name}")
            fn.set_context(self.context).set_pos(self.pos_start, self.pos_end)
            return fn, None
        result = _python_to_lynx(attr, self.context, self.pos_start, self.pos_end)
        return result, None

    def execute(self, args):
        if not callable(self.py_obj):
            return RTResult().failure(RTError(
                self.pos_start, self.pos_end,
                f"embedPy: Python object <{type(self.py_obj).__name__}> is not callable",
                self.context,
            ))
        py_args = [_lynx_to_python(a) for a in args]
        try:
            result = self.py_obj(*py_args)
        except Exception as e:  # noqa: BLE001
            return RTResult().failure(RTError(
                self.pos_start, self.pos_end,
                f"embedPy: Python error calling <{type(self.py_obj).__name__}>: {e}",
                self.context,
            ))
        lx = _python_to_lynx(result, self.context, self.pos_start, self.pos_end)
        return RTResult().success(lx)

    def copy(self):
        c = EmbedPyObject(self.py_obj)
        c.set_context(self.context)
        c.set_pos(self.pos_start, self.pos_end)
        return c

    def __repr__(self):
        return str(self.py_obj)

class EmbedPyCallable(Value):
    """Wraps a Python callable (function, method, class, lambda) for Lynxer calls."""

    def __init__(self, py_callable, name="<python>"):
        super().__init__()
        self.py_callable = py_callable
        self.name = name

    def get_attr(self, name):
        """Support chained access on callables, e.g. a class with static methods."""
        import types as _types
        try:
            attr = getattr(self.py_callable, name)
        except AttributeError:
            return None, RTError(
                self.pos_start, self.pos_end,
                f"embedPy callable '{self.name}' has no attribute '{name}'",
                self.context,
            )
        if isinstance(attr, _types.ModuleType):
            mod = EmbedPyModule(attr, f"{self.name}.{name}")
            mod.set_context(self.context).set_pos(self.pos_start, self.pos_end)
            return mod, None
        if callable(attr):
            fn = EmbedPyCallable(attr, f"{self.name}.{name}")
            fn.set_context(self.context).set_pos(self.pos_start, self.pos_end)
            return fn, None
        result = _python_to_lynx(attr, self.context, self.pos_start, self.pos_end)
        return result, None

    def execute(self, args):
        py_args = [_lynx_to_python(a) for a in args]
        try:
            result = self.py_callable(*py_args)
        except Exception as e:  # noqa: BLE001
            return RTResult().failure(RTError(
                self.pos_start, self.pos_end,
                f"embedPy: Python error calling '{self.name}': {e}",
                self.context,
            ))
        lx = _python_to_lynx(result, self.context, self.pos_start, self.pos_end)
        return RTResult().success(lx)

    def copy(self):
        c = EmbedPyCallable(self.py_callable, self.name)
        c.set_context(self.context)
        c.set_pos(self.pos_start, self.pos_end)
        return c

    def __repr__(self):
        return f"<embedPy: {self.name}>"

class EmbedPyModule(Value):
    """Wraps a Python module; attribute access returns EmbedPyCallable or nested EmbedPyModule."""

    def __init__(self, py_module, module_name=""):
        super().__init__()
        self.py_module = py_module
        self.module_name = module_name

    def get_attr(self, name):
        import types as _types
        try:
            attr = getattr(self.py_module, name)
        except AttributeError:
            return None, RTError(
                self.pos_start, self.pos_end,
                f"embedPy module '{self.module_name}' has no attribute '{name}'",
                self.context,
            )
        if isinstance(attr, _types.ModuleType):
            mod = EmbedPyModule(attr, f"{self.module_name}.{name}")
            mod.set_context(self.context).set_pos(self.pos_start, self.pos_end)
            return mod, None
        if callable(attr):
            fn = EmbedPyCallable(attr, f"{self.module_name}.{name}")
            fn.set_context(self.context).set_pos(self.pos_start, self.pos_end)
            return fn, None
        result = _python_to_lynx(attr, self.context, self.pos_start, self.pos_end)
        return result, None

    def copy(self):
        c = EmbedPyModule(self.py_module, self.module_name)
        c.set_context(self.context)
        c.set_pos(self.pos_start, self.pos_end)
        return c

    def __repr__(self):
        return f"<embedPy module: {self.module_name}>"

class EmbedPyNamespace(Value):
    """The root ``embedPy`` namespace."""

    def get_attr(self, name):
        import builtins as _builtins
        import importlib as _importlib

        builtin = getattr(_builtins, name, None)
        if builtin is not None and callable(builtin):
            fn = EmbedPyCallable(builtin, name)
            fn.set_context(self.context).set_pos(self.pos_start, self.pos_end)
            return fn, None

        try:
            mod = _importlib.import_module(name)
            em = EmbedPyModule(mod, name)
            em.set_context(self.context).set_pos(self.pos_start, self.pos_end)
            return em, None
        except ImportError:
            pass

        # 3. Not found anywhere
        return None, RTError(
            self.pos_start, self.pos_end,
            f"embedPy: '{name}' is not a Python builtin and cannot be imported as a module. "
            f"If it is a third-party package, install it first (pip install {name}).",
            self.context,
        )

    def copy(self):
        c = EmbedPyNamespace()
        c.set_context(self.context)
        c.set_pos(self.pos_start, self.pos_end)
        return c

    def __repr__(self):
        return "<embedPy>"

# end embedPy

class Module(Value):
    def __init__(self, name, symbol_table):
        super().__init__()
        self.name = name
        self.module_symbol_table = symbol_table
        self.global_ns = Namespace(symbol_table)

    def get_attr(self, name):
        if name == "global":
            return self.global_ns, None
        val = self.module_symbol_table.get(name)
        if val is not None:
            return val, None
        return None, RTError(
            self.pos_start,
            self.pos_end,
            f"Module '{self.name}' has no attribute '{name}'",
            self.context,
        )

    def copy(self):
        c = Module(self.name, self.module_symbol_table)
        c.global_ns = self.global_ns
        c.set_pos(self.pos_start, self.pos_end)
        c.set_context(self.context)
        return c

    def __repr__(self):
        return f"<module {self.name}>"

class ClassRegistry(Value):
    """Namespace of all class blueprints."""

    def __init__(self):
        super().__init__()
        self._classes = {}   # name -> ClassBlueprint

    def register(self, name, blueprint):
        self._classes[name] = blueprint

    def get_attr(self, name):
        if name in self._classes:
            return self._classes[name], None
        return None, RTError(
            self.pos_start, self.pos_end,
            f"No class '{name}' defined",
            self.context,
        )

    def copy(self):
        c = ClassRegistry()
        c._classes = self._classes
        c.set_pos(self.pos_start, self.pos_end)
        c.set_context(self.context)
        return c

    def __repr__(self):
        return f"<class registry: {list(self._classes.keys())}>"

class ClassBlueprint(Value):
    """The reusable definition of a Lynxer class.

    A blueprint keeps the legacy ``global.class.Name`` access path, but
    ``new Name(...)`` creates a separate :class:`ClassInstance` from it.
    """

    def __init__(self, name, field_defs, methods):
        """field_defs : list of (type_str,."""
        super().__init__()
        self.name = name
        self._field_defs = field_defs   # kept for repr / introspection
        self._methods = methods
        self._fields = {}

    # attribute access

    def get_attr(self, name):
        if name in self._fields:
            return self._fields[name]["value"], None
        if name in self._methods:
            bound = BoundMethod(self._methods[name], self)
            bound.set_pos(self.pos_start, self.pos_end)
            bound.set_context(self.context)
            return bound, None
        return None, RTError(
            self.pos_start, self.pos_end,
            f"Class '{self.name}' has no field or method '{name}'",
            self.context,
        )

    def set_attr(self, name, value):
        """Typed dot-assignment: str global.class.ClassName.field = value"""
        if name not in self._fields:
            return RTError(
                self.pos_start, self.pos_end,
                f"Class '{self.name}' has no field '{name}'",
                self.context,
            )
        if self._fields[name].get("const"):
            return RTError(
                self.pos_start, self.pos_end,
                f"Field '{name}' of class '{self.name}' is const and cannot be changed",
                self.context,
            )
        decl_type = self._fields[name]["type"]
        if not type_matches(decl_type, value):
            return RTError(
                self.pos_start, self.pos_end,
                f"Field '{name}' of class '{self.name}' is declared as "
                f"'{decl_type}' but received a '{value_type_name(value)}' value",
                self.context,
            )
        self._fields[name]["value"] = value
        return None

    def instantiate(self, args, context):
        """Create and initialise one independent instance."""
        res = RTResult()
        instance = ClassInstance(self)
        instance.set_context(context)

        # Field defaults are expressions, not shared runtime values.  Evaluate
        # each one for every instance and expose ``this`` while doing so.
        init_context = Context(
            f"{self.name} instance initializer",
            context,
            self.pos_start,
        )
        init_context.symbol_table = SymbolTable(
            context.symbol_table if context is not None else None
        )
        init_context.symbol_table.set("this", instance, decl_type=self.name)

        for field_type, field_name, value_node, is_const in self._field_defs:
            value = res.register(
                _shared_interpreter().visit(value_node, init_context)
            )
            if res.should_return():
                return res
            if field_type == "tuple" and isinstance(value, List):
                value = LynxTuple(value.elements).set_context(init_context)
            if field_type == "char" and isinstance(value, String):
                if len(value.value) != 1:
                    return res.failure(RTError(
                        value_node.pos_start,
                        value_node.pos_end,
                        f"Field '{field_name}' is declared as 'char' but got a "
                        f"string of length {len(value.value)}",
                        init_context,
                    ))
                value = Char(value.value).set_context(init_context)
            if not type_matches(field_type, value):
                return res.failure(RTError(
                    value_node.pos_start,
                    value_node.pos_end,
                    f"Class '{self.name}': field '{field_name}' is declared as "
                    f"'{field_type}' but the initializer produces a "
                    f"'{value_type_name(value)}' value",
                    init_context,
                ))
            instance._fields[field_name] = {
                "type": field_type,
                "value": value,
                "const": is_const,
            }

        if "init" in self._methods:
            bound = BoundMethod(self._methods["init"], instance)
            bound.set_pos(self.pos_start, self.pos_end).set_context(context)
            call_res = bound.execute(args)
            if call_res.error:
                return call_res

        return res.success(instance)

    # ---- callable: legacy global.class.ClassName() ----

    def execute(self, args):
        """Retain the old static call while accepting constructor arguments.

        Existing programs use ``global.class.Name()`` as a one-time singleton
        initialiser.  Keep that behavior for zero arguments; passing arguments
        returns a real instance, while ``new Name(...)`` is the preferred form.
        """
        res = RTResult()
        if args:
            return self.instantiate(args, self.context)
        if "init" in self._methods:
            bound = BoundMethod(self._methods["init"], self)
            bound.set_pos(self.pos_start, self.pos_end).set_context(self.context)
            call_res = bound.execute([])
            if call_res.error:
                return call_res
        return res.success(Number.null)

    # value protocol

    def copy(self):
        return self

    def __repr__(self):
        field_parts = [
            f"{info['type']} {k} = {info['value']}"
            for k, info in self._fields.items()
        ]
        method_parts = list(self._methods.keys())
        return (
            f"<class {self.name}"
            + (f" fields=[{', '.join(field_parts)}]" if field_parts else "")
            + (f" methods=[{', '.join(method_parts)}]" if method_parts else "")
            + ">"
        )

class ClassInstance(Value):
    """One object created from a :class:`ClassBlueprint`."""

    def __init__(self, blueprint):
        super().__init__()
        self.blueprint = blueprint
        self.class_name = blueprint.name
        self._fields = {}

    def get_attr(self, name):
        if name in self._fields:
            return self._fields[name]["value"], None
        if name in self.blueprint._methods:
            bound = BoundMethod(self.blueprint._methods[name], self)
            bound.set_pos(self.pos_start, self.pos_end)
            bound.set_context(self.context)
            return bound, None
        return None, RTError(
            self.pos_start,
            self.pos_end,
            f"Instance of class '{self.class_name}' has no field or method '{name}'",
            self.context,
        )

    def set_attr(self, name, value):
        if name not in self._fields:
            return RTError(
                self.pos_start,
                self.pos_end,
                f"Instance of class '{self.class_name}' has no field '{name}'",
                self.context,
            )
        field = self._fields[name]
        if field.get("const"):
            return RTError(
                self.pos_start,
                self.pos_end,
                f"Field '{name}' of instance '{self.class_name}' is const and cannot be changed",
                self.context,
            )
        if not type_matches(field["type"], value):
            return RTError(
                self.pos_start,
                self.pos_end,
                f"Field '{name}' of instance '{self.class_name}' is declared as "
                f"'{field['type']}' but received a '{value_type_name(value)}' value",
                self.context,
            )
        field["value"] = value
        return None

    def copy(self):
        # Instances are identity-bearing objects; assignments must not clone
        # the receiver that methods and fields refer to.
        return self

    def __repr__(self):
        parts = [
            f"{info['type']} {name} = {info['value']}"
            for name, info in self._fields.items()
        ]
        return f"<{self.class_name} instance" + (
            f" fields=[{', '.join(parts)}]" if parts else ""
        ) + ">"

class StructBlueprint(ClassBlueprint):
    """A data-only struct definition with required positional fields."""

    def __init__(self, name, field_defs):
        super().__init__(name, field_defs, {})

    def instantiate(self, args, context):
        res = RTResult()
        if len(args) != len(self._field_defs):
            return res.failure(RTError(
                self.pos_start,
                self.pos_end,
                f"Struct '{self.name}' expects {len(self._field_defs)} "
                f"argument(s), got {len(args)}",
                context,
            ))

        instance = StructInstance(self)
        instance.set_context(context)
        for (field_type, field_name, _unused, _is_const), value in zip(
            self._field_defs, args
        ):
            if field_type == "tuple" and isinstance(value, List):
                value = LynxTuple(value.elements).set_context(context)
            if field_type == "char" and isinstance(value, String):
                if len(value.value) != 1:
                    return res.failure(RTError(
                        self.pos_start,
                        self.pos_end,
                        f"Struct '{self.name}' field '{field_name}' expects "
                        "a single character",
                        context,
                    ))
                value = Char(value.value).set_context(context)
            if not type_matches(field_type, value):
                return res.failure(RTError(
                    self.pos_start,
                    self.pos_end,
                    f"Struct '{self.name}' field '{field_name}' is declared "
                    f"as '{field_type}' but received a "
                    f"'{value_type_name(value)}' value",
                    context,
                ))
            instance._fields[field_name] = {
                "type": field_type,
                "value": value,
                "const": False,
            }
        return res.success(instance)

    def execute(self, args):
        return self.instantiate(args, self.context)

    def __repr__(self):
        fields = ", ".join(
            f"{field_type} {field_name}"
            for field_type, field_name, _value, _const in self._field_defs
        )
        return f"<struct {self.name}({fields})>"

class StructInstance(ClassInstance):
    """One mutable, data-only value created from a :class:`StructBlueprint`."""

    def get_attr(self, name):
        if name not in self._fields:
            return None, RTError(
                self.pos_start,
                self.pos_end,
                f"Struct '{self.class_name}' has no field '{name}'",
                self.context,
            )
        return self._fields[name]["value"], None

    def __repr__(self):
        parts = [
            f"{info['type']} {name} = {info['value']}"
            for name, info in self._fields.items()
        ]
        return f"<{self.class_name} struct" + (
            f" fields=[{', '.join(parts)}]" if parts else ""
        ) + ">"

class BoundMethod(Value):
    """A class method bound to one class blueprint or instance."""

    def __init__(self, func, receiver):
        super().__init__()
        self.func = func
        self.receiver = receiver

    def execute(self, args, code_blocks=None):
        res = RTResult()
        interpreter = _shared_interpreter()
        exec_ctx = self.func.generate_new_context()
        exec_ctx.current_function = self.func
        receiver_type = (
            self.receiver.class_name
            if isinstance(self.receiver, ClassInstance)
            else self.receiver.name
        )
        exec_ctx.symbol_table.set("this", self.receiver, decl_type=receiver_type)
        res.register(
            self.func.check_and_populate_args(
                self.func.param_names,
                args,
                exec_ctx,
                self.func.param_types,
                self.func.param_defaults,
            )
        )
        if res.should_return():
            return res
        code_blocks = code_blocks or []
        if len(code_blocks) != len(self.func.code_block_names):
            return res.failure(RTError(
                self.pos_start,
                self.pos_end,
                f"Function '{self.func.name}' expects exactly "
                f"{len(self.func.code_block_names)} code block(s), but got "
                f"{len(code_blocks)}",
                exec_ctx,
            ))
        for block_name, block_value in zip(self.func.code_block_names, code_blocks):
            block_value = block_value.copy().set_context(exec_ctx)
            exec_ctx.symbol_table.set(block_name, block_value)
            exec_ctx.code_blocks[block_name] = block_value
        exec_ctx.symbol_table.set("local", LocalNamespace(exec_ctx.symbol_table))
        res.register(interpreter.visit(self.func.body_node, exec_ctx))
        if res.should_return() and res.func_return_value is None:
            return res
        ret_value = (
            res.func_return_value
            if res.func_return_value is not None
            else Number.null
        )
        return res.success(ret_value)

    def copy(self):
        c = BoundMethod(self.func, self.receiver)
        c.set_pos(self.pos_start, self.pos_end)
        c.set_context(self.context)
        return c

    def __repr__(self):
        owner = (
            self.receiver.class_name
            if isinstance(self.receiver, ClassInstance)
            else self.receiver.name
        )
        return f"<bound method {self.func.name} of {owner}>"

class VarGroup(Value):
    """Runtime representation of a vargroup."""

    def __init__(self, name, kind="vargroup"):
        super().__init__()
        self.name = name
        self.kind = kind
        self._fields = {}

    # attribute access

    def get_attr(self, name):
        if name not in self._fields:
            return None, RTError(
                self.pos_start,
                self.pos_end,
                f"vargroup '{self.name}' has no field '{name}'",
                self.context,
            )
        return self._fields[name]["value"], None

    def set_attr(self, name, value):
        if name not in self._fields:
            return RTError(
                self.pos_start,
                self.pos_end,
                f"vargroup '{self.name}' has no field '{name}'",
                self.context,
            )
        if self._fields[name].get("const"):
            return RTError(
                self.pos_start,
                self.pos_end,
                f"Field '{name}' of vargroup '{self.name}' is const and cannot be changed",
                self.context,
            )
        decl_type = self._fields[name]["type"]
        if not type_matches(decl_type, value):
            return RTError(
                self.pos_start,
                self.pos_end,
                f"Field '{name}' of vargroup '{self.name}' is declared as "
                f"'{decl_type}' but received a '{value_type_name(value)}' value",
                self.context,
            )
        self._fields[name]["value"] = value
        return None

    def add_field(self, field_type, name, value):
        if name in self._fields:
            return RTError(
                self.pos_start,
                self.pos_end,
                f'Duplicate field "{name}" in vargroup \'{self.name}\'',
                self.context,
            )
        self._fields[name] = {"type": field_type, "value": value, "const": False}
        return None

    def remove_field(self, name):
        """Remove a field by name.  Returns RTError if not found."""
        if name not in self._fields:
            return RTError(
                self.pos_start,
                self.pos_end,
                f"vargroup '{self.name}' has no field '{name}'",
                self.context,
            )
        del self._fields[name]
        return None

    # value protocol

    def copy(self):
        return self

    def __repr__(self):
        parts = []
        for k, info in self._fields.items():
            prefix = "const " if info.get("const") else ""
            parts.append(f"{prefix}{info['type']} {k} = {info['value']}")
        return f"{self.kind} {self.name} " + "{ " + ", ".join(parts) + " }"

# context

class Context:
    def __init__(self, display_name, parent=None, parent_entry_pos=None):
        self.display_name = display_name
        self.parent = parent
        self.parent_entry_pos = parent_entry_pos
        self.symbol_table: Any = None
        self.current_function: Any = None      # the Function/AsyncFunction currently executing
        self.current_global_path: list[str] | None = None
        self.code_blocks = parent.code_blocks if parent is not None else {}

# symbol table

class SymbolTable:
    def __init__(self, parent=None):
        self.symbols = {}
        self.constants = set()
        self.types = {}
        self.aliases = {}
        self.references = {}
        self.parent = parent
        # Ownership metadata is shared by nested scopes so a borrow remains
        # visible while the borrowed source is reached through a child table.
        self._ownership = (
            parent._ownership if parent is not None else {}
        )
        self._borrow_sources = (
            parent._borrow_sources if parent is not None else {}
        )
        self._borrowers = parent._borrowers if parent is not None else {}
        self._borrow_modes = parent._borrow_modes if parent is not None else {}
        self._runtime_types = parent._runtime_types if parent is not None else {}
        # Names introduced by ``switch`` pattern bindings. Shared with nested
        # scopes like the ownership tables so a later switch in the same
        # function may reuse a pattern name, while shadowing a real variable
        # stays an error.
        self._pattern_bindings = (
            parent._pattern_bindings if parent is not None else set()
        )

    def _find(self, name):
        table = self
        while table:
            if name in table.symbols or name in table.aliases:
                return table
            table = table.parent
        return None

    def _resolve(self, name):
        table = self._find(name)
        if table is None:
            return None, None
        seen = set()
        while name in table.aliases:
            marker = (id(table), name)
            if marker in seen:
                return None, None
            seen.add(marker)
            table, name = table.aliases[name]
        return table, name

    def _local_reference(self, name):
        """Return the table/name that directly owns ``name``."""
        table = self._find(name)
        return (table, name) if table is not None else (None, None)

    def _canonical_reference(self, name_or_reference):
        if isinstance(name_or_reference, tuple):
            table, name = name_or_reference
            if table is None or not isinstance(name, str):
                return None, None
            return table._resolve(name)
        return self._resolve(name_or_reference)

    @staticmethod
    def _reference_key(reference):
        table, name = reference
        return (table, name) if table is not None and name is not None else None

    def _ownership_state(self, name):
        local_table, local_name = self._local_reference(name)
        if local_table is None:
            return None
        local_key = self._reference_key((local_table, local_name))
        if local_key in self._borrow_sources:
            return "borrowed"
        canonical = self._canonical_reference((local_table, local_name))
        key = self._reference_key(canonical)
        return self._ownership.get(key, "live") if key is not None else None

    def _active_borrowers(self, reference):
        canonical = self._canonical_reference(reference)
        key = self._reference_key(canonical)
        return self._borrowers.get(key, set()) if key is not None else set()

    def _detach_borrow(self, local_key):
        source_key = self._borrow_sources.pop(local_key, None)
        self._borrow_modes.pop(local_key, None)
        if source_key is None:
            return
        borrowers = self._borrowers.get(source_key)
        if borrowers is not None:
            borrowers.discard(local_key)
            if not borrowers:
                self._borrowers.pop(source_key, None)

    def ownership_error(self, name, operation):
        """Return a user-facing ownership error, or ``None`` when allowed."""
        local_table = self._find(name)
        if local_table is None:
            return f"'{name}' is not defined"
        local_key = self._reference_key((local_table, name))
        canonical_table, canonical_name = local_table, name
        seen = set()
        while canonical_name in canonical_table.aliases:
            marker = (id(canonical_table), canonical_name)
            if marker in seen:
                return f"'{name}' has an invalid alias chain"
            seen.add(marker)
            canonical_table, canonical_name = canonical_table.aliases[canonical_name]
        canonical = self._reference_key((canonical_table, canonical_name))
        if local_key in self._borrow_sources:
            state = "borrowed"
        else:
            state = self._ownership.get(canonical, "live")
        if state == "moved":
            if operation == "write to":
                return None
            action = "move from" if operation == "move" else operation
            return (
                f"Cannot {action} moved variable '{name}'; "
                "reinitialize it before using it again"
            )
        if state == "borrowed" and operation in {"transfer into", "borrow into"}:
            return (
                f"Cannot {operation} borrowed variable '{name}'; "
                "end its borrow first"
            )
        if (
            state == "borrowed"
            and operation == "write to"
            and self._borrow_modes.get(local_key) != "mutable"
        ):
            return (
                f"Cannot write to borrowed variable '{name}'; "
                "the borrow is read-only"
            )
        if (
            operation in {"write to", "move"}
            and not (
                state == "borrowed"
                and self._borrow_modes.get(local_key) == "mutable"
            )
            and self._active_borrowers(canonical)
        ):
            return (
                f"Cannot {operation} '{name}' while it is being borrowed; "
                "end all active borrows first"
            )
        return None

    def mark_reinitialized(self, name):
        """Mark a moved variable live after a successful assignment."""
        canonical = self._canonical_reference(name)
        key = self._reference_key(canonical)
        if key is not None:
            self._ownership[key] = "live"

    def transfer(self, source_reference, destination_reference):
        """Move a variable value into an existing destination variable."""
        source_table, source_name = self._canonical_reference(source_reference)
        destination_table, destination_name = self._local_reference(
            destination_reference[1]
            if isinstance(destination_reference, tuple)
            else destination_reference
        )
        if source_table is None or source_name is None:
            return "source variable is not defined"
        if destination_table is None or destination_name is None:
            return "destination variable is not defined"
        destination_key = self._reference_key((destination_table, destination_name))
        source_key = self._reference_key((source_table, source_name))
        if source_key == destination_key:
            return "a variable cannot be transferred to itself"
        if destination_name in destination_table.aliases:
            return (
                f"Cannot transfer into shared variable '{destination_name}'; "
                "use an independent destination"
            )
        source_error = self.ownership_error(source_name, "move")
        if source_error:
            return source_error
        if self.is_const(source_name):
            return f"Cannot transfer constant '{source_name}'"
        if self._active_borrowers((source_table, source_name)):
            return (
                f"Cannot move '{source_name}' while it is being borrowed; "
                "end all active borrows first"
            )
        if self.is_const(destination_name):
            return f"Cannot transfer into constant '{destination_name}'"
        if self._active_borrowers((destination_table, destination_name)):
            return (
                f"Cannot transfer into '{destination_name}' while it is being "
                "borrowed; end all active borrows first"
            )
        source_value = source_table.get(source_name)
        if source_value is None:
            return f"source variable '{source_name}' has no value"
        destination_type = destination_table.types.get(destination_name)
        if not type_matches(destination_type, source_value):
            return (
                f"Type mismatch: '{destination_name}' is declared as "
                f"'{destination_type}' but got a '{value_type_name(source_value)}' value"
            )
        destination_value = source_value.copy()
        destination_table.symbols[destination_name] = destination_value
        destination_table.aliases.pop(destination_name, None)
        destination_table.references.pop(destination_name, None)
        self._ownership[destination_key] = "live"
        self._ownership[source_key] = "moved"
        return None

    def transfer_mutate(self, source_reference, destination_reference):
        """Move a value and retag an ``any``/``num`` destination."""
        source_table, source_name = self._canonical_reference(source_reference)
        destination_table, destination_name = self._local_reference(
            destination_reference[1] if isinstance(destination_reference, tuple)
            else destination_reference
        )
        if source_table is None or source_name is None:
            return "Cannot transfer from an undefined source variable"
        if destination_table is None or destination_name is None:
            return "Cannot transfer into an undefined destination variable"
        source_key = self._reference_key((source_table, source_name))
        destination_key = self._reference_key((destination_table, destination_name))
        if source_key == destination_key:
            return "a variable cannot be transferred to itself"
        source_error = self.ownership_error(source_name, "move")
        if source_error:
            return source_error
        if self.is_const(source_name):
            return f"Cannot transfer constant '{source_name}'"
        if self.is_const(destination_name):
            return f"Cannot transfer into constant '{destination_name}'"
        if self._active_borrowers((source_table, source_name)):
            return (
                f"Cannot move '{source_name}' while it is being borrowed; "
                "end all active borrows first"
            )
        value = source_table.get(source_name)
        if value is None:
            return f"'{source_name}' is not defined"
        declared = destination_table.types.get(destination_name)
        if declared not in (None, "any", "num") and not type_matches(declared, value):
            return (
                f"Type mismatch: '{destination_name}' is declared as "
                f"'{declared}' and cannot mutate to '{value_type_name(value)}'"
            )
        destination_table.symbols[destination_name] = value.copy()
        destination_table.aliases.pop(destination_name, None)
        destination_table.references.pop(destination_name, None)
        self._runtime_types[destination_key] = value_type_name(value)
        self._ownership[destination_key] = "live"
        self._ownership[source_key] = "moved"
        return None

    def _swap_references(
        self, first_reference, second_reference
    ) -> tuple[Any, Any, Any, Any, str | None]:
        first_table, first_name = self._local_reference(
            first_reference[1]
            if isinstance(first_reference, tuple)
            else first_reference
        )
        second_table, second_name = self._local_reference(
            second_reference[1]
            if isinstance(second_reference, tuple)
            else second_reference
        )
        if first_table is None or first_name is None:
            return None, None, None, None, "first variable is not defined"
        if second_table is None or second_name is None:
            return None, None, None, None, "second variable is not defined"

        first_key = self._reference_key((first_table, first_name))
        second_key = self._reference_key((second_table, second_name))
        if first_key == second_key:
            return None, None, None, None, "a variable cannot be swapped with itself"

        for table, name, label in (
            (first_table, first_name, "first"),
            (second_table, second_name, "second"),
        ):
            if name in table.aliases:
                return (
                    None, None, None, None,
                    (
                        f"Cannot swap {label} shared variable '{name}'; "
                        "use independent variables"
                    ),
                )
            if self.is_const(name):
                return (
                    None, None, None, None,
                    f"Cannot swap {label} constant '{name}'",
                )
            state = self._ownership_state(name)
            if state == "moved":
                return (
                    None, None, None, None,
                    (
                        f"Cannot swap {label} moved variable '{name}'; "
                        "reinitialize it before swapping"
                    ),
                )
            if state == "borrowed":
                return (
                    None, None, None, None,
                    (
                        f"Cannot swap {label} borrowed variable '{name}'; "
                        "end its borrow first"
                    ),
                )
            if self._active_borrowers((table, name)):
                return (
                    None, None, None, None,
                    (
                        f"Cannot swap {label} variable '{name}' while it is being "
                        "borrowed; end all active borrows first"
                    ),
                )

        first_value = first_table.get(first_name)
        second_value = second_table.get(second_name)
        if first_value is None or second_value is None:
            return None, None, None, None, "both variables must have values"
        return (
            first_table,
            first_name,
            second_table,
            second_name,
            None,
        )

    def _swap_values(self, first_table, first_name, second_table, second_name,
                     first_value, second_value):
        first_table.update_existing(first_name, second_value.copy())
        second_table.update_existing(second_name, first_value.copy())

    def swap_all(self, first_reference, second_reference):
        """Atomically exchange values and declared type metadata."""
        (
            first_table,
            first_name,
            second_table,
            second_name,
            error,
        ) = self._swap_references(first_reference, second_reference)
        if error:
            return error

        first_type = first_table.types.get(first_name)
        second_type = second_table.types.get(second_name)
        first_value = first_table.get(first_name)
        second_value = second_table.get(second_name)
        self._swap_values(
            first_table,
            first_name,
            second_table,
            second_name,
            first_value,
            second_value,
        )
        if second_type is None:
            first_table.types.pop(first_name, None)
        else:
            first_table.types[first_name] = second_type
        if first_type is None:
            second_table.types.pop(second_name, None)
        else:
            second_table.types[second_name] = first_type
        return None

    def swap_values(self, first_reference, second_reference):
        """Atomically exchange values while retaining declared types."""
        (
            first_table,
            first_name,
            second_table,
            second_name,
            error,
        ) = self._swap_references(first_reference, second_reference)
        if error:
            return error

        first_value = first_table.get(first_name)
        second_value = second_table.get(second_name)
        first_type = first_table.types.get(first_name)
        second_type = second_table.types.get(second_name)
        if not type_matches(first_type, second_value):
            return (
                f"Type mismatch: '{first_name}' is declared as "
                f"'{first_type}' but got a '{value_type_name(second_value)}' value"
            )
        if not type_matches(second_type, first_value):
            return (
                f"Type mismatch: '{second_name}' is declared as "
                f"'{second_type}' but got a '{value_type_name(first_value)}' value"
            )
        self._swap_values(
            first_table,
            first_name,
            second_table,
            second_name,
            first_value,
            second_value,
        )
        return None

    def borrow(self, source_reference, destination_reference):
        """Create a read-only tracked alias in an existing destination."""
        source_table, source_name = self._canonical_reference(source_reference)
        destination_table, destination_name = self._local_reference(
            destination_reference[1]
            if isinstance(destination_reference, tuple)
            else destination_reference
        )
        if source_table is None or source_name is None:
            return "source variable is not defined"
        if destination_table is None or destination_name is None:
            return "destination variable is not defined"
        source_key = self._reference_key((source_table, source_name))
        destination_key = self._reference_key((destination_table, destination_name))
        if source_key == destination_key:
            return "a variable cannot borrow from itself"
        if destination_name in destination_table.aliases:
            return (
                f"Cannot borrow into shared variable '{destination_name}'; "
                "end or detach that alias first"
            )
        source_error = self.ownership_error(source_name, "borrow from")
        if source_error:
            return source_error
        if self.is_const(destination_name):
            return f"Cannot borrow into constant '{destination_name}'"
        if destination_key in self._borrow_sources:
            return (
                f"Variable '{destination_name}' is already borrowing; "
                "end its borrow first"
            )
        source_value = source_table.get(source_name)
        if source_value is None:
            return f"source variable '{source_name}' has no value"
        destination_type = destination_table.types.get(destination_name)
        if not type_matches(destination_type, source_value):
            return (
                f"Type mismatch: '{destination_name}' is declared as "
                f"'{destination_type}' but got a '{value_type_name(source_value)}' value"
            )
        self._detach_borrow(destination_key)
        destination_table.symbols.pop(destination_name, None)
        destination_table.references[destination_name] = source_table.get_reference(source_name)
        destination_table.aliases[destination_name] = (source_table, source_name)
        self._borrow_sources[destination_key] = source_key
        self._borrow_modes[destination_key] = "readonly"
        self._borrowers.setdefault(source_key, set()).add(destination_key)
        self._ownership[destination_key] = "borrowed"
        return None

    def borrow_mutate(self, source_reference, destination_reference):
        """Create an exclusive mutable borrow into an existing variable."""
        source_table, source_name = self._canonical_reference(source_reference)
        destination_table, destination_name = self._local_reference(
            destination_reference[1] if isinstance(destination_reference, tuple)
            else destination_reference
        )
        if source_table is None or source_name is None:
            return "Cannot mutably borrow an undefined source variable"
        if destination_table is None or destination_name is None:
            return "Cannot mutably borrow into an undefined destination variable"
        source_key = self._reference_key((source_table, source_name))
        destination_key = self._reference_key((destination_table, destination_name))
        if source_key == destination_key:
            return "a variable cannot borrow from itself"
        source_error = self.ownership_error(source_name, "borrow from")
        if source_error:
            return source_error
        if self._active_borrowers(source_key):
            return (
                f"Cannot mutably borrow '{source_name}' while it has active borrows; "
                "end all active borrows first"
            )
        if destination_key in self._borrow_sources:
            return f"Variable '{destination_name}' is already borrowing"
        if self.is_const(destination_name):
            return f"Cannot borrow into constant '{destination_name}'"
        value = source_table.get(source_name)
        declared = destination_table.types.get(destination_name)
        if declared not in (None, "any", "num") and not type_matches(declared, value):
            return (
                f"Type mismatch: '{destination_name}' is declared as "
                f"'{declared}' and cannot mutate to '{value_type_name(value)}'"
            )
        destination_table.symbols.pop(destination_name, None)
        destination_table.references[destination_name] = source_table.get_reference(source_name)
        destination_table.aliases[destination_name] = (source_table, source_name)
        self._borrow_sources[destination_key] = source_key
        self._borrow_modes[destination_key] = "mutable"
        self._borrowers.setdefault(source_key, set()).add(destination_key)
        self._ownership[destination_key] = "borrowed"
        return None

    def end_borrow(self, reference):
        """End a borrow and turn the borrower into an independent value."""
        table, name = self._local_reference(
            reference[1] if isinstance(reference, tuple) else reference
        )
        if table is None:
            return "varEndBorrow() expects a defined variable"
        key = self._reference_key((table, name))
        if key not in self._borrow_sources:
            return (
                f"'{name}' is not an active borrow; "
                "varEndBorrow() expects a borrowing variable"
            )
        source_key = self._borrow_sources[key]
        source_table, source_name = source_key
        value = source_table.get(source_name)
        if value is None:
            return f"borrowed source '{source_name}' is no longer available"
        self._detach_borrow(key)
        table.aliases.pop(name, None)
        table.references.pop(name, None)
        table.symbols[name] = value.copy()
        self._ownership[key] = "live"
        return None

    def is_borrowing(self, reference):
        table, name = self._local_reference(
            reference[1] if isinstance(reference, tuple) else reference
        )
        return self._reference_key((table, name)) in self._borrow_sources

    def is_being_borrowed(self, reference):
        canonical = self._canonical_reference(reference)
        return bool(self._active_borrowers(canonical))

    def get(self, name):
        table, resolved_name = self._resolve(name)
        if table is None or resolved_name is None:
            return None
        pointer = table.references.get(resolved_name)
        if pointer:
            return _get_cpp().refGet(pointer)
        return table.symbols.get(resolved_name)

    def get_reference(self, name):
        table, resolved_name = self._resolve(name)
        if table is None or resolved_name is None:
            return None
        pointer = table.references.get(resolved_name)
        if pointer:
            return pointer
        value = table.symbols.get(resolved_name)
        if value is None:
            return None
        pointer = _get_cpp().refCreate(value)
        table.references[resolved_name] = pointer
        return pointer

    def set(self, name, value, is_const=False, decl_type=None):
        local_key = self._reference_key((self, name))
        self._detach_borrow(local_key)
        self.aliases.pop(name, None)
        pointer = self.references.get(name)
        if pointer:
            _get_cpp().refSet(pointer, value)
        self.symbols[name] = value
        if is_const:
            self.constants.add(name)
        if decl_type is not None:
            self.types[name] = decl_type
        self._ownership[local_key] = "live"

    def update_existing(self, name, value):
        table, resolved_name = self._resolve(name)
        if table is not None and resolved_name is not None:
            pointer = table.references.get(resolved_name)
            if pointer:
                _get_cpp().refSet(pointer, value)
            table.symbols[resolved_name] = value
            return table
        self.symbols[name] = value
        return self

    def is_const(self, name):
        table = self._find(name)
        if table is None:
            return False
        resolved_table, resolved_name = self._resolve(name)
        return name in table.constants or (
            resolved_table is not None and resolved_name in resolved_table.constants
        )

    def get_type(self, name):
        table = self._find(name)
        return table.types.get(name) if table else None

    def share(self, name, target):
        target_table, target_name = self._resolve(target)
        if target_table is None or target_name is None:
            return False
        if self._active_borrowers((target_table, target_name)):
            return False
        target_pointer = self.get_reference(target)
        if target_pointer is None:
            return False
        self.symbols.pop(name, None)
        self.references[name] = target_pointer
        self.aliases[name] = (target_table, target_name)
        self.types[name] = self.get_type(target) or self.types.get(name)
        return True

    def share_reference(self, name, target_table, target_name):
        if target_table is None or target_table._resolve(target_name)[0] is None:
            return False
        resolved_target = target_table._resolve(target_name)
        if target_table._active_borrowers(resolved_target):
            return False
        target_pointer = target_table.get_reference(target_name)
        if target_pointer is None:
            return False
        self.symbols.pop(name, None)
        self.references[name] = target_pointer
        self.aliases[name] = (target_table, target_name)
        return True

    def unshare(self, name):
        table = self._find(name)
        if table is None or name not in table.aliases:
            return False
        if self.is_borrowing((table, name)):
            return False
        value = self.get(name)
        if value is None:
            return False
        table.aliases.pop(name)
        table.symbols[name] = value.copy()
        table.references[name] = _get_cpp().refCreate(table.symbols[name])
        return True

    def remove(self, name):
        del self.symbols[name]

