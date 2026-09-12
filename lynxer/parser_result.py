"""Backtracking result object used by the recursive-descent parser."""

from __future__ import annotations

from typing import Any

from .error import Error


class ParseResult:
    """Accumulate a parsed node, diagnostics, and token advancement."""

    def __init__(self) -> None:
        self.error: Error | None = None
        self.node: Any = None
        self.last_registered_advance_count = 0
        self.advance_count = 0
        self.to_reverse_count = 0

    def register_advancement(self) -> None:
        self.last_registered_advance_count = 1
        self.advance_count += 1

    def register(self, result: "ParseResult") -> Any:
        self.last_registered_advance_count = result.advance_count
        self.advance_count += result.advance_count
        if result.error:
            self.error = result.error
        return result.node

    def try_register(self, result: "ParseResult") -> Any:
        if result.error:
            self.to_reverse_count = result.advance_count
            return None
        return self.register(result)

    def success(self, node: Any) -> "ParseResult":
        self.node = node
        return self

    def failure(self, error: Error) -> "ParseResult":
        if not self.error or self.last_registered_advance_count == 0:
            self.error = error
        return self
