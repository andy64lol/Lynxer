from __future__ import annotations

import os
import itertools
import string
import sys
import textwrap
import warnings
from typing import TYPE_CHECKING, Any, ClassVar

if TYPE_CHECKING:
    from .builtins import BuiltInFunction

try:
    from strings_with_arrows import string_with_arrows
except ImportError:
    from lynxer.strings_with_arrows import string_with_arrows  # type: ignore[no-redef]

DIGITS = "0123456789"
_SOURCE_DIR = os.path.dirname(os.path.abspath(__file__))
STDLIB_DIR = os.path.join(_SOURCE_DIR, "stdlib")
LETTERS = string.ascii_letters
LETTERS_DIGITS = LETTERS + DIGITS

_cython_inline_fn: Any = None

def _get_cython_inline() -> Any:
    """Lazily import Cython's inline compiler (needs setuptools' distutils shim)."""
    global _cython_inline_fn
    if _cython_inline_fn is None:
        import setuptools  # noqa: F401 — patches distutils for Cython on py3.12+
        # Import by name so Pyright can analyze Lynxer without requiring the
        # optional Cython package to be installed in its analysis environment.
        from importlib import import_module
        _cython_inline_fn = import_module("Cython.Build.Inline").cython_inline
    return _cython_inline_fn

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


def stdlib_dir() -> str:
    """Return the standard-library directory in source and frozen builds."""
    candidates = [STDLIB_DIR]
    frozen_root = getattr(sys, "_MEIPASS", None)
    if frozen_root:
        candidates.extend(
            [
                os.path.join(frozen_root, "stdlib"),
                os.path.join(frozen_root, "lynxer", "stdlib"),
            ]
        )
    for candidate in candidates:
        if os.path.isdir(candidate):
            return candidate
    return candidates[0]


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
        result += f"  --> {fn}, line {ln}, column {col}\n"
        result += "\n" + string_with_arrows(
            self.pos_start.ftxt, self.pos_start, self.pos_end
        )
        return result

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

# position

class Position:
    def __init__(self, idx, ln, col, fn, ftxt):
        self.idx = idx
        self.ln = ln
        self.col = col
        self.fn = fn
        self.ftxt = ftxt

    def advance(self, current_char=None):
        self.idx += 1
        self.col += 1
        if current_char == "\n":
            self.ln += 1
            self.col = 0
        return self

    def copy(self):
        return Position(self.idx, self.ln, self.col, self.fn, self.ftxt)

    # Bytecode serialisation
    def __getstate__(self):
        return (self.idx, self.ln, self.col, self.fn)

    def __setstate__(self, state):
        self.idx, self.ln, self.col, self.fn = state
        self.ftxt = ""

# tokens

TT_INT = "INT"
TT_FLOAT = "FLOAT"
TT_STRING = "STRING"
TT_CHAR   = "CHAR"
TT_IDENTIFIER = "IDENTIFIER"
TT_KEYWORD = "KEYWORD"
TT_PLUS = "PLUS"
TT_MINUS = "MINUS"
TT_MUL = "MUL"
TT_DIV = "DIV"
TT_MOD = "MOD"
TT_POW = "POW"
TT_ROOT = "ROOT"
TT_FLOORDIV = "FLOORDIV"
TT_EQ = "EQ"
TT_EQEQ = "EQEQ"
TT_NE = "NE"
TT_LT = "LT"
TT_GT = "GT"
TT_LTE = "LTE"
TT_GTE = "GTE"
TT_LPAREN = "LPAREN"
TT_RPAREN = "RPAREN"
TT_LBRACE = "LBRACE"
TT_RBRACE = "RBRACE"
TT_SEMICOLON = "SEMICOLON"
TT_COMMA = "COMMA"
TT_DOT = "DOT"
TT_PLUSEQ = "PLUSEQ"
TT_MINUSEQ = "MINUSEQ"
TT_MULEQ = "MULEQ"
TT_DIVEQ = "DIVEQ"
TT_MODEQ = "MODEQ"
TT_POWEQ = "POWEQ"
TT_ROOTEQ = "ROOTEQ"
TT_FLOORDIVEQ = "FLOORDIVEQ"
TT_AMP = "AMP"
TT_PIPE = "PIPE"
TT_CARET = "CARET"
TT_TILDE = "TILDE"
TT_SHL = "SHL"
TT_SHR = "SHR"
TT_LOGICAL_NOT = "LOGICAL_NOT"
TT_LOGICAL_AND = "LOGICAL_AND"
TT_LOGICAL_NAND = "LOGICAL_NAND"
TT_LOGICAL_OR = "LOGICAL_OR"
TT_LOGICAL_NOR = "LOGICAL_NOR"
TT_BITWISE_NAND = "BITWISE_NAND"
TT_BITWISE_XNOR = "BITWISE_XNOR"
TT_BITWISE_NOR = "BITWISE_NOR"
TT_RAWPY_BLOCK = "RAWPY_BLOCK"
TT_RAWPYX_BLOCK = "RAWPYX_BLOCK"
TT_EXEC_BLOCK = "EXEC_BLOCK"
TT_LBRACKET = "LBRACKET"
TT_RBRACKET = "RBRACKET"
TT_EOF = "EOF"
TT_DOCSTRING = "DOCSTRING"

TYPE_KEYWORDS = [
    "int", "float", "str", "bool", "any", "tuple", "list", "num", "char",
    "numBool", "bit", "byte",
    "int8", "int16", "int32", "int64",
    "uint8", "uint16", "uint32", "uint64",
    "float32", "float64",
    "sentinel", "codeblock", "functionAddress",
    "struct",
]

KEYWORDS = [
    "int", "float", "str", "bool", "any", "tuple", "list", "num", "char",
    "numBool", "bit", "byte",
    "int8", "int16", "int32", "int64",
    "uint8", "uint16", "uint32", "uint64",
    "float32", "float64",
    "sentinel", "codeblock",
    "global", "local", "const",
    "shared",
    "if", "elif", "else", "while", "for", "forever", "switch", "case", "default",
    "return", "import", "importAs", "importPy",
    "true", "false", "none",
    "and", "or", "not", "is",
    "vargroup",
    "try", "catch",
    "async", "await",
    "class",
    "native",
    "struct",
    "new",
    "break", "continue", "restart",
]

class Token:
    def __init__(self, type_, value=None, pos_start=None, pos_end=None):
        self.type: str = type_
        self.value: Any = value
        self.pos_start: Any = None
        self.pos_end: Any = None

        if pos_start:
            self.pos_start = pos_start.copy()
            self.pos_end = pos_start.copy()
            self.pos_end.advance()

        if pos_end:
            self.pos_end = pos_end.copy()

    def matches(self, type_, value):
        return self.type == type_ and self.value == value

    def __repr__(self):
        if self.value:
            return f"{self.type}:{self.value}"
        return f"{self.type}"

# lexer

class Lexer:
    def __init__(self, fn, text):
        self.fn = fn
        self.text = text
        self.pos = Position(-1, 0, -1, fn, text)
        self.current_char: str | None = None
        self.advance()

    def advance(self):
        self.pos.advance(self.current_char)
        self.current_char = (
            self.text[self.pos.idx] if self.pos.idx < len(self.text) else None
        )

    def peek(self):
        peek_idx = self.pos.idx + 1
        return self.text[peek_idx] if peek_idx < len(self.text) else None

    def peek2(self):
        peek_idx = self.pos.idx + 2
        return self.text[peek_idx] if peek_idx < len(self.text) else None

    def peek3(self):
        peek_idx = self.pos.idx + 3
        return self.text[peek_idx] if peek_idx < len(self.text) else None

    def make_tokens(self):
        tokens = []

        while self.current_char is not None:
            if self.current_char in " \t\n\r":
                self.advance()
            elif (
                self.current_char == "/" and self.peek() == "/"
                and self.peek2() == "/" and self.peek3() == "/"
            ):
                tokens.append(self.make_docstring())
            elif (
                self.current_char == "/" and self.peek() == "/" and self.peek2() == "/"
            ):
                self.skip_multi_comment()
            elif self.current_char == "/" and self.peek() == "/":
                self.skip_single_comment()
            elif self.current_char in DIGITS:
                tokens.append(self.make_number())
            elif self.current_char in LETTERS or self.current_char == "_":
                tok = self.make_identifier()
                tokens.append(tok)
                if tok.type == TT_IDENTIFIER and tok.value == "rawPy":
                    block_tok = self._try_consume_brace_block(
                        tok.pos_start, TT_RAWPY_BLOCK
                    )
                    if block_tok is not None:
                        tokens.append(block_tok)
                elif tok.type == TT_IDENTIFIER and tok.value == "rawPyx":
                    block_tok = self._try_consume_brace_block(
                        tok.pos_start, TT_RAWPYX_BLOCK
                    )
                    if block_tok is not None:
                        tokens.append(block_tok)
            elif self.current_char == '"':
                tok = self.make_string()
                if tok is None:
                    pos = self.pos.copy()
                    return [], IllegalCharError(pos, pos, "Unterminated string literal — missing closing '\"'")
                tokens.append(tok)
            elif self.current_char == "'":
                tok, error = self.make_char()
                if error:
                    return [], error
                tokens.append(tok)
            elif self.current_char == "+":
                pos_start = self.pos.copy()
                self.advance()
                if self.current_char == "=":
                    self.advance()
                    tokens.append(Token(TT_PLUSEQ, pos_start=pos_start, pos_end=self.pos))
                else:
                    tokens.append(Token(TT_PLUS, pos_start=pos_start, pos_end=self.pos))
            elif self.current_char == "-":
                pos_start = self.pos.copy()
                self.advance()
                if self.current_char == "=":
                    self.advance()
                    tokens.append(Token(TT_MINUSEQ, pos_start=pos_start, pos_end=self.pos))
                else:
                    tokens.append(Token(TT_MINUS, pos_start=pos_start, pos_end=self.pos))
            elif self.current_char == "!":
                pos_start = self.pos.copy()
                self.advance()
                if self.current_char == "=":
                    self.advance()
                    tokens.append(Token(TT_NE, pos_start=pos_start, pos_end=self.pos))
                elif self.current_char == "!":
                    self.advance()
                    tokens.append(Token(TT_LOGICAL_NOT, pos_start=pos_start, pos_end=self.pos))
                elif self.current_char == "&":
                    self.advance()
                    if self.current_char == "&":
                        self.advance()
                        tokens.append(Token(TT_LOGICAL_NAND, pos_start=pos_start, pos_end=self.pos))
                    else:
                        tokens.append(Token(TT_BITWISE_NAND, pos_start=pos_start, pos_end=self.pos))
                elif self.current_char == "|":
                    self.advance()
                    if self.current_char == "|":
                        self.advance()
                        tokens.append(Token(TT_LOGICAL_NOR, pos_start=pos_start, pos_end=self.pos))
                    else:
                        tokens.append(Token(TT_BITWISE_NOR, pos_start=pos_start, pos_end=self.pos))
                elif self.current_char == "^":
                    self.advance()
                    tokens.append(Token(TT_BITWISE_XNOR, pos_start=pos_start, pos_end=self.pos))
                else:
                    return [], IllegalCharError(pos_start, self.pos, "'!' must be followed by '=', '!', '&', '|', or '^'")
            elif self.current_char == "&":
                pos_start = self.pos.copy()
                self.advance()
                if self.current_char == "&":
                    self.advance()
                    tokens.append(Token(TT_LOGICAL_AND, pos_start=pos_start, pos_end=self.pos))
                else:
                    tokens.append(Token(TT_AMP, pos_start=pos_start, pos_end=self.pos))
            elif self.current_char == "|":
                pos_start = self.pos.copy()
                self.advance()
                if self.current_char == "|":
                    self.advance()
                    tokens.append(Token(TT_LOGICAL_OR, pos_start=pos_start, pos_end=self.pos))
                else:
                    tokens.append(Token(TT_PIPE, pos_start=pos_start, pos_end=self.pos))
            elif self.current_char == "^":
                tokens.append(Token(TT_CARET, pos_start=self.pos))
                self.advance()
            elif self.current_char == "~":
                tokens.append(Token(TT_TILDE, pos_start=self.pos))
                self.advance()
            elif self.current_char == "*":
                pos_start = self.pos.copy()
                self.advance()
                if self.current_char == "*":
                    self.advance()
                    if self.current_char == "=":
                        self.advance()
                        tokens.append(Token(TT_POWEQ, pos_start=pos_start, pos_end=self.pos))
                    else:
                        tokens.append(Token(TT_POW, pos_start=pos_start, pos_end=self.pos))
                elif self.current_char == "=":
                    self.advance()
                    tokens.append(Token(TT_MULEQ, pos_start=pos_start, pos_end=self.pos))
                else:
                    tokens.append(Token(TT_MUL, pos_start=pos_start, pos_end=self.pos))
            elif self.current_char == "/":
                pos_start = self.pos.copy()
                self.advance()
                if self.current_char == "*":
                    self.advance()
                    if self.current_char == "=":
                        self.advance()
                        tokens.append(Token(TT_ROOTEQ, pos_start=pos_start, pos_end=self.pos))
                    else:
                        tokens.append(Token(TT_ROOT, pos_start=pos_start, pos_end=self.pos))
                elif self.current_char == "%":
                    self.advance()
                    if self.current_char == "=":
                        self.advance()
                        tokens.append(Token(TT_FLOORDIVEQ, pos_start=pos_start, pos_end=self.pos))
                    else:
                        tokens.append(Token(TT_FLOORDIV, pos_start=pos_start, pos_end=self.pos))
                elif self.current_char == "=":
                    self.advance()
                    tokens.append(Token(TT_DIVEQ, pos_start=pos_start, pos_end=self.pos))
                else:
                    tokens.append(Token(TT_DIV, pos_start=pos_start, pos_end=self.pos))
            elif self.current_char == "%":
                pos_start = self.pos.copy()
                self.advance()
                if self.current_char == "=":
                    self.advance()
                    tokens.append(Token(TT_MODEQ, pos_start=pos_start, pos_end=self.pos))
                else:
                    tokens.append(Token(TT_MOD, pos_start=pos_start, pos_end=self.pos))
            elif self.current_char == "=":
                pos_start = self.pos.copy()
                self.advance()
                if self.current_char == "=":
                    self.advance()
                    tokens.append(Token(TT_EQEQ, pos_start=pos_start, pos_end=self.pos))
                else:
                    tokens.append(Token(TT_EQ, pos_start=pos_start, pos_end=self.pos))
            elif self.current_char == "<":
                tokens.append(self.make_less_than())
            elif self.current_char == ">":
                tokens.append(self.make_greater_than())
            elif self.current_char == "(":
                tokens.append(Token(TT_LPAREN, pos_start=self.pos))
                self.advance()
            elif self.current_char == ")":
                tokens.append(Token(TT_RPAREN, pos_start=self.pos))
                self.advance()
            elif self.current_char == "{":
                tokens.append(Token(TT_LBRACE, pos_start=self.pos))
                self.advance()
            elif self.current_char == "}":
                tokens.append(Token(TT_RBRACE, pos_start=self.pos))
                self.advance()
            elif self.current_char == ";":
                tokens.append(Token(TT_SEMICOLON, pos_start=self.pos))
                self.advance()
            elif self.current_char == ",":
                tokens.append(Token(TT_COMMA, pos_start=self.pos))
                self.advance()
            elif self.current_char == ".":
                tokens.append(Token(TT_DOT, pos_start=self.pos))
                self.advance()
            elif self.current_char == "[":
                tokens.append(Token(TT_LBRACKET, pos_start=self.pos))
                self.advance()
            elif self.current_char == "]":
                tokens.append(Token(TT_RBRACKET, pos_start=self.pos))
                self.advance()
            else:
                pos_start = self.pos.copy()
                char = self.current_char
                self.advance()
                return [], IllegalCharError(pos_start, self.pos, f"'{char}'")

        tokens.append(Token(TT_EOF, pos_start=self.pos))
        return tokens, None

    def make_number(self):
        num_str = ""
        dot_count = 0
        pos_start = self.pos.copy()

        while self.current_char is not None and self.current_char in DIGITS + ".":
            if self.current_char == ".":
                if dot_count == 1:
                    break
                dot_count += 1
            num_str += self.current_char
            self.advance()

        if dot_count == 0:
            return Token(TT_INT, int(num_str), pos_start, self.pos)
        else:
            return Token(TT_FLOAT, float(num_str), pos_start, self.pos)

    def make_string(self):
        s = ""
        pos_start = self.pos.copy()
        escape_character = False
        self.advance()

        escape_characters = {
            "n": "\n", "t": "\t", "r": "\r",
            "\\": "\\", '"': '"', "'": "'",
            "0": "\0", "a": "\a", "b": "\b",
            "f": "\f", "v": "\v",
            "e": "\033",
        }

        while self.current_char is not None and (
            self.current_char != '"' or escape_character
        ):
            if escape_character:
                s += escape_characters.get(self.current_char, self.current_char)
                escape_character = False
            else:
                if self.current_char == "\\":
                    escape_character = True
                else:
                    s += self.current_char
            self.advance()

        if self.current_char is None:
            # EOF reached before closing quote
            return None  # caller handles None as an error

        self.advance()
        return Token(TT_STRING, s, pos_start, self.pos)

    def make_char(self):
        pos_start = self.pos.copy()
        escape_characters = {
            "n": "\n", "t": "\t", "r": "\r",
            "\\": "\\", "'": "'", '"': '"',
            "0": "\0", "a": "\a", "b": "\b",
            "f": "\f", "v": "\v", "e": "\033",
        }
        self.advance()
        ch = ""
        if self.current_char == "\\":
            self.advance()
            ch = escape_characters.get(self.current_char, self.current_char)
            self.advance()
        elif self.current_char is not None and self.current_char != "'":
            ch = self.current_char
            self.advance()
        if self.current_char != "'":
            return None, IllegalCharError(
                pos_start, self.pos,
                "Expected closing ' for char literal"
            )
        self.advance()
        return Token(TT_CHAR, ch if ch else "\0", pos_start, self.pos), None

    def make_identifier(self):
        id_str = ""
        pos_start = self.pos.copy()

        while (
            self.current_char is not None and self.current_char in LETTERS_DIGITS + "_"
        ):
            id_str += self.current_char
            self.advance()

        tok_type = TT_KEYWORD if id_str in KEYWORDS else TT_IDENTIFIER
        return Token(tok_type, id_str, pos_start, self.pos)

    def make_less_than(self):
        tok_type = TT_LT
        pos_start = self.pos.copy()
        self.advance()
        if self.current_char == "=":
            self.advance()
            tok_type = TT_LTE
        elif self.current_char == "<":
            self.advance()
            tok_type = TT_SHL
        return Token(tok_type, pos_start=pos_start, pos_end=self.pos)

    def make_greater_than(self):
        tok_type = TT_GT
        pos_start = self.pos.copy()
        self.advance()
        if self.current_char == "=":
            self.advance()
            tok_type = TT_GTE
        elif self.current_char == ">":
            self.advance()
            tok_type = TT_SHR
        return Token(tok_type, pos_start=pos_start, pos_end=self.pos)

    def skip_single_comment(self):
        self.advance()
        self.advance()
        while self.current_char is not None and self.current_char != "\n":
            self.advance()

    def skip_multi_comment(self):
        self.advance()
        self.advance()
        self.advance()
        while self.current_char is not None:
            if self.current_char == "/" and self.peek() == "/" and self.peek2() == "/":
                self.advance()
                self.advance()
                self.advance()
                break
            self.advance()

    def make_docstring(self):
        """Consume a //// ... //// block and return a TT_DOCSTRING token."""
        pos_start = self.pos.copy()
        for _ in range(4):
            self.advance()          # consume opening ////
        content = []
        while self.current_char is not None:
            if (self.current_char == "/" and self.peek() == "/"
                    and self.peek2() == "/" and self.peek3() == "/"):
                for _ in range(4):
                    self.advance()  # consume closing ////
                break
            content.append(self.current_char)
            self.advance()
        pos_end = self.pos.copy()
        return Token(TT_DOCSTRING, "".join(content).strip(), pos_start, pos_end)

    def _try_consume_brace_block(self, pos_start, token_type):
        text = self.text
        n = len(text)
        i = self.pos.idx

        while i < n and text[i] in " \t\n\r":
            i += 1
        if i >= n or text[i] != "(":
            return None
        i += 1

        while i < n and text[i] in " \t\n\r":
            i += 1
        if i >= n or text[i] != ")":
            return None
        i += 1

        while i < n and text[i] in " \t\n\r":
            i += 1
        if i >= n or text[i] != "{":
            return None
        i += 1

        while self.pos.idx < i:
            self.advance()

        code = ""
        depth = 1
        while self.current_char is not None and depth > 0:
            if self.current_char == "{":
                depth += 1
                code += self.current_char
            elif self.current_char == "}":
                depth -= 1
                if depth > 0:
                    code += self.current_char
            else:
                code += self.current_char
            self.advance()

        return Token(token_type, code, pos_start, self.pos)

# nodes

class NumberNode:
    def __init__(self, tok):
        self.tok = tok
        self.pos_start = self.tok.pos_start
        self.pos_end = self.tok.pos_end

class StringNode:
    def __init__(self, tok):
        self.tok = tok
        self.pos_start = self.tok.pos_start
        self.pos_end = self.tok.pos_end

class CharNode:
    def __init__(self, tok):
        self.tok = tok
        self.pos_start = self.tok.pos_start
        self.pos_end = self.tok.pos_end

class BoolNode:
    def __init__(self, tok):
        self.tok = tok
        self.value = tok.value == "true"
        self.pos_start = self.tok.pos_start
        self.pos_end = self.tok.pos_end

class NoneNode:
    def __init__(self, tok):
        self.tok = tok
        self.pos_start = self.tok.pos_start
        self.pos_end = self.tok.pos_end

class ListElementNode:
    """One typed element in a list literal: ``int 1`` or ``str "text"``."""
    def __init__(self, type_tok, value_node):
        self.type_tok = type_tok
        self.value_node = value_node
        self.pos_start = type_tok.pos_start
        self.pos_end = value_node.pos_end

class ListNode:
    """A list literal, whose elements are explicitly typed."""
    def __init__(self, elements, pos_start, pos_end):
        self.elements = elements
        self.pos_start = pos_start
        self.pos_end = pos_end

class TupleNode:
    """A tuple literal, whose elements are explicitly typed."""
    def __init__(self, elements, pos_start, pos_end):
        self.elements = elements
        self.pos_start = pos_start
        self.pos_end = pos_end

class VarAccessNode:
    def __init__(self, var_name_tok):
        self.var_name_tok = var_name_tok
        self.pos_start = self.var_name_tok.pos_start
        self.pos_end = self.var_name_tok.pos_end

class DotAccessNode:
    def __init__(self, obj_node, attr_name_tok):
        self.obj_node = obj_node
        self.attr_name_tok = attr_name_tok
        self.pos_start = obj_node.pos_start
        self.pos_end = attr_name_tok.pos_end

class NewNode:
    """Create a class instance: ``new ClassName(args...)``."""
    def __init__(self, class_name_tok, arg_nodes, pos_start, pos_end):
        self.class_name_tok = class_name_tok
        self.arg_nodes = arg_nodes
        self.pos_start = pos_start
        self.pos_end = pos_end

class VarDeclNode:
    def __init__(self, type_tok, var_name_tok, value_node, is_const=False, is_shared=False):
        self.type_tok = type_tok
        self.var_name_tok = var_name_tok
        self.value_node = value_node
        self.is_const = is_const
        self.is_shared = is_shared
        self.pos_start = type_tok.pos_start if type_tok else var_name_tok.pos_start
        self.pos_end = value_node.pos_end

class VarAssignNode:
    def __init__(self, var_name_tok, value_node):
        self.var_name_tok = var_name_tok
        self.value_node = value_node
        self.pos_start = self.var_name_tok.pos_start
        self.pos_end = self.value_node.pos_end

class SharedNode:
    """Mark an existing variable, usually a function parameter, as shared."""
    def __init__(self, var_name_tok):
        self.var_name_tok = var_name_tok
        self.pos_start = var_name_tok.pos_start
        self.pos_end = var_name_tok.pos_end

def _uses_shared_parameters(function_value):
    body = getattr(function_value, "body_node", None)
    statements = getattr(body, "statements", ())
    return any(isinstance(statement, SharedNode) for statement in statements)

class BlockNode:
    def __init__(self, statements, pos_start, pos_end):
        self.statements = statements
        self.pos_start = pos_start
        self.pos_end = pos_end

class BinOpNode:
    def __init__(self, left_node, op_tok, right_node):
        self.left_node = left_node
        self.op_tok = op_tok
        self.right_node = right_node
        self.pos_start = self.left_node.pos_start
        self.pos_end = self.right_node.pos_end

class UnaryOpNode:
    def __init__(self, op_tok, node):
        self.op_tok = op_tok
        self.node = node
        self.pos_start = self.op_tok.pos_start
        self.pos_end = node.pos_end

class IfNode:
    def __init__(self, condition_node, then_block, else_block, pos_start, pos_end):
        self.condition_node = condition_node
        self.then_block = then_block
        self.else_block = else_block
        self.pos_start = pos_start
        self.pos_end = pos_end

class WhileNode:
    def __init__(self, condition_node, body_block, pos_start, pos_end):
        self.condition_node = condition_node
        self.body_block = body_block
        self.pos_start = pos_start
        self.pos_end = pos_end

class DoWhileNode:
    def __init__(self, condition_node, body_block, pos_start, pos_end):
        self.condition_node = condition_node
        self.body_block = body_block
        self.pos_start = pos_start
        self.pos_end = pos_end

class ForNode:
    def __init__(
        self, init_node, condition_node, update_node, body_block, pos_start, pos_end
    ):
        self.init_node = init_node
        self.condition_node = condition_node
        self.update_node = update_node
        self.body_block = body_block
        self.pos_start = pos_start
        self.pos_end = pos_end

class IterateNode:
    def __init__(self, count_node, body_block, pos_start, pos_end):
        self.count_node = count_node
        self.body_block = body_block
        self.pos_start = pos_start
        self.pos_end = pos_end

class ForeverNode:
    def __init__(self, body_block, pos_start, pos_end, has_break=False):
        self.body_block = body_block
        self.pos_start = pos_start
        self.pos_end = pos_end
        self.has_break = has_break

class SwitchNode:
    def __init__(self, value_node, cases, pos_start, pos_end):
        self.value_node = value_node
        self.cases = cases
        self.pos_start = pos_start
        self.pos_end = pos_end

class CaseNode:
    def __init__(self, match_node, body_block, pos_start, pos_end):
        self.match_node = match_node
        self.body_block = body_block
        self.pos_start = pos_start
        self.pos_end = pos_end

class DefaultNode:
    def __init__(self, body_block, pos_start, pos_end):
        self.body_block = body_block
        self.pos_start = pos_start
        self.pos_end = pos_end

class FuncDefNode:
    def __init__(
        self, kind_tok, var_name_tok, param_toks, body_block, pos_start, pos_end,
        is_async=False, code_block_toks=None
    ):
        self.kind_tok = kind_tok
        self.var_name_tok = var_name_tok
        self.param_toks = param_toks
        self.body_block = body_block
        self.pos_start = pos_start
        self.pos_end = pos_end
        self.is_async = is_async
        self.code_block_toks = code_block_toks or []

class AwaitNode:
    """await expr — suspends inside an async function until the coroutine resolves."""
    def __init__(self, expr_node, pos_start, pos_end):
        self.expr_node = expr_node
        self.pos_start = pos_start
        self.pos_end = pos_end

class AsyncLocalDefNode:
    """async funcName(params) { body } — local async sub-function inside a global."""
    def __init__(self, name_tok, param_toks, body, pos_start, pos_end):
        self.name_tok = name_tok
        self.param_toks = param_toks
        self.body = body
        self.pos_start = pos_start
        self.pos_end = pos_end

class AsyncDotCallNode:
    """async.funcName(args) — run a locally-defined async function synchronously."""
    def __init__(self, name_tok, arg_nodes, pos_start, pos_end):
        self.name_tok = name_tok
        self.arg_nodes = arg_nodes
        self.pos_start = pos_start
        self.pos_end = pos_end

class CallNode:
    def __init__(
        self, node_to_call, arg_nodes, pos_start, pos_end, block_arg_nodes=None
    ):
        self.node_to_call = node_to_call
        self.arg_nodes = arg_nodes
        self.pos_start = pos_start
        self.pos_end = pos_end
        self.block_arg_nodes = block_arg_nodes or []

class CodeBlockRefNode:
    """Reference to a named code-block parameter, used by ``exec({name})``."""
    def __init__(self, name_tok):
        self.name_tok = name_tok
        self.pos_start = name_tok.pos_start
        self.pos_end = name_tok.pos_end

class CodeBlockLiteralNode:
    """A code block supplied after a function call, e.g. ``fn(){ ... }``."""
    def __init__(self, body_block, pos_start, pos_end, param_toks=None):
        self.body_block = body_block
        self.pos_start = pos_start
        self.pos_end = pos_end
        # ``None`` means infer parameters from body references.  An empty
        # list means the author explicitly declared that the block has none.
        self.param_toks = param_toks

class ExecCallNode:
    """Execute an inline or referenced code block with declared/provided values."""
    def __init__(
        self,
        code_block_node,
        param_toks=None,
        arg_nodes=None,
        infer_params=False,
        pos_start=None,
        pos_end=None,
    ):
        self.code_block_node = code_block_node
        self.param_toks = param_toks or []
        self.arg_nodes = arg_nodes or []
        self.infer_params = infer_params
        self.pos_start = pos_start
        self.pos_end = pos_end


class ExecFileNode:
    """Execute a Lynxer source file at the current execution point."""

    def __init__(self, path_node, pos_start, pos_end):
        self.path_node = path_node
        self.pos_start = pos_start
        self.pos_end = pos_end


class ReturnNode:
    def __init__(self, node_to_return, pos_start, pos_end):
        self.node_to_return = node_to_return
        self.pos_start = pos_start
        self.pos_end = pos_end

class ImportNode:
    def __init__(self, filename_tok, pos_start, pos_end):
        self.filename_tok = filename_tok
        self.pos_start = pos_start
        self.pos_end = pos_end

class ImportAsNode:
    """importAs("module", "alias");  — import module and bind it under a custom name."""
    def __init__(self, filename_tok, alias_tok, pos_start, pos_end):
        self.filename_tok = filename_tok
        self.alias_tok = alias_tok
        self.pos_start = pos_start
        self.pos_end = pos_end

class ImportPyNode:
    """importPy(){"os", "sys", "json"};."""
    def __init__(self, module_names, pos_start, pos_end):
        self.module_names = module_names  # list[str]
        self.pos_start = pos_start
        self.pos_end = pos_end

class RawPyBlockNode:
    def __init__(self, code, pos_start, pos_end):
        self.code = code
        self.pos_start = pos_start
        self.pos_end = pos_end

class RawPyxBlockNode:
    def __init__(self, code, pos_start, pos_end):
        self.code = code
        self.pos_start = pos_start
        self.pos_end = pos_end

class ExecBlockNode:
    """Lynxer code injected into and executed in the current context."""
    def __init__(self, body_block, pos_start, pos_end):
        self.body_block = body_block
        self.pos_start = pos_start
        self.pos_end = pos_end

class BreakNode:
    def __init__(self, pos_start, pos_end):
        self.pos_start = pos_start
        self.pos_end = pos_end

class ContinueNode:
    def __init__(self, pos_start, pos_end):
        self.pos_start = pos_start
        self.pos_end = pos_end

class DocstringNode:
    """AST node for a //// ... //// file-level docstring."""
    def __init__(self, value, pos_start, pos_end):
        self.value = value
        self.pos_start = pos_start
        self.pos_end = pos_end

class ProgramNode:
    def __init__(self, setup_func, globals_list, main_func, pos_start, pos_end, docstring=None):
        self.setup_func = setup_func
        self.globals_list = globals_list
        self.main_func = main_func
        self.docstring = docstring   # plain str extracted from leading //// block, or None
        self.pos_start = pos_start
        self.pos_end = pos_end

def _block_contains_break(block_node):
    """Return whether a block contains a ``break`` statement."""
    if isinstance(block_node, IfNode):
        if _block_contains_break(block_node.then_block):
            return True
        return (
            block_node.else_block is not None
            and _block_contains_break(block_node.else_block)
        )

    for stmt in block_node.statements:
        if isinstance(stmt, BreakNode):
            return True
        if isinstance(stmt, IfNode):
            if _block_contains_break(stmt.then_block):
                return True
            if stmt.else_block is not None and _block_contains_break(stmt.else_block):
                return True
        elif isinstance(stmt, TryCatchNode):
            if _block_contains_break(stmt.try_block):
                return True
            if stmt.catch_block is not None and _block_contains_break(stmt.catch_block):
                return True
    return False

class VarGroupDeclNode:
    def __init__(self, name_tok, fields, pos_start, pos_end, is_const=False,
                 kind="vargroup"):
        self.name_tok = name_tok
        self.fields = fields
        self.is_const = is_const
        self.kind = kind
        self.pos_start = pos_start
        self.pos_end = pos_end

class DotAssignNode:
    """type obj.field = value  (typed dot-path assignment into a vargroup)"""
    def __init__(self, obj_node, attr_name_tok, value_node, decl_type, pos_start, pos_end):
        self.obj_node = obj_node
        self.attr_name_tok = attr_name_tok
        self.value_node = value_node
        self.decl_type = decl_type
        self.pos_start = pos_start
        self.pos_end = pos_end

class AddVarGroupNode:
    """addVarGroup(path_expr, type name = value)"""
    def __init__(self, path_node, field_type, field_name_tok, field_value_node,
                 pos_start, pos_end):
        self.path_node = path_node
        self.field_type = field_type
        self.field_name_tok = field_name_tok
        self.field_value_node = field_value_node
        self.pos_start = pos_start
        self.pos_end = pos_end

class RemoveVarGroupNode:
    """removeVarGroup(path_expr, field_name)"""
    def __init__(self, path_node, field_name_tok, pos_start, pos_end):
        self.path_node = path_node
        self.field_name_tok = field_name_tok
        self.pos_start = pos_start
        self.pos_end = pos_end

class TryCatchNode:
    """try { body } catch."""
    def __init__(self, try_block, catch_var_tok, catch_block, pos_start, pos_end):
        self.try_block = try_block          # BlockNode
        self.catch_var_tok = catch_var_tok  # Token (identifier) or None
        self.catch_block = catch_block      # BlockNode
        self.pos_start = pos_start
        self.pos_end = pos_end

class ClassDefNode:
    """class ClassName { [const] type."""
    def __init__(self, name_tok, field_defs, method_nodes, pos_start, pos_end):
        self.name_tok = name_tok
        self.field_defs = field_defs
        self.method_nodes = method_nodes
        self.pos_start = pos_start
        self.pos_end = pos_end

class StructDefNode:
    """A data-only struct declaration with required constructor fields."""
    def __init__(self, name_tok, field_defs, pos_start, pos_end, is_native=False):
        self.name_tok = name_tok
        self.field_defs = field_defs
        self.is_native = is_native
        self.pos_start = pos_start
        self.pos_end = pos_end

# parse result

class ParseResult:
    def __init__(self):
        self.error: Error | None = None
        self.node: Any = None
        self.last_registered_advance_count = 0
        self.advance_count = 0
        self.to_reverse_count = 0

    def register_advancement(self):
        self.last_registered_advance_count = 1
        self.advance_count += 1

    def register(self, res: "ParseResult") -> Any:
        self.last_registered_advance_count = res.advance_count
        self.advance_count += res.advance_count
        if res.error:
            self.error = res.error
        return res.node

    def try_register(self, res):
        if res.error:
            self.to_reverse_count = res.advance_count
            return None
        return self.register(res)

    def success(self, node):
        self.node = node
        return self

    def failure(self, error):
        if not self.error or self.last_registered_advance_count == 0:
            self.error = error
        return self

# parser

class Parser:
    def __init__(self, tokens, code_block_names=None):
        self.tokens = tokens
        self.tok_idx = -1
        self._loop_depth = 0       # tracks nesting depth of all loop forms
        self._switch_depth = 0     # tracks whether case is inside a switch block
        self._in_global_func = False
        self._allow_function_defs = True
        self._exec_mode = False    # parse injected exec() code with no function definitions
        self._code_block_names = code_block_names if code_block_names is not None else {}
        self.current_tok: Token = (
            tokens[0]
            if tokens
            else Token(TT_EOF, pos_start=Position(0, 0, 0, "<parser>", ""))
        )
        self.advance()

    def advance(self):
        self.tok_idx += 1
        self.update_current_tok()
        return self.current_tok

    def reverse(self, amount=1):
        self.tok_idx -= amount
        self.update_current_tok()
        return self.current_tok

    def update_current_tok(self):
        if 0 <= self.tok_idx < len(self.tokens):
            self.current_tok = self.tokens[self.tok_idx]

    def peek(self, offset=1):
        idx = self.tok_idx + offset
        if 0 <= idx < len(self.tokens):
            return self.tokens[idx]
        return None

    def is_type_keyword(self):
        return (
            (
                self.current_tok.type == TT_KEYWORD
                and self.current_tok.value in TYPE_KEYWORDS
            )
            or (
                self.current_tok.type == TT_IDENTIFIER
                and self.current_tok.value == "functionAddress"
            )
        )

    def is_type_name(self):
        """Return whether the current tokens start a built-in or class type.

        Class names remain identifiers, so ``Widget item`` is distinguishable
        from an untyped parameter/statement by the second identifier token.
        """
        next_token = self.peek(1)
        return self.is_type_keyword() or (
            self.current_tok.type == TT_IDENTIFIER
            and next_token is not None
            and next_token.type == TT_IDENTIFIER
        )

    def claim_code_block_name(self, name_tok):
        """Reserve a code-block identifier for this parsed source unit.

        Code-block identifiers are deliberately source-wide rather than
        scope-local.  This keeps ``exec(){{name}}`` and ``{{name}}`` call
        arguments unambiguous even when the declaration and use live at
        different nesting levels.
        """
        previous = self._code_block_names.get(name_tok.value)
        if previous is not None:
            return InvalidSyntaxError(
                name_tok.pos_start,
                name_tok.pos_end,
                f"Duplicate code-block identifier '{name_tok.value}'. "
                "Code-block identifiers must be unique across the whole source.",
            )
        self._code_block_names[name_tok.value] = name_tok
        return None

    def parse(self, require_main=True):
        res = ParseResult()
        pos_start = self.current_tok.pos_start.copy()
        setup_func = None
        main_func = None
        globals_list = []
        self._require_main = require_main
        # For ordering enforcement
        setup_seen = False
        main_seen = False
        any_other_seen = False

        # Consume an optional leading //// docstring before any global declarations
        docstring = None
        if self.current_tok.type == TT_DOCSTRING:
            docstring = self.current_tok.value
            res.register_advancement()
            self.advance()

        while self.current_tok.type != TT_EOF:
            next_tok = self.peek(1)
            is_func_kw = (
                self.current_tok.matches(TT_KEYWORD, "global")
                or (
                    self.current_tok.type == TT_IDENTIFIER
                    and self.current_tok.value == "global"
                    and next_tok is not None
                    and next_tok.type == TT_IDENTIFIER
                )
            )
            if is_func_kw:
                func_name_tok = self.peek(1)
                assert func_name_tok is not None

                if (
                    func_name_tok
                    and func_name_tok.type == TT_IDENTIFIER
                    and func_name_tok.value == "setup"
                ):
                    if setup_seen:
                        return res.failure(InvalidSyntaxError(
                            self.current_tok.pos_start, self.current_tok.pos_end,
                            "Duplicate 'global setup(){}' — only one setup function is allowed",
                        ))
                    if any_other_seen or main_seen:
                        return res.failure(InvalidSyntaxError(
                            self.current_tok.pos_start, self.current_tok.pos_end,
                            "'global setup(){}' must be the very first declaration in the file. "
                            "Move it above all other global functions and classes.",
                        ))
                    setup_func = res.register(self.parse_func_def())
                    if res.error:
                        return res
                    setup_seen = True
                elif (
                    func_name_tok
                    and func_name_tok.type == TT_IDENTIFIER
                    and func_name_tok.value == "main"
                ):
                    if main_seen:
                        return res.failure(InvalidSyntaxError(
                            self.current_tok.pos_start, self.current_tok.pos_end,
                            "Duplicate 'global main()' — only one main function is allowed",
                        ))
                    main_func = res.register(self.parse_func_def())
                    if res.error:
                        return res
                    main_seen = True
                else:
                    if main_seen:
                        fname = func_name_tok.value if func_name_tok else "..."
                        return res.failure(InvalidSyntaxError(
                            self.current_tok.pos_start, self.current_tok.pos_end,
                            f"'global {fname}' must be declared "
                            f"before 'global main(){{}}'. No declarations are allowed after main.",
                        ))
                    node = res.register(self.parse_func_def())
                    if res.error:
                        return res
                    globals_list.append(node)
                    any_other_seen = True
            elif self.current_tok.matches(TT_KEYWORD, "class"):
                if main_seen:
                    return res.failure(InvalidSyntaxError(
                        self.current_tok.pos_start, self.current_tok.pos_end,
                        "Class definitions must appear before 'global main(){}'. "
                        "No declarations are allowed after main.",
                    ))
                node = res.register(self.parse_class_def())
                if res.error:
                    return res
                globals_list.append(node)
                any_other_seen = True
            elif self.current_tok.matches(TT_KEYWORD, "struct"):
                if main_seen:
                    return res.failure(InvalidSyntaxError(
                        self.current_tok.pos_start, self.current_tok.pos_end,
                        "Struct definitions must appear before 'global main(){}'. "
                        "No declarations are allowed after main.",
                    ))
                node = res.register(self.parse_struct_def())
                if res.error:
                    return res
                globals_list.append(node)
                any_other_seen = True
            elif (
                self.current_tok.matches(TT_KEYWORD, "native")
                and next_tok is not None
                and next_tok.matches(TT_KEYWORD, "struct")
            ):
                if main_seen:
                    return res.failure(InvalidSyntaxError(
                        self.current_tok.pos_start, self.current_tok.pos_end,
                        "Struct definitions must appear before 'global main(){}'. "
                        "No declarations are allowed after main.",
                    ))
                node = res.register(self.parse_struct_def(is_native=True))
                if res.error:
                    return res
                globals_list.append(node)
                any_other_seen = True
            elif self.current_tok.matches(TT_KEYWORD, "const") or self.is_type_keyword():
                return res.failure(
                    InvalidSyntaxError(
                        self.current_tok.pos_start,
                        self.current_tok.pos_end,
                        f"Global variables must be declared inside 'global setup(){{}}', not at the top level. "
                        f"Move '{self.current_tok.value} ...' inside setup()",
                    )
                )
            else:
                return res.failure(
                    InvalidSyntaxError(
                        self.current_tok.pos_start,
                        self.current_tok.pos_end,
                        "Executable code is not allowed outside of a function. "
                        "Only 'global' definitions are permitted at the top level. "
                        "Put globals in setup() and entry logic in 'global main(){}'",
                    )
                )

        if setup_func is None and self._require_main:
            return res.failure(
                InvalidSyntaxError(
                    self.current_tok.pos_start,
                    self.current_tok.pos_end,
                    "Program requires a 'global setup(){}' function at the top. "
                    "Add it as the very first declaration (before all other globals and main).",
                )
            )

        pos_end = self.current_tok.pos_end.copy()
        return res.success(
            ProgramNode(setup_func, globals_list, main_func, pos_start, pos_end, docstring=docstring)
        )

    def parse_func_def(self):
        res = ParseResult()
        pos_start = self.current_tok.pos_start.copy()

        # Optional async prefix
        is_async = False
        if self.current_tok.matches(TT_KEYWORD, "async"):
            is_async = True
            res.register_advancement()
            self.advance()  # consume 'async'

        # kind: global or local
        _is_global_kw = (
            self.current_tok.type == TT_IDENTIFIER
            and self.current_tok.value == "global"
        )
        if not (
            self.current_tok.matches(TT_KEYWORD, "global")
            or _is_global_kw
            or self.current_tok.matches(TT_KEYWORD, "local")
        ):
            return res.failure(
                InvalidSyntaxError(
                    self.current_tok.pos_start,
                    self.current_tok.pos_end,
                    "Expected 'global' or 'local'" + (" after 'async'" if is_async else ""),
                )
            )
        if is_async and not _is_global_kw:
            return res.failure(
                InvalidSyntaxError(
                    self.current_tok.pos_start,
                    self.current_tok.pos_end,
                    "'async' is only allowed before 'global' functions",
                )
            )
        kind_tok = self.current_tok
        res.register_advancement()
        self.advance()

        if self.current_tok.type != TT_IDENTIFIER:
            return res.failure(
                InvalidSyntaxError(
                    self.current_tok.pos_start,
                    self.current_tok.pos_end,
                    "Expected function name",
                )
            )
        name_tok = self.current_tok
        res.register_advancement()
        self.advance()

        if self.current_tok.type != TT_LPAREN:
            return res.failure(
                InvalidSyntaxError(
                    self.current_tok.pos_start, self.current_tok.pos_end, "Expected '('"
                )
            )
        res.register_advancement()
        self.advance()

        param_toks = []
        has_default = False
        while self.current_tok.type != TT_RPAREN and self.current_tok.type != TT_EOF:
            next_tok = self.peek(1)
            if (
                self.is_type_name()
                and next_tok is not None
                and next_tok.type == TT_IDENTIFIER
            ):
                type_tok = self.current_tok
                res.register_advancement()
                self.advance()
                pname_tok = self.current_tok
                res.register_advancement()
                self.advance()
            elif self.current_tok.type == TT_IDENTIFIER:
                type_tok = None
                pname_tok = self.current_tok
                res.register_advancement()
                self.advance()
            else:
                return res.failure(
                    InvalidSyntaxError(
                        self.current_tok.pos_start,
                        self.current_tok.pos_end,
                        "Expected parameter name (optionally preceded by a type: int, float, str, bool)",
                    )
                )

            default_node = None
            if self.current_tok.type == TT_EQ:
                has_default = True
                res.register_advancement()
                self.advance()
                default_node = res.register(self.parse_expr())
                if res.error:
                    return res
            elif has_default:
                return res.failure(InvalidSyntaxError(
                    pname_tok.pos_start,
                    pname_tok.pos_end,
                    "Required parameters cannot follow a parameter with a default value",
                ))
            param_toks.append((type_tok, pname_tok, default_node))

            if self.current_tok.type == TT_COMMA:
                res.register_advancement()
                self.advance()
            else:
                break

        if self.current_tok.type != TT_RPAREN:
            return res.failure(
                InvalidSyntaxError(
                    self.current_tok.pos_start, self.current_tok.pos_end, "Expected ')'"
                )
            )
        res.register_advancement()
        self.advance()

        param_names = [param_tok.value for _, param_tok, _ in param_toks]
        code_block_toks = []
        while self._looks_like_code_block_signature():
            res.register_advancement()
            self.advance()  # consume the code-block signature '{'

            while self.current_tok.type != TT_RBRACE and self.current_tok.type != TT_EOF:
                if self.current_tok.type != TT_IDENTIFIER:
                    return res.failure(InvalidSyntaxError(
                        self.current_tok.pos_start,
                        self.current_tok.pos_end,
                        "Expected a code-block name",
                    ))
                code_block_toks.append(self.current_tok)
                res.register_advancement()
                self.advance()

                if self.current_tok.type == TT_COMMA:
                    res.register_advancement()
                    self.advance()
                    if self.current_tok.type == TT_RBRACE:
                        return res.failure(InvalidSyntaxError(
                            self.current_tok.pos_start,
                            self.current_tok.pos_end,
                            "Expected a code-block name after ','",
                        ))
                elif self.current_tok.type != TT_RBRACE:
                    return res.failure(InvalidSyntaxError(
                        self.current_tok.pos_start,
                        self.current_tok.pos_end,
                        "Expected ',' or '}' after code-block name",
                    ))

            if self.current_tok.type != TT_RBRACE:
                return res.failure(InvalidSyntaxError(
                    self.current_tok.pos_start,
                    self.current_tok.pos_end,
                    "Expected '}' after code-block parameter list",
                ))
            res.register_advancement()
            self.advance()

        is_setup = name_tok.value == "setup"
        if code_block_toks and is_setup:
            return res.failure(InvalidSyntaxError(
                name_tok.pos_start,
                self.current_tok.pos_end,
                "'global setup(){}' cannot declare code-block parameters because "
                "setup() is invoked without caller-supplied blocks",
            ))
        if code_block_toks and name_tok.value == "main":
            return res.failure(InvalidSyntaxError(
                name_tok.pos_start,
                self.current_tok.pos_end,
                "'global main(){}' cannot declare code-block parameters because "
                "main() is invoked as the program entry point",
            ))

        seen_names = set(param_names)
        for block_tok in code_block_toks:
            if block_tok.value in seen_names:
                return res.failure(InvalidSyntaxError(
                    block_tok.pos_start,
                    block_tok.pos_end,
                    f"Duplicate parameter name '{block_tok.value}' — "
                    "code-block names must be unique and must not overlap "
                    "value parameters",
                ))
            seen_names.add(block_tok.value)
            duplicate_error = self.claim_code_block_name(block_tok)
            if duplicate_error is not None:
                return res.failure(duplicate_error)

        _is_global_def = kind_tok.value == "global" or (
            kind_tok.type == TT_IDENTIFIER and kind_tok.value == "global"
        )

        prev_in_global_func = self._in_global_func
        if _is_global_def and not is_setup:
            self._in_global_func = True
        else:
            self._in_global_func = False

        body = res.register(self.parse_block(in_setup=is_setup, allow_local_funcs=True))
        self._in_global_func = prev_in_global_func  # restore
        if res.error:
            return res

        pos_end = self.current_tok.pos_end.copy()
        return res.success(
            FuncDefNode(kind_tok, name_tok, param_toks, body, pos_start, pos_end,
                        is_async=is_async, code_block_toks=code_block_toks)
        )

    def _looks_like_code_block_signature(self):
        """Return whether the brace after a function's ')' is a block signature.

        A normal function body also starts with '{', so the only unambiguous
        distinction is whether its matching '}' is immediately followed by the
        actual function-body '{'.
        """
        if self.current_tok.type != TT_LBRACE:
            return False

        depth = 0
        index = self.tok_idx
        while index < len(self.tokens):
            token = self.tokens[index]
            if token.type == TT_LBRACE:
                depth += 1
            elif token.type == TT_RBRACE:
                depth -= 1
                if depth == 0:
                    next_token = self.tokens[index + 1] if index + 1 < len(self.tokens) else None
                    return next_token is not None and next_token.type == TT_LBRACE
            index += 1
        return False

    def parse_class_def(self):
        """Parse: class ClassName { [const] type field = value; ... def method(params){} ... }"""
        res = ParseResult()
        pos_start = self.current_tok.pos_start.copy()

        # consume 'class'
        res.register_advancement()
        self.advance()

        if self.current_tok.type != TT_IDENTIFIER:
            return res.failure(InvalidSyntaxError(
                self.current_tok.pos_start, self.current_tok.pos_end,
                "Expected class name after 'class'"
            ))
        name_tok = self.current_tok
        res.register_advancement()
        self.advance()

        if self.current_tok.type != TT_LBRACE:
            return res.failure(InvalidSyntaxError(
                self.current_tok.pos_start, self.current_tok.pos_end,
                "Expected '{' to open class body"
            ))
        res.register_advancement()
        self.advance()

        field_defs = []
        method_nodes = []

        while self.current_tok.type != TT_RBRACE and self.current_tok.type != TT_EOF:
            if self.current_tok.matches(TT_KEYWORD, "local"):
                method_node = res.register(self.parse_func_def())
                if res.error:
                    return res
                method_nodes.append(method_node)
            elif (
                self.current_tok.matches(TT_KEYWORD, "const")
                or self.is_type_keyword()
                or (
                    self.current_tok.type == TT_IDENTIFIER
                    and (next_token := self.peek(1)) is not None
                    and next_token.type == TT_IDENTIFIER
                )
            ):
                is_const = False
                if self.current_tok.matches(TT_KEYWORD, "const"):
                    is_const = True
                    res.register_advancement()
                    self.advance()
                if not self.is_type_name():
                    return res.failure(InvalidSyntaxError(
                        self.current_tok.pos_start, self.current_tok.pos_end,
                        "Expected a built-in or class type for class field"
                    ))
                type_tok = self.current_tok
                res.register_advancement()
                self.advance()
                if self.current_tok.type != TT_IDENTIFIER:
                    return res.failure(InvalidSyntaxError(
                        self.current_tok.pos_start, self.current_tok.pos_end,
                        "Expected field name"
                    ))
                field_name_tok = self.current_tok
                res.register_advancement()
                self.advance()
                if self.current_tok.type != TT_EQ:
                    return res.failure(InvalidSyntaxError(
                        self.current_tok.pos_start, self.current_tok.pos_end,
                        "Expected '=' after field name in class"
                    ))
                res.register_advancement()
                self.advance()
                value_node = res.register(self.parse_expr())
                if res.error:
                    return res
                if self.current_tok.type != TT_SEMICOLON:
                    return res.failure(InvalidSyntaxError(
                        self.current_tok.pos_start, self.current_tok.pos_end,
                        "Expected ';' after class field declaration"
                    ))
                res.register_advancement()
                self.advance()
                field_defs.append((type_tok.value, field_name_tok, value_node, is_const))
            else:
                return res.failure(InvalidSyntaxError(
                    self.current_tok.pos_start, self.current_tok.pos_end,
                    "Expected a field declaration (type name = value;) or method (local name(){}) in class body"
                ))

        if not field_defs and not method_nodes:
            return res.failure(InvalidSyntaxError(
                self.current_tok.pos_start, self.current_tok.pos_end,
                f"Class '{name_tok.value}' must not be empty — add at least one field or method"
            ))

        if self.current_tok.type != TT_RBRACE:
            return res.failure(InvalidSyntaxError(
                self.current_tok.pos_start, self.current_tok.pos_end,
                "Expected '}' to close class body"
            ))
        pos_end = self.current_tok.pos_end.copy()
        res.register_advancement()
        self.advance()

        return res.success(ClassDefNode(name_tok, field_defs, method_nodes, pos_start, pos_end))

    def parse_struct_def(self, is_native=False):
        """Parse ``struct Name { type field; ... }``."""
        res = ParseResult()
        pos_start = self.current_tok.pos_start.copy()
        res.register_advancement()
        self.advance()

        if self.current_tok.type != TT_IDENTIFIER:
            return res.failure(InvalidSyntaxError(
                self.current_tok.pos_start, self.current_tok.pos_end,
                "Expected struct name after 'struct'",
            ))
        name_tok = self.current_tok
        res.register_advancement()
        self.advance()

        if self.current_tok.type != TT_LBRACE:
            return res.failure(InvalidSyntaxError(
                self.current_tok.pos_start, self.current_tok.pos_end,
                "Expected '{' to open struct body",
            ))
        res.register_advancement()
        self.advance()

        field_defs = []
        while self.current_tok.type not in (TT_RBRACE, TT_EOF):
            if not self.is_type_name():
                return res.failure(InvalidSyntaxError(
                    self.current_tok.pos_start, self.current_tok.pos_end,
                    "Expected a type and field name in struct body",
                ))
            type_tok = self.current_tok
            res.register_advancement()
            self.advance()
            if self.current_tok.type != TT_IDENTIFIER:
                return res.failure(InvalidSyntaxError(
                    self.current_tok.pos_start, self.current_tok.pos_end,
                    "Expected struct field name",
                ))
            field_name_tok = self.current_tok
            res.register_advancement()
            self.advance()
            if self.current_tok.type != TT_SEMICOLON:
                return res.failure(InvalidSyntaxError(
                    self.current_tok.pos_start, self.current_tok.pos_end,
                    "Struct fields do not have defaults; expected ';'",
                ))
            res.register_advancement()
            self.advance()
            field_defs.append((type_tok.value, field_name_tok, None, False))

        if not field_defs:
            return res.failure(InvalidSyntaxError(
                self.current_tok.pos_start, self.current_tok.pos_end,
                f"Struct '{name_tok.value}' must have at least one field",
            ))
        if self.current_tok.type != TT_RBRACE:
            return res.failure(InvalidSyntaxError(
                self.current_tok.pos_start, self.current_tok.pos_end,
                "Expected '}' to close struct body",
            ))
        pos_end = self.current_tok.pos_end.copy()
        res.register_advancement()
        self.advance()
        return res.success(StructDefNode(
            name_tok, field_defs, pos_start, pos_end, is_native=is_native
        ))

    def parse_block(
        self,
        in_setup=False,
        allow_local_funcs=False,
        allow_function_defs=None,
    ):
        res = ParseResult()
        pos_start = self.current_tok.pos_start.copy()

        if self.current_tok.type != TT_LBRACE:
            return res.failure(
                InvalidSyntaxError(
                    self.current_tok.pos_start, self.current_tok.pos_end, "Expected '{'"
                )
            )
        res.register_advancement()
        self.advance()

        statements = []
        previous_allow_function_defs = self._allow_function_defs
        if allow_function_defs is not None:
            self._allow_function_defs = (
                previous_allow_function_defs and allow_function_defs
            )
        try:
            while self.current_tok.type != TT_RBRACE and self.current_tok.type != TT_EOF:
                # //// docstrings inside a block are treated as comments — skip them.
                if self.current_tok.type == TT_DOCSTRING:
                    res.register_advancement()
                    self.advance()
                    continue
                stmt = res.register(
                    self.parse_statement(
                        in_setup=in_setup,
                        allow_local_funcs=allow_local_funcs and not self._exec_mode,
                    )
                )
                if res.error:
                    return res
                statements.append(stmt)
        finally:
            self._allow_function_defs = previous_allow_function_defs

        if self.current_tok.type != TT_RBRACE:
            return res.failure(
                InvalidSyntaxError(
                    self.current_tok.pos_start, self.current_tok.pos_end, "Expected '}'"
                )
            )
        pos_end = self.current_tok.pos_end.copy()
        res.register_advancement()
        self.advance()

        return res.success(BlockNode(statements, pos_start, pos_end))

    def parse_exec_source(self, code, in_setup=False):
        """Parse an ``exec(){...}`` body as statements to inject at runtime.

        The nested parser deliberately starts outside a global function and
        disables local-function parsing.  This rejects global, local, and
        async function definitions while still allowing normal statements and
        calls to functions defined by the surrounding program.
        """
        lexer = Lexer(self.current_tok.pos_start.fn, code)
        tokens, error = lexer.make_tokens()
        if error:
            return ParseResult().failure(error)

        parser = Parser(tokens, self._code_block_names)
        parser._exec_mode = True
        parser._loop_depth = self._loop_depth
        return parser.parse_exec_block(in_setup=in_setup)

    def parse_exec_block(self, in_setup=False):
        """Parse standalone statements until the injected source reaches EOF."""
        res = ParseResult()
        pos_start = self.current_tok.pos_start.copy()
        statements = []

        while self.current_tok.type != TT_EOF:
            if self.current_tok.type == TT_DOCSTRING:
                res.register_advancement()
                self.advance()
                continue

            stmt = res.register(
                self.parse_statement(in_setup=in_setup, allow_local_funcs=False)
            )
            if res.error:
                return res
            statements.append(stmt)

        return res.success(
            BlockNode(statements, pos_start, self.current_tok.pos_end.copy())
        )

    def parse_statement(self, in_setup=False, allow_local_funcs=False):
        res = ParseResult()
        pos_start = self.current_tok.pos_start.copy()

        if self.current_tok.matches(TT_KEYWORD, "import"):
            if not in_setup:
                return res.failure(
                    InvalidSyntaxError(
                        self.current_tok.pos_start,
                        self.current_tok.pos_end,
                        "import() may only be used inside setup()",
                    )
                )
            node = res.register(self.parse_import())
            if res.error:
                return res
            return res.success(node)

        if self.current_tok.matches(TT_KEYWORD, "importAs"):
            if not in_setup:
                return res.failure(
                    InvalidSyntaxError(
                        self.current_tok.pos_start,
                        self.current_tok.pos_end,
                        "importAs() may only be used inside setup()",
                    )
                )
            node = res.register(self.parse_importAs())
            if res.error:
                return res
            return res.success(node)

        if self.current_tok.matches(TT_KEYWORD, "importPy"):
            if not in_setup:
                return res.failure(
                    InvalidSyntaxError(
                        self.current_tok.pos_start,
                        self.current_tok.pos_end,
                        "importPy(){...} may only be used inside global setup(){}",
                    )
                )
            node = res.register(self.parse_importPy())
            if res.error:
                return res
            return res.success(node)

        next_tok = self.peek(1)
        if (
            self.current_tok.matches(TT_KEYWORD, "local")
        ) and next_tok is not None and next_tok.type == TT_DOT:
            expr = res.register(self.parse_expr())
            if res.error:
                return res
            if self.current_tok.type != TT_SEMICOLON:
                if not (
                    (isinstance(expr, CallNode) and expr.block_arg_nodes)
                    or isinstance(expr, ExecCallNode)
                ):
                    return res.failure(
                        InvalidSyntaxError(
                            expr.pos_end,
                            self.current_tok.pos_start,
                            "Missing ';' after statement",
                        )
                    )
            else:
                res.register_advancement()
                self.advance()
            return res.success(expr)

        if (
            allow_local_funcs
            and self._allow_function_defs
            and self.current_tok.matches(TT_KEYWORD, "local")
        ):
            node = res.register(self.parse_func_def())
            if res.error:
                return res
            return res.success(node)

        if self.current_tok.matches(TT_KEYWORD, "local"):
            if not self._allow_function_defs:
                return res.failure(InvalidSyntaxError(
                    self.current_tok.pos_start,
                    self.current_tok.pos_end,
                    "Function definitions are not allowed inside loop or switch blocks",
                ))
            return res.failure(
                InvalidSyntaxError(
                    self.current_tok.pos_start,
                    self.current_tok.pos_end,
                    "'local' function definitions are only allowed inside a function body",
                )
            )

        if self.current_tok.matches(TT_KEYWORD, "async"):
            peek1 = self.peek(1)
            if peek1 and peek1.type == TT_IDENTIFIER:
                if not allow_local_funcs or not self._allow_function_defs:
                    if not self._allow_function_defs:
                        return res.failure(InvalidSyntaxError(
                            self.current_tok.pos_start,
                            self.current_tok.pos_end,
                            "Function definitions are not allowed inside loop or switch blocks",
                        ))
                    return res.failure(InvalidSyntaxError(
                        self.current_tok.pos_start, self.current_tok.pos_end,
                        "'async' function definitions are only allowed inside a function body",
                    ))
                node = res.register(self.parse_async_local_def())
                if res.error:
                    return res
                return res.success(node)
            # async.funcName(args); — expression statement
            expr = res.register(self.parse_async_dot_call())
            if res.error:
                return res
            if self.current_tok.type != TT_SEMICOLON:
                return res.failure(InvalidSyntaxError(
                    expr.pos_end, self.current_tok.pos_start,
                    "Missing ';' after async call",
                ))
            res.register_advancement(); self.advance()
            return res.success(expr)

        next_tok = self.peek(1)
        if (
            self.current_tok.matches(TT_KEYWORD, "global")
            or (self.current_tok.type == TT_IDENTIFIER and self.current_tok.value == "global")
        ) and next_tok is not None and next_tok.type == TT_DOT:
            expr = res.register(self.parse_expr())
            if res.error:
                return res
            if self.current_tok.type != TT_SEMICOLON:
                if not (
                    (isinstance(expr, CallNode) and expr.block_arg_nodes)
                    or isinstance(expr, ExecCallNode)
                ):
                    return res.failure(
                        InvalidSyntaxError(
                            expr.pos_end,
                            self.current_tok.pos_start,
                            "Missing ';' after statement",
                        )
                    )
            else:
                res.register_advancement()
                self.advance()
            return res.success(expr)

        next_tok = self.peek(1)
        if self.current_tok.matches(TT_KEYWORD, "global") or (
            self.current_tok.type == TT_IDENTIFIER
            and self.current_tok.value == "global"
            and next_tok is not None
            and next_tok.type == TT_IDENTIFIER
        ):
            if self._in_global_func:
                if not self._allow_function_defs:
                    return res.failure(InvalidSyntaxError(
                        self.current_tok.pos_start,
                        self.current_tok.pos_end,
                        "Function definitions are not allowed inside loop or switch blocks",
                    ))
                node = res.register(self.parse_func_def())
                if res.error:
                    return res
                return res.success(node)
            if in_setup:
                return res.failure(
                    InvalidSyntaxError(
                        self.current_tok.pos_start,
                        self.current_tok.pos_end,
                        "Cannot define a 'global' function inside 'global setup(){}'. "
                        "Nested globals belong inside other global functions, not in setup.",
                    )
                )
            return res.failure(
                InvalidSyntaxError(
                    self.current_tok.pos_start,
                    self.current_tok.pos_end,
                    "'global' function definitions must be at the top level of the file "
                    "or nested inside another global function. "
                    "Move the function before 'global main(){}'.",
                )
            )

        if self.current_tok.matches(TT_KEYWORD, "if"):
            node = res.register(self.parse_if())
            if res.error:
                return res
            return res.success(node)

        if self.current_tok.matches(TT_KEYWORD, "while"):
            node = res.register(self.parse_while())
            if res.error:
                return res
            return res.success(node)

        next_tok = self.peek(1)
        if (
            self.current_tok.type == TT_IDENTIFIER
            and self.current_tok.value == "doWhile"
            and next_tok is not None
            and next_tok.type == TT_LPAREN
        ):
            node = res.register(self.parse_do_while())
            if res.error:
                return res
            return res.success(node)

        if self.current_tok.matches(TT_KEYWORD, "for"):
            node = res.register(self.parse_for())
            if res.error:
                return res
            return res.success(node)

        if self.current_tok.matches(TT_KEYWORD, "forever"):
            node = res.register(self.parse_forever())
            if res.error:
                return res
            return res.success(node)

        if self.current_tok.matches(TT_KEYWORD, "switch"):
            node = res.register(self.parse_switch())
            if res.error:
                return res
            return res.success(node)

        if self.current_tok.matches(TT_KEYWORD, "case"):
            if self._switch_depth == 0:
                return res.failure(InvalidSyntaxError(
                    self.current_tok.pos_start,
                    self.current_tok.pos_end,
                    "'case' is only valid inside a 'switch' block",
                ))
            node = res.register(self.parse_case())
            if res.error:
                return res
            return res.success(node)

        if self.current_tok.matches(TT_KEYWORD, "default"):
            if self._switch_depth == 0:
                return res.failure(InvalidSyntaxError(
                    self.current_tok.pos_start,
                    self.current_tok.pos_end,
                    "'default' is only valid inside a 'switch' block",
                ))
            node = res.register(self.parse_default())
            if res.error:
                return res
            return res.success(node)

        if self.current_tok.matches(TT_KEYWORD, "try"):
            node = res.register(self.parse_try_catch())
            if res.error:
                return res
            return res.success(node)

        if self.current_tok.matches(TT_KEYWORD, "await"):
            expr = res.register(self.parse_expr())
            if res.error:
                return res
            if self.current_tok.type != TT_SEMICOLON:
                return res.failure(
                    InvalidSyntaxError(
                        expr.pos_end,
                        self.current_tok.pos_start,
                        "Missing ';' after 'await' expression",
                    )
                )
            res.register_advancement()
            self.advance()
            return res.success(expr)

        if self.current_tok.matches(TT_KEYWORD, "break"):
            pos_start = self.current_tok.pos_start.copy()
            pos_end = self.current_tok.pos_end.copy()
            if self._loop_depth == 0:
                return res.failure(InvalidSyntaxError(
                    pos_start, pos_end,
                    "'break' is only valid inside a loop",
                ))
            res.register_advancement()
            self.advance()
            if self.current_tok.type != TT_SEMICOLON:
                return res.failure(InvalidSyntaxError(
                    self.current_tok.pos_start, self.current_tok.pos_end,
                    "Expected ';' after 'break'",
                ))
            res.register_advancement()
            self.advance()
            return res.success(BreakNode(pos_start, pos_end))

        if (self.current_tok.matches(TT_KEYWORD, "continue")
                or self.current_tok.matches(TT_KEYWORD, "restart")):
            kw = self.current_tok.value
            pos_start = self.current_tok.pos_start.copy()
            pos_end = self.current_tok.pos_end.copy()
            if self._loop_depth == 0:
                return res.failure(InvalidSyntaxError(
                    pos_start, pos_end,
                    f"'{kw}' is only valid inside a loop",
                ))
            res.register_advancement()
            self.advance()
            if self.current_tok.type != TT_SEMICOLON:
                return res.failure(InvalidSyntaxError(
                    self.current_tok.pos_start, self.current_tok.pos_end,
                    f"Expected ';' after '{kw}'",
                ))
            res.register_advancement()
            self.advance()
            return res.success(ContinueNode(pos_start, pos_end))

        if self.current_tok.matches(TT_KEYWORD, "return"):
            node = res.register(self.parse_return())
            if res.error:
                return res
            return res.success(node)

        if self.current_tok.matches(TT_KEYWORD, "const"):
            node = res.register(self.parse_const_decl())
            if res.error:
                return res
            return res.success(node)

        if self.current_tok.matches(TT_KEYWORD, "shared"):
            res.register_advancement()
            self.advance()
            if (
                self.current_tok.type == TT_IDENTIFIER
                and (next_tok := self.peek(1)) is not None
                and next_tok.type == TT_SEMICOLON
            ):
                name_tok = self.current_tok
                pos_end = next_tok.pos_end.copy()
                res.register_advancement()
                self.advance()
                res.register_advancement()
                self.advance()
                return res.success(SharedNode(name_tok))
            node = res.register(self.parse_var_decl(is_shared=True))
            if res.error:
                return res
            return res.success(node)

        # A struct declaration is a declaration form, not a variable whose
        # type is "struct".  Handle it before the generic typed declaration
        # branch because "struct" is also a type keyword for field matching.
        if self.current_tok.matches(TT_KEYWORD, "struct"):
            node = res.register(self.parse_vargroup_decl(kind="struct"))
            if res.error:
                return res
            return res.success(node)

        if self.is_type_keyword():
            next1 = self.peek(1)
            next2 = self.peek(2)
            next1_starts_dotpath = next1 and (
                next1.type == TT_IDENTIFIER
                or next1.matches(TT_KEYWORD, "global")
            )
            if next1_starts_dotpath and next2 and next2.type == TT_DOT:
                node = res.register(self.parse_typed_dot_assign())
                if res.error:
                    return res
                return res.success(node)
            node = res.register(self.parse_var_decl())
            if res.error:
                return res
            return res.success(node)

        if self.current_tok.matches(TT_KEYWORD, "vargroup"):
            next1 = self.peek(1)
            next2 = self.peek(2)
            if next1 and next1.type == TT_IDENTIFIER and next2 and next2.type == TT_DOT:
                node = res.register(self.parse_typed_dot_assign())
                if res.error:
                    return res
                return res.success(node)
            node = res.register(self.parse_vargroup_decl())
            if res.error:
                return res
            return res.success(node)

        if self.current_tok.type == TT_IDENTIFIER:
            next_tok = self.peek(1)

            # User-defined class type declaration, e.g.
            # ``Counter counter = new Counter(1);``.
            if next_tok is not None and next_tok.type == TT_IDENTIFIER:
                node = res.register(self.parse_var_decl())
                if res.error:
                    return res
                return res.success(node)

            if (
                self.current_tok.value == "iterate"
                and next_tok
                and next_tok.type == TT_LPAREN
            ):
                node = res.register(self.parse_iterate())
                if res.error:
                    return res
                return res.success(node)

            if (
                self.current_tok.value == "addVarGroup"
                and next_tok
                and next_tok.type == TT_LPAREN
            ):
                node = res.register(self.parse_add_vargroup())
                if res.error:
                    return res
                return res.success(node)

            if (
                self.current_tok.value == "removeVarGroup"
                and next_tok
                and next_tok.type == TT_LPAREN
            ):
                node = res.register(self.parse_remove_vargroup())
                if res.error:
                    return res
                return res.success(node)

            if (
                self.current_tok.value == "rawPy"
                and next_tok
                and next_tok.type == TT_RAWPY_BLOCK
            ):
                res.register_advancement()
                self.advance()
                code = self.current_tok.value
                pos_end = self.current_tok.pos_end.copy()
                res.register_advancement()
                self.advance()
                return res.success(RawPyBlockNode(code, pos_start, pos_end))

            if (
                self.current_tok.value == "rawPyx"
                and next_tok
                and next_tok.type == TT_RAWPYX_BLOCK
            ):
                res.register_advancement()
                self.advance()
                code = self.current_tok.value
                pos_end = self.current_tok.pos_end.copy()
                res.register_advancement()
                self.advance()
                return res.success(RawPyxBlockNode(code, pos_start, pos_end))

            if (
                self.current_tok.value == "exec"
                and next_tok
                and next_tok.type == TT_LBRACE
            ):
                return res.failure(InvalidSyntaxError(
                    self.current_tok.pos_start,
                    next_tok.pos_end,
                    "The exec block syntax is now exec(){...}; "
                    "use parentheses before the block",
                ))

            if (
                self.current_tok.value == "exec"
                and next_tok
                and next_tok.type == TT_EXEC_BLOCK
            ):
                if self._exec_mode:
                    return res.failure(InvalidSyntaxError(
                        self.current_tok.pos_start,
                        self.current_tok.pos_end,
                        "Code blocks are not allowed inside exec(); "
                        "call the function without a nested exec block",
                    ))
                res.register_advancement()
                self.advance()
                code = self.current_tok.value
                pos_end = self.current_tok.pos_end.copy()
                exec_res = self.parse_exec_source(code, in_setup=in_setup)
                if exec_res.error:
                    return res.failure(exec_res.error)
                res.register_advancement()
                self.advance()
                return res.success(ExecBlockNode(exec_res.node, pos_start, pos_end))

            if next_tok and next_tok.type in (
                TT_EQ,
                TT_PLUSEQ,
                TT_MINUSEQ,
                TT_MULEQ,
                TT_DIVEQ,
                TT_MODEQ,
                TT_POWEQ,
                TT_ROOTEQ,
                TT_FLOORDIVEQ,
            ):
                node = res.register(self.parse_assign())
                if res.error:
                    return res
                return res.success(node)

            expr = res.register(self.parse_expr())
            if res.error:
                return res

            if isinstance(expr, DotAccessNode) and self.current_tok.type == TT_EQ:
                # Untyped dot assignment is the natural form for instance
                # fields (``this.value = ...``).  Vargroups still get their
                # explicit-type validation in visit_DotAssignNode.
                res.register_advancement()
                self.advance()
                rhs = res.register(self.parse_expr())
                if res.error:
                    return res
                if self.current_tok.type != TT_SEMICOLON:
                    return res.failure(InvalidSyntaxError(
                        self.current_tok.pos_start,
                        self.current_tok.pos_end,
                        "Expected ';' after field assignment",
                    ))
                pos_end = self.current_tok.pos_end.copy()
                res.register_advancement()
                self.advance()
                return res.success(DotAssignNode(
                    expr.obj_node,
                    expr.attr_name_tok,
                    rhs,
                    None,
                    expr.pos_start,
                    pos_end,
                ))

            if self.current_tok.type != TT_SEMICOLON:
                if not (
                    (isinstance(expr, CallNode) and expr.block_arg_nodes)
                    or isinstance(expr, ExecCallNode)
                ):
                    return res.failure(
                        InvalidSyntaxError(
                            expr.pos_end,
                            self.current_tok.pos_start,
                            "Missing ';' after this statement",
                        )
                    )
            else:
                res.register_advancement()
                self.advance()
            return res.success(expr)

        return res.failure(
            InvalidSyntaxError(
                self.current_tok.pos_start,
                self.current_tok.pos_end,
                f"Unexpected token '{self.current_tok.value or self.current_tok.type}' — expected a statement",
            )
        )

    def parse_var_decl(self, is_shared=False):
        res = ParseResult()
        type_tok = self.current_tok
        res.register_advancement()
        self.advance()

        if self.current_tok.type != TT_IDENTIFIER:
            return res.failure(
                InvalidSyntaxError(
                    self.current_tok.pos_start,
                    self.current_tok.pos_end,
                    "Expected variable name",
                )
            )
        name_tok = self.current_tok
        res.register_advancement()
        self.advance()

        if type_tok.value == "codeblock":
            duplicate_error = self.claim_code_block_name(name_tok)
            if duplicate_error is not None:
                return res.failure(duplicate_error)

        if self.current_tok.type != TT_EQ:
            return res.failure(
                InvalidSyntaxError(
                    self.current_tok.pos_start, self.current_tok.pos_end, "Expected '='"
                )
            )
        res.register_advancement()
        self.advance()

        value = res.register(self.parse_expr())
        if res.error:
            return res

        if type_tok.value == "codeblock" and isinstance(value, CodeBlockLiteralNode):
            if self.current_tok.type == TT_LBRACKET:
                value.param_toks = res.register(self.parse_codeblock_params())
                if res.error:
                    return res

        if type_tok.value == "tuple" and isinstance(value, ListNode):
            warn_legacy_syntax_position(
                value.pos_start,
                warning_message("legacy_tuple"),
            )

        if self.current_tok.type != TT_SEMICOLON:
            if type_tok.value == "codeblock" and isinstance(value, CodeBlockLiteralNode):
                return res.success(VarDeclNode(
                    type_tok, name_tok, value, is_const=False, is_shared=is_shared
                ))
            return res.failure(
                InvalidSyntaxError(
                    self.current_tok.pos_start, self.current_tok.pos_end, "Expected ';'"
                )
            )
        res.register_advancement()
        self.advance()

        return res.success(VarDeclNode(
            type_tok, name_tok, value, is_const=False, is_shared=is_shared
        ))

    def parse_const_decl(self):
        res = ParseResult()
        res.register_advancement()
        self.advance()

        # const vargroup name = [...];
        if self.current_tok.matches(TT_KEYWORD, "vargroup"):
            node = res.register(self.parse_vargroup_decl())
            if res.error:
                return res
            node.is_const = True
            return res.success(node)

        if not self.is_type_name():
            return res.failure(
                InvalidSyntaxError(
                    self.current_tok.pos_start,
                    self.current_tok.pos_end,
                    "Expected a built-in or class type after 'const'",
                )
            )
        type_tok = self.current_tok
        res.register_advancement()
        self.advance()

        if self.current_tok.type != TT_IDENTIFIER:
            return res.failure(
                InvalidSyntaxError(
                    self.current_tok.pos_start,
                    self.current_tok.pos_end,
                    "Expected variable name",
                )
            )
        name_tok = self.current_tok
        res.register_advancement()
        self.advance()

        if type_tok.value == "codeblock":
            duplicate_error = self.claim_code_block_name(name_tok)
            if duplicate_error is not None:
                return res.failure(duplicate_error)

        if self.current_tok.type != TT_EQ:
            return res.failure(
                InvalidSyntaxError(
                    self.current_tok.pos_start, self.current_tok.pos_end, "Expected '='"
                )
            )
        res.register_advancement()
        self.advance()

        value = res.register(self.parse_expr())
        if res.error:
            return res

        if type_tok.value == "codeblock" and isinstance(value, CodeBlockLiteralNode):
            if self.current_tok.type == TT_LBRACKET:
                value.param_toks = res.register(self.parse_codeblock_params())
                if res.error:
                    return res

        if type_tok.value == "tuple" and isinstance(value, ListNode):
            warn_legacy_syntax_position(
                value.pos_start,
                warning_message("legacy_tuple"),
            )

        if self.current_tok.type != TT_SEMICOLON:
            if type_tok.value == "codeblock" and isinstance(value, CodeBlockLiteralNode):
                return res.success(VarDeclNode(type_tok, name_tok, value, is_const=True))
            return res.failure(
                InvalidSyntaxError(
                    self.current_tok.pos_start, self.current_tok.pos_end, "Expected ';'"
                )
            )
        res.register_advancement()
        self.advance()

        return res.success(VarDeclNode(type_tok, name_tok, value, is_const=True))

    def parse_assign(self):
        res = ParseResult()
        name_tok = self.current_tok
        res.register_advancement()
        self.advance()

        op_tok = self.current_tok
        res.register_advancement()
        self.advance()

        value = res.register(self.parse_expr())
        if res.error:
            return res

        if self.current_tok.type != TT_SEMICOLON:
            return res.failure(
                InvalidSyntaxError(
                    self.current_tok.pos_start, self.current_tok.pos_end, "Expected ';'"
                )
            )
        res.register_advancement()
        self.advance()

        if op_tok.type == TT_PLUSEQ:
            value = BinOpNode(VarAccessNode(name_tok), Token(TT_PLUS), value)
        elif op_tok.type == TT_MINUSEQ:
            value = BinOpNode(VarAccessNode(name_tok), Token(TT_MINUS), value)
        elif op_tok.type == TT_MULEQ:
            value = BinOpNode(VarAccessNode(name_tok), Token(TT_MUL), value)
        elif op_tok.type == TT_DIVEQ:
            value = BinOpNode(VarAccessNode(name_tok), Token(TT_DIV), value)
        elif op_tok.type == TT_MODEQ:
            value = BinOpNode(VarAccessNode(name_tok), Token(TT_MOD), value)
        elif op_tok.type == TT_POWEQ:
            value = BinOpNode(VarAccessNode(name_tok), Token(TT_POW), value)
        elif op_tok.type == TT_ROOTEQ:
            value = BinOpNode(VarAccessNode(name_tok), Token(TT_ROOT), value)
        elif op_tok.type == TT_FLOORDIVEQ:
            value = BinOpNode(VarAccessNode(name_tok), Token(TT_FLOORDIV), value)

        return res.success(VarAssignNode(name_tok, value))

    # vargroup

    def parse_typed_dot_assign(self):
        """type vg.field = value;  or  type vg.a.b.field = value;"""
        res = ParseResult()
        pos_start = self.current_tok.pos_start.copy()

        type_tok = self.current_tok
        decl_type = type_tok.value  # e.g. "int", "str", "vargroup"
        res.register_advancement()
        self.advance()

        expr = res.register(self.parse_expr())
        if res.error:
            return res

        if not isinstance(expr, DotAccessNode):
            return res.failure(
                InvalidSyntaxError(
                    pos_start,
                    self.current_tok.pos_start,
                    "Expected a dot-path (e.g. player.coins) after type keyword in vargroup assignment",
                )
            )

        if self.current_tok.type != TT_EQ:
            return res.failure(
                InvalidSyntaxError(
                    self.current_tok.pos_start,
                    self.current_tok.pos_end,
                    "Expected '=' in vargroup field assignment",
                )
            )
        res.register_advancement()
        self.advance()

        rhs = res.register(self.parse_expr())
        if res.error:
            return res

        if self.current_tok.type != TT_SEMICOLON:
            return res.failure(
                InvalidSyntaxError(
                    self.current_tok.pos_start,
                    self.current_tok.pos_end,
                    "Expected ';' after vargroup field assignment",
                )
            )
        pos_end = self.current_tok.pos_end.copy()
        res.register_advancement()
        self.advance()

        return res.success(
            DotAssignNode(expr.obj_node, expr.attr_name_tok, rhs, decl_type, pos_start, pos_end)
        )

    def parse_iterate(self):
        # parse: iterate(count) { body }
        res = ParseResult()
        pos_start = self.current_tok.pos_start.copy()
        res.register_advancement()
        self.advance()  # consume 'iterate'

        if self.current_tok.type != TT_LPAREN:
            return res.failure(InvalidSyntaxError(
                self.current_tok.pos_start, self.current_tok.pos_end,
                "Expected '(' after iterate",
            ))
        res.register_advancement()
        self.advance()

        count_node = res.register(self.parse_expr())
        if res.error:
            return res

        if self.current_tok.type != TT_RPAREN:
            return res.failure(InvalidSyntaxError(
                self.current_tok.pos_start, self.current_tok.pos_end,
                "Expected ')' after iterate count",
            ))
        res.register_advancement()
        self.advance()

        if self.current_tok.type != TT_LBRACE:
            return res.failure(InvalidSyntaxError(
                self.current_tok.pos_start, self.current_tok.pos_end,
                "Expected '{' after iterate(...)",
            ))
        self._loop_depth += 1
        body = res.register(self.parse_block(allow_function_defs=False))
        self._loop_depth -= 1
        if res.error:
            return res

        return res.success(IterateNode(count_node, body, pos_start, body.pos_end))

    def parse_forever(self):
        # parse: forever() { body }
        res = ParseResult()
        pos_start = self.current_tok.pos_start.copy()
        res.register_advancement()
        self.advance()  # consume 'forever'

        if self.current_tok.type != TT_LPAREN:
            return res.failure(InvalidSyntaxError(
                self.current_tok.pos_start, self.current_tok.pos_end,
                "Expected '(' after forever",
            ))
        res.register_advancement()
        self.advance()

        if self.current_tok.type != TT_RPAREN:
            return res.failure(InvalidSyntaxError(
                self.current_tok.pos_start, self.current_tok.pos_end,
                "forever() does not accept arguments",
            ))
        res.register_advancement()
        self.advance()

        if self.current_tok.type != TT_LBRACE:
            return res.failure(InvalidSyntaxError(
                self.current_tok.pos_start, self.current_tok.pos_end,
                "Expected '{' after forever()",
            ))
        self._loop_depth += 1
        body = res.register(self.parse_block(allow_function_defs=False))
        self._loop_depth -= 1
        if res.error:
            return res

        return res.success(
            ForeverNode(
                body,
                pos_start,
                body.pos_end,
                has_break=_block_contains_break(body),
            )
        )

    def parse_vargroup_field(self):
        """Parse one field inside a vargroup body."""
        res = ParseResult()
        pos_start = self.current_tok.pos_start.copy()

        if self.current_tok.matches(TT_KEYWORD, "vargroup"):
            # nested vargroup field
            res.register_advancement()
            self.advance()

            if self.current_tok.type != TT_IDENTIFIER:
                return res.failure(
                    InvalidSyntaxError(
                        self.current_tok.pos_start,
                        self.current_tok.pos_end,
                        "Expected nested vargroup name",
                    )
                )
            name_tok = self.current_tok
            res.register_advancement()
            self.advance()

            if self.current_tok.type != TT_EQ:
                return res.failure(
                    InvalidSyntaxError(
                        self.current_tok.pos_start,
                        self.current_tok.pos_end,
                        "Expected '=' after nested vargroup name",
                    )
                )
            res.register_advancement()
            self.advance()

            if self.current_tok.type not in (TT_LBRACE, TT_LBRACKET):
                return res.failure(
                    InvalidSyntaxError(
                        self.current_tok.pos_start,
                        self.current_tok.pos_end,
                        "Expected '{' to open nested vargroup body",
                    )
                )
            open_tok = self.current_tok
            close_type = TT_RBRACKET if open_tok.type == TT_LBRACKET else TT_RBRACE
            if open_tok.type == TT_LBRACKET:
                warn_legacy_syntax(
                    open_tok,
                    warning_message("legacy_vargroup"),
                )
            res.register_advancement()
            self.advance()

            nested_fields = []
            while self.current_tok.type != close_type:
                if self.current_tok.type == TT_EOF:
                    return res.failure(
                        InvalidSyntaxError(
                            self.current_tok.pos_start,
                            self.current_tok.pos_end,
                            "Expected '}' to close nested vargroup",
                        )
                    )
                f = res.register(self.parse_vargroup_field())
                if res.error:
                    return res
                nested_fields.append(f)
                if self.current_tok.type == TT_COMMA:
                    res.register_advancement()
                    self.advance()
                elif self.current_tok.type != close_type:
                    return res.failure(
                        InvalidSyntaxError(
                            self.current_tok.pos_start,
                            self.current_tok.pos_end,
                            "Expected ',' or '}' in nested vargroup body",
                        )
                    )

            pos_end = self.current_tok.pos_end.copy()
            res.register_advancement()
            self.advance()  # consume the nested vargroup delimiter

            nested_node = VarGroupDeclNode(name_tok, nested_fields, pos_start, pos_end)
            return res.success(("vargroup", name_tok, nested_node, False))

        is_const = False
        if self.current_tok.matches(TT_KEYWORD, "const"):
            is_const = True
            res.register_advancement()
            self.advance()

        if not self.is_type_keyword():
            return res.failure(
                InvalidSyntaxError(
                    self.current_tok.pos_start,
                    self.current_tok.pos_end,
                    "Expected a type keyword or 'vargroup' for field declaration",
                )
            )
        type_tok = self.current_tok
        res.register_advancement()
        self.advance()

        if self.current_tok.type != TT_IDENTIFIER:
            return res.failure(
                InvalidSyntaxError(
                    self.current_tok.pos_start,
                    self.current_tok.pos_end,
                    "Expected field name",
                )
            )
        name_tok = self.current_tok
        res.register_advancement()
        self.advance()

        if self.current_tok.type != TT_EQ:
            return res.failure(
                InvalidSyntaxError(
                    self.current_tok.pos_start,
                    self.current_tok.pos_end,
                    "Expected '=' after field name",
                )
            )
        res.register_advancement()
        self.advance()

        value_node = res.register(self.parse_expr())
        if res.error:
            return res

        return res.success((type_tok.value, name_tok, value_node, is_const))

    def parse_vargroup_decl(self, kind="vargroup"):
        """Parse: vargroup name = { fields... }; """
        res = ParseResult()
        pos_start = self.current_tok.pos_start.copy()
        res.register_advancement()
        self.advance()  # consume declaration keyword

        if self.current_tok.type != TT_IDENTIFIER:
            return res.failure(
                InvalidSyntaxError(
                    self.current_tok.pos_start,
                    self.current_tok.pos_end,
                    f"Expected {kind} name",
                )
            )
        name_tok = self.current_tok
        res.register_advancement()
        self.advance()

        if self.current_tok.type != TT_EQ:
            return res.failure(
                InvalidSyntaxError(
                    self.current_tok.pos_start,
                    self.current_tok.pos_end,
                    f"Expected '=' after {kind} name",
                )
            )
        res.register_advancement()
        self.advance()

        if self.current_tok.type not in (TT_LBRACE, TT_LBRACKET):
            return res.failure(
                InvalidSyntaxError(
                    self.current_tok.pos_start,
                    self.current_tok.pos_end,
                    f"Expected '{{' to open {kind} body",
                )
            )
        open_tok = self.current_tok
        close_type = TT_RBRACKET if open_tok.type == TT_LBRACKET else TT_RBRACE
        if open_tok.type == TT_LBRACKET:
            warn_legacy_syntax(
                open_tok,
                warning_message("legacy_vargroup"),
            )
        res.register_advancement()
        self.advance()

        fields = []
        while self.current_tok.type != close_type:
            if self.current_tok.type == TT_EOF:
                return res.failure(
                    InvalidSyntaxError(
                        self.current_tok.pos_start,
                        self.current_tok.pos_end,
                            f"Expected '}}' to close {kind}",
                    )
                )
            field = res.register(self.parse_vargroup_field())
            if res.error:
                return res
            fields.append(field)
            if self.current_tok.type == TT_COMMA:
                res.register_advancement()
                self.advance()
            elif self.current_tok.type != close_type:
                return res.failure(
                    InvalidSyntaxError(
                        self.current_tok.pos_start,
                        self.current_tok.pos_end,
                        f"Expected ',' or '}}' in {kind} body",
                    )
                )

        res.register_advancement()
        self.advance()  # consume the vargroup delimiter

        if self.current_tok.type != TT_SEMICOLON:
            return res.failure(
                InvalidSyntaxError(
                    self.current_tok.pos_start,
                    self.current_tok.pos_end,
                    f"Expected ';' after {kind} declaration",
                )
            )
        pos_end = self.current_tok.pos_end.copy()
        res.register_advancement()
        self.advance()

        return res.success(VarGroupDeclNode(
            name_tok, fields, pos_start, pos_end, kind=kind
        ))

    def parse_add_vargroup(self):
        """Parse: addVarGroup(path_expr, type name = value); """
        res = ParseResult()
        pos_start = self.current_tok.pos_start.copy()
        res.register_advancement()
        self.advance()  # consume 'addVarGroup'

        if self.current_tok.type != TT_LPAREN:
            return res.failure(
                InvalidSyntaxError(
                    self.current_tok.pos_start,
                    self.current_tok.pos_end,
                    "Expected '(' after addVarGroup",
                )
            )
        res.register_advancement()
        self.advance()

        path_node = res.register(self.parse_expr())
        if res.error:
            return res

        if self.current_tok.type != TT_COMMA:
            return res.failure(
                InvalidSyntaxError(
                    self.current_tok.pos_start,
                    self.current_tok.pos_end,
                    "Expected ',' after path in addVarGroup",
                )
            )
        res.register_advancement()
        self.advance()

        if self.current_tok.matches(TT_KEYWORD, "vargroup"):
            res.register_advancement()
            self.advance()
            field_type = "vargroup"

            if self.current_tok.type != TT_IDENTIFIER:
                return res.failure(
                    InvalidSyntaxError(
                        self.current_tok.pos_start,
                        self.current_tok.pos_end,
                        "Expected field name",
                    )
                )
            field_name_tok = self.current_tok
            res.register_advancement()
            self.advance()

            if self.current_tok.type != TT_EQ:
                return res.failure(
                    InvalidSyntaxError(
                        self.current_tok.pos_start,
                        self.current_tok.pos_end,
                        "Expected '='",
                    )
                )
            res.register_advancement()
            self.advance()

            if self.current_tok.type != TT_LBRACKET:
                return res.failure(
                    InvalidSyntaxError(
                        self.current_tok.pos_start,
                        self.current_tok.pos_end,
                        "Expected '[' to open nested vargroup body",
                    )
                )
            res.register_advancement()
            self.advance()

            nested_fields = []
            while self.current_tok.type != TT_RBRACKET:
                if self.current_tok.type == TT_EOF:
                    return res.failure(
                        InvalidSyntaxError(
                            self.current_tok.pos_start,
                            self.current_tok.pos_end,
                            "Expected ']' to close nested vargroup",
                        )
                    )
                f = res.register(self.parse_vargroup_field())
                if res.error:
                    return res
                nested_fields.append(f)
                if self.current_tok.type == TT_COMMA:
                    res.register_advancement()
                    self.advance()
                elif self.current_tok.type != TT_RBRACKET:
                    return res.failure(
                        InvalidSyntaxError(
                            self.current_tok.pos_start,
                            self.current_tok.pos_end,
                            "Expected ',' or ']'",
                        )
                    )

            res.register_advancement()
            self.advance()  # consume ']'

            field_value_node = VarGroupDeclNode(
                field_name_tok, nested_fields,
                field_name_tok.pos_start, self.current_tok.pos_start,
            )
        else:
            if not self.is_type_keyword():
                return res.failure(
                    InvalidSyntaxError(
                        self.current_tok.pos_start,
                        self.current_tok.pos_end,
                        "Expected a type keyword or 'vargroup' for field declaration",
                    )
                )
            type_tok = self.current_tok
            field_type = type_tok.value
            res.register_advancement()
            self.advance()

            if self.current_tok.type != TT_IDENTIFIER:
                return res.failure(
                    InvalidSyntaxError(
                        self.current_tok.pos_start,
                        self.current_tok.pos_end,
                        "Expected field name",
                    )
                )
            field_name_tok = self.current_tok
            res.register_advancement()
            self.advance()

            if self.current_tok.type != TT_EQ:
                return res.failure(
                    InvalidSyntaxError(
                        self.current_tok.pos_start,
                        self.current_tok.pos_end,
                        "Expected '='",
                    )
                )
            res.register_advancement()
            self.advance()

            field_value_node = res.register(self.parse_expr())
            if res.error:
                return res

        if self.current_tok.type != TT_RPAREN:
            return res.failure(
                InvalidSyntaxError(
                    self.current_tok.pos_start,
                    self.current_tok.pos_end,
                    "Expected ')'",
                )
            )
        res.register_advancement()
        self.advance()

        if self.current_tok.type != TT_SEMICOLON:
            return res.failure(
                InvalidSyntaxError(
                    self.current_tok.pos_start,
                    self.current_tok.pos_end,
                    "Expected ';' after addVarGroup",
                )
            )
        pos_end = self.current_tok.pos_end.copy()
        res.register_advancement()
        self.advance()

        return res.success(
            AddVarGroupNode(path_node, field_type, field_name_tok, field_value_node,
                            pos_start, pos_end)
        )

    def parse_remove_vargroup(self):
        """Parse: removeVarGroup(path_expr, field_name); """
        res = ParseResult()
        pos_start = self.current_tok.pos_start.copy()
        res.register_advancement()
        self.advance()  # consume 'removeVarGroup'

        if self.current_tok.type != TT_LPAREN:
            return res.failure(
                InvalidSyntaxError(
                    self.current_tok.pos_start,
                    self.current_tok.pos_end,
                    "Expected '(' after removeVarGroup",
                )
            )
        res.register_advancement()
        self.advance()

        path_node = res.register(self.parse_expr())
        if res.error:
            return res

        if self.current_tok.type != TT_COMMA:
            return res.failure(
                InvalidSyntaxError(
                    self.current_tok.pos_start,
                    self.current_tok.pos_end,
                    "Expected ',' after path in removeVarGroup",
                )
            )
        res.register_advancement()
        self.advance()

        if self.current_tok.type != TT_IDENTIFIER:
            return res.failure(
                InvalidSyntaxError(
                    self.current_tok.pos_start,
                    self.current_tok.pos_end,
                    "Expected field name to remove",
                )
            )
        field_name_tok = self.current_tok
        res.register_advancement()
        self.advance()

        if self.current_tok.type != TT_RPAREN:
            return res.failure(
                InvalidSyntaxError(
                    self.current_tok.pos_start,
                    self.current_tok.pos_end,
                    "Expected ')'",
                )
            )
        res.register_advancement()
        self.advance()

        if self.current_tok.type != TT_SEMICOLON:
            return res.failure(
                InvalidSyntaxError(
                    self.current_tok.pos_start,
                    self.current_tok.pos_end,
                    "Expected ';' after removeVarGroup",
                )
            )
        pos_end = self.current_tok.pos_end.copy()
        res.register_advancement()
        self.advance()

        return res.success(RemoveVarGroupNode(path_node, field_name_tok, pos_start, pos_end))

    # /vargroup

    def parse_import(self):
        res = ParseResult()
        pos_start = self.current_tok.pos_start.copy()
        res.register_advancement()
        self.advance()

        if self.current_tok.type != TT_LPAREN:
            return res.failure(
                InvalidSyntaxError(
                    self.current_tok.pos_start,
                    self.current_tok.pos_end,
                    "Expected '(' after import",
                )
            )
        res.register_advancement()
        self.advance()

        if self.current_tok.type != TT_STRING:
            return res.failure(
                InvalidSyntaxError(
                    self.current_tok.pos_start,
                    self.current_tok.pos_end,
                    "Expected filename string",
                )
            )
        filename_tok = self.current_tok
        res.register_advancement()
        self.advance()

        if self.current_tok.type != TT_RPAREN:
            return res.failure(
                InvalidSyntaxError(
                    self.current_tok.pos_start, self.current_tok.pos_end, "Expected ')'"
                )
            )
        res.register_advancement()
        self.advance()

        if self.current_tok.type != TT_SEMICOLON:
            return res.failure(
                InvalidSyntaxError(
                    self.current_tok.pos_start, self.current_tok.pos_end, "Expected ';'"
                )
            )
        pos_end = self.current_tok.pos_end.copy()
        res.register_advancement()
        self.advance()

        return res.success(ImportNode(filename_tok, pos_start, pos_end))

    def parse_importAs(self):
        res = ParseResult()
        pos_start = self.current_tok.pos_start.copy()
        res.register_advancement()
        self.advance()  # consume 'importAs'

        if self.current_tok.type != TT_LPAREN:
            return res.failure(
                InvalidSyntaxError(
                    self.current_tok.pos_start,
                    self.current_tok.pos_end,
                    "Expected '(' after importAs",
                )
            )
        res.register_advancement()
        self.advance()

        if self.current_tok.type != TT_STRING:
            return res.failure(
                InvalidSyntaxError(
                    self.current_tok.pos_start,
                    self.current_tok.pos_end,
                    "Expected module filename string as first argument to importAs()",
                )
            )
        filename_tok = self.current_tok
        res.register_advancement()
        self.advance()

        if self.current_tok.type != TT_COMMA:
            return res.failure(
                InvalidSyntaxError(
                    self.current_tok.pos_start,
                    self.current_tok.pos_end,
                    "Expected ',' between module name and alias in importAs()",
                )
            )
        res.register_advancement()
        self.advance()

        if self.current_tok.type != TT_STRING:
            return res.failure(
                InvalidSyntaxError(
                    self.current_tok.pos_start,
                    self.current_tok.pos_end,
                    "Expected alias string as second argument to importAs()",
                )
            )
        alias_tok = self.current_tok
        res.register_advancement()
        self.advance()

        if self.current_tok.type != TT_RPAREN:
            return res.failure(
                InvalidSyntaxError(
                    self.current_tok.pos_start, self.current_tok.pos_end, "Expected ')'"
                )
            )
        res.register_advancement()
        self.advance()

        if self.current_tok.type != TT_SEMICOLON:
            return res.failure(
                InvalidSyntaxError(
                    self.current_tok.pos_start, self.current_tok.pos_end, "Expected ';'"
                )
            )
        pos_end = self.current_tok.pos_end.copy()
        res.register_advancement()
        self.advance()

        return res.success(ImportAsNode(filename_tok, alias_tok, pos_start, pos_end))

    def parse_importPy(self):
        """Parse:  importPy(){"os", "sys", "json"};."""
        res = ParseResult()
        pos_start = self.current_tok.pos_start.copy()
        res.register_advancement()
        self.advance()  # consume 'importPy'

        # Expect ()
        if self.current_tok.type != TT_LPAREN:
            return res.failure(InvalidSyntaxError(
                self.current_tok.pos_start, self.current_tok.pos_end,
                "Expected '(' after importPy",
            ))
        res.register_advancement()
        self.advance()

        if self.current_tok.type != TT_RPAREN:
            return res.failure(InvalidSyntaxError(
                self.current_tok.pos_start, self.current_tok.pos_end,
                "importPy() takes no arguments inside the parentheses — "
                "put module names in the braces: importPy(){\"os\", \"sys\"};",
            ))
        res.register_advancement()
        self.advance()

        # Expect {
        if self.current_tok.type != TT_LBRACE:
            return res.failure(InvalidSyntaxError(
                self.current_tok.pos_start, self.current_tok.pos_end,
                "Expected '{' containing module names after importPy()",
            ))
        res.register_advancement()
        self.advance()

        # Collect comma-separated string literals
        module_names = []
        if self.current_tok.type == TT_RBRACE:
            # empty importPy(){} is allowed (no-op)
            pass
        else:
            if self.current_tok.type != TT_STRING:
                return res.failure(InvalidSyntaxError(
                    self.current_tok.pos_start, self.current_tok.pos_end,
                    "Expected a quoted module name string inside importPy(){...}",
                ))
            module_names.append(self.current_tok.value)
            res.register_advancement()
            self.advance()

            while self.current_tok.type == TT_COMMA:
                res.register_advancement()
                self.advance()
                if self.current_tok.type != TT_STRING:
                    return res.failure(InvalidSyntaxError(
                        self.current_tok.pos_start, self.current_tok.pos_end,
                        "Expected a quoted module name string after ',' in importPy(){...}",
                    ))
                module_names.append(self.current_tok.value)
                res.register_advancement()
                self.advance()

        # Expect }
        if self.current_tok.type != TT_RBRACE:
            return res.failure(InvalidSyntaxError(
                self.current_tok.pos_start, self.current_tok.pos_end,
                "Expected '}' to close importPy(){...}",
            ))
        res.register_advancement()
        self.advance()

        # Expect ;
        if self.current_tok.type != TT_SEMICOLON:
            return res.failure(InvalidSyntaxError(
                self.current_tok.pos_start, self.current_tok.pos_end,
                "Expected ';' after importPy(){...}",
            ))
        pos_end = self.current_tok.pos_end.copy()
        res.register_advancement()
        self.advance()

        return res.success(ImportPyNode(module_names, pos_start, pos_end))

    def parse_return(self):
        res = ParseResult()
        pos_start = self.current_tok.pos_start.copy()
        res.register_advancement()
        self.advance()

        expr = None
        if self.current_tok.type != TT_SEMICOLON:
            expr = res.register(self.parse_expr())
            if res.error:
                return res

        if self.current_tok.type != TT_SEMICOLON:
            return res.failure(
                InvalidSyntaxError(
                    self.current_tok.pos_start,
                    self.current_tok.pos_end,
                    "Expected ';' after return",
                )
            )
        pos_end = self.current_tok.pos_end.copy()
        res.register_advancement()
        self.advance()

        return res.success(ReturnNode(expr, pos_start, pos_end))

    def parse_if(self):
        res = ParseResult()
        pos_start = self.current_tok.pos_start.copy()
        res.register_advancement()
        self.advance()

        if self.current_tok.type != TT_LPAREN:
            return res.failure(
                InvalidSyntaxError(
                    self.current_tok.pos_start, self.current_tok.pos_end, "Expected '('"
                )
            )
        res.register_advancement()
        self.advance()

        condition = res.register(self.parse_expr())
        if res.error:
            return res

        if self.current_tok.type != TT_RPAREN:
            return res.failure(
                InvalidSyntaxError(
                    self.current_tok.pos_start, self.current_tok.pos_end, "Expected ')'"
                )
            )
        res.register_advancement()
        self.advance()

        then_block = res.register(self.parse_block(allow_local_funcs=True))
        if res.error:
            return res

        else_block = None
        if self.current_tok.matches(TT_KEYWORD, "elif"):
            else_block = res.register(self.parse_if())
            if res.error:
                return res
        elif self.current_tok.matches(TT_KEYWORD, "else"):
            res.register_advancement()
            self.advance()
            if self.current_tok.matches(TT_KEYWORD, "elif"):
                else_block = res.register(self.parse_if())
            else:
                else_block = res.register(self.parse_block(allow_local_funcs=True))
            if res.error:
                return res

        pos_end = self.current_tok.pos_end.copy()
        return res.success(
            IfNode(condition, then_block, else_block, pos_start, pos_end)
        )

    def parse_while(self):
        res = ParseResult()
        pos_start = self.current_tok.pos_start.copy()
        res.register_advancement()
        self.advance()

        if self.current_tok.type != TT_LPAREN:
            return res.failure(
                InvalidSyntaxError(
                    self.current_tok.pos_start, self.current_tok.pos_end, "Expected '('"
                )
            )
        res.register_advancement()
        self.advance()

        condition = res.register(self.parse_expr())
        if res.error:
            return res

        if self.current_tok.type != TT_RPAREN:
            return res.failure(
                InvalidSyntaxError(
                    self.current_tok.pos_start, self.current_tok.pos_end, "Expected ')'"
                )
            )
        res.register_advancement()
        self.advance()

        self._loop_depth += 1
        body = res.register(self.parse_block(
            allow_local_funcs=True,
            allow_function_defs=False,
        ))
        self._loop_depth -= 1
        if res.error:
            return res

        pos_end = self.current_tok.pos_end.copy()
        return res.success(WhileNode(condition, body, pos_start, pos_end))

    def parse_do_while(self):
        """Parse: doWhile(condition) { body }."""
        res = ParseResult()
        pos_start = self.current_tok.pos_start.copy()
        res.register_advancement()
        self.advance()  # consume 'doWhile'

        if self.current_tok.type != TT_LPAREN:
            return res.failure(InvalidSyntaxError(
                self.current_tok.pos_start,
                self.current_tok.pos_end,
                "Expected '(' after doWhile",
            ))
        res.register_advancement()
        self.advance()

        condition = None
        if self.current_tok.type != TT_RPAREN:
            condition = res.register(self.parse_expr())
            if res.error:
                return res

        if self.current_tok.type != TT_RPAREN:
            return res.failure(InvalidSyntaxError(
                self.current_tok.pos_start,
                self.current_tok.pos_end,
                "Expected ')' after doWhile condition",
            ))
        res.register_advancement()
        self.advance()

        if self.current_tok.type != TT_LBRACE:
            return res.failure(InvalidSyntaxError(
                self.current_tok.pos_start,
                self.current_tok.pos_end,
                "Expected '{' after doWhile(condition)",
            ))

        self._loop_depth += 1
        body = res.register(self.parse_block(allow_function_defs=False))
        self._loop_depth -= 1
        if res.error:
            return res

        return res.success(DoWhileNode(condition, body, pos_start, body.pos_end))

    def parse_switch(self):
        res = ParseResult()
        pos_start = self.current_tok.pos_start.copy()
        res.register_advancement()
        self.advance()  # consume 'switch'

        if self.current_tok.type != TT_LPAREN:
            return res.failure(InvalidSyntaxError(
                self.current_tok.pos_start,
                self.current_tok.pos_end,
                "Expected '(' after 'switch'",
            ))
        res.register_advancement()
        self.advance()

        if self.current_tok.type == TT_RPAREN:
            return res.failure(InvalidSyntaxError(
                self.current_tok.pos_start,
                self.current_tok.pos_end,
                "switch() requires a value to match",
            ))
        value_node = res.register(self.parse_expr())
        if res.error:
            return res

        if self.current_tok.type != TT_RPAREN:
            return res.failure(InvalidSyntaxError(
                self.current_tok.pos_start,
                self.current_tok.pos_end,
                "Expected ')' after switch value",
            ))
        res.register_advancement()
        self.advance()

        if self.current_tok.type != TT_LBRACE:
            return res.failure(InvalidSyntaxError(
                self.current_tok.pos_start,
                self.current_tok.pos_end,
                "Expected '{' after switch(...)",
            ))

        self._switch_depth += 1
        body = res.register(self.parse_block(
            allow_local_funcs=False,
            allow_function_defs=False,
        ))
        self._switch_depth -= 1
        if res.error:
            return res

        cases = body.statements
        default_count = 0
        for case in cases:
            if isinstance(case, DefaultNode):
                default_count += 1
                if default_count > 1:
                    return res.failure(InvalidSyntaxError(
                        case.pos_start,
                        case.pos_end,
                        "A switch can contain only one default() block",
                    ))
            elif not isinstance(case, CaseNode):
                return res.failure(InvalidSyntaxError(
                    case.pos_start,
                    case.pos_end,
                    "Only case(...){} or default(){} blocks are allowed directly inside a switch",
                ))

        pos_end = body.pos_end
        return res.success(SwitchNode(value_node, cases, pos_start, pos_end))

    def parse_case(self):
        res = ParseResult()
        pos_start = self.current_tok.pos_start.copy()
        res.register_advancement()
        self.advance()  # consume 'case'

        if self.current_tok.type != TT_LPAREN:
            return res.failure(InvalidSyntaxError(
                self.current_tok.pos_start,
                self.current_tok.pos_end,
                "Expected '(' after 'case'",
            ))
        res.register_advancement()
        self.advance()

        if self.current_tok.type == TT_RPAREN:
            return res.failure(InvalidSyntaxError(
                self.current_tok.pos_start,
                self.current_tok.pos_end,
                "case() requires a value to match",
            ))
        match_node = res.register(self.parse_expr())
        if res.error:
            return res

        if self.current_tok.type != TT_RPAREN:
            return res.failure(InvalidSyntaxError(
                self.current_tok.pos_start,
                self.current_tok.pos_end,
                "Expected ')' after case value",
            ))
        res.register_advancement()
        self.advance()

        body = res.register(self.parse_block(
            allow_local_funcs=True,
            allow_function_defs=None,
        ))
        if res.error:
            return res

        return res.success(CaseNode(match_node, body, pos_start, body.pos_end))

    def parse_default(self):
        res = ParseResult()
        pos_start = self.current_tok.pos_start.copy()
        res.register_advancement()
        self.advance()  # consume 'default'

        if self.current_tok.type != TT_LPAREN:
            return res.failure(InvalidSyntaxError(
                self.current_tok.pos_start,
                self.current_tok.pos_end,
                "Expected '(' after 'default'",
            ))
        res.register_advancement()
        self.advance()

        if self.current_tok.type != TT_RPAREN:
            return res.failure(InvalidSyntaxError(
                self.current_tok.pos_start,
                self.current_tok.pos_end,
                "default() does not accept a value",
            ))
        res.register_advancement()
        self.advance()

        body = res.register(self.parse_block(
            allow_local_funcs=True,
            allow_function_defs=None,
        ))
        if res.error:
            return res

        return res.success(DefaultNode(body, pos_start, body.pos_end))

    def parse_try_catch(self):
        res = ParseResult()
        pos_start = self.current_tok.pos_start.copy()
        res.register_advancement()
        self.advance()  # consume 'try'

        try_block = res.register(self.parse_block(allow_local_funcs=True))
        if res.error:
            return res

        if not self.current_tok.matches(TT_KEYWORD, "catch"):
            return res.failure(
                InvalidSyntaxError(
                    self.current_tok.pos_start,
                    self.current_tok.pos_end,
                    "Expected 'catch' after 'try' block",
                )
            )
        res.register_advancement()
        self.advance()  # consume 'catch'

        catch_var_tok = None
        if self.current_tok.type == TT_LPAREN:
            res.register_advancement()
            self.advance()  # consume '('

            if not (self.current_tok.type == TT_KEYWORD and self.current_tok.value == "str"):
                return res.failure(
                    InvalidSyntaxError(
                        self.current_tok.pos_start,
                        self.current_tok.pos_end,
                        "Expected 'str' type keyword for the catch variable — e.g. catch(str err)",
                    )
                )
            res.register_advancement()
            self.advance()  # consume 'str'

            if self.current_tok.type != TT_IDENTIFIER:
                return res.failure(
                    InvalidSyntaxError(
                        self.current_tok.pos_start,
                        self.current_tok.pos_end,
                        "Expected a variable name after 'str' in catch clause",
                    )
                )
            catch_var_tok = self.current_tok
            res.register_advancement()
            self.advance()  # consume identifier

            if self.current_tok.type != TT_RPAREN:
                return res.failure(
                    InvalidSyntaxError(
                        self.current_tok.pos_start,
                        self.current_tok.pos_end,
                        "Expected ')' to close catch clause",
                    )
                )
            res.register_advancement()
            self.advance()  # consume ')'

        catch_block = res.register(self.parse_block(allow_local_funcs=True))
        if res.error:
            return res

        pos_end = self.current_tok.pos_end.copy()
        return res.success(
            TryCatchNode(try_block, catch_var_tok, catch_block, pos_start, pos_end)
        )

    def parse_for(self):
        res = ParseResult()
        pos_start = self.current_tok.pos_start.copy()
        res.register_advancement()
        self.advance()

        if self.current_tok.type != TT_LPAREN:
            return res.failure(
                InvalidSyntaxError(
                    self.current_tok.pos_start, self.current_tok.pos_end, "Expected '('"
                )
            )
        res.register_advancement()
        self.advance()

        init_node = res.register(self.parse_for_init())
        if res.error:
            return res

        if self.current_tok.type != TT_SEMICOLON:
            return res.failure(
                InvalidSyntaxError(
                    self.current_tok.pos_start,
                    self.current_tok.pos_end,
                    "Expected ';' after for-init",
                )
            )
        res.register_advancement()
        self.advance()

        condition = res.register(self.parse_expr())
        if res.error:
            return res

        update_node = None
        if self.current_tok.type == TT_SEMICOLON:
            res.register_advancement()
            self.advance()
            if self.current_tok.type != TT_RPAREN:
                update_node = res.register(self.parse_for_update())
                if res.error:
                    return res
        elif self.current_tok.type != TT_RPAREN:
            return res.failure(
                InvalidSyntaxError(
                    self.current_tok.pos_start,
                    self.current_tok.pos_end,
                    "Expected ';' or ')' after for-condition",
                )
            )

        if update_node is None:
            update_node = self.make_default_for_update(init_node)

        if self.current_tok.type != TT_RPAREN:
            return res.failure(
                InvalidSyntaxError(
                    self.current_tok.pos_start, self.current_tok.pos_end, "Expected ')'"
                )
            )
        res.register_advancement()
        self.advance()

        self._loop_depth += 1
        body = res.register(self.parse_block(
            allow_local_funcs=True,
            allow_function_defs=False,
        ))
        self._loop_depth -= 1
        if res.error:
            return res

        pos_end = self.current_tok.pos_end.copy()
        return res.success(
            ForNode(init_node, condition, update_node, body, pos_start, pos_end)
        )

    def make_default_for_update(self, init_node):
        """Build the implicit ``i = i + 1`` update for a short for-loop."""
        name_tok = init_node.var_name_tok
        one_tok = Token(
            TT_INT,
            1,
            pos_start=name_tok.pos_start,
            pos_end=name_tok.pos_end,
        )
        plus_tok = Token(
            TT_PLUS,
            pos_start=name_tok.pos_start,
            pos_end=name_tok.pos_end,
        )
        return VarAssignNode(
            name_tok,
            BinOpNode(VarAccessNode(name_tok), plus_tok, NumberNode(one_tok)),
        )

    def parse_for_init(self):
        res = ParseResult()
        if self.is_type_keyword():
            type_tok = self.current_tok
            res.register_advancement()
            self.advance()

            if self.current_tok.type != TT_IDENTIFIER:
                return res.failure(
                    InvalidSyntaxError(
                        self.current_tok.pos_start,
                        self.current_tok.pos_end,
                        "Expected variable name",
                    )
                )
            name_tok = self.current_tok
            res.register_advancement()
            self.advance()

            if self.current_tok.type != TT_EQ:
                return res.failure(
                    InvalidSyntaxError(
                        self.current_tok.pos_start,
                        self.current_tok.pos_end,
                        "Expected '='",
                    )
                )
            res.register_advancement()
            self.advance()

            value = res.register(self.parse_expr())
            if res.error:
                return res
            return res.success(VarDeclNode(type_tok, name_tok, value, is_const=False))

        elif self.current_tok.type == TT_IDENTIFIER:
            name_tok = self.current_tok
            res.register_advancement()
            self.advance()

            if self.current_tok.type != TT_EQ:
                return res.failure(
                    InvalidSyntaxError(
                        self.current_tok.pos_start,
                        self.current_tok.pos_end,
                        "Expected '='",
                    )
                )
            res.register_advancement()
            self.advance()

            value = res.register(self.parse_expr())
            if res.error:
                return res
            return res.success(VarAssignNode(name_tok, value))

        return res.failure(
            InvalidSyntaxError(
                self.current_tok.pos_start,
                self.current_tok.pos_end,
                "Expected for-loop init statement",
            )
        )

    def parse_for_update(self):
        res = ParseResult()
        if self.current_tok.type != TT_IDENTIFIER:
            return res.failure(
                InvalidSyntaxError(
                    self.current_tok.pos_start,
                    self.current_tok.pos_end,
                    "Expected variable name in for-update",
                )
            )
        name_tok = self.current_tok
        res.register_advancement()
        self.advance()

        op_tok = self.current_tok
        if op_tok.type not in (
            TT_EQ,
            TT_PLUSEQ,
            TT_MINUSEQ,
            TT_MULEQ,
            TT_DIVEQ,
            TT_MODEQ,
            TT_POWEQ,
            TT_ROOTEQ,
            TT_FLOORDIVEQ,
        ):
            return res.failure(
                InvalidSyntaxError(
                    self.current_tok.pos_start,
                    self.current_tok.pos_end,
                    "Expected '=', '+=', '-=', '*=', '/=', '%=', '**=', '/*=', or '/%=' in for-update",
                )
            )
        res.register_advancement()
        self.advance()

        value = res.register(self.parse_expr())
        if res.error:
            return res

        compound_ops = {
            TT_PLUSEQ: TT_PLUS,
            TT_MINUSEQ: TT_MINUS,
            TT_MULEQ: TT_MUL,
            TT_DIVEQ: TT_DIV,
            TT_MODEQ: TT_MOD,
            TT_POWEQ: TT_POW,
            TT_ROOTEQ: TT_ROOT,
            TT_FLOORDIVEQ: TT_FLOORDIV,
        }
        if op_tok.type in compound_ops:
            value = BinOpNode(
                VarAccessNode(name_tok),
                Token(
                    compound_ops[op_tok.type],
                    pos_start=op_tok.pos_start,
                    pos_end=op_tok.pos_end,
                ),
                value,
            )
        return res.success(VarAssignNode(name_tok, value))

    def parse_expr(self):
        return self.parse_or_expr()

    def parse_or_expr(self):
        res = ParseResult()
        left = res.register(self.parse_and_expr())
        if res.error:
            return res

        while (
            self.current_tok.matches(TT_KEYWORD, "or")
            or self.current_tok.type in (TT_LOGICAL_OR, TT_LOGICAL_NOR)
        ):
            op_tok = self.current_tok
            res.register_advancement()
            self.advance()
            right = res.register(self.parse_and_expr())
            if res.error:
                return res
            left = BinOpNode(left, op_tok, right)

        return res.success(left)

    def parse_and_expr(self):
        res = ParseResult()
        left = res.register(self.parse_not_expr())
        if res.error:
            return res

        while (
            self.current_tok.matches(TT_KEYWORD, "and")
            or self.current_tok.type in (TT_LOGICAL_AND, TT_LOGICAL_NAND)
        ):
            op_tok = self.current_tok
            res.register_advancement()
            self.advance()
            right = res.register(self.parse_not_expr())
            if res.error:
                return res
            left = BinOpNode(left, op_tok, right)

        return res.success(left)

    def parse_not_expr(self):
        res = ParseResult()
        if (
            self.current_tok.matches(TT_KEYWORD, "not")
            or self.current_tok.type == TT_LOGICAL_NOT
        ):
            op_tok = self.current_tok
            res.register_advancement()
            self.advance()
            node = res.register(self.parse_not_expr())
            if res.error:
                return res
            return res.success(UnaryOpNode(op_tok, node))

        return self.parse_comp_expr()

    def parse_comp_expr(self):
        res = ParseResult()
        left = res.register(self.parse_bitwise_or_expr())
        if res.error:
            return res

        peek_tok = self.peek()
        if (
            self.current_tok.matches(TT_KEYWORD, "not")
            and peek_tok is not None
            and peek_tok.matches(TT_KEYWORD, "is")
        ):
            op_tok = self.current_tok
            op_tok.value = "not is"
            warn_legacy_syntax(
                op_tok,
                warning_message("legacy_not_is"),
            )
            res.register_advancement()
            self.advance()
            res.register_advancement()
            self.advance()
            right = res.register(self.parse_bitwise_or_expr())
            if res.error:
                return res
            return res.success(BinOpNode(left, op_tok, right))

        if self.current_tok.matches(TT_KEYWORD, "is"):
            op_tok = self.current_tok
            warn_legacy_syntax(
                op_tok,
                warning_message("legacy_is"),
            )
            res.register_advancement()
            self.advance()
            right = res.register(self.parse_bitwise_or_expr())
            if res.error:
                return res
            return res.success(BinOpNode(left, op_tok, right))

        if self.current_tok.type in (TT_EQEQ, TT_NE, TT_LT, TT_GT, TT_LTE, TT_GTE):
            op_tok = self.current_tok
            res.register_advancement()
            self.advance()
            right = res.register(self.parse_bitwise_or_expr())
            if res.error:
                return res
            return res.success(BinOpNode(left, op_tok, right))

        return res.success(left)

    def parse_bitwise_or_expr(self):
        res = ParseResult()
        left = res.register(self.parse_bitwise_xor_expr())
        if res.error:
            return res
        while self.current_tok.type in (TT_PIPE, TT_BITWISE_NOR):
            op_tok = self.current_tok
            res.register_advancement()
            self.advance()
            right = res.register(self.parse_bitwise_xor_expr())
            if res.error:
                return res
            left = BinOpNode(left, op_tok, right)
        return res.success(left)

    def parse_bitwise_xor_expr(self):
        res = ParseResult()
        left = res.register(self.parse_bitwise_and_expr())
        if res.error:
            return res
        while self.current_tok.type in (TT_CARET, TT_BITWISE_XNOR):
            op_tok = self.current_tok
            res.register_advancement()
            self.advance()
            right = res.register(self.parse_bitwise_and_expr())
            if res.error:
                return res
            left = BinOpNode(left, op_tok, right)
        return res.success(left)

    def parse_bitwise_and_expr(self):
        res = ParseResult()
        left = res.register(self.parse_shift_expr())
        if res.error:
            return res
        while self.current_tok.type in (TT_AMP, TT_BITWISE_NAND):
            op_tok = self.current_tok
            res.register_advancement()
            self.advance()
            right = res.register(self.parse_shift_expr())
            if res.error:
                return res
            left = BinOpNode(left, op_tok, right)
        return res.success(left)

    def parse_shift_expr(self):
        res = ParseResult()
        left = res.register(self.parse_arith_expr())
        if res.error:
            return res
        while self.current_tok.type in (TT_SHL, TT_SHR):
            op_tok = self.current_tok
            res.register_advancement()
            self.advance()
            right = res.register(self.parse_arith_expr())
            if res.error:
                return res
            left = BinOpNode(left, op_tok, right)
        return res.success(left)

    def parse_arith_expr(self):
        res = ParseResult()
        left = res.register(self.parse_term())
        if res.error:
            return res

        while self.current_tok.type in (TT_PLUS, TT_MINUS):
            op_tok = self.current_tok
            res.register_advancement()
            self.advance()
            right = res.register(self.parse_term())
            if res.error:
                return res
            left = BinOpNode(left, op_tok, right)

        return res.success(left)

    def parse_term(self):
        res = ParseResult()
        left = res.register(self.parse_power())
        if res.error:
            return res

        while self.current_tok.type in (TT_MUL, TT_DIV, TT_MOD, TT_FLOORDIV):
            op_tok = self.current_tok
            res.register_advancement()
            self.advance()
            right = res.register(self.parse_power())
            if res.error:
                return res
            left = BinOpNode(left, op_tok, right)

        return res.success(left)

    def parse_power(self):
        """Parse right-associative exponentiation and root expressions."""
        res = ParseResult()
        left = res.register(self.parse_factor())
        if res.error:
            return res

        if self.current_tok.type in (TT_POW, TT_ROOT):
            op_tok = self.current_tok
            res.register_advancement()
            self.advance()
            right = res.register(self.parse_power())
            if res.error:
                return res
            left = BinOpNode(left, op_tok, right)

        return res.success(left)

    def parse_factor(self):
        res = ParseResult()
        tok = self.current_tok

        if tok.type in (TT_PLUS, TT_MINUS, TT_TILDE):
            res.register_advancement()
            self.advance()
            factor = res.register(self.parse_factor())
            if res.error:
                return res
            return res.success(UnaryOpNode(tok, factor))

        return self.parse_call()

    def parse_new(self):
        """Parse ``new ClassName(arg1, arg2, ...)``."""
        res = ParseResult()
        pos_start = self.current_tok.pos_start.copy()
        res.register_advancement()
        self.advance()  # consume new

        if self.current_tok.type != TT_IDENTIFIER:
            return res.failure(InvalidSyntaxError(
                self.current_tok.pos_start,
                self.current_tok.pos_end,
                "Expected a class name after 'new'",
            ))
        class_name_tok = self.current_tok
        res.register_advancement()
        self.advance()

        if self.current_tok.type != TT_LPAREN:
            return res.failure(InvalidSyntaxError(
                self.current_tok.pos_start,
                self.current_tok.pos_end,
                "Expected '(' after class name in 'new ClassName(...)'",
            ))
        res.register_advancement()
        self.advance()

        arg_nodes = []
        if self.current_tok.type != TT_RPAREN:
            arg_nodes.append(res.register(self.parse_expr()))
            if res.error:
                return res
            while self.current_tok.type == TT_COMMA:
                res.register_advancement()
                self.advance()
                arg_nodes.append(res.register(self.parse_expr()))
                if res.error:
                    return res
            if self.current_tok.type != TT_RPAREN:
                return res.failure(InvalidSyntaxError(
                    self.current_tok.pos_start,
                    self.current_tok.pos_end,
                    "Expected ',' or ')' in constructor call",
                ))

        pos_end = self.current_tok.pos_end.copy()
        res.register_advancement()
        self.advance()
        return res.success(NewNode(class_name_tok, arg_nodes, pos_start, pos_end))

    def parse_call(self):
        res = ParseResult()
        pos_start = self.current_tok.pos_start.copy()
        atom = res.register(self.parse_atom())
        if res.error:
            return res

        while True:
            if self.current_tok.type == TT_DOT:
                res.register_advancement()
                self.advance()
                is_attr = (
                    self.current_tok.type == TT_IDENTIFIER
                    or self.current_tok.matches(TT_KEYWORD, "class")
                    or (
                        self.current_tok.type == TT_KEYWORD
                        and self.current_tok.value in TYPE_KEYWORDS
                    )
                )
                if not is_attr:
                    return res.failure(
                        InvalidSyntaxError(
                            self.current_tok.pos_start,
                            self.current_tok.pos_end,
                            "Expected attribute name after '.'",
                        )
                    )
                attr_tok = self.current_tok
                res.register_advancement()
                self.advance()
                atom = DotAccessNode(atom, attr_tok)

            elif self.current_tok.type == TT_LPAREN:
                res.register_advancement()
                self.advance()
                is_exec_call = (
                    isinstance(atom, VarAccessNode)
                    and atom.var_name_tok.value == "exec"
                )

                if is_exec_call:
                    if self._looks_like_exec_declaration():
                        param_toks = res.register(self.parse_exec_params())
                        if res.error:
                            return res
                        arg_nodes = []
                    else:
                        param_toks = []
                        arg_nodes = res.register(self.parse_exec_args())
                        if res.error:
                            return res

                    if self.current_tok.type != TT_LBRACE:
                        if not param_toks and len(arg_nodes) == 1:
                            path_node = arg_nodes[0]
                            atom = ExecFileNode(
                                path_node,
                                pos_start,
                                path_node.pos_end,
                            )
                            continue
                        return res.failure(
                            InvalidSyntaxError(
                                self.current_tok.pos_start,
                                self.current_tok.pos_end,
                                "Expected '{' after exec(...) or a single .lynx filename",
                            )
                        )
                    block_node = res.register(self.parse_exec_block_argument())
                    if res.error:
                        return res
                    atom = ExecCallNode(
                        block_node,
                        param_toks,
                        arg_nodes,
                        infer_params=(
                            (
                                isinstance(block_node, CodeBlockRefNode)
                                and not param_toks
                            )
                            or bool(arg_nodes)
                        ),
                        pos_start=pos_start,
                        pos_end=block_node.pos_end,
                    )
                else:
                    arg_nodes = []

                    if self.current_tok.type != TT_RPAREN:
                        arg_nodes.append(res.register(self.parse_expr()))
                        if res.error:
                            return res

                        while self.current_tok.type == TT_COMMA:
                            res.register_advancement()
                            self.advance()
                            arg_nodes.append(res.register(self.parse_expr()))
                            if res.error:
                                return res

                        if self.current_tok.type != TT_RPAREN:
                            return res.failure(
                                InvalidSyntaxError(
                                    self.current_tok.pos_start,
                                    self.current_tok.pos_end,
                                    "Expected ',' or ')'",
                                )
                            )

                    pos_end = self.current_tok.pos_end.copy()
                    res.register_advancement()
                    self.advance()
                    atom = CallNode(atom, arg_nodes, pos_start, pos_end)

            elif (
                self.current_tok.type == TT_LBRACE
                and isinstance(atom, CallNode)
                and isinstance(atom.node_to_call, VarAccessNode)
                and atom.node_to_call.var_name_tok.value == "exec"
            ):
                block_node = res.register(self.parse_exec_block_argument())
                if res.error:
                    return res
                atom = ExecCallNode(
                    block_node,
                    [],
                    atom.arg_nodes,
                    infer_params=bool(atom.arg_nodes),
                    pos_start=atom.pos_start,
                    pos_end=block_node.pos_end,
                )

            elif (
                self.current_tok.type == TT_LBRACE
                and (next_token := self.peek(1)) is not None
                and next_token.type == TT_LBRACE
                and isinstance(atom, CallNode)
            ):
                block_node = res.register(self.parse_code_block_ref_argument())
                if res.error:
                    return res
                atom.block_arg_nodes.append(block_node)
                atom.pos_end = block_node.pos_end

            elif self.current_tok.type == TT_LBRACE and isinstance(atom, CallNode):
                if self._exec_mode:
                    return res.failure(InvalidSyntaxError(
                        self.current_tok.pos_start,
                        self.current_tok.pos_end,
                        "Code blocks are not allowed inside exec(); "
                        "call the function without a trailing code block",
                    ))
                block_node = res.register(self.parse_code_block_literal())
                if res.error:
                    return res
                atom.block_arg_nodes.append(block_node)
                atom.pos_end = block_node.pos_end

            else:
                break

        return res.success(atom)

    def _looks_like_exec_declaration(self):
        """Return whether the first exec value is a typed declaration."""
        next_token = self.peek(1)
        return (
            self.is_type_keyword()
            and next_token is not None
            and next_token.type == TT_IDENTIFIER
        )

    def parse_exec_args(self):
        """Parse positional values in ``exec(value, ...)``."""
        res = ParseResult()
        arg_nodes = []

        if self.current_tok.type != TT_RPAREN:
            arg_nodes.append(res.register(self.parse_expr()))
            if res.error:
                return res

            while self.current_tok.type == TT_COMMA:
                res.register_advancement()
                self.advance()
                arg_nodes.append(res.register(self.parse_expr()))
                if res.error:
                    return res

            if self.current_tok.type != TT_RPAREN:
                return res.failure(InvalidSyntaxError(
                    self.current_tok.pos_start,
                    self.current_tok.pos_end,
                    "Expected ',' or ')'",
                ))

        res.register_advancement()
        self.advance()
        return res.success(arg_nodes)

    def parse_exec_params(self):
        """Parse declared values in ``exec(type name, ...)``."""
        res = ParseResult()
        param_toks = []
        seen_names = set()

        while self.current_tok.type != TT_RPAREN and self.current_tok.type != TT_EOF:
            next_tok = self.peek(1)
            if (
                self.is_type_keyword()
                and next_tok is not None
                and next_tok.type == TT_IDENTIFIER
            ):
                type_tok = self.current_tok
                res.register_advancement()
                self.advance()
                name_tok = self.current_tok
                res.register_advancement()
                self.advance()
            elif self.current_tok.type == TT_IDENTIFIER:
                type_tok = None
                name_tok = self.current_tok
                res.register_advancement()
                self.advance()
            else:
                return res.failure(InvalidSyntaxError(
                    self.current_tok.pos_start,
                    self.current_tok.pos_end,
                    "Expected parameter name (optionally preceded by a type: "
                    "int, float, str, bool)",
                ))

            if name_tok.value in seen_names:
                return res.failure(InvalidSyntaxError(
                    name_tok.pos_start,
                    name_tok.pos_end,
                    f"Duplicate exec parameter name '{name_tok.value}'",
                ))
            seen_names.add(name_tok.value)
            param_toks.append((type_tok, name_tok))

            if self.current_tok.type == TT_COMMA:
                res.register_advancement()
                self.advance()
            else:
                break

        if self.current_tok.type != TT_RPAREN:
            return res.failure(InvalidSyntaxError(
                self.current_tok.pos_start,
                self.current_tok.pos_end,
                "Expected ')'",
            ))
        res.register_advancement()
        self.advance()
        return res.success(param_toks)

    def parse_exec_block_argument(self):
        """Parse ``{...}`` or ``{{codeblockName}}`` after ``exec(...)``."""
        res = ParseResult()

        if self.current_tok.type != TT_LBRACE:
            return res.failure(InvalidSyntaxError(
                self.current_tok.pos_start,
                self.current_tok.pos_end,
                "Expected '{' after exec(...)",
            ))

        next_token = self.peek(1)
        if next_token is not None and next_token.type == TT_LBRACE:
            return res.success(res.register(self.parse_code_block_ref_argument()))

        block_node = res.register(self.parse_code_block_literal())
        if res.error:
            return res
        return res.success(block_node)

    def parse_code_block_ref_argument(self):
        """Parse a named block argument such as ``{{savedBlock}}``."""
        res = ParseResult()
        next_token = self.peek(1)
        if self.current_tok.type != TT_LBRACE or (
            next_token is None or next_token.type != TT_LBRACE
        ):
            return res.failure(InvalidSyntaxError(
                self.current_tok.pos_start,
                self.current_tok.pos_end,
                "Expected '{{codeblockName}}'",
            ))

        res.register_advancement()
        self.advance()  # consume the outer '{'
        block_node = res.register(self.parse_code_block_ref())
        if res.error:
            return res
        if self.current_tok.type != TT_RBRACE:
            return res.failure(InvalidSyntaxError(
                self.current_tok.pos_start,
                self.current_tok.pos_end,
                "Expected '}' after '{{codeblockName}}'",
            ))
        res.register_advancement()
        self.advance()  # consume the outer '}'
        return res.success(block_node)

    def parse_code_block_ref(self):
        res = ParseResult()
        res.register_advancement()
        self.advance()  # consume '{'

        if self.current_tok.type != TT_IDENTIFIER:
            return res.failure(InvalidSyntaxError(
                self.current_tok.pos_start,
                self.current_tok.pos_end,
                "Expected a code-block name inside exec({ ... })",
            ))
        name_tok = self.current_tok
        res.register_advancement()
        self.advance()

        if self.current_tok.type != TT_RBRACE:
            return res.failure(InvalidSyntaxError(
                self.current_tok.pos_start,
                self.current_tok.pos_end,
                "Expected '}' after code-block name",
            ))
        res.register_advancement()
        self.advance()
        return res.success(CodeBlockRefNode(name_tok))

    def parse_code_block_literal(self):
        """Parse a caller-supplied block without allowing function definitions."""
        res = ParseResult()
        pos_start = self.current_tok.pos_start.copy()
        previous_exec_mode = self._exec_mode
        previous_global_func = self._in_global_func
        previous_loop_depth = self._loop_depth
        self._exec_mode = True
        self._in_global_func = False
        try:
            body = res.register(self.parse_block(in_setup=False, allow_local_funcs=False))
        finally:
            self._exec_mode = previous_exec_mode
            self._in_global_func = previous_global_func
            self._loop_depth = previous_loop_depth
        if res.error:
            return res
        return res.success(CodeBlockLiteralNode(body, pos_start, body.pos_end))

    def parse_codeblock_params(self):
        """Parse an optional stored-codeblock parameter list: ``[str name]``."""
        res = ParseResult()
        param_toks = []
        seen_names = set()

        if self.current_tok.type != TT_LBRACKET:
            return res.failure(InvalidSyntaxError(
                self.current_tok.pos_start,
                self.current_tok.pos_end,
                "Expected '[' before codeblock parameters",
            ))
        res.register_advancement()
        self.advance()

        while self.current_tok.type != TT_RBRACKET and self.current_tok.type != TT_EOF:
            next_tok = self.peek(1)
            if self.is_type_keyword() and next_tok is not None and next_tok.type == TT_IDENTIFIER:
                type_tok = self.current_tok
                res.register_advancement()
                self.advance()
                name_tok = self.current_tok
                res.register_advancement()
                self.advance()
            elif self.current_tok.type == TT_IDENTIFIER:
                type_tok = None
                name_tok = self.current_tok
                res.register_advancement()
                self.advance()
            else:
                return res.failure(InvalidSyntaxError(
                    self.current_tok.pos_start,
                    self.current_tok.pos_end,
                    "Expected a codeblock parameter name, optionally preceded by a type",
                ))

            if name_tok.value in seen_names:
                return res.failure(InvalidSyntaxError(
                    name_tok.pos_start,
                    name_tok.pos_end,
                    f"Duplicate codeblock parameter '{name_tok.value}'",
                ))
            seen_names.add(name_tok.value)
            param_toks.append((type_tok, name_tok))

            if self.current_tok.type == TT_COMMA:
                res.register_advancement()
                self.advance()
                if self.current_tok.type == TT_RBRACKET:
                    return res.failure(InvalidSyntaxError(
                        self.current_tok.pos_start,
                        self.current_tok.pos_end,
                        "Expected a codeblock parameter after ','",
                    ))
            elif self.current_tok.type != TT_RBRACKET:
                return res.failure(InvalidSyntaxError(
                    self.current_tok.pos_start,
                    self.current_tok.pos_end,
                    "Expected ',' or ']' after codeblock parameter",
                ))

        if self.current_tok.type != TT_RBRACKET:
            return res.failure(InvalidSyntaxError(
                self.current_tok.pos_start,
                self.current_tok.pos_end,
                "Expected ']' after codeblock parameters",
            ))
        res.register_advancement()
        self.advance()
        return res.success(param_toks)

    def parse_atom(self):
        res = ParseResult()
        tok = self.current_tok

        if tok.type == TT_INT or tok.type == TT_FLOAT:
            res.register_advancement()
            self.advance()
            return res.success(NumberNode(tok))

        elif tok.type == TT_STRING:
            res.register_advancement()
            self.advance()
            return res.success(StringNode(tok))

        elif tok.type == TT_CHAR:
            res.register_advancement()
            self.advance()
            return res.success(CharNode(tok))

        elif tok.matches(TT_KEYWORD, "true") or tok.matches(TT_KEYWORD, "false"):
            res.register_advancement()
            self.advance()
            return res.success(BoolNode(tok))

        elif tok.matches(TT_KEYWORD, "none"):
            res.register_advancement()
            self.advance()
            return res.success(NoneNode(tok))

        elif tok.type == TT_LBRACKET:
            return self.parse_list_literal()

        elif tok.type == TT_LBRACE:
            return self.parse_code_block_literal()

        elif (tok.type == TT_IDENTIFIER
              or tok.matches(TT_KEYWORD, "global")
              or tok.matches(TT_KEYWORD, "local")
              or tok.matches(TT_KEYWORD, "sentinel")):
            res.register_advancement()
            self.advance()
            return res.success(VarAccessNode(tok))

        elif tok.type == TT_LPAREN:
            next_tok = self.peek(1)
            if next_tok is not None and (
                next_tok.type == TT_RPAREN
                or next_tok.type == TT_KEYWORD
                and next_tok.value in TYPE_KEYWORDS
            ):
                return self.parse_tuple_literal()
            res.register_advancement()
            self.advance()
            expr = res.register(self.parse_expr())
            if res.error:
                return res
            if self.current_tok.type == TT_RPAREN:
                res.register_advancement()
                self.advance()
                return res.success(expr)
            return res.failure(
                InvalidSyntaxError(
                    self.current_tok.pos_start, self.current_tok.pos_end, "Expected ')'"
                )
            )

        elif tok.matches(TT_KEYWORD, "new"):
            return self.parse_new()

        elif tok.matches(TT_KEYWORD, "await"):
            return self.parse_await()

        elif tok.matches(TT_KEYWORD, "async"):
            return self.parse_async_dot_call()

        return res.failure(
            InvalidSyntaxError(
                tok.pos_start,
                tok.pos_end,
                "Expected int, float, str, bool, none, identifier, '(', or 'await'",
            )
        )

    def parse_tuple_literal(self):
        """Parse a typed tuple literal: ``(int 1, str "two")``."""
        res = ParseResult()
        pos_start = self.current_tok.pos_start.copy()

        res.register_advancement()
        self.advance()  # consume '('

        elements = []
        if self.current_tok.type != TT_RPAREN:
            while True:
                if not self.is_type_keyword():
                    return res.failure(
                        InvalidSyntaxError(
                            self.current_tok.pos_start,
                            self.current_tok.pos_end,
                            "Expected a type keyword before each tuple element "
                            "(for example, '(int 1, str \"two\")')",
                        )
                    )

                type_tok = self.current_tok
                res.register_advancement()
                self.advance()

                value_node = res.register(self.parse_expr())
                if res.error:
                    return res
                elements.append(ListElementNode(type_tok, value_node))

                if self.current_tok.type != TT_COMMA:
                    break
                res.register_advancement()
                self.advance()

                if self.current_tok.type == TT_RPAREN:
                    break

        if self.current_tok.type != TT_RPAREN:
            return res.failure(
                InvalidSyntaxError(
                    self.current_tok.pos_start,
                    self.current_tok.pos_end,
                    "Expected ',' or ')' in tuple literal",
                )
            )

        pos_end = self.current_tok.pos_end.copy()
        res.register_advancement()
        self.advance()  # consume ')'
        return res.success(TupleNode(elements, pos_start, pos_end))

    def parse_list_literal(self):
        """Parse a typed list literal: ``[int 1, str "two"]``."""
        res = ParseResult()
        pos_start = self.current_tok.pos_start.copy()

        res.register_advancement()
        self.advance()  # consume '['

        elements = []
        if self.current_tok.type != TT_RBRACKET:
            while True:
                if not self.is_type_keyword():
                    return res.failure(
                        InvalidSyntaxError(
                            self.current_tok.pos_start,
                            self.current_tok.pos_end,
                            "Expected a type keyword before each list element "
                            "(for example, '[int 1, str \"two\"]')",
                        )
                    )

                type_tok = self.current_tok
                res.register_advancement()
                self.advance()

                value_node = res.register(self.parse_expr())
                if res.error:
                    return res
                elements.append(ListElementNode(type_tok, value_node))

                if self.current_tok.type != TT_COMMA:
                    break
                res.register_advancement()
                self.advance()

                if self.current_tok.type == TT_RBRACKET:
                    return res.failure(
                        InvalidSyntaxError(
                            self.current_tok.pos_start,
                            self.current_tok.pos_end,
                            "Expected a list element after ','",
                        )
                    )

        if self.current_tok.type != TT_RBRACKET:
            return res.failure(
                InvalidSyntaxError(
                    self.current_tok.pos_start,
                    self.current_tok.pos_end,
                    "Expected ',' or ']' in list literal",
                )
            )

        pos_end = self.current_tok.pos_end.copy()
        res.register_advancement()
        self.advance()  # consume ']'
        return res.success(ListNode(elements, pos_start, pos_end))

    def parse_await(self):
        """await expr  — only valid inside an async function body."""
        res = ParseResult()
        pos_start = self.current_tok.pos_start.copy()
        res.register_advancement()
        self.advance()  # consume 'await'
        expr = res.register(self.parse_call())
        if res.error:
            return res
        return res.success(AwaitNode(expr, pos_start, expr.pos_end))

    def parse_async_local_def(self):
        """async funcName(params) { body } — local async sub-function."""
        res = ParseResult()
        pos_start = self.current_tok.pos_start.copy()
        res.register_advancement(); self.advance()  # consume 'async'
        if self.current_tok.type != TT_IDENTIFIER:
            return res.failure(InvalidSyntaxError(
                self.current_tok.pos_start, self.current_tok.pos_end,
                "Expected function name after 'async'",
            ))
        name_tok = self.current_tok
        res.register_advancement(); self.advance()
        if self.current_tok.type != TT_LPAREN:
            return res.failure(InvalidSyntaxError(
                self.current_tok.pos_start, self.current_tok.pos_end, "Expected '('",
            ))
        res.register_advancement(); self.advance()
        param_toks = []
        has_default = False
        while self.current_tok.type != TT_RPAREN and self.current_tok.type != TT_EOF:
            next_tok = self.peek(1)
            if self.is_type_keyword() and next_tok is not None and next_tok.type == TT_IDENTIFIER:
                type_tok = self.current_tok
                res.register_advancement(); self.advance()
                pname_tok = self.current_tok
                res.register_advancement(); self.advance()
            elif self.current_tok.type == TT_IDENTIFIER:
                type_tok = None
                pname_tok = self.current_tok
                res.register_advancement(); self.advance()
            else:
                return res.failure(InvalidSyntaxError(
                    self.current_tok.pos_start, self.current_tok.pos_end,
                    "Expected parameter name",
                ))
            default_node = None
            if self.current_tok.type == TT_EQ:
                has_default = True
                res.register_advancement(); self.advance()
                default_node = res.register(self.parse_expr())
                if res.error:
                    return res
            elif has_default:
                return res.failure(InvalidSyntaxError(
                    pname_tok.pos_start,
                    pname_tok.pos_end,
                    "Required parameters cannot follow a parameter with a default value",
                ))
            param_toks.append((type_tok, pname_tok, default_node))
            if self.current_tok.type == TT_COMMA:
                res.register_advancement(); self.advance()
            else:
                break
        if self.current_tok.type != TT_RPAREN:
            return res.failure(InvalidSyntaxError(
                self.current_tok.pos_start, self.current_tok.pos_end, "Expected ')'",
            ))
        res.register_advancement(); self.advance()
        body = res.register(self.parse_block(allow_local_funcs=False))
        if res.error:
            return res
        return res.success(AsyncLocalDefNode(name_tok, param_toks, body, pos_start, self.current_tok.pos_end.copy()))

    def parse_async_dot_call(self):
        """async.funcName(args) — call a locally-defined async function."""
        res = ParseResult()
        pos_start = self.current_tok.pos_start.copy()
        res.register_advancement(); self.advance()  # consume 'async'
        if self.current_tok.type != TT_DOT:
            return res.failure(InvalidSyntaxError(
                self.current_tok.pos_start, self.current_tok.pos_end,
                "Expected '.' after 'async' (usage: async.funcName(args))",
            ))
        res.register_advancement(); self.advance()  # consume '.'
        if self.current_tok.type != TT_IDENTIFIER:
            return res.failure(InvalidSyntaxError(
                self.current_tok.pos_start, self.current_tok.pos_end, "Expected function name",
            ))
        name_tok = self.current_tok
        res.register_advancement(); self.advance()
        if self.current_tok.type != TT_LPAREN:
            return res.failure(InvalidSyntaxError(
                self.current_tok.pos_start, self.current_tok.pos_end, "Expected '('",
            ))
        res.register_advancement(); self.advance()
        arg_nodes = []
        if self.current_tok.type != TT_RPAREN:
            arg_nodes.append(res.register(self.parse_expr()))
            if res.error: return res
            while self.current_tok.type == TT_COMMA:
                res.register_advancement(); self.advance()
                arg_nodes.append(res.register(self.parse_expr()))
                if res.error: return res
            if self.current_tok.type != TT_RPAREN:
                return res.failure(InvalidSyntaxError(
                    self.current_tok.pos_start, self.current_tok.pos_end, "Expected ',' or ')'",
                ))
        pos_end = self.current_tok.pos_end.copy()
        res.register_advancement(); self.advance()  # consume ')'
        return res.success(AsyncDotCallNode(name_tok, arg_nodes, pos_start, pos_end))

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

    def set_pos(self, pos_start: Any = None, pos_end: Any = None) -> "Value":
        self.pos_start = pos_start
        self.pos_end = pos_end
        return self

    def set_context(self, context: Any = None) -> "Value":
        self.context = context
        return self

    def added_to(self, other: "Value") -> tuple["Value | None", "RTError | None"]:
        return None, self.illegal_operation(other)

    def subbed_by(self, other: "Value") -> tuple["Value | None", "RTError | None"]:
        return None, self.illegal_operation(other)

    def multed_by(self, other: "Value") -> tuple["Value | None", "RTError | None"]:
        return None, self.illegal_operation(other)

    def dived_by(self, other: "Value") -> tuple["Value | None", "RTError | None"]:
        return None, self.illegal_operation(other)

    def modded_by(self, other: "Value") -> tuple["Value | None", "RTError | None"]:
        return None, self.illegal_operation(other)

    def floordivided_by(self, other: "Value") -> tuple["Value | None", "RTError | None"]:
        return None, self.illegal_operation(other)

    def powered_by(self, other: "Value") -> tuple["Value | None", "RTError | None"]:
        return None, self.illegal_operation(other)

    def rooted_by(self, other: "Value") -> tuple["Value | None", "RTError | None"]:
        return None, self.illegal_operation(other)

    def get_comparison_eq(self, other: "Value") -> tuple["Value | None", "RTError | None"]:
        return None, self.illegal_operation(other)

    def get_comparison_ne(self, other: "Value") -> tuple["Value | None", "RTError | None"]:
        return None, self.illegal_operation(other)

    def get_comparison_lt(self, other: "Value") -> tuple["Value | None", "RTError | None"]:
        return None, self.illegal_operation(other)

    def get_comparison_gt(self, other: "Value") -> tuple["Value | None", "RTError | None"]:
        return None, self.illegal_operation(other)

    def get_comparison_lte(self, other: "Value") -> tuple["Value | None", "RTError | None"]:
        return None, self.illegal_operation(other)

    def get_comparison_gte(self, other: "Value") -> tuple["Value | None", "RTError | None"]:
        return None, self.illegal_operation(other)

    def anded_by(self, other: "Value") -> tuple["Value | None", "RTError | None"]:
        return None, self.illegal_operation(other)

    def ored_by(self, other: "Value") -> tuple["Value | None", "RTError | None"]:
        return None, self.illegal_operation(other)

    def nanded_by(self, other: "Value") -> tuple["Value | None", "RTError | None"]:
        return None, self.illegal_operation(other)

    def nored_by(self, other: "Value") -> tuple["Value | None", "RTError | None"]:
        return None, self.illegal_operation(other)

    def notted(self) -> tuple["Value | None", "RTError | None"]:
        return None, self.illegal_operation()

    def bit_anded_by(self, other: "Value") -> tuple["Value | None", "RTError | None"]:
        return None, self.illegal_operation(other)

    def bit_ored_by(self, other: "Value") -> tuple["Value | None", "RTError | None"]:
        return None, self.illegal_operation(other)

    def bit_xored_by(self, other: "Value") -> tuple["Value | None", "RTError | None"]:
        return None, self.illegal_operation(other)

    def bit_nanded_by(self, other: "Value") -> tuple["Value | None", "RTError | None"]:
        return None, self.illegal_operation(other)

    def bit_xnored_by(self, other: "Value") -> tuple["Value | None", "RTError | None"]:
        return None, self.illegal_operation(other)

    def bit_nored_by(self, other: "Value") -> tuple["Value | None", "RTError | None"]:
        return None, self.illegal_operation(other)

    def bit_notted(self) -> tuple["Value | None", "RTError | None"]:
        return None, self.illegal_operation()

    def shifted_left_by(self, other: "Value") -> tuple["Value | None", "RTError | None"]:
        return None, self.illegal_operation(other)

    def shifted_right_by(self, other: "Value") -> tuple["Value | None", "RTError | None"]:
        return None, self.illegal_operation(other)

    def execute(self, args):
        return RTResult().failure(self.illegal_operation())

    def copy(self) -> "Value":
        raise Exception("No copy method defined")

    def is_true(self) -> bool:
        return False

    def illegal_operation(self, other: "Value | None" = None) -> "RTError":
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
        from . import cpp
        return cpp.refGet(self.pointer)

    def set_value(self, value):
        if self.pointer is None:
            return False
        from . import cpp
        cpp.refSet(self.pointer, value)
        return True

    def copy(self):
        c = Address(self.pointer, self.symbol_table, self.name)
        c.set_pos(self.pos_start, self.pos_end)
        c.set_context(self.context)
        return c

    def __str__(self):
        target = self.get_value()
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
    null: ClassVar["Number"]
    false: ClassVar["Number"]
    true: ClassVar["Number"]

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
    if isinstance(v, VarGroup):
        return "vargroup"
    if isinstance(v, Function) or (
        "BuiltInFunction" in globals() and isinstance(v, BuiltInFunction)
    ):
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
                SHARED_INTERPRETER.visit(default_node, exec_ctx)
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
        code_block_names=None, param_defaults=None
    ):
        super().__init__(name)
        self.body_node = body_node
        self.param_names = param_names
        self.param_types = param_types or [None] * len(param_names)
        self.param_defaults = param_defaults or [None] * len(param_names)
        self.is_global = is_global
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
        interpreter = SHARED_INTERPRETER
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
        code_block_names=None, param_defaults=None
    ):
        super().__init__(name)
        self.body_node = body_node
        self.param_names = param_names
        self.param_types = param_types or [None] * len(param_names)
        self.param_defaults = param_defaults or [None] * len(param_names)
        self.is_global = is_global
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
            body_res = await SHARED_INTERPRETER.async_visit(body_node, exec_ctx)
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
        except Exception:
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
        except Exception as e:
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
        except Exception as e:
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
        import types as _types

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
            value = res.register(SHARED_INTERPRETER.visit(value_node, init_context))
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
        interpreter = SHARED_INTERPRETER
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

    def get(self, name):
        table, resolved_name = self._resolve(name)
        if table is None or resolved_name is None:
            return None
        pointer = table.references.get(resolved_name)
        if pointer:
            from . import cpp
            return cpp.refGet(pointer)
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
        from . import cpp
        pointer = cpp.refCreate(value)
        table.references[resolved_name] = pointer
        return pointer

    def set(self, name, value, is_const=False, decl_type=None):
        self.aliases.pop(name, None)
        pointer = self.references.get(name)
        if pointer:
            from . import cpp
            cpp.refSet(pointer, value)
        else:
            self.symbols[name] = value
        self.symbols[name] = value
        if is_const:
            self.constants.add(name)
        if decl_type is not None:
            self.types[name] = decl_type

    def update_existing(self, name, value):
        table, resolved_name = self._resolve(name)
        if table is not None and resolved_name is not None:
            pointer = table.references.get(resolved_name)
            if pointer:
                from . import cpp
                cpp.refSet(pointer, value)
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
        value = self.get(name)
        if value is None:
            return False
        table.aliases.pop(name)
        table.symbols[name] = value.copy()
        from . import cpp
        table.references[name] = cpp.refCreate(table.symbols[name])
        return True

    def remove(self, name):
        del self.symbols[name]

# importPy shared module registry
_rawpy_global_modules: dict = {}

# overrideMain entry-point registry
_main_override: "str | None" = None

# forever-loop configuration. These are reset for each top-level run.
_forever_delay = 0.02
_forever_warning_suppressed = False
_deprecation_warning_suppressed = False
_deprecation_warning_deferred = False
_pending_deprecation_warnings: list[tuple[Any, str]] = []
_setup_in_progress = False

# global call hierarchy helpers

def _can_call_global(caller_path, callee_path):
    """Return whether a global may call another global in its hierarchy."""
    if not caller_path or not callee_path:
        return True
    # Separate top-level global trees are independent.
    if caller_path[0] != callee_path[0]:
        return True
    # Within one tree, calls may move along the caller's own ancestor/
    # descendant chain.  Sibling branches remain isolated.
    caller_is_prefix = caller_path == callee_path[:len(caller_path)]
    callee_is_prefix = callee_path == caller_path[:len(callee_path)]
    return caller_is_prefix or callee_is_prefix

def _get_current_global_path(context):
    """Walk the context chain to find the nearest current_global_path."""
    c = context
    while c is not None:
        if c.current_global_path is not None:
            return c.current_global_path
        c = c.parent
    return None


def _module_path(filename: str, base_dir: str) -> tuple[str | None, bool | str, str | None]:
    """Resolve an import without escaping the importing directory.

    Returns ``(path, use_bytecode, error)``.  Auto-detected bytecode is used
    only when it is at least as new as its source, preventing an old sibling
    ``.lynxc`` from silently overriding edited source.
    """
    if os.path.isabs(filename):
        return None, False, "module paths must be relative"

    normalized = os.path.normpath(filename)
    path_parts = normalized.replace("\\", "/").split("/")
    if ".." in path_parts:
        return None, False, "module paths may not escape the importing directory"

    base_dir = os.path.realpath(os.path.abspath(base_dir or os.getcwd()))
    source_path = os.path.realpath(os.path.join(base_dir, normalized))
    try:
        if os.path.commonpath((base_dir, source_path)) != base_dir:
            return None, False, "module paths may not escape the importing directory"
    except ValueError:
        return None, False, "module path is on a different filesystem root"

    if filename.endswith((".so", ".dylib", ".dll")):
        if os.path.isfile(source_path):
            return source_path, "native", None
        return None, False, f"native module '{filename}' was not found"

    bytecode_path = os.path.splitext(source_path)[0] + ".lynxc"
    source_exists = os.path.isfile(source_path)
    bytecode_exists = os.path.isfile(bytecode_path)

    if filename.endswith(".lynxc"):
        if bytecode_exists:
            return bytecode_path, True, None
        return None, False, f"compiled module '{filename}' was not found"

    if bytecode_exists:
        if not source_exists:
            return bytecode_path, True, None
        try:
            if os.path.getmtime(bytecode_path) >= os.path.getmtime(source_path):
                return bytecode_path, True, None
        except OSError:
            # The source tree may be changing while an import is resolved.
            # Prefer readable source rather than failing on a transient stat.
            pass
    if source_exists:
        return source_path, False, None

    stdlib_root = os.path.realpath(stdlib_dir())
    stdlib_path = os.path.realpath(os.path.join(stdlib_root, normalized))
    try:
        if os.path.commonpath((stdlib_root, stdlib_path)) != stdlib_root:
            return None, False, "module path is invalid"
    except ValueError:
        return None, False, "module path is invalid"
    if os.path.isfile(stdlib_path):
        return stdlib_path, False, None

    return None, False, f"module '{filename}' was not found"

def _preregister_nested_globals(parent_func, block_node, context):
    """Scan the statements of *block_node* for nested ``global`` function
    definitions and register them in ``parent_func.inner_globals``.

    The scan is *deep*: it recurses into the bodies of control-flow nodes
    (if / while / for / try) so that globals defined inside conditional branches
    are still discoverable via dot-access without the outer function ever being
    called.  Existing entries are never overwritten — a definition that appeared
    first (textually) wins, which matches the behaviour of ``visit_FuncDefNode``.
    """
    if isinstance(block_node, IfNode):
        _preregister_nested_globals(parent_func, block_node.then_block, context)
        if block_node.else_block is not None:
            _preregister_nested_globals(parent_func, block_node.else_block, context)
        return

    for stmt in block_node.statements:
        # Direct nested global
        if isinstance(stmt, FuncDefNode):
            is_global_def = (
                stmt.kind_tok.value == "global"
                or (stmt.kind_tok.type == TT_IDENTIFIER and stmt.kind_tok.value == "global")
            )
            if not is_global_def:
                continue
            child_name = stmt.var_name_tok.value
            # Skip duplicate — keep whichever was registered first.
            if child_name in parent_func.inner_globals:
                continue
            param_names = [p[1].value for p in stmt.param_toks]
            param_types = [p[0].value if p[0] else None for p in stmt.param_toks]
            param_defaults = [p[2] for p in stmt.param_toks]
            code_block_names = [tok.value for tok in stmt.code_block_toks]
            if stmt.is_async:
                child_func = AsyncFunction(
                    child_name,
                    stmt.body_block,
                    param_names,
                    param_types,
                    is_global=True,
                    code_block_names=code_block_names,
                    param_defaults=param_defaults,
                )
            else:
                child_func = Function(
                    child_name,
                    stmt.body_block,
                    param_names,
                    param_types,
                    is_global=True,
                    code_block_names=code_block_names,
                    param_defaults=param_defaults,
                )
            child_func.set_context(context)
            child_func.set_pos(stmt.pos_start, stmt.pos_end)
            parent_path = parent_func.global_path or [parent_func.name]
            child_func.global_path = parent_path + [child_name]
            parent_func.inner_globals[child_name] = child_func
            # Recurse so that 3+-level nesting is fully pre-populated.
            _preregister_nested_globals(child_func, stmt.body_block, context)

        # Control-flow: search inside branches, loops, and try blocks too,
        # so globals defined inside conditionals are still pre-registered.
        elif isinstance(stmt, IfNode):
            # then_block is always present; else_block may be None
            _preregister_nested_globals(parent_func, stmt.then_block, context)
            if stmt.else_block is not None:
                _preregister_nested_globals(parent_func, stmt.else_block, context)

        elif isinstance(stmt, (WhileNode, DoWhileNode, ForNode, IterateNode, ForeverNode)):
            # Loop nodes all carry exactly one body_block.
            _preregister_nested_globals(parent_func, stmt.body_block, context)

        elif isinstance(stmt, SwitchNode):
            for case in stmt.cases:
                _preregister_nested_globals(parent_func, case.body_block, context)

        elif isinstance(stmt, TryCatchNode):
            # try_block and catch_block are both BlockNodes
            _preregister_nested_globals(parent_func, stmt.try_block, context)
            if stmt.catch_block is not None:
                _preregister_nested_globals(parent_func, stmt.catch_block, context)

# interpreter

class Interpreter:
    def visit(self, node, context):
        method_name = f"visit_{type(node).__name__}"
        method = getattr(self, method_name, self.no_visit_method)
        return method(node, context)

    def no_visit_method(self, node, context):
        raise Exception(f"No visit_{type(node).__name__} method defined")

    def visit_NumberNode(self, node, context):
        return RTResult().success(
            Number(node.tok.value)
            .set_context(context)
            .set_pos(node.pos_start, node.pos_end)
        )

    def visit_StringNode(self, node, context):
        return RTResult().success(
            String(node.tok.value)
            .set_context(context)
            .set_pos(node.pos_start, node.pos_end)
        )

    def visit_CharNode(self, node, context):
        return RTResult().success(
            Char(node.tok.value)
            .set_context(context)
            .set_pos(node.pos_start, node.pos_end)
        )

    def visit_BoolNode(self, node, context):
        val = Number(1 if node.value else 0, is_bool=True)
        return RTResult().success(
            val.set_context(context).set_pos(node.pos_start, node.pos_end)
        )

    def visit_NoneNode(self, node, context):
        return RTResult().success(
            Null().set_context(context).set_pos(node.pos_start, node.pos_end)
        )

    def visit_ListNode(self, node, context):
        res = RTResult()
        elements = []

        for index, element_node in enumerate(node.elements):
            value = res.register(self.visit(element_node.value_node, context))
            if res.should_return():
                return res

            element_type = element_node.type_tok.value
            if element_type == "tuple" and isinstance(value, List):
                value = LynxTuple(value.elements)
                value.set_context(context)
            if element_type == "char" and isinstance(value, String):
                if len(value.value) != 1:
                    return res.failure(RTError(
                        element_node.pos_start,
                        element_node.pos_end,
                        f"List element {index} is declared as 'char' but got a "
                        f"string of length {len(value.value)} — char requires "
                        "exactly one character",
                        context,
                    ))
                value = Char(value.value)
                value.set_context(context)

            if not type_matches(element_type, value):
                return res.failure(RTError(
                    element_node.pos_start,
                    element_node.pos_end,
                    f"List element {index} is declared as '{element_type}' "
                    f"but got a '{value_type_name(value)}' value",
                    context,
                ))
            elements.append(value)

        return res.success(
            List(elements).set_context(context).set_pos(node.pos_start, node.pos_end)
        )

    def visit_TupleNode(self, node, context):
        res = RTResult()
        elements = []

        for index, element_node in enumerate(node.elements):
            value = res.register(self.visit(element_node.value_node, context))
            if res.should_return():
                return res

            element_type = element_node.type_tok.value
            if element_type == "tuple" and isinstance(value, List):
                value = LynxTuple(value.elements)
                value.set_context(context)
            if element_type == "char" and isinstance(value, String):
                if len(value.value) != 1:
                    return res.failure(RTError(
                        element_node.pos_start,
                        element_node.pos_end,
                        f"Tuple element {index} is declared as 'char' but got a "
                        f"string of length {len(value.value)} — char requires "
                        "exactly one character",
                        context,
                    ))
                value = Char(value.value)
                value.set_context(context)

            if not type_matches(element_type, value):
                return res.failure(RTError(
                    element_node.pos_start,
                    element_node.pos_end,
                    f"Tuple element {index} is declared as '{element_type}' "
                    f"but got a '{value_type_name(value)}' value",
                    context,
                ))
            elements.append(value)

        return res.success(
            LynxTuple(elements).set_context(context).set_pos(node.pos_start, node.pos_end)
        )

    def visit_VarAccessNode(self, node, context):
        res = RTResult()
        var_name = node.var_name_tok.value
        value = context.symbol_table.get(var_name)
        if value is None:
            return res.failure(
                RTError(
                    node.pos_start,
                    node.pos_end,
                    f"'{var_name}' is not defined",
                    context,
                )
            )
        value = value.copy().set_pos(node.pos_start, node.pos_end).set_context(context)
        return res.success(value)

    def visit_SharedNode(self, node, context):
        res = RTResult()
        name = node.var_name_tok.value
        value = context.symbol_table.get(name)
        reference = getattr(value, "_lynxer_ref", None) if value is not None else None
        if not (
            isinstance(reference, tuple)
            and len(reference) == 2
            and context.symbol_table.share_reference(name, reference[0], reference[1])
        ):
            return res.failure(RTError(
                node.pos_start,
                node.pos_end,
                f"'{name}' is not a reference-capable function argument; "
                "call the function with a variable",
                context,
            ))
        return res.success(value)

    def visit_VarDeclNode(self, node, context):
        res = RTResult()
        var_name = node.var_name_tok.value
        decl_type = node.type_tok.value if node.type_tok else None
        value = res.register(self.visit(node.value_node, context))
        if res.should_return():
            return res
        if decl_type == "tuple" and isinstance(value, List):
            value = LynxTuple(value.elements)
            value.set_context(context)
        if decl_type == "char" and isinstance(value, String):
            if len(value.value) != 1:
                return res.failure(RTError(
                    node.pos_start, node.pos_end,
                    f"Type mismatch: '{var_name}' is 'char' but got a string of length {len(value.value)} — char requires exactly one character",
                    context,
                ))
            value = Char(value.value)
            value.set_context(context)
        if not type_matches(decl_type, value):
            return res.failure(
                RTError(
                    node.pos_start,
                    node.pos_end,
                    f"Type mismatch: '{var_name}' is declared as '{decl_type}' "
                    f"but got a '{value_type_name(value)}' value",
                    context,
                )
            )
        if node.is_shared:
            if not isinstance(node.value_node, VarAccessNode):
                return res.failure(RTError(
                    node.pos_start, node.pos_end,
                    "A shared variable must be initialized from another variable",
                    context,
                ))
            target_name = node.value_node.var_name_tok.value
            if not context.symbol_table.share(var_name, target_name):
                return res.failure(RTError(
                    node.pos_start, node.pos_end,
                    f"Cannot share '{var_name}' with undefined variable '{target_name}'",
                    context,
                ))
            return res.success(value)
        context.symbol_table.set(
            var_name, value, is_const=node.is_const, decl_type=decl_type
        )
        if decl_type == "codeblock":
            context.code_blocks[var_name] = value
        return res.success(value)

    def visit_VarAssignNode(self, node, context):
        res = RTResult()
        var_name = node.var_name_tok.value
        if context.symbol_table.is_const(var_name):
            return res.failure(
                RTError(
                    node.pos_start,
                    node.pos_end,
                    f"Cannot assign to constant '{var_name}'",
                    context,
                )
            )
        value = res.register(self.visit(node.value_node, context))
        if res.should_return():
            return res
        decl_type = context.symbol_table.get_type(var_name)
        if decl_type == "tuple" and isinstance(value, List):
            if isinstance(node.value_node, ListNode):
                warn_legacy_syntax_position(
                    node.value_node.pos_start,
                    warning_message("legacy_tuple"),
                )
            value = LynxTuple(value.elements)
            value.set_context(context)
        if decl_type == "char" and isinstance(value, String):
            if len(value.value) != 1:
                return res.failure(RTError(
                    node.pos_start, node.pos_end,
                    f"Type mismatch: '{var_name}' is 'char' but got a string of length {len(value.value)} — char requires exactly one character",
                    context,
                ))
            value = Char(value.value)
            value.set_context(context)
        if not type_matches(decl_type, value):
            return res.failure(
                RTError(
                    node.pos_start,
                    node.pos_end,
                    f"Type mismatch: '{var_name}' is declared as '{decl_type}' "
                    f"but got a '{value_type_name(value)}' value",
                    context,
                )
            )
        context.symbol_table.update_existing(var_name, value)
        if decl_type == "codeblock":
            context.code_blocks[var_name] = value
        return res.success(value)

    def visit_BlockNode(self, node, context):
        res = RTResult()
        for stmt in node.statements:
            res.register(self.visit(stmt, context))
            if res.should_return():
                return res
        return res.success(Number.null)

    def visit_BinOpNode(self, node, context):
        res = RTResult()
        left = res.register(self.visit(node.left_node, context))
        if res.should_return():
            return res
        right = res.register(self.visit(node.right_node, context))
        if res.should_return():
            return res

        op = node.op_tok

        result, error = None, None
        if op.type == TT_PLUS:
            result, error = left.added_to(right)
        elif op.type == TT_MINUS:
            result, error = left.subbed_by(right)
        elif op.type == TT_MUL:
            result, error = left.multed_by(right)
        elif op.type == TT_DIV:
            result, error = left.dived_by(right)
        elif op.type == TT_MOD:
            result, error = left.modded_by(right)
        elif op.type == TT_POW:
            result, error = left.powered_by(right)
        elif op.type == TT_ROOT:
            result, error = left.rooted_by(right)
        elif op.type == TT_FLOORDIV:
            result, error = left.floordivided_by(right)
        elif op.type == TT_EQEQ or op.matches(TT_KEYWORD, "is"):
            result, error = left.get_comparison_eq(right)
        elif op.type == TT_NE or (op.type == TT_KEYWORD and op.value == "not is"):
            result, error = left.get_comparison_ne(right)
        elif op.type == TT_LT:
            result, error = left.get_comparison_lt(right)
        elif op.type == TT_GT:
            result, error = left.get_comparison_gt(right)
        elif op.type == TT_LTE:
            result, error = left.get_comparison_lte(right)
        elif op.type == TT_GTE:
            result, error = left.get_comparison_gte(right)
        elif op.matches(TT_KEYWORD, "and") or op.type == TT_LOGICAL_AND:
            result, error = left.anded_by(right)
        elif op.matches(TT_KEYWORD, "or") or op.type == TT_LOGICAL_OR:
            result, error = left.ored_by(right)
        elif op.type == TT_LOGICAL_NAND:
            result, error = left.nanded_by(right)
        elif op.type == TT_LOGICAL_NOR:
            result, error = left.nored_by(right)
        elif op.type == TT_AMP:
            result, error = left.bit_anded_by(right)
        elif op.type == TT_PIPE:
            result, error = left.bit_ored_by(right)
        elif op.type == TT_CARET:
            result, error = left.bit_xored_by(right)
        elif op.type == TT_BITWISE_NAND:
            result, error = left.bit_nanded_by(right)
        elif op.type == TT_BITWISE_XNOR:
            result, error = left.bit_xnored_by(right)
        elif op.type == TT_BITWISE_NOR:
            result, error = left.bit_nored_by(right)
        elif op.type == TT_SHL:
            result, error = left.shifted_left_by(right)
        elif op.type == TT_SHR:
            result, error = left.shifted_right_by(right)

        if error:
            return res.failure(error)
        if result is None:
            return res.failure(RTError(
                node.pos_start, node.pos_end,
                f"Unsupported operator '{node.op_tok.type}'",
                context,
            ))
        return res.success(result.set_pos(node.pos_start, node.pos_end))

    def visit_UnaryOpNode(self, node, context):
        res = RTResult()
        value = res.register(self.visit(node.node, context))
        if res.should_return():
            return res

        error = None
        if node.op_tok.type == TT_MINUS:
            value, error = value.multed_by(Number(-1))
        elif (
            node.op_tok.matches(TT_KEYWORD, "not")
            or node.op_tok.type == TT_LOGICAL_NOT
        ):
            value, error = value.notted()
        elif node.op_tok.type == TT_TILDE:
            value, error = value.bit_notted()

        if error:
            return res.failure(error)
        return res.success(value.set_pos(node.pos_start, node.pos_end))

    def visit_IfNode(self, node, context):
        res = RTResult()
        condition = res.register(self.visit(node.condition_node, context))
        if res.should_return():
            return res

        if condition.is_true():
            res.register(self.visit(node.then_block, context))
            if res.should_return():
                return res
        elif node.else_block:
            res.register(self.visit(node.else_block, context))
            if res.should_return():
                return res

        return res.success(Number.null)

    def visit_IterateNode(self, node, context):
        res = RTResult()
        count_val = res.register(self.visit(node.count_node, context))
        if res.should_return():
            return res
        if not isinstance(count_val, Number):
            return res.failure(RTError(
                node.count_node.pos_start, node.count_node.pos_end,
                "iterate() count must be an integer",
                context,
            ))
        count = int(count_val.value)
        for _ in range(count):
            res.register(self.visit(node.body_block, context))
            if res.should_return() and not res.loop_should_continue and not res.loop_should_break:
                return res
            if res.loop_should_break:
                break
            res.loop_should_continue = False
        return res.success(Number.null)

    def visit_ForeverNode(self, node, context):
        res = RTResult()
        warn_forever_no_break(node)

        import time

        while True:
            res.register(self.visit(node.body_block, context))
            if res.error or res.func_return_value is not None:
                return res
            if res.loop_should_break:
                return res.success(Number.null)
            res.loop_should_continue = False
            time.sleep(_forever_delay)

    def visit_WhileNode(self, node, context):
        res = RTResult()
        while True:
            condition = res.register(self.visit(node.condition_node, context))
            if res.should_return():
                return res
            if not condition.is_true():
                break

            res.register(self.visit(node.body_block, context))
            if (
                res.should_return()
                and not res.loop_should_continue
                and not res.loop_should_break
            ):
                return res
            if res.loop_should_break:
                break
            res.loop_should_continue = False

        return res.success(Number.null)

    def visit_DoWhileNode(self, node, context):
        res = RTResult()
        while True:
            body_res = RTResult()
            body_res.register(self.visit(node.body_block, context))
            if body_res.error or body_res.func_return_value is not None:
                return body_res
            if body_res.loop_should_break:
                break
            if node.condition_node is None:
                continue

            condition_res = RTResult()
            condition = condition_res.register(
                self.visit(node.condition_node, context)
            )
            if condition_res.error:
                return condition_res
            if not condition.is_true():
                break

        return res.success(Number.null)

    def visit_SwitchNode(self, node, context):
        res = RTResult()
        switch_value = res.register(self.visit(node.value_node, context))
        if res.should_return():
            return res

        default_case = None
        for case in node.cases:
            if isinstance(case, DefaultNode):
                default_case = case
                continue

            case_value = res.register(self.visit(case.match_node, context))
            if res.should_return():
                return res

            matches, error = switch_value.get_comparison_eq(case_value)
            if error:
                # Values of different or unsupported types simply do not match.
                continue
            if matches.is_true():
                res.register(self.visit(case.body_block, context))
                if res.should_return():
                    return res
                break

        else:
            if default_case is not None:
                res.register(self.visit(default_case.body_block, context))
                if res.should_return():
                    return res

        return res.success(Number.null)

    def visit_ForNode(self, node, context):
        res = RTResult()
        for_ctx = Context("<for>", context, node.pos_start)
        for_ctx.symbol_table = SymbolTable(context.symbol_table)

        init_res = RTResult()
        init_res.register(self.visit(node.init_node, for_ctx))
        if init_res.error:
            return init_res

        while True:
            cond_res = RTResult()
            condition = cond_res.register(self.visit(node.condition_node, for_ctx))
            if cond_res.error:
                return cond_res
            if not condition.is_true():
                break

            body_res = RTResult()
            body_res.register(self.visit(node.body_block, for_ctx))
            if body_res.error or body_res.func_return_value is not None:
                return body_res
            should_break = body_res.loop_should_break
            should_continue = body_res.loop_should_continue

            if not should_break:
                upd_res = RTResult()
                upd_res.register(self.visit(node.update_node, for_ctx))
                if upd_res.error:
                    return upd_res

            if should_break:
                break
        return res.success(Number.null)

    def visit_BreakNode(self, node, context):
        res = RTResult()
        res.loop_should_break = True
        return res

    def visit_ContinueNode(self, node, context):
        res = RTResult()
        res.loop_should_continue = True
        return res

    def visit_TryCatchNode(self, node, context):
        res = RTResult()

        try_res = RTResult()
        try_res.register(self.visit(node.try_block, context))

        if try_res.error:
            if node.catch_var_tok:
                var_name = node.catch_var_tok.value

                if context.symbol_table.is_const(var_name):
                    return res.failure(RTError(
                        node.catch_var_tok.pos_start,
                        node.catch_var_tok.pos_end,
                        f"Cannot bind catch variable '{var_name}': "
                        f"it is declared as const",
                        context,
                    ))

                existing_type = context.symbol_table.get_type(var_name)
                if existing_type is not None and existing_type not in ("str", "any"):
                    return res.failure(RTError(
                        node.catch_var_tok.pos_start,
                        node.catch_var_tok.pos_end,
                        f"Cannot bind catch variable '{var_name}' as 'str': "
                        f"'{var_name}' is already declared as '{existing_type}'",
                        context,
                    ))

                err_str = String(try_res.error.details)
                err_str.set_context(context)
                context.symbol_table.set(var_name, err_str, decl_type="str")

            catch_res = RTResult()
            catch_res.register(self.visit(node.catch_block, context))
            if catch_res.error:
                return res.failure(catch_res.error)
            if catch_res.func_return_value is not None:
                return res.success_return(catch_res.func_return_value)
            if catch_res.loop_should_break:
                out = RTResult()
                out.loop_should_break = True
                return out
            if catch_res.loop_should_continue:
                out = RTResult()
                out.loop_should_continue = True
                return out
            return res.success(Number.null)

        if try_res.func_return_value is not None:
            return res.success_return(try_res.func_return_value)
        if try_res.loop_should_break:
            out = RTResult()
            out.loop_should_break = True
            return out
        if try_res.loop_should_continue:
            out = RTResult()
            out.loop_should_continue = True
            return out
        return res.success(Number.null)

    def run_setup(self, setup_node, context):
        """Run ``global setup`` while preserving its top-level scope.

        Setup is intentionally evaluated in the program/module symbol table
        rather than through ``Function.execute``: declarations made there are
        global to the program or module.  Bind its parameters first so setup
        follows the same default-parameter rules as every other function.
        """
        param_names = [param[1].value for param in setup_node.param_toks]
        param_types = [param[0].value if param[0] else None for param in setup_node.param_toks]
        param_defaults = [param[2] for param in setup_node.param_toks]
        setup_function = Function(
            setup_node.var_name_tok.value,
            setup_node.body_block,
            param_names,
            param_types,
            is_global=True,
            param_defaults=param_defaults,
        )
        setup_function.set_context(context).set_pos(
            setup_node.pos_start, setup_node.pos_end
        )

        result = RTResult()
        result.register(
            setup_function.check_and_populate_args(
                param_names,
                [],
                context,
                param_types,
                param_defaults,
            )
        )
        if result.should_return():
            return result
        result.register(self.visit(setup_node.body_block, context))
        return result

    def visit_FuncDefNode(self, node, context):
        res = RTResult()
        func_name = node.var_name_tok.value
        param_names = [p[1].value for p in node.param_toks]
        param_types = [p[0].value if p[0] else None for p in node.param_toks]
        param_defaults = [p[2] for p in node.param_toks]
        code_block_names = [tok.value for tok in node.code_block_toks]
        is_global = node.kind_tok.value == "global" or (
            node.kind_tok.type == TT_IDENTIFIER and node.kind_tok.value == "global"
        )
        if node.is_async:
            func_value = AsyncFunction(
                func_name,
                node.body_block,
                param_names,
                param_types,
                is_global,
                code_block_names,
                param_defaults,
            )
        else:
            func_value = Function(
                func_name,
                node.body_block,
                param_names,
                param_types,
                is_global,
                code_block_names,
                param_defaults,
            )
        func_value.set_context(context).set_pos(node.pos_start, node.pos_end)

        if is_global:
            parent_fn = context.current_function
            if parent_fn is not None and parent_fn.is_global:
                parent_path = parent_fn.global_path or [parent_fn.name]
                func_value.global_path = parent_path + [func_name]
                parent_fn.inner_globals[func_name] = func_value
            else:
                # Top-level global
                func_value.global_path = [func_name]

            # needing to call global.a() first.
            _preregister_nested_globals(func_value, node.body_block, context)

        context.symbol_table.set(func_name, func_value)

        if not is_global and context.current_function is not None:
            context.current_function.inner_locals[func_name] = func_value

        return res.success(func_value)

    def visit_AsyncLocalDefNode(self, node, context):
        res = RTResult()
        func_name = node.name_tok.value
        param_names = [p[1].value for p in node.param_toks]
        param_types = [p[0].value if p[0] else None for p in node.param_toks]
        param_defaults = [p[2] for p in node.param_toks]
        func_value = AsyncFunction(
            func_name,
            node.body,
            param_names,
            param_types,
            param_defaults=param_defaults,
        )
        func_value.set_context(context).set_pos(node.pos_start, node.pos_end)
        context.symbol_table.set(f"__async__{func_name}", func_value)
        return res.success(Number.null)

    def visit_AsyncDotCallNode(self, node, context):
        import asyncio
        res = RTResult()
        func_name = node.name_tok.value
        func_value = context.symbol_table.get(f"__async__{func_name}")
        if func_value is None:
            return res.failure(RTError(
                node.pos_start, node.pos_end,
                f"No async function '{func_name}' defined in this scope — define it with 'async {func_name}(){{}}' first",
                context,
            ))
        args = []
        for arg_node in node.arg_nodes:
            val = res.register(self.visit(arg_node, context))
            if res.should_return(): return res
            args.append(val)
        call_res = func_value.execute(args)
        if call_res.error: return call_res
        coro_val = call_res.value
        if not isinstance(coro_val, CoroutineValue):
            return res.failure(RTError(node.pos_start, node.pos_end, f"'{func_name}' is not an async function", context))
        try:
            coro_result = asyncio.run(coro_val.coro)
        except Exception as e:
            return res.failure(RTError(node.pos_start, node.pos_end, f"async.{func_name}() raised: {type(e).__name__}: {e}", context))
        if coro_result.error: return coro_result
        return res.success(coro_result.value if coro_result.value is not None else Number.null)

    def visit_AwaitNode(self, node, context):
        """Sync context — await is not allowed here."""
        return RTResult().failure(RTError(
            node.pos_start,
            node.pos_end,
            "'await' can only be used inside an 'async' function body",
            context,
        ))

    # async visitor path

    async def async_visit(self, node, context):
        """Dispatch to async_visit_<NodeType> if available, else fall back to sync visit."""
        method_name = f"async_visit_{type(node).__name__}"
        method = getattr(self, method_name, None)
        if method is not None:
            return await method(node, context)
        return self.visit(node, context)

    async def async_visit_BlockNode(self, node, context):
        res = RTResult()
        for stmt in node.statements:
            res.register(await self.async_visit(stmt, context))
            if res.should_return():
                return res
        return res.success(Number.null)

    async def async_visit_ExecBlockNode(self, node, context):
        return await self.async_visit(node.body_block, context)

    async def async_visit_AwaitNode(self, node, context):
        res = RTResult()
        value = res.register(await self.async_visit(node.expr_node, context))
        if res.should_return():
            return res

        if not isinstance(value, CoroutineValue):
            return res.failure(RTError(
                node.pos_start,
                node.pos_end,
                "Can only 'await' a coroutine (result of calling an 'async' function)",
                context,
            ))

        coro_res = await value.coro
        return coro_res  # coro_res is an RTResult already

    async def async_visit_ListNode(self, node, context):
        res = RTResult()
        elements = []

        for index, element_node in enumerate(node.elements):
            value = res.register(
                await self.async_visit(element_node.value_node, context)
            )
            if res.should_return():
                return res

            element_type = element_node.type_tok.value
            if element_type == "tuple" and isinstance(value, List):
                value = LynxTuple(value.elements)
                value.set_context(context)
            if element_type == "char" and isinstance(value, String):
                if len(value.value) != 1:
                    return res.failure(RTError(
                        element_node.pos_start,
                        element_node.pos_end,
                        f"List element {index} is declared as 'char' but got a "
                        f"string of length {len(value.value)} — char requires "
                        "exactly one character",
                        context,
                    ))
                value = Char(value.value)
                value.set_context(context)

            if not type_matches(element_type, value):
                return res.failure(RTError(
                    element_node.pos_start,
                    element_node.pos_end,
                    f"List element {index} is declared as '{element_type}' "
                    f"but got a '{value_type_name(value)}' value",
                    context,
                ))
            elements.append(value)

        return res.success(
            List(elements).set_context(context).set_pos(node.pos_start, node.pos_end)
        )

    async def async_visit_TupleNode(self, node, context):
        res = RTResult()
        elements = []

        for index, element_node in enumerate(node.elements):
            value = res.register(
                await self.async_visit(element_node.value_node, context)
            )
            if res.should_return():
                return res

            element_type = element_node.type_tok.value
            if element_type == "tuple" and isinstance(value, List):
                value = LynxTuple(value.elements)
                value.set_context(context)
            if element_type == "char" and isinstance(value, String):
                if len(value.value) != 1:
                    return res.failure(RTError(
                        element_node.pos_start,
                        element_node.pos_end,
                        f"Tuple element {index} is declared as 'char' but got a "
                        f"string of length {len(value.value)} — char requires "
                        "exactly one character",
                        context,
                    ))
                value = Char(value.value)
                value.set_context(context)

            if not type_matches(element_type, value):
                return res.failure(RTError(
                    element_node.pos_start,
                    element_node.pos_end,
                    f"Tuple element {index} is declared as '{element_type}' "
                    f"but got a '{value_type_name(value)}' value",
                    context,
                ))
            elements.append(value)

        return res.success(
            LynxTuple(elements).set_context(context).set_pos(node.pos_start, node.pos_end)
        )

    async def async_visit_VarDeclNode(self, node, context):
        res = RTResult()
        var_name = node.var_name_tok.value
        decl_type = node.type_tok.value if node.type_tok else None
        value = res.register(await self.async_visit(node.value_node, context))
        if res.should_return():
            return res
        if decl_type == "tuple" and isinstance(value, List):
            value = LynxTuple(value.elements)
            value.set_context(context)
        if not type_matches(decl_type, value):
            return res.failure(RTError(
                node.pos_start, node.pos_end,
                f"Type mismatch: '{var_name}' is declared as '{decl_type}' "
                f"but received a '{value_type_name(value)}' value",
                context,
            ))
        if node.is_shared:
            if not isinstance(node.value_node, VarAccessNode):
                return res.failure(RTError(
                    node.pos_start, node.pos_end,
                    "A shared variable must be initialized from another variable",
                    context,
                ))
            target_name = node.value_node.var_name_tok.value
            if not context.symbol_table.share(var_name, target_name):
                return res.failure(RTError(
                    node.pos_start, node.pos_end,
                    f"Cannot share '{var_name}' with undefined variable '{target_name}'",
                    context,
                ))
            return res.success(value)
        context.symbol_table.set(var_name, value, is_const=node.is_const, decl_type=decl_type)
        return res.success(value)

    async def async_visit_SharedNode(self, node, context):
        return self.visit_SharedNode(node, context)

    async def async_visit_VarAssignNode(self, node, context):
        res = RTResult()
        var_name = node.var_name_tok.value
        if context.symbol_table.is_const(var_name):
            return res.failure(RTError(
                node.pos_start, node.pos_end,
                f"Cannot assign to constant '{var_name}'",
                context,
            ))
        value = res.register(await self.async_visit(node.value_node, context))
        if res.should_return():
            return res
        decl_type = context.symbol_table.get_type(var_name)
        if decl_type == "tuple" and isinstance(value, List):
            if isinstance(node.value_node, ListNode):
                warn_legacy_syntax_position(
                    node.value_node.pos_start,
                    warning_message("legacy_tuple"),
                )
            value = LynxTuple(value.elements)
            value.set_context(context)
        if not type_matches(decl_type, value):
            return res.failure(RTError(
                node.pos_start, node.pos_end,
                f"Type mismatch: '{var_name}' is declared as '{decl_type}' "
                f"but received a '{value_type_name(value)}' value",
                context,
            ))
        context.symbol_table.update_existing(var_name, value)
        return res.success(value)

    async def async_visit_BinOpNode(self, node, context):
        res = RTResult()
        left = res.register(await self.async_visit(node.left_node, context))
        if res.should_return():
            return res
        right = res.register(await self.async_visit(node.right_node, context))
        if res.should_return():
            return res

        op = node.op_tok
        result, error = None, None
        if op.type == TT_PLUS:
            result, error = left.added_to(right)
        elif op.type == TT_MINUS:
            result, error = left.subbed_by(right)
        elif op.type == TT_MUL:
            result, error = left.multed_by(right)
        elif op.type == TT_DIV:
            result, error = left.dived_by(right)
        elif op.type == TT_MOD:
            result, error = left.modded_by(right)
        elif op.type == TT_POW:
            result, error = left.powered_by(right)
        elif op.type == TT_ROOT:
            result, error = left.rooted_by(right)
        elif op.type == TT_FLOORDIV:
            result, error = left.floordivided_by(right)
        elif op.type == TT_EQEQ or op.matches(TT_KEYWORD, "is"):
            result, error = left.get_comparison_eq(right)
        elif op.type == TT_NE or (op.type == TT_KEYWORD and op.value == "not is"):
            result, error = left.get_comparison_ne(right)
        elif op.type == TT_LT:
            result, error = left.get_comparison_lt(right)
        elif op.type == TT_GT:
            result, error = left.get_comparison_gt(right)
        elif op.type == TT_LTE:
            result, error = left.get_comparison_lte(right)
        elif op.type == TT_GTE:
            result, error = left.get_comparison_gte(right)
        elif op.matches(TT_KEYWORD, "and") or op.type == TT_LOGICAL_AND:
            result, error = left.anded_by(right)
        elif op.matches(TT_KEYWORD, "or") or op.type == TT_LOGICAL_OR:
            result, error = left.ored_by(right)
        elif op.type == TT_LOGICAL_NAND:
            result, error = left.nanded_by(right)
        elif op.type == TT_LOGICAL_NOR:
            result, error = left.nored_by(right)
        elif op.type == TT_AMP:
            result, error = left.bit_anded_by(right)
        elif op.type == TT_PIPE:
            result, error = left.bit_ored_by(right)
        elif op.type == TT_CARET:
            result, error = left.bit_xored_by(right)
        elif op.type == TT_BITWISE_NAND:
            result, error = left.bit_nanded_by(right)
        elif op.type == TT_BITWISE_XNOR:
            result, error = left.bit_xnored_by(right)
        elif op.type == TT_BITWISE_NOR:
            result, error = left.bit_nored_by(right)
        elif op.type == TT_SHL:
            result, error = left.shifted_left_by(right)
        elif op.type == TT_SHR:
            result, error = left.shifted_right_by(right)

        if error:
            return res.failure(error)
        if result is None:
            return res.failure(RTError(
                node.pos_start, node.pos_end,
                f"Unsupported operator '{node.op_tok.type}'", context,
            ))
        return res.success(result.set_pos(node.pos_start, node.pos_end))

    async def async_visit_UnaryOpNode(self, node, context):
        res = RTResult()
        value = res.register(await self.async_visit(node.node, context))
        if res.should_return():
            return res

        error = None
        if node.op_tok.type == TT_MINUS:
            value, error = value.multed_by(Number(-1))
        elif (
            node.op_tok.matches(TT_KEYWORD, "not")
            or node.op_tok.type == TT_LOGICAL_NOT
        ):
            value, error = value.notted()
        elif node.op_tok.type == TT_TILDE:
            value, error = value.bit_notted()

        if error:
            return res.failure(error)
        return res.success(value.set_pos(node.pos_start, node.pos_end))

    async def async_visit_IfNode(self, node, context):
        res = RTResult()
        condition = res.register(await self.async_visit(node.condition_node, context))
        if res.should_return():
            return res
        if condition.is_true():
            res.register(await self.async_visit(node.then_block, context))
            if res.should_return():
                return res
        elif node.else_block:
            res.register(await self.async_visit(node.else_block, context))
            if res.should_return():
                return res
        return res.success(Number.null)

    async def async_visit_WhileNode(self, node, context):
        res = RTResult()
        while True:
            condition = res.register(await self.async_visit(node.condition_node, context))
            if res.should_return():
                return res
            if not condition.is_true():
                break
            res.register(await self.async_visit(node.body_block, context))
            if (
                res.should_return()
                and not res.loop_should_continue
                and not res.loop_should_break
            ):
                return res
            if res.loop_should_break:
                break
            res.loop_should_continue = False
        return res.success(Number.null)

    async def async_visit_DoWhileNode(self, node, context):
        res = RTResult()
        while True:
            body_res = RTResult()
            body_res.register(await self.async_visit(node.body_block, context))
            if body_res.error or body_res.func_return_value is not None:
                return body_res
            if body_res.loop_should_break:
                break
            if node.condition_node is None:
                continue

            condition_res = RTResult()
            condition = condition_res.register(
                await self.async_visit(node.condition_node, context)
            )
            if condition_res.error:
                return condition_res
            if not condition.is_true():
                break

        return res.success(Number.null)

    async def async_visit_SwitchNode(self, node, context):
        res = RTResult()
        switch_value = res.register(await self.async_visit(node.value_node, context))
        if res.should_return():
            return res

        default_case = None
        for case in node.cases:
            if isinstance(case, DefaultNode):
                default_case = case
                continue

            case_value = res.register(await self.async_visit(case.match_node, context))
            if res.should_return():
                return res

            matches, error = switch_value.get_comparison_eq(case_value)
            if error:
                continue
            if matches.is_true():
                res.register(await self.async_visit(case.body_block, context))
                if res.should_return():
                    return res
                break

        else:
            if default_case is not None:
                res.register(await self.async_visit(default_case.body_block, context))
                if res.should_return():
                    return res

        return res.success(Number.null)

    async def async_visit_IterateNode(self, node, context):
        res = RTResult()
        count_val = res.register(await self.async_visit(node.count_node, context))
        if res.should_return():
            return res
        if not isinstance(count_val, Number):
            return res.failure(RTError(
                node.count_node.pos_start, node.count_node.pos_end,
                "iterate() count must be an integer",
                context,
            ))
        count = int(count_val.value)
        for _ in range(count):
            res.register(await self.async_visit(node.body_block, context))
            if res.should_return() and not res.loop_should_continue and not res.loop_should_break:
                return res
            if res.loop_should_break:
                break
            res.loop_should_continue = False
        return res.success(Number.null)

    async def async_visit_ForeverNode(self, node, context):
        res = RTResult()
        warn_forever_no_break(node)

        import asyncio

        while True:
            res.register(await self.async_visit(node.body_block, context))
            if res.error or res.func_return_value is not None:
                return res
            if res.loop_should_break:
                return res.success(Number.null)
            res.loop_should_continue = False
            await asyncio.sleep(_forever_delay)

    async def async_visit_ForNode(self, node, context):
        res = RTResult()
        for_ctx = Context("<for>", context, node.pos_start)
        for_ctx.symbol_table = SymbolTable(context.symbol_table)

        init_res = RTResult()
        init_res.register(await self.async_visit(node.init_node, for_ctx))
        if init_res.error:
            return init_res

        while True:
            cond = RTResult()
            condition = cond.register(await self.async_visit(node.condition_node, for_ctx))
            if cond.error:
                return cond
            if not condition.is_true():
                break

            body = RTResult()
            body.register(await self.async_visit(node.body_block, for_ctx))
            if body.error or body.func_return_value is not None:
                return body
            should_break = body.loop_should_break

            if not should_break:
                upd = RTResult()
                upd.register(await self.async_visit(node.update_node, for_ctx))
                if upd.error:
                    return upd

            if should_break:
                break
        return res.success(Number.null)

    async def async_visit_BreakNode(self, node, context):
        res = RTResult()
        res.loop_should_break = True
        return res

    async def async_visit_ContinueNode(self, node, context):
        res = RTResult()
        res.loop_should_continue = True
        return res

    async def async_visit_TryCatchNode(self, node, context):
        res = RTResult()
        try_res = RTResult()
        try_res.register(await self.async_visit(node.try_block, context))

        if try_res.error:
            if node.catch_var_tok:
                var_name = node.catch_var_tok.value
                if context.symbol_table.is_const(var_name):
                    return res.failure(RTError(
                        node.catch_var_tok.pos_start, node.catch_var_tok.pos_end,
                        f"Cannot bind catch variable '{var_name}': it is declared as const",
                        context,
                    ))
                existing_type = context.symbol_table.get_type(var_name)
                if existing_type is not None and existing_type not in ("str", "any"):
                    return res.failure(RTError(
                        node.catch_var_tok.pos_start, node.catch_var_tok.pos_end,
                        f"Cannot bind catch variable '{var_name}' as 'str': "
                        f"'{var_name}' is already declared as '{existing_type}'",
                        context,
                    ))
                err_str = String(try_res.error.details)
                err_str.set_context(context)
                context.symbol_table.set(var_name, err_str, decl_type="str")

            catch_res = RTResult()
            catch_res.register(await self.async_visit(node.catch_block, context))
            if catch_res.error:
                return res.failure(catch_res.error)
            if catch_res.func_return_value is not None:
                return res.success_return(catch_res.func_return_value)
            if catch_res.loop_should_break:
                out = RTResult(); out.loop_should_break = True; return out
            if catch_res.loop_should_continue:
                out = RTResult(); out.loop_should_continue = True; return out
            return res.success(Number.null)

        if try_res.func_return_value is not None:
            return res.success_return(try_res.func_return_value)
        if try_res.loop_should_break:
            out = RTResult(); out.loop_should_break = True; return out
        if try_res.loop_should_continue:
            out = RTResult(); out.loop_should_continue = True; return out
        return res.success(Number.null)

    async def async_visit_ReturnNode(self, node, context):
        res = RTResult()
        if node.node_to_return:
            value = res.register(await self.async_visit(node.node_to_return, context))
            if res.should_return():
                return res
        else:
            value = Number.null
        return res.success_return(value)

    async def async_visit_CodeBlockLiteralNode(self, node, context):
        return self.visit_CodeBlockLiteralNode(node, context)

    async def async_visit_CodeBlockRefNode(self, node, context):
        return self.visit_CodeBlockRefNode(node, context)

    async def async_visit_ExecCallNode(self, node, context):
        res = RTResult()
        args = []
        for arg_node in node.arg_nodes:
            arg_value = res.register(await self.async_visit(arg_node, context))
            args.append(arg_value)
            if res.should_return():
                return res
        block = res.register(await self.async_visit(node.code_block_node, context))
        if res.should_return():
            return res
        if not isinstance(block, CodeBlockValue):
            return res.failure(RTError(
                node.pos_start,
                node.pos_end,
                "exec() expects a code-block parameter reference",
                context,
            ))
        bindings, error = _build_exec_bindings(node, block, args, context)
        if error:
            return res.failure(error)
        assert bindings is not None
        previous = {}
        try:
            for name, declared_type, value in bindings:
                previous[name] = context.symbol_table.symbols.get(name)
                context.symbol_table.set(
                    name,
                    value.copy().set_context(context),
                    decl_type=declared_type,
                )
            return await self.async_visit(block.body_node, context)
        finally:
            for name, old_value in previous.items():
                if old_value is None:
                    context.symbol_table.symbols.pop(name, None)
                else:
                    context.symbol_table.symbols[name] = old_value

    async def async_visit_ExecFileNode(self, node, context):
        return self.visit_ExecFileNode(node, context)

    async def async_visit_NewNode(self, node, context):
        # Constructors are synchronous Lynxer methods, but argument
        # expressions may still be evaluated from an async function.
        res = RTResult()
        class_registry = context.symbol_table.get("class")
        if not isinstance(class_registry, ClassRegistry):
            return res.failure(RTError(
                node.pos_start, node.pos_end,
                "No class registry is available in this scope",
                context,
            ))
        blueprint, error = class_registry.get_attr(node.class_name_tok.value)
        if error:
            error.pos_start = node.pos_start
            error.pos_end = node.pos_end
            error.context = context
            return res.failure(error)
        assert isinstance(blueprint, ClassBlueprint)
        args = []
        for arg_node in node.arg_nodes:
            arg_value = res.register(await self.async_visit(arg_node, context))
            if (
                isinstance(node.node_to_call, VarAccessNode)
                and node.node_to_call.var_name_tok.value == "unshare"
                and isinstance(arg_node, VarAccessNode)
                and arg_value is not None
            ):
                arg_value._lynxer_name = arg_node.var_name_tok.value
            args.append(arg_value)
            if res.should_return():
                return res
        instance = res.register(blueprint.instantiate(args, context))
        if res.should_return():
            return res
        assert instance is not None
        return res.success(
            instance.set_pos(node.pos_start, node.pos_end).set_context(context)
        )

    async def async_visit_CallNode(self, node, context):
        res = RTResult()
        args = []
        block_args = []
        value_to_call = res.register(await self.async_visit(node.node_to_call, context))
        if res.should_return():
            return res
        value_to_call = value_to_call.copy().set_pos(node.pos_start, node.pos_end)

        if (isinstance(node.node_to_call, VarAccessNode)
                and isinstance(value_to_call, (Function, AsyncFunction))
                and value_to_call.is_global
                and not _uses_shared_parameters(value_to_call)):
            return res.failure(RTError(
                node.pos_start, node.pos_end,
                f"Global function '{value_to_call.name}' must be called as "
                f"'global.{value_to_call.name}(...)' not '{value_to_call.name}(...)'",
                context,
            ))

        if (isinstance(node.node_to_call, VarAccessNode)
                and isinstance(value_to_call, (Function, AsyncFunction))
                and not value_to_call.is_global):
            return res.failure(RTError(
                node.pos_start, node.pos_end,
                f"Local function '{value_to_call.name}' must be called as "
                f"'local.{value_to_call.name}(...)' not '{value_to_call.name}(...)'",
                context,
            ))

        for arg_node in node.arg_nodes:
            arg_value = res.register(await self.async_visit(arg_node, context))
            if isinstance(arg_node, VarAccessNode) and arg_value is not None:
                arg_value._lynxer_ref = (context.symbol_table, arg_node.var_name_tok.value)
            args.append(arg_value)
            if res.should_return():
                return res

        for block_node in node.block_arg_nodes:
            block_args.append(res.register(await self.async_visit(block_node, context)))
            if res.should_return():
                return res

        if isinstance(value_to_call, (Function, AsyncFunction, BoundMethod)):
            return_value = res.register(value_to_call.execute(args, block_args))
        elif block_args:
            return res.failure(RTError(
                node.pos_start,
                node.pos_end,
                "Only user-defined functions can receive code blocks",
                context,
            ))
        else:
            return_value = res.register(value_to_call.execute(args))
        if res.should_return():
            return res
        if return_value is None:
            return res.failure(RTError(
                node.pos_start, node.pos_end,
                "Callable returned no runtime value",
                context,
            ))
        return_value = (
            return_value.copy()
            .set_pos(node.pos_start, node.pos_end)
            .set_context(context)
        )
        return res.success(return_value)

    async def async_visit_DotAccessNode(self, node, context):
        res = RTResult()
        obj = res.register(await self.async_visit(node.obj_node, context))
        if res.should_return():
            return res

        attr_name = node.attr_name_tok.value
        if hasattr(obj, "get_attr"):
            value, error = obj.get_attr(attr_name)
            if error:
                error.pos_start = node.pos_start
                error.pos_end = node.pos_end
                error.context = context
                return res.failure(error)
            value = value.copy().set_pos(node.pos_start, node.pos_end).set_context(context)
            return res.success(value)

        return res.failure(RTError(
            node.pos_start, node.pos_end,
            f"Value of type '{value_type_name(obj)}' does not support attribute access",
            context,
        ))

    async def async_visit_FuncDefNode(self, node, context):
        return self.visit_FuncDefNode(node, context)

    async def async_visit_AsyncLocalDefNode(self, node, context):
        return self.visit_AsyncLocalDefNode(node, context)

    async def async_visit_AsyncDotCallNode(self, node, context):
        res = RTResult()
        func_name = node.name_tok.value
        func_value = context.symbol_table.get(f"__async__{func_name}")
        if func_value is None:
            return res.failure(RTError(
                node.pos_start, node.pos_end,
                f"No async function '{func_name}' defined in this scope",
                context,
            ))
        args = []
        for arg_node in node.arg_nodes:
            val = res.register(await self.async_visit(arg_node, context))
            if res.should_return(): return res
            args.append(val)
        call_res = func_value.execute(args)
        if call_res.error: return call_res
        return res.success(call_res.value)

    async def async_visit_DotAssignNode(self, node, context):
        res = RTResult()
        obj = res.register(await self.async_visit(node.obj_node, context))
        if res.should_return():
            return res

        if not isinstance(obj, (VarGroup, ClassBlueprint, ClassInstance)):
            return res.failure(
                RTError(
                    node.pos_start,
                    node.pos_end,
                    "Dot-assignment target must be a vargroup, class field, "
                    "or class instance field",
                    context,
                )
            )
        if node.decl_type is None and not isinstance(obj, ClassInstance):
            return res.failure(RTError(
                node.pos_start,
                node.pos_end,
                "Vargroup and legacy class-field assignment requires an explicit type",
                context,
            ))

        attr_name = node.attr_name_tok.value

        if node.decl_type is not None and attr_name in obj._fields:
            field_decl = obj._fields[attr_name]["type"]
            if node.decl_type != field_decl and node.decl_type != "any" and field_decl != "any":
                return res.failure(
                    RTError(
                        node.pos_start,
                        node.pos_end,
                        f"Type mismatch: field '{attr_name}' is declared as '{field_decl}' "
                        f"but assignment specifies '{node.decl_type}'",
                        context,
                    )
                )

        value = res.register(await self.async_visit(node.value_node, context))
        if res.should_return():
            return res

        obj.set_context(context).set_pos(node.pos_start, node.pos_end)
        error = obj.set_attr(attr_name, value)
        if error:
            error.pos_start = node.pos_start
            error.pos_end = node.pos_end
            error.context = context
            return res.failure(error)

        return res.success(value)

    async def async_visit_VarGroupDeclNode(self, node, context):
        return self.visit_VarGroupDeclNode(node, context)

    async def async_visit_StructDefNode(self, node, context):
        return self.visit_StructDefNode(node, context)

    async def async_visit_AddVarGroupNode(self, node, context):
        return self.visit_AddVarGroupNode(node, context)

    async def async_visit_RemoveVarGroupNode(self, node, context):
        return self.visit_RemoveVarGroupNode(node, context)

    # /async visitor path

    def visit_CodeBlockLiteralNode(self, node, context):
        if node.param_toks is not None:
            declared_names = {name_tok.value for _, name_tok in node.param_toks}
            used_names = _exec_codeblock_variable_names(node.body_block)
            undeclared = [name for name in used_names if name not in declared_names]
            if undeclared:
                return RTResult().failure(RTError(
                    node.pos_start,
                    node.pos_end,
                    "Codeblock uses undeclared variable(s): "
                    + ", ".join(undeclared),
                    context,
                ))
        return RTResult().success(
            CodeBlockValue(node.body_block, node.param_toks)
            .set_context(context)
            .set_pos(node.pos_start, node.pos_end)
        )

    def visit_CodeBlockRefNode(self, node, context):
        res = RTResult()
        block_name = node.name_tok.value
        block = context.code_blocks.get(block_name)
        if block is None:
            block = context.symbol_table.get(block_name)
        if block is None:
            return res.failure(RTError(
                node.pos_start,
                node.pos_end,
                f"Code-block '{block_name}' is not defined",
                context,
            ))
        if not isinstance(block, CodeBlockValue):
            return res.failure(RTError(
                node.pos_start,
                node.pos_end,
                f"'{node.name_tok.value}' is not a code-block parameter",
                context,
            ))
        return res.success(block.copy().set_context(context).set_pos(
            node.pos_start, node.pos_end
        ))

    def visit_ExecCallNode(self, node, context):
        res = RTResult()
        args = []
        for arg_node in node.arg_nodes:
            arg_value = res.register(self.visit(arg_node, context))
            args.append(arg_value)
            if res.should_return():
                return res
        block = res.register(self.visit(node.code_block_node, context))
        if res.should_return():
            return res
        if not isinstance(block, CodeBlockValue):
            return res.failure(RTError(
                node.pos_start,
                node.pos_end,
                "exec() expects a code-block parameter reference",
                context,
            ))
        bindings, error = _build_exec_bindings(node, block, args, context)
        if error:
            return res.failure(error)
        assert bindings is not None
        previous = {}
        try:
            for name, declared_type, value in bindings:
                previous[name] = context.symbol_table.symbols.get(name)
                context.symbol_table.set(
                    name,
                    value.copy().set_context(context),
                    decl_type=declared_type,
                )
            return self.visit(block.body_node, context)
        finally:
            for name, old_value in previous.items():
                if old_value is None:
                    context.symbol_table.symbols.pop(name, None)
                else:
                    context.symbol_table.symbols[name] = old_value

    def visit_ExecFileNode(self, node, context):
        res = RTResult()
        path_value = res.register(self.visit(node.path_node, context))
        if res.should_return():
            return res
        if not isinstance(path_value, String):
            return res.failure(RTError(
                node.pos_start,
                node.pos_end,
                "exec() file form expects one string path ending in '.lynx'",
                context,
            ))

        requested_path = path_value.value.strip()
        if not requested_path.lower().endswith(".lynx"):
            return res.failure(RTError(
                node.pos_start,
                node.pos_end,
                "exec() file paths must end in '.lynx'",
                context,
            ))

        file_value = context.symbol_table.get("__file__")
        base_dir = (
            os.path.dirname(file_value.value)
            if isinstance(file_value, String)
            else os.getcwd()
        )
        filepath = os.path.realpath(
            requested_path
            if os.path.isabs(requested_path)
            else os.path.join(base_dir, requested_path)
        )
        if not os.path.isfile(filepath):
            return res.failure(RTError(
                node.pos_start,
                node.pos_end,
                f"exec() file not found: '{requested_path}'",
                context,
            ))

        try:
            with open(filepath, "r", encoding="utf-8") as source_file:
                source = source_file.read()
        except (OSError, UnicodeError) as exc:
            return res.failure(RTError(
                node.pos_start,
                node.pos_end,
                f"Could not read exec() file '{requested_path}': {exc}",
                context,
            ))

        exec_table = SymbolTable(context.symbol_table)
        _register_builtins(exec_table)
        exec_table.set("class", ClassRegistry())
        exec_table.set("global", Namespace(exec_table))
        error = run_file(
            filepath,
            source,
            exec_table,
            execute_main=True,
        )
        if error:
            return res.failure(RTError(
                node.pos_start,
                node.pos_end,
                f"Error executing Lynxer file '{requested_path}':\n{error.as_string()}",
                context,
            ))
        return res.success(Number.null)

    def visit_CallNode(self, node, context):
        res = RTResult()
        args = []
        block_args = []

        value_to_call = res.register(self.visit(node.node_to_call, context))
        if res.should_return():
            return res
        value_to_call = value_to_call.copy().set_pos(node.pos_start, node.pos_end)

        if (isinstance(node.node_to_call, VarAccessNode)
                and isinstance(value_to_call, (Function, AsyncFunction))
                and value_to_call.is_global
                and not _uses_shared_parameters(value_to_call)):
            return res.failure(RTError(
                node.pos_start, node.pos_end,
                f"Global function '{value_to_call.name}' must be called as "
                f"'global.{value_to_call.name}(...)' not '{value_to_call.name}(...)'",
                context,
            ))

        if (isinstance(node.node_to_call, VarAccessNode)
                and isinstance(value_to_call, (Function, AsyncFunction))
                and not value_to_call.is_global):
            return res.failure(RTError(
                node.pos_start, node.pos_end,
                f"Local function '{value_to_call.name}' must be called as "
                f"'local.{value_to_call.name}(...)' not '{value_to_call.name}(...)'",
                context,
            ))

        if (isinstance(value_to_call, (Function, AsyncFunction))
                and value_to_call.is_global
                and value_to_call.global_path is not None
                and len(value_to_call.global_path) > 1):
            caller_path = _get_current_global_path(context)
            if caller_path is not None and not _can_call_global(caller_path, value_to_call.global_path):
                callee_str = "global." + ".".join(value_to_call.global_path)
                caller_str = "global." + ".".join(caller_path)
                return res.failure(RTError(
                    node.pos_start, node.pos_end,
                    f"Hierarchical call restriction: '{caller_str}' cannot call "
                    f"'{callee_str}'. Within the same global tree, a nested global "
                    f"may only call along its own ancestor/descendant path. "
                    f"Sideways calls within the same tree are not allowed.",
                    context,
                ))

        for arg_node in node.arg_nodes:
            arg_value = res.register(self.visit(arg_node, context))
            if isinstance(arg_node, VarAccessNode) and arg_value is not None:
                arg_value._lynxer_ref = (context.symbol_table, arg_node.var_name_tok.value)
            if (
                isinstance(node.node_to_call, VarAccessNode)
                and node.node_to_call.var_name_tok.value == "unshare"
                and isinstance(arg_node, VarAccessNode)
                and arg_value is not None
            ):
                arg_value._lynxer_name = arg_node.var_name_tok.value
            args.append(arg_value)
            if res.should_return():
                return res

        for block_node in node.block_arg_nodes:
            block_args.append(res.register(self.visit(block_node, context)))
            if res.should_return():
                return res

        if isinstance(value_to_call, (Function, AsyncFunction, BoundMethod)):
            return_value = res.register(value_to_call.execute(args, block_args))
        elif block_args:
            return res.failure(RTError(
                node.pos_start,
                node.pos_end,
                "Only user-defined functions can receive code blocks",
                context,
            ))
        else:
            return_value = res.register(value_to_call.execute(args))
        if res.should_return():
            return res
        if return_value is None:
            return res.failure(RTError(
                node.pos_start, node.pos_end,
                "Callable returned no runtime value",
                context,
            ))
        return_value = (
            return_value.copy()
            .set_pos(node.pos_start, node.pos_end)
            .set_context(context)
        )
        return res.success(return_value)

    def visit_DotAccessNode(self, node, context):
        res = RTResult()
        obj = res.register(self.visit(node.obj_node, context))
        if res.should_return():
            return res

        attr_name = node.attr_name_tok.value
        if hasattr(obj, "get_attr"):
            value, error = obj.get_attr(attr_name)
            if error:
                error.pos_start = node.pos_start
                error.pos_end = node.pos_end
                error.context = context
                return res.failure(error)
            value = (
                value.copy().set_pos(node.pos_start, node.pos_end).set_context(context)
            )
            return res.success(value)

        return res.failure(
            RTError(
                node.pos_start,
                node.pos_end,
                f"Value of type '{value_type_name(obj)}' does not support attribute access",
                context,
            )
        )

    def visit_NewNode(self, node, context):
        res = RTResult()
        class_registry = context.symbol_table.get("class")
        if not isinstance(class_registry, ClassRegistry):
            return res.failure(RTError(
                node.pos_start,
                node.pos_end,
                "No class registry is available in this scope",
                context,
            ))
        blueprint, error = class_registry.get_attr(node.class_name_tok.value)
        if error:
            error.pos_start = node.pos_start
            error.pos_end = node.pos_end
            error.context = context
            return res.failure(error)
        assert isinstance(blueprint, ClassBlueprint)

        args = []
        for arg_node in node.arg_nodes:
            # The async visitor has its own constructor path below; this
            # synchronous path handles ordinary expressions.
            args.append(res.register(self.visit(arg_node, context)))
            if res.should_return():
                return res
        instance_res = blueprint.instantiate(args, context)
        instance = res.register(instance_res)
        if res.should_return():
            return res
        assert instance is not None
        return res.success(
            instance.set_pos(node.pos_start, node.pos_end).set_context(context)
        )

    # vargroup visitors

    def _build_vargroup(self, name, fields, context, kind="vargroup"):
        res = RTResult()
        vg = VarGroup(name, kind=kind)
        for field_tuple in fields:
            field_type, name_tok, value_node, is_const = field_tuple
            field_name = name_tok.value
            if field_name in vg._fields:
                return res.failure(
                    RTError(
                        name_tok.pos_start,
                        name_tok.pos_end,
                        f'Duplicate field "{field_name}" in vargroup \'{name}\'',
                        context,
                    )
                )
            if field_type == "vargroup":
                nested = res.register(
                    self._build_vargroup(
                        value_node.name_tok.value, value_node.fields, context
                    )
                )
                if res.should_return():
                    return res
                vg._fields[field_name] = {"type": "vargroup", "value": nested, "const": is_const}
            else:
                value = res.register(self.visit(value_node, context))
                if res.should_return():
                    return res
                if not type_matches(field_type, value):
                    return res.failure(
                        RTError(
                            name_tok.pos_start,
                            value_node.pos_end,
                            f"Field '{field_name}' is declared as '{field_type}' "
                            f"but received a '{value_type_name(value)}' value",
                            context,
                        )
                    )
                vg._fields[field_name] = {"type": field_type, "value": value, "const": is_const}
        return res.success(vg)

    def visit_VarGroupDeclNode(self, node, context):
        res = RTResult()
        vg = res.register(
            self._build_vargroup(
                node.name_tok.value, node.fields, context, kind=node.kind
            )
        )
        if res.should_return():
            return res
        context.symbol_table.set(
            node.name_tok.value, vg, is_const=node.is_const,
            decl_type=node.kind,
        )
        return res.success(vg)

    def visit_ClassDefNode(self, node, context):
        """Register a class blueprint in the nearest ClassRegistry."""
        res = RTResult()

        methods = {}
        for method_node in node.method_nodes:
            func_name = method_node.var_name_tok.value
            param_names = [p[1].value for p in method_node.param_toks]
            param_types = [p[0].value if p[0] else None for p in method_node.param_toks]
            param_defaults = [p[2] for p in method_node.param_toks]
            code_block_names = [tok.value for tok in method_node.code_block_toks]
            func = Function(
                func_name,
                method_node.body_block,
                param_names,
                param_types,
                is_global=False,
                code_block_names=code_block_names,
                param_defaults=param_defaults,
            )
            func.set_context(context).set_pos(method_node.pos_start, method_node.pos_end)
            methods[func_name] = func

        # field_defs: (type_str, name_str, value_node, is_const)
        field_defs = [
            (fd[0], fd[1].value, fd[2], fd[3])
            for fd in node.field_defs
        ]

        blueprint = ClassBlueprint(node.name_tok.value, field_defs, methods)
        blueprint.set_context(context).set_pos(node.pos_start, node.pos_end)

        for (field_type, field_name, value_node, is_const) in field_defs:
            value = res.register(self.visit(value_node, context))
            if res.should_return():
                return res
            if not type_matches(field_type, value):
                return res.failure(RTError(
                    node.pos_start, node.pos_end,
                    f"Class '{node.name_tok.value}': field '{field_name}' is declared as "
                    f"'{field_type}' but the initializer produces a "
                    f"'{value_type_name(value)}' value",
                    context,
                ))
            blueprint._fields[field_name] = {
                "type": field_type,
                "value": value,
                "const": is_const,
            }

        class_registry = context.symbol_table.get("class")
        if not isinstance(class_registry, ClassRegistry):
            class_registry = ClassRegistry()
            class_registry.set_pos(node.pos_start, node.pos_end).set_context(context)
            context.symbol_table.set("class", class_registry)

        class_registry.register(node.name_tok.value, blueprint)
        return res.success(Number.null)

    def visit_StructDefNode(self, node, context):
        """Register a data-only struct blueprint in the shared type registry."""
        res = RTResult()
        field_defs = [
            (field_type, field_name_tok.value, None, False)
            for field_type, field_name_tok, _value_node, _is_const in node.field_defs
        ]
        blueprint = StructBlueprint(node.name_tok.value, field_defs)
        blueprint.set_context(context).set_pos(node.pos_start, node.pos_end)

        class_registry = context.symbol_table.get("class")
        if not isinstance(class_registry, ClassRegistry):
            class_registry = ClassRegistry()
            class_registry.set_pos(node.pos_start, node.pos_end).set_context(context)
            context.symbol_table.set("class", class_registry)
        class_registry.register(node.name_tok.value, blueprint)
        return res.success(Number.null)

    def visit_DotAssignNode(self, node, context):
        res = RTResult()
        obj = res.register(self.visit(node.obj_node, context))
        if res.should_return():
            return res

        if not isinstance(obj, (VarGroup, ClassBlueprint, ClassInstance)):
            return res.failure(
                RTError(
                    node.pos_start,
                    node.pos_end,
                    "Dot-assignment target must be a vargroup, class field, "
                    "or class instance field",
                    context,
                )
            )
        if node.decl_type is None and not isinstance(obj, ClassInstance):
            return res.failure(RTError(
                node.pos_start,
                node.pos_end,
                "Vargroup and legacy class-field assignment requires an explicit type",
                context,
            ))

        attr_name = node.attr_name_tok.value

        if node.decl_type is not None and attr_name in obj._fields:
            field_decl = obj._fields[attr_name]["type"]
            if node.decl_type != field_decl and node.decl_type != "any" and field_decl != "any":
                return res.failure(
                    RTError(
                        node.pos_start,
                        node.pos_end,
                        f"Type mismatch: field '{attr_name}' is declared as '{field_decl}' "
                        f"but assignment specifies '{node.decl_type}'",
                        context,
                    )
                )

        value = res.register(self.visit(node.value_node, context))
        if res.should_return():
            return res

        obj.set_context(context).set_pos(node.pos_start, node.pos_end)
        error = obj.set_attr(attr_name, value)
        if error:
            error.pos_start = node.pos_start
            error.pos_end = node.pos_end
            error.context = context
            return res.failure(error)

        return res.success(value)

    def visit_AddVarGroupNode(self, node, context):
        res = RTResult()
        obj = res.register(self.visit(node.path_node, context))
        if res.should_return():
            return res

        if not isinstance(obj, VarGroup):
            return res.failure(
                RTError(
                    node.pos_start,
                    node.pos_end,
                    "addVarGroup() first argument must be a vargroup",
                    context,
                )
            )

        field_name = node.field_name_tok.value

        if node.field_type == "vargroup":
            # field_value_node is a VarGroupDeclNode
            value = res.register(
                self._build_vargroup(
                    field_name, node.field_value_node.fields, context
                )
            )
        else:
            value = res.register(self.visit(node.field_value_node, context))
        if res.should_return():
            return res

        if node.field_type != "vargroup" and not type_matches(node.field_type, value):
            return res.failure(
                RTError(
                    node.pos_start,
                    node.pos_end,
                    f"Field '{field_name}' declared as '{node.field_type}' "
                    f"but received a '{value_type_name(value)}' value",
                    context,
                )
            )

        obj.set_context(context).set_pos(node.pos_start, node.pos_end)
        error = obj.add_field(node.field_type, field_name, value)
        if error:
            error.pos_start = node.pos_start
            error.pos_end = node.pos_end
            error.context = context
            return res.failure(error)

        return res.success(Number.null)

    def visit_RemoveVarGroupNode(self, node, context):
        res = RTResult()
        obj = res.register(self.visit(node.path_node, context))
        if res.should_return():
            return res

        if not isinstance(obj, VarGroup):
            return res.failure(
                RTError(
                    node.pos_start,
                    node.pos_end,
                    "removeVarGroup() first argument must be a vargroup",
                    context,
                )
            )

        obj.set_context(context).set_pos(node.pos_start, node.pos_end)
        error = obj.remove_field(node.field_name_tok.value)
        if error:
            error.pos_start = node.pos_start
            error.pos_end = node.pos_end
            error.context = context
            return res.failure(error)

        return res.success(Number.null)

    # /vargroup visitors

    def visit_ImportPyNode(self, node, context):
        """Pre-import Python modules into _rawpy_global_modules."""
        res = RTResult()
        import importlib as _importlib
        for mod_name in node.module_names:
            try:
                mod = _importlib.import_module(mod_name)
                _rawpy_global_modules[mod_name] = mod
            except ImportError as e:
                return res.failure(RTError(
                    node.pos_start, node.pos_end,
                    f"importPy: cannot import Python module '{mod_name}': {e}. "
                    f"Make sure the package is installed.",
                    context,
                ))
        return res.success(Number.null)

    def visit_RawPyBlockNode(self, node, context):
        res = RTResult()
        py_ns = {"__builtins__": __builtins__}
        py_ns.update(_rawpy_global_modules)
        tbl = context.symbol_table
        while tbl is not None:
            for name, val in tbl.symbols.items():
                if name not in py_ns:
                    if isinstance(val, Number):
                        py_ns[name] = bool(val.value) if val.is_bool else val.value
                    elif isinstance(val, Char):
                        py_ns[name] = val.value
                    elif isinstance(val, String):
                        py_ns[name] = val.value
                    elif isinstance(val, LynxTuple):
                        py_ns[name] = tuple(
                            e.value if isinstance(e, (Number, String)) else str(e)
                            for e in val.elements
                        )
                    elif isinstance(val, List):
                        py_ns[name] = [
                            e.value if isinstance(e, (Number, String)) else str(e)
                            for e in val.elements
                        ]
            tbl = tbl.parent

        try:
            exec(textwrap.dedent(node.code), py_ns)
        except Exception as e:
            return res.failure(
                RTError(
                    node.pos_start,
                    node.pos_end,
                    f"Python error in rawPy block: {type(e).__name__}: {e}",
                    context,
                )
            )

        for name, val in py_ns.items():
            if name.startswith("__") or callable(val):
                continue
            new_val = None
            if isinstance(val, bool):
                new_val = Number(1 if val else 0, is_bool=True)
            elif isinstance(val, int):
                new_val = Number(val)
            elif isinstance(val, float):
                new_val = Number(val)
            elif isinstance(val, str):
                new_val = String(val)
            if new_val is not None and context.symbol_table.get(name) is not None:
                context.symbol_table.update_existing(name, new_val)

        return res.success(Number.null)

    def visit_ExecBlockNode(self, node, context):
        """Run injected Lynxer statements in the surrounding context."""
        return self.visit(node.body_block, context)

    def visit_RawPyxBlockNode(self, node, context):
        res = RTResult()
        cy_locals = {}
        tbl = context.symbol_table
        while tbl is not None:
            for name, val in tbl.symbols.items():
                if name not in cy_locals:
                    if isinstance(val, Number):
                        cy_locals[name] = bool(val.value) if val.is_bool else val.value
                    elif isinstance(val, String):
                        cy_locals[name] = val.value
            tbl = tbl.parent

        try:
            cython_inline = _get_cython_inline()
            result_locals = cython_inline(
                textwrap.dedent(node.code),
                locals=cy_locals,
                globals=cy_locals,
                quiet=True,
            )
            if isinstance(result_locals, dict):
                cy_locals.update(result_locals)
        except BaseException:
            py_ns = {"__builtins__": __builtins__}
            py_ns.update(cy_locals)
            try:
                exec(textwrap.dedent(node.code), py_ns)
            except Exception as e:
                return res.failure(
                    RTError(
                        node.pos_start,
                        node.pos_end,
                        f"rawPyx error: {type(e).__name__}: {e}",
                        context,
                    )
                )
            cy_locals.update(
                {k: v for k, v in py_ns.items() if not k.startswith("__") and not callable(v)}
            )

        for name, val in cy_locals.items():
            if name.startswith("__") or callable(val):
                continue
            new_val = None
            if isinstance(val, bool):
                new_val = Number(1 if val else 0, is_bool=True)
            elif isinstance(val, int):
                new_val = Number(val)
            elif isinstance(val, float):
                new_val = Number(val)
            elif isinstance(val, str):
                new_val = String(val)
            if new_val is not None and context.symbol_table.get(name) is not None:
                context.symbol_table.update_existing(name, new_val)

        return res.success(Number.null)

    def visit_ReturnNode(self, node, context):
        res = RTResult()
        if node.node_to_return:
            value = res.register(self.visit(node.node_to_return, context))
            if res.should_return():
                return res
        else:
            value = Number.null
        return res.success_return(value)

    def visit_ImportNode(self, node, context):
        res = RTResult()
        filename = node.filename_tok.value

        explicit_bytecode = filename.endswith(".lynxc")
        native_module = filename.endswith((".so", ".dylib", ".dll"))
        if not filename.endswith(".lynx") and not explicit_bytecode and not native_module:
            filename += ".lynx"

        module_name = os.path.splitext(os.path.basename(filename))[0]

        existing = global_symbol_table.get(module_name)
        if isinstance(existing, Module):
            return res.success(Number.null)

        file_val = global_symbol_table.get("__file__")
        base_dir = os.path.dirname(file_val.value) if file_val else ""
        filepath, use_bytecode, resolve_error = _module_path(filename, base_dir)

        if filepath is None:
            return res.failure(
                RTError(
                    node.pos_start,
                    node.pos_end,
                    f"Could not import module \"{module_name}\": {resolve_error}",
                    context,
                )
            )

        module_table = SymbolTable(global_symbol_table)
        _register_builtins(module_table)
        module_table.set("class", ClassRegistry())

        if use_bytecode == "native":
            try:
                from .builtins import _load_native_module, populate_native_module_table
                _, native_state = _load_native_module(filepath, imported=True)
                populate_native_module_table(native_state, module_table)
                error = None
            except Exception as e:
                error = RTError(
                    node.pos_start, node.pos_end,
                    f'Failed to load native module "{filename}": {e}',
                    context,
                )
        elif use_bytecode:
            try:
                error = run_bytecode_file(filepath, module_table)
            except Exception as e:
                return res.failure(
                    RTError(
                        node.pos_start,
                        node.pos_end,
                        f'Failed to load bytecode "{filename}": {e}',
                        context,
                    )
                )
        else:
            try:
                with open(filepath, "r") as f:
                    script = f.read()
            except Exception as e:
                return res.failure(
                    RTError(
                        node.pos_start,
                        node.pos_end,
                        f'Failed to import "{filename}": {e}',
                        context,
                    )
                )
            error = run_file(filepath, script, module_table)

        if error:
            return res.failure(
                RTError(
                    node.pos_start,
                    node.pos_end,
                    f'Error in imported file "{filename}":\n{error.as_string()}',
                    context,
                )
            )

        module = Module(module_name, module_table)
        module.set_pos(node.pos_start, node.pos_end).set_context(context)
        global_symbol_table.set(module_name, module)
        return res.success(Number.null)

    def visit_ImportAsNode(self, node, context):
        res = RTResult()
        filename = node.filename_tok.value
        alias_name = node.alias_tok.value

        if not alias_name or not alias_name.isidentifier():
            return res.failure(RTError(
                node.pos_start, node.pos_end,
                f"importAs alias '{alias_name}' is not a valid identifier",
                context,
            ))

        explicit_bytecode = filename.endswith(".lynxc")
        native_module = filename.endswith((".so", ".dylib", ".dll"))
        if not filename.endswith(".lynx") and not explicit_bytecode and not native_module:
            filename += ".lynx"

        module_name = os.path.splitext(os.path.basename(filename))[0]

        existing = global_symbol_table.get(alias_name)
        if isinstance(existing, Module):
            return res.success(Number.null)

        file_val = global_symbol_table.get("__file__")
        base_dir = os.path.dirname(file_val.value) if file_val else ""
        filepath, use_bytecode, resolve_error = _module_path(filename, base_dir)

        if filepath is None:
            return res.failure(RTError(
                node.pos_start, node.pos_end,
                f"Could not import module \"{module_name}\": {resolve_error}",
                context,
            ))

        module_table = SymbolTable(global_symbol_table)
        _register_builtins(module_table)
        module_table.set("class", ClassRegistry())

        if use_bytecode == "native":
            try:
                from .builtins import _load_native_module, populate_native_module_table
                _, native_state = _load_native_module(filepath, imported=True)
                populate_native_module_table(native_state, module_table)
                error = None
            except Exception as e:
                error = RTError(
                    node.pos_start, node.pos_end,
                    f'Failed to load native module "{filename}": {e}',
                    context,
                )
        elif use_bytecode:
            try:
                error = run_bytecode_file(filepath, module_table)
            except Exception as e:
                return res.failure(RTError(
                    node.pos_start, node.pos_end,
                    f'Failed to load bytecode "{filename}": {e}',
                    context,
                ))
        else:
            try:
                with open(filepath, "r") as f:
                    script = f.read()
            except Exception as e:
                return res.failure(RTError(
                    node.pos_start, node.pos_end,
                    f'Failed to importAs "{filename}": {e}',
                    context,
                ))
            error = run_file(filepath, script, module_table)

        if error:
            return res.failure(RTError(
                node.pos_start, node.pos_end,
                f'Error in imported file "{filename}":\n{error.as_string()}',
                context,
            ))

        module = Module(module_name, module_table)
        module.set_pos(node.pos_start, node.pos_end).set_context(context)
        global_symbol_table.set(alias_name, module)
        return res.success(Number.null)

    def visit_ProgramNode(self, node, context):
        res = RTResult()

        for decl in node.globals_list:
            res.register(self.visit(decl, context))
            if res.error:
                return res

        if node.main_func is not None:
            res.register(self.visit(node.main_func, context))
            if res.error:
                return res

        if node.setup_func:
            global _setup_in_progress
            previous_setup_state = _setup_in_progress
            _setup_in_progress = True
            try:
                setup_res = self.run_setup(node.setup_func, context)
                if setup_res.error:
                    return setup_res
            finally:
                _setup_in_progress = previous_setup_state

        entry_name = _main_override if _main_override else "main"
        entry_fn = context.symbol_table.get(entry_name)
        if entry_fn is None:
            if _main_override:
                return res.failure(RTError(
                    node.pos_start, node.pos_end,
                    f"overrideMain: no global function named '{_main_override}' found. "
                    f"Make sure 'global {_main_override}(){{}}' is declared in the file.",
                    context,
                ))
            else:
                return res.failure(RTError(
                    node.pos_start, node.pos_end,
                    "Program has no entry point. "
                    "Add 'global main(){}' as the last declaration, "
                    "or call overrideMain(\"funcName\") inside global setup(){} "
                    "to use a different global function as the entry point.",
                    context,
                ))

        call_res = RTResult()
        call_res.register(entry_fn.execute([]))
        if call_res.error:
            return call_res

        return res.success(Number.null)

def _register_builtins(symbol_table: SymbolTable) -> None:
    """Load built-ins only after the runtime types have finished initializing.

    Keeping this import inside the registration boundary makes ``lynxer`` and
    ``builtins`` independently importable without a partially initialized
    module cycle.
    """
    global _builtins_registration_deferred
    builtins_module = sys.modules.get(f"{__package__}.builtins")
    if builtins_module is not None and not hasattr(
        builtins_module, "BuiltInFunction"
    ):
        _builtins_registration_deferred = True
        return
    from .builtins import BuiltInFunction, register_builtins

    globals()["BuiltInFunction"] = BuiltInFunction
    register_builtins(symbol_table)
    _builtins_registration_deferred = False

# global symbol table

_builtins_registration_deferred = False

def _new_global_symbol_table():
    table = SymbolTable()
    table.set("true", Number.true)
    table.set("false", Number.false)
    _register_builtins(table)
    table.set("embedPy", EmbedPyNamespace())
    return table


global_symbol_table = _new_global_symbol_table()

SHARED_INTERPRETER = Interpreter()


def reset_runtime_state():
    """Start a clean top-level runtime for an independent program run."""
    global global_symbol_table
    global_symbol_table = _new_global_symbol_table()
    _rawpy_global_modules.clear()


def _interpreter_error(fn, text, context_name, exc):
    """Turn an unexpected host exception into a normal Lynxer error."""
    context = Context(context_name)
    start = Position(0, 0, 0, fn, text)
    details = str(exc).strip() or type(exc).__name__
    return RTError(start, start.copy(), f"Interpreter failure: {details}", context)


# run

def run(fn, text, suppress_deprecation_warnings=False):
    global _forever_delay, _forever_warning_suppressed, _setup_in_progress
    global _main_override, _deprecation_warning_suppressed
    global _deprecation_warning_deferred
    reset_runtime_state()
    _main_override = None
    _forever_delay = 0.02
    _forever_warning_suppressed = False
    _deprecation_warning_suppressed = bool(suppress_deprecation_warnings)
    _pending_deprecation_warnings.clear()
    _setup_in_progress = False
    _deprecation_warning_deferred = True

    try:
        lexer = Lexer(fn, text)
        tokens, error = lexer.make_tokens()
        if error:
            return None, error

        parser = Parser(tokens)
        ast = parser.parse()
        if ast.error:
            return None, ast.error
    finally:
        _deprecation_warning_deferred = False

    interpreter = SHARED_INTERPRETER
    context = Context("<program>")
    context.symbol_table = global_symbol_table
    global_symbol_table.set("__file__", String(os.path.abspath(fn)))
    global_symbol_table.set("global", Namespace(global_symbol_table))
    global_symbol_table.set("class", ClassRegistry())

    try:
        result = interpreter.visit(ast.node, context)
    except Exception as exc:
        _flush_deprecation_warnings()
        return None, _interpreter_error(fn, text, "<program>", exc)
    _flush_deprecation_warnings()
    return result.value, result.error

def run_file(fn, text, symbol_table, execute_main=False):
    lexer = Lexer(fn, text)
    tokens, error = lexer.make_tokens()
    if error:
        return error

    parser = Parser(tokens)
    ast = parser.parse(require_main=False)
    if ast.error:
        return ast.error

    interpreter = SHARED_INTERPRETER
    context = Context(f"<import:{os.path.basename(fn)}>")
    context.symbol_table = symbol_table
    symbol_table.set("__file__", String(os.path.abspath(fn)))

    node = ast.node

    try:
        for decl in node.globals_list:
            r = RTResult()
            r.register(interpreter.visit(decl, context))
            if r.error:
                return r.error

        if node.setup_func:
            global _setup_in_progress
            previous_setup_state = _setup_in_progress
            _setup_in_progress = True
            try:
                r = interpreter.run_setup(node.setup_func, context)
                if r.error:
                    return r.error
            finally:
                _setup_in_progress = previous_setup_state

        if execute_main and node.main_func is not None:
            main_decl_result = RTResult()
            main_decl_result.register(interpreter.visit(node.main_func, context))
            if main_decl_result.error:
                return main_decl_result.error
            main_name = node.main_func.var_name_tok.value
            main_function = symbol_table.get(main_name)
            if main_function is None:
                return _interpreter_error(
                    fn,
                    text,
                    f"<exec:{os.path.basename(fn)}>",
                    RuntimeError(f"entry point '{main_name}' was not registered"),
                )
            call_result = main_function.execute([])
            if call_result.error:
                return call_result.error
    except Exception as exc:
        return _interpreter_error(fn, text, f"<import:{os.path.basename(fn)}>", exc)

    return None

# Bytecode helpers remain available from this module for compatibility with
# callers that historically imported them from ``lynxer.lynxer``.
from .bytecode import (  # noqa: E402
    BYTECODE_MAGIC,
    BYTECODE_VERSION,
    compile_to_bytecode,
    run_bytecode,
    run_bytecode_file,
)
