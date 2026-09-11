"""Lynxer diagnostic and runtime error types.

Errors, parser/lexer diagnostics, and the deprecation-warning machinery all
live here so every other pipeline stage can raise and report them without
importing the interpreter.
"""

from __future__ import annotations

import os
import sys
import warnings
from typing import Any

try:
    from .strings_with_arrows import string_with_arrows
except ImportError:  # lynxer package not importable as a package
    from lynxer.strings_with_arrows import string_with_arrows  # type: ignore[no-redef]

_WARNING_MESSAGES_PATH = os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "warnings.txt"
)


def _warning_message_paths() -> list[str]:
    """Return the source and frozen-bundle locations for the warning catalog."""
    paths = [_WARNING_MESSAGES_PATH]
    frozen_root = getattr(sys, "_MEIPASS", None)
    if frozen_root:
        paths.append(os.path.join(frozen_root, "lynxer", "warnings.txt"))
    return list(dict.fromkeys(paths))

def _load_warning_messages() -> dict[str, str]:
    for warning_path in _warning_message_paths():
        try:
            messages: dict[str, str] = {}
            with open(warning_path, "r", encoding="utf-8") as warning_file:
                for line_number, line in enumerate(warning_file, 1):
                    stripped = line.rstrip("\r\n")
                    if not stripped or stripped.startswith("#"):
                        continue
                    try:
                        key, message = stripped.split("\t", 1)
                    except ValueError as exc:
                        raise RuntimeError(
                            f"Invalid warning message at {warning_path}, "
                            f"line {line_number}: expected a tab-separated key and message"
                        ) from exc
                    if not key or not message:
                        raise RuntimeError(
                            f"Invalid warning message at {warning_path}, "
                            f"line {line_number}: key and message must not be empty"
                        )
                    if key in messages:
                        raise RuntimeError(
                            f"Invalid warning message at {warning_path}, "
                            f"line {line_number}: duplicate key '{key}'"
                        )
                    messages[key] = message
            return messages
        except FileNotFoundError:
            continue
        except OSError as exc:
            raise RuntimeError(
                f"Could not load Lynxer warning messages from {warning_path}"
            ) from exc

    searched_paths = ", ".join(_warning_message_paths())
    raise RuntimeError(
        f"Could not load Lynxer warning messages; searched: {searched_paths}"
    )


_WARNING_MESSAGES = _load_warning_messages()


def warning_message(key: str) -> str:
    try:
        return _WARNING_MESSAGES[key]
    except KeyError as exc:
        raise RuntimeError(f"Unknown Lynxer warning message: {key}") from exc

# Warning suppression state. ``lynxer.runtime.run`` flips these between
# top-level program runs; they stay module-level so the warn helpers above
# always observe the current values.
_forever_warning_suppressed = False
_deprecation_warning_suppressed = False
_deprecation_warning_deferred = False
_pending_deprecation_warnings: list[tuple[Any, str]] = []
# errors

class Error:
    def __init__(self, pos_start, pos_end, error_name, details):
        self.pos_start = pos_start
        self.pos_end = pos_end
        self.error_name = error_name
        self.details = details

    def as_string(self):
        ln = self.pos_start.ln + 1
        col = self.pos_start.col + 1
        fn = self.pos_start.fn
        result = f"\n[Lynxer] {self.error_name}\n"
        result += f"  {self.details}\n"
        suggestion = self.suggestion()
        if suggestion:
            result += f"  Suggestion: {suggestion}\n"
        result += f"  --> {fn}, line {ln}, column {col}\n"
        result += "\n" + string_with_arrows(
            self.pos_start.ftxt, self.pos_start, self.pos_end
        )
        return result

    def suggestion(self):
        details = str(self.details)
        if "Expected ';'" in details:
            return "add a semicolon at the end of this statement."
        if "may only be used inside setup()" in details:
            return "move this import into global setup(){...}."
        if "Expected filename string" in details or "filename string" in details:
            return 'pass the module path as a quoted string, for example import("math");.'
        if "Expected ')'" in details:
            return "close the argument list with ')'."
        if "Expected '}'" in details:
            return "close the block with '}'."
        if "Expected variable name" in details:
            return "provide an identifier after the type or declaration keyword."
        if "Expected int, float, str, bool, none, identifier" in details:
            return "add a value or expression here."
        return ""

class IllegalCharError(Error):
    def __init__(self, pos_start, pos_end, details):
        super().__init__(pos_start, pos_end, "Unexpected Character", details)

class ExpectedCharError(Error):
    def __init__(self, pos_start, pos_end, details):
        super().__init__(pos_start, pos_end, "Missing Character", details)

class InvalidSyntaxError(Error):
    def __init__(self, pos_start, pos_end, details=""):
        super().__init__(pos_start, pos_end, "Syntax Error", details)

class LynxSyntaxDeprecationWarning(UserWarning):
    """A warning for syntax retained only for backwards compatibility."""

class LynxerForeverWarning(UserWarning):
    """A warning for a forever loop that has no visible break statement."""

def warn_legacy_syntax(token, details):
    """Warn at the source location of a legacy syntax form."""
    warn_legacy_syntax_position(token.pos_start, details)

def warn_legacy_syntax_position(pos, details):
    """Warn at a parser node or token source location."""
    if _deprecation_warning_suppressed:
        return
    if _deprecation_warning_deferred:
        _pending_deprecation_warnings.append((pos, details))
        return
    _emit_deprecation_warning(pos, details)


def _emit_deprecation_warning(pos, details):
    warnings.warn_explicit(
        details,
        LynxSyntaxDeprecationWarning,
        pos.fn or "<source>",
        pos.ln + 1,
    )


def _flush_deprecation_warnings():
    pending = list(_pending_deprecation_warnings)
    _pending_deprecation_warnings.clear()
    if _deprecation_warning_suppressed:
        return
    for pos, details in pending:
        _emit_deprecation_warning(pos, details)


def warn_forever_no_break(node):
    """Warn when a forever loop has no visible way to stop."""
    if node.has_break or _forever_warning_suppressed:
        return
    warnings.warn_explicit(
        warning_message("forever_no_break"),
        LynxerForeverWarning,
        node.pos_start.fn or "<source>",
        node.pos_start.ln + 1,
    )

class RTError(Error):
    def __init__(self, pos_start, pos_end, details, context):
        super().__init__(pos_start, pos_end, "Runtime Error", details)
        self.context = context

    def as_string(self):
        result = self.generate_traceback()
        result += f"\n[Lynxer] {self.error_name}\n"
        result += f"  {self.details}\n"
        result += "\n" + string_with_arrows(
            self.pos_start.ftxt, self.pos_start, self.pos_end
        )
        return result

    def generate_traceback(self):
        result = ""
        pos = self.pos_start
        ctx = self.context
        while ctx:
            result = (
                f"  --> {pos.fn}, line {pos.ln + 1}, in {ctx.display_name}\n"
            ) + result
            pos = ctx.parent_entry_pos
            ctx = ctx.parent
        return "Traceback (most recent call last):\n" + result
