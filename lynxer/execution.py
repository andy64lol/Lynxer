"""Dependency-neutral execution results and per-run state.

This module is intentionally lower-level than the interpreter and builtin
layers.  It contains only data passed through an execution and therefore can
be reused by the Python and future native implementations without importing
the evaluator.
"""

from __future__ import annotations

from typing import Any


class ExecutionState:
    """Mutable state owned by one independent Lynxer program run."""

    def __init__(self) -> None:
        self.interpreter: Any = None
        self.global_symbol_table: Any = None
        self.rawpy_global_modules: dict[str, Any] = {}
        self.lynx_modules: dict[str, Any] = {}
        self.forever_delay = 0.02
        self.setup_in_progress = False
        self.main_override: str | None = None

    def reset(self) -> None:
        """Clear run-local registries and restore default execution options."""
        self.interpreter = None
        self.global_symbol_table = None
        self.rawpy_global_modules.clear()
        self.lynx_modules.clear()
        self.forever_delay = 0.02
        self.setup_in_progress = False
        self.main_override = None


class RTResult:
    """Result envelope shared by parser visitors and runtime functions."""

    def __init__(self) -> None:
        self.reset()

    def reset(self) -> None:
        self.value: Any = None
        self.error: Any = None
        self.func_return_value: Any = None
        self.loop_should_continue = False
        self.loop_should_break = False

    def register(self, result: "RTResult") -> Any:
        """Copy control-flow flags from a nested result and return its value."""
        self.error = result.error
        self.func_return_value = result.func_return_value
        self.loop_should_continue = result.loop_should_continue
        self.loop_should_break = result.loop_should_break
        return result.value

    def success(self, value: Any) -> "RTResult":
        self.reset()
        self.value = value
        return self

    def success_return(self, value: Any) -> "RTResult":
        self.reset()
        self.func_return_value = value
        return self

    def failure(self, error: Any) -> "RTResult":
        self.reset()
        self.error = error
        return self

    def should_return(self) -> bool:
        return bool(
            self.error
            or self.func_return_value is not None
            or self.loop_should_continue
            or self.loop_should_break
        )
