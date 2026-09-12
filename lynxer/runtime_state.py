"""Explicit ownership for interpreter state.

The evaluator can still expose a compatibility default, but new callers can
create an isolated :class:`RuntimeContext` and pass it to ``run``.  This
prevents module-level registries from leaking between embedded executions.
"""

from __future__ import annotations

from typing import Any

from .execution import ExecutionState


class RuntimeContext:
    """Own one interpreter, execution state, and global symbol table."""

    def __init__(self) -> None:
        self.execution_state = ExecutionState()
        self.interpreter: Any = None
        self.global_symbol_table: Any = None

    def reset(self) -> None:
        """Discard all run-local state while retaining the context object."""
        self.execution_state.reset()
        self.global_symbol_table = None


def new_runtime_context() -> RuntimeContext:
    """Create an isolated runtime suitable for one program invocation."""
    return RuntimeContext()
