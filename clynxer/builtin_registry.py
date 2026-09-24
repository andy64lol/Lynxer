"""Builtin registration independent of builtin implementations.

The implementation class remains in :mod:`lynxer.builtins` for compatibility,
but registry lifecycle and the public registration API live here.  Keeping
this module free of imports from the implementation avoids the old
runtime → builtins → runtime startup cycle.
"""

from __future__ import annotations

from collections.abc import Callable
from typing import Any, TypeVar


BuiltinHandler = TypeVar("BuiltinHandler", bound=Callable[..., Any])


class BuiltinRegistry:
    """Own builtin instances for one language runtime."""

    def __init__(self, function_type: type[Any]) -> None:
        self.function_type = function_type
        self.functions: dict[str, Any] = {}

    def register(self, name: str, handler: Callable[..., Any] | None = None) -> Any:
        """Create and store one builtin implementation."""
        if not name.isidentifier():
            raise ValueError(f"Invalid builtin name: {name!r}")
        if handler is not None:
            setattr(self.function_type, f"execute_{name}", handler)
        function = self.function_type(name)
        setattr(self.function_type, name, function)
        self.functions[name] = function
        return function

    def install(self, symbol_table: Any, execution_state: Any = None) -> None:
        """Install all registered functions into a symbol table."""
        for name in self.functions:
            # Builtin definitions are process-wide, but their callable values
            # are runtime-owned.  A fresh instance prevents one embedded run
            # from overwriting another run's execution state.
            function = self.function_type(name)
            function.execution_state = execution_state
            symbol_table.set(name, function)
