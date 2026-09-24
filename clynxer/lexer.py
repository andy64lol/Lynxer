"""Lexical analysis for Lynxer source code.

Token types, keywords, :class:`Position`, :class:`Token`, and the
:class:`Lexer` itself.
"""

from __future__ import annotations

import string
from typing import Any

from .error import IllegalCharError

DIGITS = "0123456789"
LETTERS = string.ascii_letters
LETTERS_DIGITS = LETTERS + DIGITS
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

# tokens

TT_INT = "INT"
TT_FLOAT = "FLOAT"
TT_STRING = "STRING"
TT_INTER_STRING = "INTER_STRING"
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
    "func",
    "enum",
    "class",
    "native",
    "struct",
    "new",
    "break", "continue", "restart",
    "inter",
]

# Built-ins that accept an ``inter"..."`` argument. Everywhere else the parser
# rejects the form so interpolation never leaks into ordinary expressions.
INTERPOLATION_BUILTINS = ("print", "println", "input", "inputln")

# Marker written in front of a character that came from an escape sequence
# inside an ``inter"..."`` body. It lets the parser tell a real ``{``/``}``
# interpolation delimiter from an escaped ``\{``/``\}`` one. ``\x01`` cannot be
# produced by any documented Lynxer escape sequence.
INTER_ESCAPE_MARK = "\x01"

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
                elif tok.matches(TT_KEYWORD, "inter"):
                    inter_tok, inter_error = self._try_consume_inter_string(
                        tok.pos_start
                    )
                    if inter_error:
                        return [], inter_error
                    if inter_tok is not None:
                        tokens[-1] = inter_tok
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

    def _try_consume_inter_string(self, pos_start):
        """Consume ``inter"..."`` as one interpolated-string token.

        Returns ``(None, None)`` when ``inter`` is not followed by a string
        literal (the keyword token is then kept so the parser can report the
        real problem), ``(None, error)`` for an unterminated body, and
        ``(token, None)`` on success.
        """
        text = self.text
        n = len(text)
        i = self.pos.idx

        while i < n and text[i] in " \t":
            i += 1
        if i >= n or text[i] != '"':
            return None, None

        while self.pos.idx < i:
            self.advance()

        tok = self.make_inter_string(pos_start)
        if tok is None:
            return None, IllegalCharError(
                pos_start,
                self.pos,
                'Unterminated inter"..." string literal — missing closing \'"\'',
            )
        return tok, None

    def make_inter_string(self, pos_start):
        """Read the body of an ``inter"..."`` literal.

        Escapes are decoded exactly like ``make_string``, but characters that
        came from an escape sequence are prefixed with :data:`INTER_ESCAPE_MARK`
        so the parser can still tell delimiters (``{``/``}``) apart from
        escaped braces and from backslashes produced by escapes.
        """
        s = ""
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
                if self.current_char in "{}\\":
                    s += INTER_ESCAPE_MARK
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
            return None

        self.advance()
        return Token(TT_INTER_STRING, s, pos_start, self.pos)

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

