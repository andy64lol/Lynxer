"""Formatting and syntax-checking helpers for Lynxer source files."""

from __future__ import annotations

import re
from typing import Any

from .lynxer import (
    TT_COMMA,
    TT_DOCSTRING,
    TT_DOT,
    TT_EOF,
    TT_LBRACE,
    TT_LBRACKET,
    TT_LPAREN,
    TT_RBRACE,
    TT_RBRACKET,
    TT_RPAREN,
    TT_SEMICOLON,
    Lexer,
    Parser,
    Token,
)


class FormattingError(Exception):
    """Raised when source cannot be tokenized or parsed for formatting."""

    def __init__(self, error: Any):
        self.error = error
        super().__init__(str(error))


def lint_source(filename: str, source: str) -> Any:
    """Return a lexer/parser error, or ``None`` when *source* is valid.

    Linting deliberately does not execute the program and does not require a
    ``main()`` function, so it can also check imported standard-library files.
    """
    lexer = Lexer(filename, source)
    tokens, error = lexer.make_tokens()
    if error:
        return error

    result = Parser(tokens).parse(require_main=False)
    return result.error


def _tokens(filename: str, source: str) -> list[Token]:
    lexer = Lexer(filename, source)
    tokens, error = lexer.make_tokens()
    if error:
        raise FormattingError(error)

    result = Parser(tokens).parse(require_main=False)
    if result.error:
        raise FormattingError(result.error)
    return tokens


def _token_text(token: Token, source: str) -> str:
    start = token.pos_start.idx
    end = token.pos_end.idx
    return source[start:end]


def _comments_in_gap(gap: str, include_delimited: bool = False) -> list[str]:
    """Extract comments from whitespace between two tokens."""
    comments: list[str] = []
    for line in gap.splitlines():
        stripped = line.strip()
        if stripped.startswith("////"):
            continue
        if stripped.startswith("///"):
            if include_delimited and stripped.endswith("///"):
                comments.append(stripped)
        elif stripped.startswith("//"):
            comments.append(stripped)
    return comments


def _as_multiline_comment(comment: str) -> str:
    """Convert an ordinary ``//`` comment to Lynxer's delimited form."""
    return f"///{comment[2:]} ///"


def _oneline_comment(comment: str) -> str:
    """Return a comment that is safe to keep in a one-line source file."""
    if comment.startswith("///"):
        return comment
    return _as_multiline_comment(comment)


def _is_word(token: Token) -> bool:
    return token.type in {
        "INT",
        "FLOAT",
        "STRING",
        "CHAR",
        "IDENTIFIER",
        "KEYWORD",
        "RAWPY_BLOCK",
        "RAWPYX_BLOCK",
        "EXEC_BLOCK",
        TT_DOCSTRING,
    }


def _is_operator(token: Token) -> bool:
    return token.type in {
        "PLUS",
        "MINUS",
        "MUL",
        "DIV",
        "MOD",
        "POW",
        "ROOT",
        "FLOORDIV",
        "EQ",
        "EQEQ",
        "NE",
        "LT",
        "GT",
        "LTE",
        "GTE",
        "PLUSEQ",
        "MINUSEQ",
        "MULEQ",
        "DIVEQ",
        "MODEQ",
        "POWEQ",
        "ROOTEQ",
        "FLOORDIVEQ",
        "AMP",
        "PIPE",
        "CARET",
        "TILDE",
        "SHL",
        "SHR",
        "LOGICAL_NOT",
        "LOGICAL_AND",
        "LOGICAL_NAND",
        "LOGICAL_OR",
        "LOGICAL_NOR",
        "BITWISE_NAND",
        "BITWISE_XNOR",
        "BITWISE_NOR",
    }


def _needs_space(previous: Token | None, current: Token) -> bool:
    if previous is None:
        return False
    if current.type in {TT_COMMA, TT_SEMICOLON, TT_RPAREN, TT_RBRACKET, TT_RBRACE, TT_DOT}:
        return False
    if previous.type in {TT_LPAREN, TT_LBRACKET, TT_DOT}:
        return False
    if current.type == TT_LPAREN:
        return False
    if current.type in {"PLUS", "MINUS", "TILDE", "LOGICAL_NOT"}:
        return _is_word(previous) or previous.type in {TT_RPAREN, TT_RBRACKET}
    if previous.type in {"PLUS", "MINUS", "TILDE", "LOGICAL_NOT"}:
        return _is_word(current) or current.type in {TT_LPAREN, TT_LBRACKET}
    if _is_operator(previous) or _is_operator(current):
        return True
    if _is_word(previous) and _is_word(current):
        return True
    if previous.type in {TT_RPAREN, TT_RBRACKET, TT_RBRACE} and _is_word(current):
        return True
    return False


class _Writer:
    def __init__(self, oneline: bool):
        self.oneline = oneline
        self.indent = 0
        self.lines = [""]

    @property
    def current(self) -> str:
        return self.lines[-1]

    def append(self, text: str, space: bool = False) -> None:
        if not text:
            return
        if space and self.current and not self.current.endswith((" ", "\n")):
            self.lines[-1] += " "
        self.lines[-1] += text

    def newline(self, force: bool = False) -> None:
        if self.oneline:
            if self.current and not self.current.endswith(" "):
                self.lines[-1] += " "
            return
        if force or self.current.strip():
            self.lines.append("    " * self.indent)

    def align_current_indent(self) -> None:
        if not self.current.strip():
            self.lines[-1] = "    " * self.indent

    def trim(self) -> str:
        if self.oneline:
            return re.sub(r"[ \t\r\n]+", " ", self.lines[0]).strip()
        while self.lines and not self.lines[-1].strip():
            self.lines.pop()
        return "\n".join(line.rstrip() for line in self.lines).strip() + "\n"


def format_source(filename: str, source: str, oneline: bool = False) -> str:
    """Return consistently spaced Lynxer source without changing its tokens."""
    tokens = _tokens(filename, source)
    writer = _Writer(oneline)
    previous: Token | None = None
    previous_end = 0
    paren_depth = 0

    for index, token in enumerate(tokens):
        if token.type == TT_EOF:
            break
        next_token = tokens[index + 1] if index + 1 < len(tokens) else None

        # The lexer emits the identifier and then a block token for rawPy,
        # rawPyx, and exec. The block token already spans the identifier.
        if (
            token.type == "IDENTIFIER"
            and next_token is not None
            and next_token.type in {"RAWPY_BLOCK", "RAWPYX_BLOCK", "EXEC_BLOCK"}
            and token.pos_start.idx == next_token.pos_start.idx
        ):
            continue

        gap = source[previous_end : token.pos_start.idx]
        for comment in _comments_in_gap(gap, include_delimited=oneline):
            if oneline:
                writer.append(
                    _oneline_comment(comment),
                    space=bool(writer.current.strip()),
                )
                writer.append(" ")
            else:
                writer.newline()
                writer.append(comment)
                writer.newline()

        raw = _token_text(token, source)
        if token.type == TT_DOCSTRING:
            writer.newline()
            writer.append(raw.strip())
            writer.newline()
            previous = token
            previous_end = token.pos_end.idx
            continue

        if token.type == TT_LBRACE:
            writer.append("{", space=bool(writer.current.strip()))
            writer.indent += 1
            writer.newline()
        elif token.type == TT_RBRACE:
            writer.indent = max(0, writer.indent - 1)
            writer.newline()
            writer.align_current_indent()
            writer.append("}")
            if not next_token or next_token.type not in {
                TT_SEMICOLON,
                TT_COMMA,
                TT_RPAREN,
                TT_RBRACKET,
            } and not (
                next_token
                and next_token.type == "KEYWORD"
                and next_token.value in {"else", "elif", "catch"}
            ):
                writer.newline()
        elif token.type == TT_SEMICOLON:
            writer.append(";")
            if paren_depth:
                writer.append(" ")
            else:
                writer.newline()
        elif token.type == TT_COMMA:
            writer.append(",")
            writer.append(" ")
        else:
            writer.append(raw, space=_needs_space(previous, token))

        if token.type == TT_LPAREN:
            paren_depth += 1
        elif token.type == TT_RPAREN:
            paren_depth = max(0, paren_depth - 1)

        previous = token
        previous_end = token.pos_end.idx

    if not oneline:
        for comment in _comments_in_gap(source[previous_end:]):
            writer.newline()
            writer.append(comment)
            writer.newline()
    else:
        for comment in _comments_in_gap(
            source[previous_end:], include_delimited=True
        ):
            writer.append(
                _oneline_comment(comment),
                space=bool(writer.current.strip()),
            )

    return writer.trim()
