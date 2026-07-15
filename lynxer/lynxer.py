from __future__ import annotations
from .strings_with_arrows import string_with_arrows
import string
import os
import sys
import textwrap
from typing import ClassVar

DIGITS = "0123456789"
STDLIB_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "stdlib")
LETTERS = string.ascii_letters
LETTERS_DIGITS = LETTERS + DIGITS

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


# tokens

TT_INT = "INT"
TT_FLOAT = "FLOAT"
TT_STRING = "STRING"
TT_IDENTIFIER = "IDENTIFIER"
TT_KEYWORD = "KEYWORD"
TT_PLUS = "PLUS"
TT_MINUS = "MINUS"
TT_MUL = "MUL"
TT_DIV = "DIV"
TT_MOD = "MOD"
TT_EQ = "EQ"
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
TT_RAWPY_BLOCK = "RAWPY_BLOCK"
TT_EOF = "EOF"

TYPE_KEYWORDS = ["int", "float", "str", "bool"]

KEYWORDS = [
    "int", "float", "str", "bool",
    "void", "def", "const",
    "if", "else", "while", "for",
    "return", "import",
    "true", "false",
    "and", "or", "not", "is",
]


class Token:
    def __init__(self, type_, value=None, pos_start=None, pos_end=None):
        self.type = type_
        self.value = value

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
        self.current_char = None
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

    def make_tokens(self):
        tokens = []

        while self.current_char is not None:
            if self.current_char in " \t\n\r":
                self.advance()
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
                if tok.type == TT_IDENTIFIER and tok.value == "rawpy":
                    block_tok = self._try_consume_rawpy_block(tok.pos_start)
                    if block_tok is not None:
                        tokens.append(block_tok)
            elif self.current_char == '"':
                tokens.append(self.make_string())
            elif self.current_char == "+":
                tokens.append(Token(TT_PLUS, pos_start=self.pos))
                self.advance()
            elif self.current_char == "-":
                tokens.append(Token(TT_MINUS, pos_start=self.pos))
                self.advance()
            elif self.current_char == "*":
                tokens.append(Token(TT_MUL, pos_start=self.pos))
                self.advance()
            elif self.current_char == "/":
                tokens.append(Token(TT_DIV, pos_start=self.pos))
                self.advance()
            elif self.current_char == "%":
                tokens.append(Token(TT_MOD, pos_start=self.pos))
                self.advance()
            elif self.current_char == "=":
                tokens.append(Token(TT_EQ, pos_start=self.pos))
                self.advance()
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

        escape_characters = {"n": "\n", "t": "\t", "\\": "\\", '"': '"'}

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

        self.advance()
        return Token(TT_STRING, s, pos_start, self.pos)

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
        return Token(tok_type, pos_start=pos_start, pos_end=self.pos)

    def make_greater_than(self):
        tok_type = TT_GT
        pos_start = self.pos.copy()
        self.advance()
        if self.current_char == "=":
            self.advance()
            tok_type = TT_GTE
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

    def _try_consume_rawpy_block(self, pos_start):
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

        return Token(TT_RAWPY_BLOCK, code, pos_start, self.pos)


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


class BoolNode:
    def __init__(self, tok):
        self.tok = tok
        self.value = tok.value == "true"
        self.pos_start = self.tok.pos_start
        self.pos_end = self.tok.pos_end


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


class VarDeclNode:
    def __init__(self, type_tok, var_name_tok, value_node, is_const=False):
        self.type_tok = type_tok
        self.var_name_tok = var_name_tok
        self.value_node = value_node
        self.is_const = is_const
        self.pos_start = type_tok.pos_start if type_tok else var_name_tok.pos_start
        self.pos_end = value_node.pos_end


class VarAssignNode:
    def __init__(self, var_name_tok, value_node):
        self.var_name_tok = var_name_tok
        self.value_node = value_node
        self.pos_start = self.var_name_tok.pos_start
        self.pos_end = self.value_node.pos_end


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


class FuncDefNode:
    def __init__(
        self, kind_tok, var_name_tok, param_toks, body_block, pos_start, pos_end
    ):
        self.kind_tok = kind_tok
        self.var_name_tok = var_name_tok
        self.param_toks = param_toks
        self.body_block = body_block
        self.pos_start = pos_start
        self.pos_end = pos_end


class CallNode:
    def __init__(self, node_to_call, arg_nodes, pos_start, pos_end):
        self.node_to_call = node_to_call
        self.arg_nodes = arg_nodes
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


class RawPyBlockNode:
    def __init__(self, code, pos_start, pos_end):
        self.code = code
        self.pos_start = pos_start
        self.pos_end = pos_end


class ProgramNode:
    def __init__(self, setup_func, globals_list, main_func, pos_start, pos_end):
        self.setup_func = setup_func
        self.globals_list = globals_list
        self.main_func = main_func
        self.pos_start = pos_start
        self.pos_end = pos_end


# parse result


class ParseResult:
    def __init__(self):
        self.error = None
        self.node = None
        self.last_registered_advance_count = 0
        self.advance_count = 0
        self.to_reverse_count = 0

    def register_advancement(self):
        self.last_registered_advance_count = 1
        self.advance_count += 1

    def register(self, res):
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
    def __init__(self, tokens):
        self.tokens = tokens
        self.tok_idx = -1
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
            self.current_tok.type == TT_KEYWORD
            and self.current_tok.value in TYPE_KEYWORDS
        )

    def parse(self, require_main=True):
        res = ParseResult()
        pos_start = self.current_tok.pos_start.copy()
        setup_func = None
        main_func = None
        globals_list = []
        self._require_main = require_main

        while self.current_tok.type != TT_EOF:
            if self.current_tok.matches(TT_KEYWORD, "void"):
                next_tok = self.peek(1)
                if (
                    next_tok
                    and next_tok.type == TT_IDENTIFIER
                    and next_tok.value == "setup"
                ):
                    setup_func = res.register(self.parse_func_def())
                    if res.error:
                        return res
                elif (
                    next_tok
                    and next_tok.type == TT_IDENTIFIER
                    and next_tok.value == "main"
                ):
                    main_func = res.register(self.parse_func_def())
                    if res.error:
                        return res
                else:
                    node = res.register(self.parse_func_def())
                    if res.error:
                        return res
                    globals_list.append(node)
            elif self.current_tok.matches(TT_KEYWORD, "const"):
                node = res.register(self.parse_const_decl())
                if res.error:
                    return res
                globals_list.append(node)
            elif self.is_type_keyword():
                return res.failure(
                    InvalidSyntaxError(
                        self.current_tok.pos_start,
                        self.current_tok.pos_end,
                        f"Global variables must be declared inside 'void setup(){{}}', not at the top level. "
                        f"Move '{self.current_tok.value} ...' inside setup()",
                    )
                )
            else:
                return res.failure(
                    InvalidSyntaxError(
                        self.current_tok.pos_start,
                        self.current_tok.pos_end,
                        f"Executable code is not allowed outside of a function. "
                        f"Only 'void' function definitions and 'const' declarations are permitted at the top level. "
                        f"Put all logic inside a function such as 'void main(){{}}'",
                    )
                )

        if main_func is None and self._require_main:
            return res.failure(
                InvalidSyntaxError(
                    self.current_tok.pos_start,
                    self.current_tok.pos_end,
                    "Program requires a 'void main()' function",
                )
            )

        pos_end = self.current_tok.pos_end.copy()
        return res.success(
            ProgramNode(setup_func, globals_list, main_func, pos_start, pos_end)
        )

    def parse_func_def(self):
        res = ParseResult()
        pos_start = self.current_tok.pos_start.copy()

        # kind: void or def
        if not (
            self.current_tok.matches(TT_KEYWORD, "void")
            or self.current_tok.matches(TT_KEYWORD, "def")
        ):
            return res.failure(
                InvalidSyntaxError(
                    self.current_tok.pos_start,
                    self.current_tok.pos_end,
                    "Expected 'void' or 'def'",
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
        while self.current_tok.type != TT_RPAREN and self.current_tok.type != TT_EOF:
            if (
                self.is_type_keyword()
                and self.peek(1)
                and self.peek(1).type == TT_IDENTIFIER
            ):
                type_tok = self.current_tok
                res.register_advancement()
                self.advance()
                pname_tok = self.current_tok
                res.register_advancement()
                self.advance()
                param_toks.append((type_tok, pname_tok))
            elif self.current_tok.type == TT_IDENTIFIER:
                param_toks.append((None, self.current_tok))
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

        is_setup = name_tok.value == "setup"
        body = res.register(self.parse_block(in_setup=is_setup, allow_local_funcs=True))
        if res.error:
            return res

        pos_end = self.current_tok.pos_end.copy()
        return res.success(
            FuncDefNode(kind_tok, name_tok, param_toks, body, pos_start, pos_end)
        )

    def parse_block(self, in_setup=False, allow_local_funcs=False):
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
        while self.current_tok.type != TT_RBRACE and self.current_tok.type != TT_EOF:
            stmt = res.register(
                self.parse_statement(
                    in_setup=in_setup, allow_local_funcs=allow_local_funcs
                )
            )
            if res.error:
                return res
            statements.append(stmt)

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

        if allow_local_funcs and self.current_tok.matches(TT_KEYWORD, "def"):
            node = res.register(self.parse_func_def())
            if res.error:
                return res
            return res.success(node)

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

        if self.current_tok.matches(TT_KEYWORD, "for"):
            node = res.register(self.parse_for())
            if res.error:
                return res
            return res.success(node)

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

        if self.is_type_keyword():
            node = res.register(self.parse_var_decl())
            if res.error:
                return res
            return res.success(node)

        if self.current_tok.type == TT_IDENTIFIER:
            next_tok = self.peek(1)

            if (
                self.current_tok.value == "rawpy"
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

            if next_tok and next_tok.type == TT_EQ:
                node = res.register(self.parse_assign())
                if res.error:
                    return res
                return res.success(node)

            expr = res.register(self.parse_expr())
            if res.error:
                return res
            if self.current_tok.type != TT_SEMICOLON:
                return res.failure(
                    InvalidSyntaxError(
                        self.current_tok.pos_start,
                        self.current_tok.pos_end,
                        f"Expected ';' after statement — got '{self.current_tok.value or self.current_tok.type}'",
                    )
                )
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

    def parse_var_decl(self):
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

        if self.current_tok.type != TT_SEMICOLON:
            return res.failure(
                InvalidSyntaxError(
                    self.current_tok.pos_start, self.current_tok.pos_end, "Expected ';'"
                )
            )
        res.register_advancement()
        self.advance()

        return res.success(VarDeclNode(type_tok, name_tok, value, is_const=False))

    def parse_const_decl(self):
        res = ParseResult()
        pos_start = self.current_tok.pos_start.copy()
        res.register_advancement()
        self.advance()

        if not self.is_type_keyword():
            return res.failure(
                InvalidSyntaxError(
                    self.current_tok.pos_start,
                    self.current_tok.pos_end,
                    "Expected type keyword after 'const'",
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

        if self.current_tok.type != TT_SEMICOLON:
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

        return res.success(VarAssignNode(name_tok, value))

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
        if self.current_tok.matches(TT_KEYWORD, "else"):
            res.register_advancement()
            self.advance()
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

        body = res.register(self.parse_block(allow_local_funcs=True))
        if res.error:
            return res

        pos_end = self.current_tok.pos_end.copy()
        return res.success(WhileNode(condition, body, pos_start, pos_end))

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

        if self.current_tok.type != TT_SEMICOLON:
            return res.failure(
                InvalidSyntaxError(
                    self.current_tok.pos_start,
                    self.current_tok.pos_end,
                    "Expected ';' after for-condition",
                )
            )
        res.register_advancement()
        self.advance()

        update_node = res.register(self.parse_for_update())
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

        body = res.register(self.parse_block(allow_local_funcs=True))
        if res.error:
            return res

        pos_end = self.current_tok.pos_end.copy()
        return res.success(
            ForNode(init_node, condition, update_node, body, pos_start, pos_end)
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

        if self.current_tok.type != TT_EQ:
            return res.failure(
                InvalidSyntaxError(
                    self.current_tok.pos_start,
                    self.current_tok.pos_end,
                    "Expected '=' in for-update",
                )
            )
        res.register_advancement()
        self.advance()

        value = res.register(self.parse_expr())
        if res.error:
            return res
        return res.success(VarAssignNode(name_tok, value))

    def parse_expr(self):
        return self.parse_or_expr()

    def parse_or_expr(self):
        res = ParseResult()
        left = res.register(self.parse_and_expr())
        if res.error:
            return res

        while self.current_tok.matches(TT_KEYWORD, "or"):
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

        while self.current_tok.matches(TT_KEYWORD, "and"):
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
        if self.current_tok.matches(TT_KEYWORD, "not"):
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
        left = res.register(self.parse_arith_expr())
        if res.error:
            return res

        if (
            self.current_tok.matches(TT_KEYWORD, "not")
            and self.peek()
            and self.peek().matches(TT_KEYWORD, "is")
        ):
            op_tok = self.current_tok
            op_tok.value = "not is"
            res.register_advancement()
            self.advance()
            res.register_advancement()
            self.advance()
            right = res.register(self.parse_arith_expr())
            if res.error:
                return res
            return res.success(BinOpNode(left, op_tok, right))

        if self.current_tok.matches(TT_KEYWORD, "is"):
            op_tok = self.current_tok
            res.register_advancement()
            self.advance()
            right = res.register(self.parse_arith_expr())
            if res.error:
                return res
            return res.success(BinOpNode(left, op_tok, right))

        if self.current_tok.type in (TT_LT, TT_GT, TT_LTE, TT_GTE):
            op_tok = self.current_tok
            res.register_advancement()
            self.advance()
            right = res.register(self.parse_arith_expr())
            if res.error:
                return res
            return res.success(BinOpNode(left, op_tok, right))

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
        left = res.register(self.parse_factor())
        if res.error:
            return res

        while self.current_tok.type in (TT_MUL, TT_DIV, TT_MOD):
            op_tok = self.current_tok
            res.register_advancement()
            self.advance()
            right = res.register(self.parse_factor())
            if res.error:
                return res
            left = BinOpNode(left, op_tok, right)

        return res.success(left)

    def parse_factor(self):
        res = ParseResult()
        tok = self.current_tok

        if tok.type in (TT_PLUS, TT_MINUS):
            res.register_advancement()
            self.advance()
            factor = res.register(self.parse_factor())
            if res.error:
                return res
            return res.success(UnaryOpNode(tok, factor))

        return self.parse_call()

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
                if self.current_tok.type != TT_IDENTIFIER:
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

            else:
                break

        return res.success(atom)

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

        elif tok.matches(TT_KEYWORD, "true") or tok.matches(TT_KEYWORD, "false"):
            res.register_advancement()
            self.advance()
            return res.success(BoolNode(tok))

        elif tok.type == TT_IDENTIFIER:
            res.register_advancement()
            self.advance()
            return res.success(VarAccessNode(tok))

        elif tok.type == TT_LPAREN:
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

        return res.failure(
            InvalidSyntaxError(
                tok.pos_start,
                tok.pos_end,
                "Expected int, float, str, bool, identifier, or '('",
            )
        )


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
        self.set_pos()
        self.set_context()

    def set_pos(self, pos_start=None, pos_end=None):
        self.pos_start = pos_start
        self.pos_end = pos_end
        return self

    def set_context(self, context=None):
        self.context = context
        return self

    def added_to(self, other):
        return None, self.illegal_operation(other)

    def subbed_by(self, other):
        return None, self.illegal_operation(other)

    def multed_by(self, other):
        return None, self.illegal_operation(other)

    def dived_by(self, other):
        return None, self.illegal_operation(other)

    def modded_by(self, other):
        return None, self.illegal_operation(other)

    def get_comparison_eq(self, other):
        return None, self.illegal_operation(other)

    def get_comparison_ne(self, other):
        return None, self.illegal_operation(other)

    def get_comparison_lt(self, other):
        return None, self.illegal_operation(other)

    def get_comparison_gt(self, other):
        return None, self.illegal_operation(other)

    def get_comparison_lte(self, other):
        return None, self.illegal_operation(other)

    def get_comparison_gte(self, other):
        return None, self.illegal_operation(other)

    def anded_by(self, other):
        return None, self.illegal_operation(other)

    def ored_by(self, other):
        return None, self.illegal_operation(other)

    def notted(self):
        return None, self.illegal_operation()

    def execute(self, args):
        return RTResult().failure(self.illegal_operation())

    def copy(self):
        raise Exception("No copy method defined")

    def is_true(self):
        return False

    def illegal_operation(self, other=None):
        if not other:
            other = self
        return RTError(self.pos_start, other.pos_end, "Illegal operation", self.context)


class Number(Value):
    null: ClassVar["Number"]
    false: ClassVar["Number"]
    true: ClassVar["Number"]

    def __init__(self, value):
        super().__init__()
        self.value = value

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

    def get_comparison_eq(self, other):
        if isinstance(other, Number):
            return Number(int(self.value == other.value)).set_context(
                self.context
            ), None
        return None, Value.illegal_operation(self, other)

    def get_comparison_ne(self, other):
        if isinstance(other, Number):
            return Number(int(self.value != other.value)).set_context(
                self.context
            ), None
        return None, Value.illegal_operation(self, other)

    def get_comparison_lt(self, other):
        if isinstance(other, Number):
            return Number(int(self.value < other.value)).set_context(self.context), None
        return None, Value.illegal_operation(self, other)

    def get_comparison_gt(self, other):
        if isinstance(other, Number):
            return Number(int(self.value > other.value)).set_context(self.context), None
        return None, Value.illegal_operation(self, other)

    def get_comparison_lte(self, other):
        if isinstance(other, Number):
            return Number(int(self.value <= other.value)).set_context(
                self.context
            ), None
        return None, Value.illegal_operation(self, other)

    def get_comparison_gte(self, other):
        if isinstance(other, Number):
            return Number(int(self.value >= other.value)).set_context(
                self.context
            ), None
        return None, Value.illegal_operation(self, other)

    def anded_by(self, other):
        if isinstance(other, Number):
            return Number(int(self.value and other.value)).set_context(
                self.context
            ), None
        return None, Value.illegal_operation(self, other)

    def ored_by(self, other):
        if isinstance(other, Number):
            return Number(int(self.value or other.value)).set_context(
                self.context
            ), None
        return None, Value.illegal_operation(self, other)

    def notted(self):
        return Number(1 if self.value == 0 else 0).set_context(self.context), None

    def copy(self):
        c = Number(self.value)
        c.set_pos(self.pos_start, self.pos_end)
        c.set_context(self.context)
        return c

    def is_true(self):
        return self.value != 0

    def __str__(self):
        v = self.value
        if isinstance(v, float) and v == int(v):
            return str(int(v))
        return str(v)

    def __repr__(self):
        return self.__str__()


Number.null = Number(0)
Number.false = Number(0)
Number.true = Number(1)


class String(Value):
    def __init__(self, value):
        super().__init__()
        self.value = value

    def added_to(self, other):
        if isinstance(other, String):
            return String(self.value + other.value).set_context(self.context), None
        return None, Value.illegal_operation(self, other)

    def get_comparison_eq(self, other):
        if isinstance(other, String):
            return Number(int(self.value == other.value)).set_context(
                self.context
            ), None
        return None, Value.illegal_operation(self, other)

    def get_comparison_ne(self, other):
        if isinstance(other, String):
            return Number(int(self.value != other.value)).set_context(
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


class BaseFunction(Value):
    def __init__(self, name):
        super().__init__()
        self.name = name or "<anonymous>"

    def generate_new_context(self):
        new_context = Context(self.name, self.context, self.pos_start)
        new_context.symbol_table = SymbolTable(new_context.parent.symbol_table)
        return new_context

    def check_args(self, arg_names, args):
        res = RTResult()
        if len(args) > len(arg_names):
            return res.failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    f"{len(args) - len(arg_names)} too many arguments passed into '{self.name}'",
                    self.context,
                )
            )
        if len(args) < len(arg_names):
            return res.failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    f"{len(arg_names) - len(args)} too few arguments passed into '{self.name}'",
                    self.context,
                )
            )
        return res.success(None)

    def populate_args(self, arg_names, args, exec_ctx):
        for i in range(len(args)):
            arg_name = arg_names[i]
            arg_value = args[i]
            arg_value.set_context(exec_ctx)
            exec_ctx.symbol_table.set(arg_name, arg_value)

    def check_and_populate_args(self, arg_names, args, exec_ctx):
        res = RTResult()
        res.register(self.check_args(arg_names, args))
        if res.should_return():
            return res
        self.populate_args(arg_names, args, exec_ctx)
        return res.success(None)


class Function(BaseFunction):
    def __init__(self, name, body_node, param_names):
        super().__init__(name)
        self.body_node = body_node
        self.param_names = param_names

    def execute(self, args):
        res = RTResult()
        interpreter = Interpreter()
        exec_ctx = self.generate_new_context()

        res.register(self.check_and_populate_args(self.param_names, args, exec_ctx))
        if res.should_return():
            return res

        value = res.register(interpreter.visit(self.body_node, exec_ctx))
        if res.should_return() and res.func_return_value is None:
            return res

        ret_value = (
            res.func_return_value if res.func_return_value is not None else Number.null
        )
        return res.success(ret_value)

    def copy(self):
        c = Function(self.name, self.body_node, self.param_names)
        c.set_context(self.context)
        c.set_pos(self.pos_start, self.pos_end)
        return c

    def __repr__(self):
        return f"<function {self.name}>"


class BuiltInFunction(BaseFunction):
    print: ClassVar["BuiltInFunction"]
    input: ClassVar["BuiltInFunction"]
    rawpy: ClassVar["BuiltInFunction"]
    str_of: ClassVar["BuiltInFunction"]
    int_of: ClassVar["BuiltInFunction"]
    float_of: ClassVar["BuiltInFunction"]

    def __init__(self, name):
        super().__init__(name)

    def execute(self, args):
        res = RTResult()
        exec_ctx = self.generate_new_context()

        method_name = f"execute_{self.name}"
        method = getattr(self, method_name, self.no_visit_method)
        return_value = res.register(method(args, exec_ctx))

        if res.should_return():
            return res
        return res.success(return_value)

    def no_visit_method(self, node, context):
        raise Exception(f"No execute_{self.name} method defined")

    def copy(self):
        c = BuiltInFunction(self.name)
        c.set_context(self.context)
        c.set_pos(self.pos_start, self.pos_end)
        return c

    def __repr__(self):
        return f"<built-in {self.name}>"

    def execute_print(self, args, exec_ctx):
        output = "".join(str(a) for a in args)
        sys.stdout.write(output)
        sys.stdout.flush()
        return RTResult().success(Number.null)

    def execute_input(self, args, exec_ctx):
        if len(args) > 1:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "input() takes 0 or 1 arguments",
                    exec_ctx,
                )
            )
        prompt = str(args[0]) if args else ""
        text = input(prompt)
        return RTResult().success(String(text))

    def execute_rawpy(self, args, exec_ctx):
        if len(args) != 1 or not isinstance(args[0], String):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    'rawpy() expects exactly one string argument — rawpy("python code")',
                    exec_ctx,
                )
            )
        try:
            exec(args[0].value, {"__builtins__": __builtins__})
        except Exception as e:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    f"Python error in rawpy(): {e}",
                    exec_ctx,
                )
            )
        return RTResult().success(Number.null)

    def execute_str_of(self, args, exec_ctx):
        if len(args) != 1:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "str_of() takes exactly 1 argument",
                    exec_ctx,
                )
            )
        return RTResult().success(String(str(args[0])))

    def execute_int_of(self, args, exec_ctx):
        if len(args) != 1:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "int_of() takes exactly 1 argument",
                    exec_ctx,
                )
            )
        v = args[0]
        try:
            return RTResult().success(Number(int(float(v.value))))
        except Exception:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    f"Cannot convert '{v}' to int",
                    exec_ctx,
                )
            )

    def execute_float_of(self, args, exec_ctx):
        if len(args) != 1:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "float_of() takes exactly 1 argument",
                    exec_ctx,
                )
            )
        v = args[0]
        try:
            return RTResult().success(Number(float(v.value)))
        except Exception:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    f"Cannot convert '{v}' to float",
                    exec_ctx,
                )
            )


BuiltInFunction.print = BuiltInFunction("print")
BuiltInFunction.input = BuiltInFunction("input")
BuiltInFunction.rawpy = BuiltInFunction("rawpy")
BuiltInFunction.str_of = BuiltInFunction("str_of")
BuiltInFunction.int_of = BuiltInFunction("int_of")
BuiltInFunction.float_of = BuiltInFunction("float_of")

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


# context


class Context:
    def __init__(self, display_name, parent=None, parent_entry_pos=None):
        self.display_name = display_name
        self.parent = parent
        self.parent_entry_pos = parent_entry_pos
        self.symbol_table = None


# symbol table


class SymbolTable:
    def __init__(self, parent=None):
        self.symbols = {}
        self.constants = set()
        self.parent = parent

    def get(self, name):
        value = self.symbols.get(name, None)
        if value is None and self.parent:
            return self.parent.get(name)
        return value

    def set(self, name, value, is_const=False):
        self.symbols[name] = value
        if is_const:
            self.constants.add(name)

    def is_const(self, name):
        if name in self.constants:
            return True
        if self.parent:
            return self.parent.is_const(name)
        return False

    def remove(self, name):
        del self.symbols[name]


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

    def visit_BoolNode(self, node, context):
        val = Number(1 if node.value else 0)
        return RTResult().success(
            val.set_context(context).set_pos(node.pos_start, node.pos_end)
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

    def visit_VarDeclNode(self, node, context):
        res = RTResult()
        var_name = node.var_name_tok.value
        value = res.register(self.visit(node.value_node, context))
        if res.should_return():
            return res
        context.symbol_table.set(var_name, value, is_const=node.is_const)
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
        context.symbol_table.set(var_name, value)
        return res.success(value)

    def visit_BlockNode(self, node, context):
        res = RTResult()
        for stmt in node.statements:
            value = res.register(self.visit(stmt, context))
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
        elif op.matches(TT_KEYWORD, "is"):
            result, error = left.get_comparison_eq(right)
        elif op.type == TT_KEYWORD and op.value == "not is":
            result, error = left.get_comparison_ne(right)
        elif op.type == TT_LT:
            result, error = left.get_comparison_lt(right)
        elif op.type == TT_GT:
            result, error = left.get_comparison_gt(right)
        elif op.type == TT_LTE:
            result, error = left.get_comparison_lte(right)
        elif op.type == TT_GTE:
            result, error = left.get_comparison_gte(right)
        elif op.matches(TT_KEYWORD, "and"):
            result, error = left.anded_by(right)
        elif op.matches(TT_KEYWORD, "or"):
            result, error = left.ored_by(right)

        if error:
            return res.failure(error)
        return res.success(result.set_pos(node.pos_start, node.pos_end))

    def visit_UnaryOpNode(self, node, context):
        res = RTResult()
        value = res.register(self.visit(node.node, context))
        if res.should_return():
            return res

        error = None
        if node.op_tok.type == TT_MINUS:
            value, error = value.multed_by(Number(-1))
        elif node.op_tok.matches(TT_KEYWORD, "not"):
            value, error = value.notted()

        if error:
            return res.failure(error)
        return res.success(value.set_pos(node.pos_start, node.pos_end))

    def visit_IfNode(self, node, context):
        res = RTResult()
        condition = res.register(self.visit(node.condition_node, context))
        if res.should_return():
            return res

        if condition.is_true():
            value = res.register(self.visit(node.then_block, context))
            if res.should_return():
                return res
        elif node.else_block:
            value = res.register(self.visit(node.else_block, context))
            if res.should_return():
                return res

        return res.success(Number.null)

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

            if not should_break:
                upd_res = RTResult()
                upd_res.register(self.visit(node.update_node, for_ctx))
                if upd_res.error:
                    return upd_res

            if should_break:
                break

        return res.success(Number.null)

    def visit_FuncDefNode(self, node, context):
        res = RTResult()
        func_name = node.var_name_tok.value
        param_names = [p[1].value for p in node.param_toks]
        func_value = Function(func_name, node.body_block, param_names)
        func_value.set_context(context).set_pos(node.pos_start, node.pos_end)
        context.symbol_table.set(func_name, func_value)
        return res.success(func_value)

    def visit_CallNode(self, node, context):
        res = RTResult()
        args = []

        value_to_call = res.register(self.visit(node.node_to_call, context))
        if res.should_return():
            return res
        value_to_call = value_to_call.copy().set_pos(node.pos_start, node.pos_end)

        for arg_node in node.arg_nodes:
            args.append(res.register(self.visit(arg_node, context)))
            if res.should_return():
                return res

        return_value = res.register(value_to_call.execute(args))
        if res.should_return():
            return res
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
                f"Value of type '{type(obj).__name__}' does not support attribute access",
                context,
            )
        )

    def visit_RawPyBlockNode(self, node, context):
        res = RTResult()
        py_ns = {"__builtins__": __builtins__}
        tbl = context.symbol_table
        while tbl is not None:
            for name, val in tbl.symbols.items():
                if name not in py_ns:
                    if isinstance(val, Number):
                        py_ns[name] = val.value
                    elif isinstance(val, String):
                        py_ns[name] = val.value
            tbl = tbl.parent

        try:
            exec(textwrap.dedent(node.code), py_ns)
        except Exception as e:
            return res.failure(
                RTError(
                    node.pos_start,
                    node.pos_end,
                    f"Python error in rawpy block: {type(e).__name__}: {e}",
                    context,
                )
            )

        for name, val in py_ns.items():
            if name.startswith("__") or callable(val):
                continue
            if isinstance(val, bool):
                context.symbol_table.set(name, Number(1 if val else 0))
            elif isinstance(val, int):
                context.symbol_table.set(name, Number(val))
            elif isinstance(val, float):
                context.symbol_table.set(name, Number(val))
            elif isinstance(val, str):
                context.symbol_table.set(name, String(val))

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

        if not filename.endswith(".lynx"):
            filename += ".lynx"

        module_name = os.path.splitext(os.path.basename(filename))[0]

        file_val = global_symbol_table.get("__file__")
        base_dir = os.path.dirname(file_val.value) if file_val else ""
        filepath = os.path.join(base_dir, filename) if base_dir else filename
        if not os.path.exists(filepath):
            stdlib_path = os.path.join(STDLIB_DIR, filename)
            if os.path.exists(stdlib_path):
                filepath = stdlib_path

        try:
            with open(filepath, "r") as f:
                script = f.read()
        except FileNotFoundError:
            return res.failure(
                RTError(
                    node.pos_start,
                    node.pos_end,
                    f"Module \"{module_name}\" not found — checked '{filepath}' and stdlib/",
                    context,
                )
            )
        except Exception as e:
            return res.failure(
                RTError(
                    node.pos_start,
                    node.pos_end,
                    f'Failed to import "{filename}": {e}',
                    context,
                )
            )

        module_table = SymbolTable(global_symbol_table)
        module_table.set("true", Number.true)
        module_table.set("false", Number.false)
        module_table.set("print", BuiltInFunction.print)
        module_table.set("input", BuiltInFunction.input)
        module_table.set("rawpy", BuiltInFunction.rawpy)
        module_table.set("str_of", BuiltInFunction.str_of)
        module_table.set("int_of", BuiltInFunction.int_of)
        module_table.set("float_of", BuiltInFunction.float_of)

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

    def visit_ProgramNode(self, node, context):
        res = RTResult()

        for decl in node.globals_list:
            res.register(self.visit(decl, context))
            if res.error:
                return res

        res.register(self.visit(node.main_func, context))
        if res.error:
            return res

        if node.setup_func:
            setup_res = RTResult()
            setup_res.register(self.visit(node.setup_func.body_block, context))
            if setup_res.error:
                return setup_res

        main_fn = context.symbol_table.get("main")
        if main_fn:
            call_res = RTResult()
            call_res.register(main_fn.execute([]))
            if call_res.error:
                return call_res

        return res.success(Number.null)


# global symbol table

global_symbol_table = SymbolTable()
global_symbol_table.set("true", Number.true)
global_symbol_table.set("false", Number.false)
global_symbol_table.set("print", BuiltInFunction.print)
global_symbol_table.set("input", BuiltInFunction.input)
global_symbol_table.set("rawpy", BuiltInFunction.rawpy)
global_symbol_table.set("str_of", BuiltInFunction.str_of)
global_symbol_table.set("int_of", BuiltInFunction.int_of)
global_symbol_table.set("float_of", BuiltInFunction.float_of)

# run


def run(fn, text):
    lexer = Lexer(fn, text)
    tokens, error = lexer.make_tokens()
    if error:
        return None, error

    parser = Parser(tokens)
    ast = parser.parse()
    if ast.error:
        return None, ast.error

    interpreter = Interpreter()
    context = Context("<program>")
    context.symbol_table = global_symbol_table
    global_symbol_table.set("__file__", String(os.path.abspath(fn)))
    global_symbol_table.set("global", Namespace(global_symbol_table))

    result = interpreter.visit(ast.node, context)
    return result.value, result.error


def run_file(fn, text, symbol_table):
    lexer = Lexer(fn, text)
    tokens, error = lexer.make_tokens()
    if error:
        return error

    parser = Parser(tokens)
    ast = parser.parse(require_main=False)
    if ast.error:
        return ast.error

    interpreter = Interpreter()
    context = Context(f"<import:{os.path.basename(fn)}>")
    context.symbol_table = symbol_table
    symbol_table.set("__file__", String(os.path.abspath(fn)))

    node = ast.node

    for decl in node.globals_list:
        r = RTResult()
        r.register(interpreter.visit(decl, context))
        if r.error:
            return r.error

    if node.setup_func:
        r = RTResult()
        r.register(interpreter.visit(node.setup_func.body_block, context))
        if r.error:
            return r.error

    return None
