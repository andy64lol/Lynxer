"""Lynxer built-in functions and their runtime registry.

The implementation base class remains in ``lynxer.py`` for the moment because
the interpreter value types are defined there.  This module is the public
extension point: built-ins are exposed through :class:`BuiltInFunction`, and
new built-ins should be added with :func:`register_builtin` instead of
changing the interpreter's global/module setup code.
"""

from __future__ import annotations

from collections.abc import Callable
from typing import Any

from . import lynxer as _runtime


class BuiltInFunction(_runtime._BuiltinFunctionRuntime):
    """Public built-in function type and extension point.

    The runtime base supplies the existing ``execute_<name>`` implementations.
    This subclass owns the instances that are installed into Lynxer symbol
    tables, keeping registration and future extensions in this module.
    """


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
    "asyncRun",
    "asyncGather",
    "asyncSleep",
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
    "overrideMain",
)


BUILTIN_FUNCTIONS: dict[str, BuiltInFunction] = {}


def register_builtin(name: str, handler: BuiltinHandler | None = None) -> BuiltInFunction:
    """Register and return a built-in function.

    ``handler`` is an optional callable receiving ``(builtin, args,
    exec_ctx)`` and returning an ``RTResult``.  Existing handlers are inherited
    from the runtime base class, so the common case is simply adding a name
    whose ``execute_<name>`` method is defined in this module.
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


# Reuse the implementations currently shared by the interpreter while giving
# the public instances the correct module and a single registry.
for _name in BUILTIN_FUNCTION_NAMES:
    register_builtin(_name)
