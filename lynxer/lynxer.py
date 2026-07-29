from __future__ import annotations
from strings_with_arrows import string_with_arrows
import string
import os
import sys
import textwrap
import pickle
from typing import ClassVar

# ── Bytecode constants ────────────────────────────────────────────────────────
BYTECODE_MAGIC   = b"LYNXC\x00"
BYTECODE_VERSION = 1

DIGITS = "0123456789"
STDLIB_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "stdlib")
LETTERS = string.ascii_letters
LETTERS_DIGITS = LETTERS + DIGITS

_cython_inline_fn = None


def _get_cython_inline():
    """Lazily import Cython's inline compiler (needs setuptools' distutils shim)."""
    global _cython_inline_fn
    if _cython_inline_fn is None:
        import setuptools  # noqa: F401 — patches distutils for Cython on py3.12+
        from Cython.Build.Inline import cython_inline

        _cython_inline_fn = cython_inline
    return _cython_inline_fn

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
TT_PLUSEQ = "PLUSEQ"
TT_MINUSEQ = "MINUSEQ"
TT_MULEQ = "MULEQ"
TT_DIVEQ = "DIVEQ"
TT_MODEQ = "MODEQ"
TT_AMP = "AMP"
TT_PIPE = "PIPE"
TT_CARET = "CARET"
TT_TILDE = "TILDE"
TT_SHL = "SHL"
TT_SHR = "SHR"
TT_RAWPY_BLOCK = "RAWPY_BLOCK"
TT_RAWPYX_BLOCK = "RAWPYX_BLOCK"
TT_LBRACKET = "LBRACKET"
TT_RBRACKET = "RBRACKET"
TT_EOF = "EOF"

TYPE_KEYWORDS = ["int", "float", "str", "bool", "any", "tuple"]

KEYWORDS = [
    "int", "float", "str", "bool", "any", "tuple",
    "global", "local", "const",
    "if", "else", "while", "for",
    "return", "import", "importAs", "importPy",
    "true", "false", "none",
    "and", "or", "not", "is",
    "vargroup",
    "try", "catch",
    "async", "await",
    "class",
    "break", "continue", "restart",
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
                tokens.append(self.make_string())
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
            elif self.current_char == "&":
                tokens.append(Token(TT_AMP, pos_start=self.pos))
                self.advance()
            elif self.current_char == "|":
                tokens.append(Token(TT_PIPE, pos_start=self.pos))
                self.advance()
            elif self.current_char == "^":
                tokens.append(Token(TT_CARET, pos_start=self.pos))
                self.advance()
            elif self.current_char == "~":
                tokens.append(Token(TT_TILDE, pos_start=self.pos))
                self.advance()
            elif self.current_char == "*":
                pos_start = self.pos.copy()
                self.advance()
                if self.current_char == "=":
                    self.advance()
                    tokens.append(Token(TT_MULEQ, pos_start=pos_start, pos_end=self.pos))
                else:
                    tokens.append(Token(TT_MUL, pos_start=pos_start, pos_end=self.pos))
            elif self.current_char == "/":
                pos_start = self.pos.copy()
                self.advance()
                if self.current_char == "=":
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
            "e": "\033",   # ESC — for ANSI escape sequences
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


class IterateNode:
    # iterate(count) { body } — run body N times
    def __init__(self, count_node, body_block, pos_start, pos_end):
        self.count_node = count_node
        self.body_block = body_block
        self.pos_start = pos_start
        self.pos_end = pos_end


class FuncDefNode:
    def __init__(
        self, kind_tok, var_name_tok, param_toks, body_block, pos_start, pos_end,
        is_async=False
    ):
        self.kind_tok = kind_tok
        self.var_name_tok = var_name_tok
        self.param_toks = param_toks
        self.body_block = body_block
        self.pos_start = pos_start
        self.pos_end = pos_end
        self.is_async = is_async


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


class ImportAsNode:
    """importAs("module", "alias");  — import module and bind it under a custom name."""
    def __init__(self, filename_tok, alias_tok, pos_start, pos_end):
        self.filename_tok = filename_tok
        self.alias_tok = alias_tok
        self.pos_start = pos_start
        self.pos_end = pos_end


class ImportPyNode:
    """importPy(){"os", "sys", "json"};
    Pre-imports Python modules into a shared global dict so rawPy / rawPyx
    blocks can use them without writing 'import x' every time.
    Only valid inside global setup(){}.
    """
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


class BreakNode:
    def __init__(self, pos_start, pos_end):
        self.pos_start = pos_start
        self.pos_end = pos_end


class ContinueNode:
    def __init__(self, pos_start, pos_end):
        self.pos_start = pos_start
        self.pos_end = pos_end


class ProgramNode:
    def __init__(self, setup_func, globals_list, main_func, pos_start, pos_end):
        self.setup_func = setup_func
        self.globals_list = globals_list
        self.main_func = main_func
        self.pos_start = pos_start
        self.pos_end = pos_end


class VarGroupDeclNode:
    # fields: list of (type_str, name_tok, value_node, is_const)
    def __init__(self, name_tok, fields, pos_start, pos_end, is_const=False):
        self.name_tok = name_tok
        self.fields = fields
        self.is_const = is_const
        self.pos_start = pos_start
        self.pos_end = pos_end


class DotAssignNode:
    """type obj.field = value  (typed dot-path assignment into a vargroup)"""
    def __init__(self, obj_node, attr_name_tok, value_node, decl_type, pos_start, pos_end):
        self.obj_node = obj_node
        self.attr_name_tok = attr_name_tok
        self.value_node = value_node
        self.decl_type = decl_type  # type keyword given at the assignment site
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
    """try { body } catch { handler }
       try { body } catch(str varname) { handler }

    If the try block raises a runtime error the handler runs instead.
    When catch_var_tok is given, the error message is bound as a str in
    the handler's scope under that name.
    """
    def __init__(self, try_block, catch_var_tok, catch_block, pos_start, pos_end):
        self.try_block = try_block          # BlockNode
        self.catch_var_tok = catch_var_tok  # Token (identifier) or None
        self.catch_block = catch_block      # BlockNode
        self.pos_start = pos_start
        self.pos_end = pos_end


class ClassDefNode:
    """class ClassName { [const] type field = value; ... def method(params){} ... }

    field_defs : list of (type_str, name_tok, value_node, is_const)
    method_nodes: list of FuncDefNode  (kind == 'def')
    """
    def __init__(self, name_tok, field_defs, method_nodes, pos_start, pos_end):
        self.name_tok = name_tok
        self.field_defs = field_defs
        self.method_nodes = method_nodes
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
        self._loop_depth = 0       # tracks nesting depth of for/while/iterate
        self._in_global_func = False  # True when parsing a non-setup global body
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
        # For ordering enforcement
        setup_seen = False
        main_seen = False
        any_other_seen = False  # any global func or class that is not setup/main

        while self.current_tok.type != TT_EOF:
            is_func_kw = (
                self.current_tok.matches(TT_KEYWORD, "global")
                or (
                    self.current_tok.type == TT_IDENTIFIER
                    and self.current_tok.value == "global"
                    and self.peek(1) is not None
                    and self.peek(1).type == TT_IDENTIFIER
                )
            )
            if is_func_kw:
                func_name_tok = self.peek(1)

                if (
                    func_name_tok
                    and func_name_tok.type == TT_IDENTIFIER
                    and func_name_tok.value == "setup"
                ):
                    # setup must be the very first declaration
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
                    # main must come after setup (and all other globals)
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
                    # Regular global function — must come after setup and before main
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
                # Classes must come after setup and before main
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

        if main_func is None and self._require_main:
            return res.failure(
                InvalidSyntaxError(
                    self.current_tok.pos_start,
                    self.current_tok.pos_end,
                    "Program requires a 'global main(){}' function as the last declaration. "
                    "Add it after all other global functions and classes.",
                )
            )

        pos_end = self.current_tok.pos_end.copy()
        return res.success(
            ProgramNode(setup_func, globals_list, main_func, pos_start, pos_end)
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
        _is_global_def = kind_tok.value == "global" or (
            kind_tok.type == TT_IDENTIFIER and kind_tok.value == "global"
        )

        # Manage _in_global_func flag: True only while inside a non-setup global body
        prev_in_global_func = self._in_global_func
        if _is_global_def and not is_setup:
            self._in_global_func = True
        else:
            # local functions and setup never allow nested globals
            self._in_global_func = False

        body = res.register(self.parse_block(in_setup=is_setup, allow_local_funcs=True))
        self._in_global_func = prev_in_global_func  # restore
        if res.error:
            return res

        pos_end = self.current_tok.pos_end.copy()
        return res.success(
            FuncDefNode(kind_tok, name_tok, param_toks, body, pos_start, pos_end,
                        is_async=is_async)
        )

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

        field_defs = []    # list of (type_str, name_tok, value_node, is_const)
        method_nodes = []  # list of FuncDefNode (kind == 'def')

        while self.current_tok.type != TT_RBRACE and self.current_tok.type != TT_EOF:
            if self.current_tok.matches(TT_KEYWORD, "local"):
                method_node = res.register(self.parse_func_def())
                if res.error:
                    return res
                method_nodes.append(method_node)
            elif self.current_tok.matches(TT_KEYWORD, "const") or self.is_type_keyword():
                is_const = False
                if self.current_tok.matches(TT_KEYWORD, "const"):
                    is_const = True
                    res.register_advancement()
                    self.advance()
                if not self.is_type_keyword():
                    return res.failure(InvalidSyntaxError(
                        self.current_tok.pos_start, self.current_tok.pos_end,
                        "Expected a type keyword (int, float, str, bool, any, tuple) for class field"
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

        # local.funcName(...); — local namespace call as a standalone statement
        if (
            self.current_tok.matches(TT_KEYWORD, "local")
        ) and self.peek(1) is not None and self.peek(1).type == TT_DOT:
            expr = res.register(self.parse_expr())
            if res.error:
                return res
            if self.current_tok.type != TT_SEMICOLON:
                return res.failure(
                    InvalidSyntaxError(
                        expr.pos_end,
                        self.current_tok.pos_start,
                        "Missing ';' after statement",
                    )
                )
            res.register_advancement()
            self.advance()
            return res.success(expr)

        if allow_local_funcs and self.current_tok.matches(TT_KEYWORD, "local"):
            node = res.register(self.parse_func_def())
            if res.error:
                return res
            return res.success(node)

        # 'local funcName(){}' outside a function body — helpful error
        if self.current_tok.matches(TT_KEYWORD, "local"):
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
                if not allow_local_funcs:
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

        # global.funcName(...); — namespace call as a standalone statement
        if (
            self.current_tok.matches(TT_KEYWORD, "global")
            or (self.current_tok.type == TT_IDENTIFIER and self.current_tok.value == "global")
        ) and self.peek(1) is not None and self.peek(1).type == TT_DOT:
            expr = res.register(self.parse_expr())
            if res.error:
                return res
            if self.current_tok.type != TT_SEMICOLON:
                return res.failure(
                    InvalidSyntaxError(
                        expr.pos_end,
                        self.current_tok.pos_start,
                        "Missing ';' after statement",
                    )
                )
            res.register_advancement()
            self.advance()
            return res.success(expr)

        if self.current_tok.matches(TT_KEYWORD, "global") or (
            self.current_tok.type == TT_IDENTIFIER
            and self.current_tok.value == "global"
            and self.peek(1) is not None
            and self.peek(1).type == TT_IDENTIFIER
        ):
            if self._in_global_func:
                # Nested global definition inside a non-setup global body — allowed
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

        if self.current_tok.matches(TT_KEYWORD, "for"):
            node = res.register(self.parse_for())
            if res.error:
                return res
            return res.success(node)

        if self.current_tok.matches(TT_KEYWORD, "try"):
            node = res.register(self.parse_try_catch())
            if res.error:
                return res
            return res.success(node)

        if self.current_tok.matches(TT_KEYWORD, "await"):
            # await used as a standalone statement expression: await someAsyncCall();
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
                    "'break' is only valid inside a for, while, or iterate loop",
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
                    f"'{kw}' is only valid inside a for, while, or iterate loop",
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

        if self.is_type_keyword():
            # typed dot-assignment: int vg.field = value;
            # Also handles: int global.class.Name.field = value;
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
            # typed dot-assignment with vargroup type: vargroup vg.field = val;
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

            if next_tok and next_tok.type in (TT_EQ, TT_PLUSEQ, TT_MINUSEQ, TT_MULEQ, TT_DIVEQ, TT_MODEQ):
                node = res.register(self.parse_assign())
                if res.error:
                    return res
                return res.success(node)

            expr = res.register(self.parse_expr())
            if res.error:
                return res

            # Untyped dot-assignment is not allowed — tell the user to add a type
            if isinstance(expr, DotAccessNode) and self.current_tok.type == TT_EQ:
                return res.failure(
                    InvalidSyntaxError(
                        expr.pos_start,
                        self.current_tok.pos_end,
                        "vargroup field assignment requires an explicit type. "
                        "Use: type vargroup.field = value;  e.g.  int player.coins = 500;",
                    )
                )

            if self.current_tok.type != TT_SEMICOLON:
                return res.failure(
                    InvalidSyntaxError(
                        expr.pos_end,
                        self.current_tok.pos_start,
                        "Missing ';' after this statement",
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
        res.register_advancement()
        self.advance()

        # const vargroup name = [...];
        if self.current_tok.matches(TT_KEYWORD, "vargroup"):
            node = res.register(self.parse_vargroup_decl())
            if res.error:
                return res
            node.is_const = True
            return res.success(node)

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

        return res.success(VarAssignNode(name_tok, value))

    # ------------------------------------------------------------------ vargroup

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
        body = res.register(self.parse_block())
        self._loop_depth -= 1
        if res.error:
            return res

        return res.success(IterateNode(count_node, body, pos_start, body.pos_end))

    def parse_vargroup_field(self):
        """Parse one field inside a vargroup body: type name = value  OR  vargroup name = [...]"""
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
                            "Expected ',' or ']' in nested vargroup body",
                        )
                    )

            pos_end = self.current_tok.pos_end.copy()
            res.register_advancement()
            self.advance()  # consume ']'

            nested_node = VarGroupDeclNode(name_tok, nested_fields, pos_start, pos_end)
            return res.success(("vargroup", name_tok, nested_node, False))

        # regular field: [const] type name = expr
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

    def parse_vargroup_decl(self):
        """Parse: vargroup name = [ fields... ]; """
        res = ParseResult()
        pos_start = self.current_tok.pos_start.copy()
        res.register_advancement()
        self.advance()  # consume 'vargroup'

        if self.current_tok.type != TT_IDENTIFIER:
            return res.failure(
                InvalidSyntaxError(
                    self.current_tok.pos_start,
                    self.current_tok.pos_end,
                    "Expected vargroup name",
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
                    "Expected '=' after vargroup name",
                )
            )
        res.register_advancement()
        self.advance()

        if self.current_tok.type != TT_LBRACKET:
            return res.failure(
                InvalidSyntaxError(
                    self.current_tok.pos_start,
                    self.current_tok.pos_end,
                    "Expected '[' to open vargroup body",
                )
            )
        res.register_advancement()
        self.advance()

        fields = []
        while self.current_tok.type != TT_RBRACKET:
            if self.current_tok.type == TT_EOF:
                return res.failure(
                    InvalidSyntaxError(
                        self.current_tok.pos_start,
                        self.current_tok.pos_end,
                        "Expected ']' to close vargroup",
                    )
                )
            field = res.register(self.parse_vargroup_field())
            if res.error:
                return res
            fields.append(field)
            if self.current_tok.type == TT_COMMA:
                res.register_advancement()
                self.advance()
            elif self.current_tok.type != TT_RBRACKET:
                return res.failure(
                    InvalidSyntaxError(
                        self.current_tok.pos_start,
                        self.current_tok.pos_end,
                        "Expected ',' or ']' in vargroup body",
                    )
                )

        res.register_advancement()
        self.advance()  # consume ']'

        if self.current_tok.type != TT_SEMICOLON:
            return res.failure(
                InvalidSyntaxError(
                    self.current_tok.pos_start,
                    self.current_tok.pos_end,
                    "Expected ';' after vargroup declaration",
                )
            )
        pos_end = self.current_tok.pos_end.copy()
        res.register_advancement()
        self.advance()

        return res.success(VarGroupDeclNode(name_tok, fields, pos_start, pos_end))

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

        # first argument: path expression (vargroup variable / dot chain)
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

        # second argument: field declaration  — either "type name = value" or "vargroup name = [...]"
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

    # ------------------------------------------------------------------ /vargroup

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
        """Parse:  importPy(){"os", "sys", "json"};
        The () is empty; the {} holds a comma-separated list of string module names.
        """
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

        self._loop_depth += 1
        body = res.register(self.parse_block(allow_local_funcs=True))
        self._loop_depth -= 1
        if res.error:
            return res

        pos_end = self.current_tok.pos_end.copy()
        return res.success(WhileNode(condition, body, pos_start, pos_end))

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

        self._loop_depth += 1
        body = res.register(self.parse_block(allow_local_funcs=True))
        self._loop_depth -= 1
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
        left = res.register(self.parse_bitwise_or_expr())
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
            right = res.register(self.parse_bitwise_or_expr())
            if res.error:
                return res
            return res.success(BinOpNode(left, op_tok, right))

        if self.current_tok.matches(TT_KEYWORD, "is"):
            op_tok = self.current_tok
            res.register_advancement()
            self.advance()
            right = res.register(self.parse_bitwise_or_expr())
            if res.error:
                return res
            return res.success(BinOpNode(left, op_tok, right))

        if self.current_tok.type in (TT_LT, TT_GT, TT_LTE, TT_GTE):
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
        while self.current_tok.type == TT_PIPE:
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
        while self.current_tok.type == TT_CARET:
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
        while self.current_tok.type == TT_AMP:
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

        if tok.type in (TT_PLUS, TT_MINUS, TT_TILDE):
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
                # Allow 'class' keyword and type keywords (str, int, float, bool, any, tuple)
                # as attribute names so embedPy.str(), embedPy.int(), etc. work.
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

        elif tok.matches(TT_KEYWORD, "none"):
            res.register_advancement()
            self.advance()
            return res.success(NoneNode(tok))

        elif (tok.type == TT_IDENTIFIER
              or tok.matches(TT_KEYWORD, "global")
              or tok.matches(TT_KEYWORD, "local")):
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
        while self.current_tok.type != TT_RPAREN and self.current_tok.type != TT_EOF:
            if self.is_type_keyword() and self.peek(1) and self.peek(1).type == TT_IDENTIFIER:
                type_tok = self.current_tok
                res.register_advancement(); self.advance()
                pname_tok = self.current_tok
                res.register_advancement(); self.advance()
                param_toks.append((type_tok, pname_tok))
            elif self.current_tok.type == TT_IDENTIFIER:
                param_toks.append((None, self.current_tok))
                res.register_advancement(); self.advance()
            else:
                return res.failure(InvalidSyntaxError(
                    self.current_tok.pos_start, self.current_tok.pos_end,
                    "Expected parameter name",
                ))
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

    def bit_anded_by(self, other):
        return None, self.illegal_operation(other)

    def bit_ored_by(self, other):
        return None, self.illegal_operation(other)

    def bit_xored_by(self, other):
        return None, self.illegal_operation(other)

    def bit_notted(self):
        return None, self.illegal_operation()

    def shifted_left_by(self, other):
        return None, self.illegal_operation(other)

    def shifted_right_by(self, other):
        return None, self.illegal_operation(other)

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
    if isinstance(v, String):
        return "str"
    if isinstance(v, LynxTuple):
        return "tuple"
    if isinstance(v, List):
        return "list"
    if isinstance(v, VarGroup):
        return "vargroup"
    if isinstance(v, (Function, BuiltInFunction)):
        return "function"
    return "any"


NUMERIC_TYPES = {"int", "float"}


def type_matches(declared_type, value):
    if declared_type in (None, "any"):
        return True
    actual = value_type_name(value)
    if declared_type in NUMERIC_TYPES:
        return actual in NUMERIC_TYPES
    if declared_type == "vargroup":
        return actual == "vargroup"
    return actual == declared_type


class BaseFunction(Value):
    def __init__(self, name):
        super().__init__()
        self.name = name or "<anonymous>"

    def generate_new_context(self):
        new_context = Context(self.name, self.context, self.pos_start)
        parent_table = new_context.parent.symbol_table if new_context.parent else None
        new_context.symbol_table = SymbolTable(parent_table)
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

    def check_and_populate_args(self, arg_names, args, exec_ctx, arg_types=None):
        res = RTResult()
        res.register(self.check_args(arg_names, args))
        if res.should_return():
            return res
        err = self.populate_args(arg_names, args, exec_ctx, arg_types)
        if err:
            return err
        return res.success(None)


class Function(BaseFunction):
    def __init__(self, name, body_node, param_names, param_types=None, is_global=False):
        super().__init__(name)
        self.body_node = body_node
        self.param_names = param_names
        self.param_types = param_types or [None] * len(param_names)
        self.is_global = is_global
        self.inner_locals = {}   # nested 'local' functions defined inside this function
        self.inner_globals = {}  # nested 'global' functions defined inside this global
        self.global_path = None  # list like ['a', 'b'] for global.a.b; None for locals

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

    def execute(self, args):
        res = RTResult()
        interpreter = SHARED_INTERPRETER
        exec_ctx = self.generate_new_context()
        exec_ctx.current_function = self  # track for inner-local/inner-global registration
        if self.is_global:
            exec_ctx.current_global_path = self.global_path  # for hierarchy enforcement

        res.register(
            self.check_and_populate_args(
                self.param_names, args, exec_ctx, self.param_types
            )
        )
        if res.should_return():
            return res

        # Expose 'local' namespace so local.funcName() calls work
        exec_ctx.symbol_table.set("local", LocalNamespace(exec_ctx.symbol_table))

        res.register(interpreter.visit(self.body_node, exec_ctx))
        if res.should_return() and res.func_return_value is None:
            return res

        ret_value = (
            res.func_return_value if res.func_return_value is not None else Number.null
        )
        return res.success(ret_value)

    def copy(self):
        c = Function(self.name, self.body_node, self.param_names, self.param_types, self.is_global)
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

    def __init__(self, name, body_node, param_names, param_types=None, is_global=False):
        super().__init__(name)
        self.body_node = body_node
        self.param_names = param_names
        self.param_types = param_types or [None] * len(param_names)
        self.is_global = is_global
        self.inner_locals = {}   # nested 'local' functions defined inside this function
        self.inner_globals = {}  # nested 'global' functions defined inside this global
        self.global_path = None  # list like ['a', 'b'] for global.a.b

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

    def execute(self, args):
        res = RTResult()
        exec_ctx = self.generate_new_context()
        exec_ctx.current_function = self  # track for inner-local/inner-global registration
        if self.is_global:
            exec_ctx.current_global_path = self.global_path

        # Validate / populate args synchronously — arg errors are sync failures
        res.register(
            self.check_and_populate_args(
                self.param_names, args, exec_ctx, self.param_types
            )
        )
        if res.should_return():
            return res

        # Expose 'local' namespace so local.funcName() calls work
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
        c = AsyncFunction(self.name, self.body_node, self.param_names, self.param_types, self.is_global)
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
        # Coroutines cannot be duplicated; return self (single-use)
        return self

    def __repr__(self):
        return "<coroutine>"


class BuiltInFunction(BaseFunction):
    print: ClassVar["BuiltInFunction"]
    input: ClassVar["BuiltInFunction"]
    inputln: ClassVar["BuiltInFunction"]
    rawPy: ClassVar["BuiltInFunction"]
    rawPyx: ClassVar["BuiltInFunction"]
    strOf: ClassVar["BuiltInFunction"]
    intOf: ClassVar["BuiltInFunction"]
    floatOf: ClassVar["BuiltInFunction"]
    returnType: ClassVar["BuiltInFunction"]
    returnLength: ClassVar["BuiltInFunction"]
    seqFromTo: ClassVar["BuiltInFunction"]
    range: ClassVar["BuiltInFunction"]
    cleanRawPyxCache: ClassVar["BuiltInFunction"]
    # list built-ins
    splitStr: ClassVar["BuiltInFunction"]
    listFlatten: ClassVar["BuiltInFunction"]
    listUnique: ClassVar["BuiltInFunction"]
    listJsonArray: ClassVar["BuiltInFunction"]
    listJsonObject: ClassVar["BuiltInFunction"]
    listPush: ClassVar["BuiltInFunction"]
    listPop: ClassVar["BuiltInFunction"]
    listGet: ClassVar["BuiltInFunction"]
    listSet: ClassVar["BuiltInFunction"]
    listSlice: ClassVar["BuiltInFunction"]
    listContains: ClassVar["BuiltInFunction"]
    listJoin: ClassVar["BuiltInFunction"]
    listIndex: ClassVar["BuiltInFunction"]
    listRemove: ClassVar["BuiltInFunction"]
    anyOf: ClassVar["BuiltInFunction"]
    allOf: ClassVar["BuiltInFunction"]
    sumOf: ClassVar["BuiltInFunction"]
    sortList: ClassVar["BuiltInFunction"]
    reverseList: ClassVar["BuiltInFunction"]
    listMin: ClassVar["BuiltInFunction"]
    listMax: ClassVar["BuiltInFunction"]
    asyncRun: ClassVar["BuiltInFunction"]
    asyncGather: ClassVar["BuiltInFunction"]
    asyncSleep: ClassVar["BuiltInFunction"]
    # tuple built-ins
    tupleCreate: ClassVar["BuiltInFunction"]
    tupleGet: ClassVar["BuiltInFunction"]
    tupleLen: ClassVar["BuiltInFunction"]
    tupleContains: ClassVar["BuiltInFunction"]
    tupleIndex: ClassVar["BuiltInFunction"]
    tupleSlice: ClassVar["BuiltInFunction"]
    tupleToList: ClassVar["BuiltInFunction"]
    listToTuple: ClassVar["BuiltInFunction"]
    tupleConcat: ClassVar["BuiltInFunction"]
    tupleCount: ClassVar["BuiltInFunction"]
    tupleFirst: ClassVar["BuiltInFunction"]
    tupleLast: ClassVar["BuiltInFunction"]
    tupleJsonArray: ClassVar["BuiltInFunction"]

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

    def execute_println(self, args, exec_ctx):
        output = "".join(str(a) for a in args)
        sys.stdout.write(output + "\n")
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
    
    def execute_inputln(self, args, exec_ctx):
        if len(args) > 1:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "inputln() takes 0 or 1 arguments",
                    exec_ctx,
                )
            )
        prompt = str(args[0]) if args else ""
        text = input(prompt)
        return RTResult().success(String(text + "\n"))

    def execute_rawPy(self, args, exec_ctx):
        if len(args) != 1 or not isinstance(args[0], String):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    'rawPy() expects exactly one string argument — rawPy("python code")',
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
                    f"Python error in rawPy(): {e}",
                    exec_ctx,
                )
            )
        return RTResult().success(Number.null)

    def execute_strOf(self, args, exec_ctx):
        if len(args) != 1:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "strOf() takes exactly 1 argument",
                    exec_ctx,
                )
            )
        return RTResult().success(String(str(args[0])))

    def execute_intOf(self, args, exec_ctx):
        if len(args) != 1:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "intOf() takes exactly 1 argument",
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

    def execute_floatOf(self, args, exec_ctx):
        if len(args) != 1:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "floatOf() takes exactly 1 argument",
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

    def execute_rawPyx(self, args, exec_ctx):
        if len(args) != 1 or not isinstance(args[0], String):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    'rawPyx() expects exactly one string argument — rawPyx("cython code")',
                    exec_ctx,
                )
            )
        try:
            cython_inline = _get_cython_inline()
            cy_locals = {}
            cython_inline(args[0].value, locals=cy_locals, globals=cy_locals, quiet=True)
        except Exception as e:  # noqa: F841 — cy_locals unused for the string form
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    f"Cython error in rawPyx(): {type(e).__name__}: {e}",
                    exec_ctx,
                )
            )
        return RTResult().success(Number.null)

    def execute_returnType(self, args, exec_ctx):
        if len(args) != 1:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "returnType() takes exactly 1 argument",
                    exec_ctx,
                )
            )
        return RTResult().success(String(value_type_name(args[0])))

    def execute_returnLength(self, args, exec_ctx):
        if len(args) != 1:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "returnLength() takes exactly 1 argument",
                    exec_ctx,
                )
            )
        v = args[0]
        if isinstance(v, String):
            return RTResult().success(Number(len(v.value)))
        if isinstance(v, (List, LynxTuple)):
            return RTResult().success(Number(len(v.elements)))
        return RTResult().failure(
            RTError(
                self.pos_start,
                self.pos_end,
                f"returnLength() does not support values of type '{type(v).__name__}'",
                exec_ctx,
            )
        )

    def execute_seqFromTo(self, args, exec_ctx):
        if len(args) != 3 or not all(isinstance(a, Number) for a in args):
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "seqFromTo() expects exactly 3 numeric arguments — seqFromTo(start, stop, step)",
                    exec_ctx,
                )
            )
        start, stop, step = (int(a.value) for a in args)
        if step == 0:
            return RTResult().failure(
                RTError(
                    self.pos_start,
                    self.pos_end,
                    "seqFromTo() step cannot be 0",
                    exec_ctx,
                )
            )
        elements = [Number(n).set_context(exec_ctx) for n in range(start, stop, step)]
        return RTResult().success(List(elements))

    def execute_range(self, args, exec_ctx):
        """range(stop)  or  range(start, stop)  or  range(start, stop, step)
        start is included, stop is excluded, step defaults to 1.
        Mirrors Python's range() semantics."""
        if not args or len(args) > 3 or not all(isinstance(a, Number) for a in args):
            return RTResult().failure(RTError(
                self.pos_start, self.pos_end,
                "range() expects 1, 2, or 3 integer arguments: "
                "range(stop), range(start, stop), or range(start, stop, step)",
                exec_ctx,
            ))
        if len(args) == 1:
            start, stop, step = 0, int(args[0].value), 1
        elif len(args) == 2:
            start, stop, step = int(args[0].value), int(args[1].value), 1
        else:
            start, stop, step = int(args[0].value), int(args[1].value), int(args[2].value)
        if step == 0:
            return RTResult().failure(RTError(
                self.pos_start, self.pos_end,
                "range() step cannot be 0",
                exec_ctx,
            ))
        elements = [Number(n).set_context(exec_ctx) for n in range(start, stop, step)]
        return RTResult().success(List(elements))

    def execute_cleanRawPyxCache(self, args, exec_ctx):
        import shutil
        import os
        cache_dir = os.path.expanduser("~/.cython/inline")
        try:
            if os.path.isdir(cache_dir):
                shutil.rmtree(cache_dir)
        except Exception as e:
            return RTResult().failure(RTError(
                self.pos_start, self.pos_end,
                f"cleanRawPyxCache() failed: {e}",
                exec_ctx,
            ))
        return RTResult().success(Number.null)

    # ── list built-ins ────────────────────────────────────────────────────────

    def execute_listJsonArray(self, args, exec_ctx):
        import json as _json
        if len(args) != 1 or not isinstance(args[0], List):
            return RTResult().failure(RTError(
                self.pos_start, self.pos_end,
                "listJsonArray(list) expects a list",
                exec_ctx,
            ))
        try:
            items = [e.value if isinstance(e, (Number, String)) else str(e)
                     for e in args[0].elements]
            return RTResult().success(String(_json.dumps(items)))
        except Exception as e:
            return RTResult().failure(RTError(
                self.pos_start, self.pos_end,
                f"listJsonArray() failed: {e}",
                exec_ctx,
            ))

    def execute_listJsonObject(self, args, exec_ctx):
        import json as _json
        if len(args) != 1 or not isinstance(args[0], List):
            return RTResult().failure(RTError(
                self.pos_start, self.pos_end,
                "listJsonObject(list) expects a flat key/value list",
                exec_ctx,
            ))
        els = args[0].elements
        if len(els) % 2 != 0:
            return RTResult().failure(RTError(
                self.pos_start, self.pos_end,
                "listJsonObject() requires an even-length list (key, value, key, value, ...)",
                exec_ctx,
            ))
        try:
            obj = {}
            for i in range(0, len(els), 2):
                k = els[i].value if isinstance(els[i], (Number, String)) else str(els[i])
                v = els[i + 1].value if isinstance(els[i + 1], (Number, String)) else str(els[i + 1])
                obj[str(k)] = v
            return RTResult().success(String(_json.dumps(obj)))
        except Exception as e:
            return RTResult().failure(RTError(
                self.pos_start, self.pos_end,
                f"listJsonObject() failed: {e}",
                exec_ctx,
            ))

    def execute_splitStr(self, args, exec_ctx):
        if (len(args) != 2 or not isinstance(args[0], String)
                or not isinstance(args[1], String)):
            return RTResult().failure(RTError(
                self.pos_start, self.pos_end,
                "splitStr(str, sep) expects two string arguments",
                exec_ctx,
            ))
        parts = args[0].value.split(args[1].value)
        elements = [String(p).set_context(exec_ctx) for p in parts]
        return RTResult().success(List(elements))

    def execute_listFlatten(self, args, exec_ctx):
        if len(args) != 1 or not isinstance(args[0], List):
            return RTResult().failure(RTError(
                self.pos_start, self.pos_end,
                "listFlatten(list) expects a list",
                exec_ctx,
            ))
        flat = []
        for el in args[0].elements:
            if isinstance(el, List):
                flat.extend(el.elements)
            else:
                flat.append(el)
        return RTResult().success(List(flat))

    def execute_listUnique(self, args, exec_ctx):
        if len(args) != 1 or not isinstance(args[0], List):
            return RTResult().failure(RTError(
                self.pos_start, self.pos_end,
                "listUnique(list) expects a list",
                exec_ctx,
            ))
        seen_strs: list[str] = []
        unique_els = []
        for el in args[0].elements:
            s = str(el)
            if s not in seen_strs:
                seen_strs.append(s)
                unique_els.append(el)
        return RTResult().success(List(unique_els))

    def execute_listPush(self, args, exec_ctx):
        if len(args) != 2 or not isinstance(args[0], List):
            return RTResult().failure(RTError(
                self.pos_start, self.pos_end,
                "listPush(list, item) expects a list and a value",
                exec_ctx,
            ))
        new_elements = list(args[0].elements) + [args[1]]
        return RTResult().success(List(new_elements))

    def execute_listPop(self, args, exec_ctx):
        if len(args) != 1 or not isinstance(args[0], List):
            return RTResult().failure(RTError(
                self.pos_start, self.pos_end,
                "listPop(list) expects a list",
                exec_ctx,
            ))
        if not args[0].elements:
            return RTResult().failure(RTError(
                self.pos_start, self.pos_end,
                "listPop() called on an empty list",
                exec_ctx,
            ))
        return RTResult().success(args[0].elements.pop())

    def execute_listGet(self, args, exec_ctx):
        if len(args) != 2 or not isinstance(args[0], List) or not isinstance(args[1], Number):
            return RTResult().failure(RTError(
                self.pos_start, self.pos_end,
                "listGet(list, idx) expects a list and an integer index",
                exec_ctx,
            ))
        lst = args[0]
        idx = int(args[1].value)
        if idx < -len(lst.elements) or idx >= len(lst.elements):
            return RTResult().failure(RTError(
                self.pos_start, self.pos_end,
                f"listGet() index {idx} out of range for list of length {len(lst.elements)}",
                exec_ctx,
            ))
        return RTResult().success(lst.elements[idx])

    def execute_listSet(self, args, exec_ctx):
        if len(args) != 3 or not isinstance(args[0], List) or not isinstance(args[1], Number):
            return RTResult().failure(RTError(
                self.pos_start, self.pos_end,
                "listSet(list, idx, val) expects a list, an integer index, and a value",
                exec_ctx,
            ))
        lst = args[0]
        idx = int(args[1].value)
        if idx < -len(lst.elements) or idx >= len(lst.elements):
            return RTResult().failure(RTError(
                self.pos_start, self.pos_end,
                f"listSet() index {idx} out of range for list of length {len(lst.elements)}",
                exec_ctx,
            ))
        new_elements = list(lst.elements)
        new_elements[idx] = args[2]
        return RTResult().success(List(new_elements))

    def execute_listSlice(self, args, exec_ctx):
        if (len(args) != 3 or not isinstance(args[0], List)
                or not isinstance(args[1], Number) or not isinstance(args[2], Number)):
            return RTResult().failure(RTError(
                self.pos_start, self.pos_end,
                "listSlice(list, start, stop) expects a list and two integer indices",
                exec_ctx,
            ))
        start = int(args[1].value)
        stop = int(args[2].value)
        return RTResult().success(List(args[0].elements[start:stop]))

    def execute_listContains(self, args, exec_ctx):
        if len(args) != 2 or not isinstance(args[0], List):
            return RTResult().failure(RTError(
                self.pos_start, self.pos_end,
                "listContains(list, item) expects a list and a value",
                exec_ctx,
            ))
        target = str(args[1])
        found = any(str(e) == target for e in args[0].elements)
        return RTResult().success(Number(1 if found else 0, is_bool=True))

    def execute_listJoin(self, args, exec_ctx):
        if len(args) != 2 or not isinstance(args[0], List) or not isinstance(args[1], String):
            return RTResult().failure(RTError(
                self.pos_start, self.pos_end,
                "listJoin(list, sep) expects a list and a string separator",
                exec_ctx,
            ))
        sep = args[1].value
        result = sep.join(str(e) for e in args[0].elements)
        return RTResult().success(String(result))

    def execute_listIndex(self, args, exec_ctx):
        if len(args) != 2 or not isinstance(args[0], List):
            return RTResult().failure(RTError(
                self.pos_start, self.pos_end,
                "listIndex(list, item) expects a list and a value",
                exec_ctx,
            ))
        target = str(args[1])
        for i, e in enumerate(args[0].elements):
            if str(e) == target:
                return RTResult().success(Number(i))
        return RTResult().success(Number(-1))

    def execute_listRemove(self, args, exec_ctx):
        if len(args) != 2 or not isinstance(args[0], List) or not isinstance(args[1], Number):
            return RTResult().failure(RTError(
                self.pos_start, self.pos_end,
                "listRemove(list, idx) expects a list and an integer index",
                exec_ctx,
            ))
        lst = args[0]
        idx = int(args[1].value)
        if idx < -len(lst.elements) or idx >= len(lst.elements):
            return RTResult().failure(RTError(
                self.pos_start, self.pos_end,
                f"listRemove() index {idx} out of range for list of length {len(lst.elements)}",
                exec_ctx,
            ))
        new_elements = list(lst.elements)
        new_elements.pop(idx)
        return RTResult().success(List(new_elements))

    def execute_anyOf(self, args, exec_ctx):
        if len(args) != 1 or not isinstance(args[0], List):
            return RTResult().failure(RTError(
                self.pos_start, self.pos_end,
                "anyOf(list) expects a list",
                exec_ctx,
            ))
        result = any(e.is_true() for e in args[0].elements)
        return RTResult().success(Number(1 if result else 0, is_bool=True))

    def execute_allOf(self, args, exec_ctx):
        if len(args) != 1 or not isinstance(args[0], List):
            return RTResult().failure(RTError(
                self.pos_start, self.pos_end,
                "allOf(list) expects a list",
                exec_ctx,
            ))
        result = all(e.is_true() for e in args[0].elements)
        return RTResult().success(Number(1 if result else 0, is_bool=True))

    def execute_sumOf(self, args, exec_ctx):
        if len(args) != 1 or not isinstance(args[0], List):
            return RTResult().failure(RTError(
                self.pos_start, self.pos_end,
                "sumOf(list) expects a list",
                exec_ctx,
            ))
        try:
            total = sum(e.value for e in args[0].elements if isinstance(e, Number))
            return RTResult().success(Number(total))
        except Exception as e:
            return RTResult().failure(RTError(
                self.pos_start, self.pos_end,
                f"sumOf() failed: {e}",
                exec_ctx,
            ))

    def _list_sort_key(self, e):
        if isinstance(e, (Number, String)):
            return e.value
        return str(e)

    def execute_sortList(self, args, exec_ctx):
        if len(args) not in (1, 2) or not isinstance(args[0], List):
            return RTResult().failure(RTError(
                self.pos_start, self.pos_end,
                "sortList(list) or sortList(list, reverse) expects a list",
                exec_ctx,
            ))
        reverse = args[1].is_true() if len(args) == 2 else False
        try:
            sorted_els = sorted(args[0].elements, key=self._list_sort_key, reverse=reverse)
            return RTResult().success(List(sorted_els))
        except Exception as e:
            return RTResult().failure(RTError(
                self.pos_start, self.pos_end,
                f"sortList() failed: {e}",
                exec_ctx,
            ))

    def execute_reverseList(self, args, exec_ctx):
        if len(args) != 1 or not isinstance(args[0], List):
            return RTResult().failure(RTError(
                self.pos_start, self.pos_end,
                "reverseList(list) expects a list",
                exec_ctx,
            ))
        return RTResult().success(List(list(reversed(args[0].elements))))

    def execute_listMin(self, args, exec_ctx):
        if len(args) != 1 or not isinstance(args[0], List):
            return RTResult().failure(RTError(
                self.pos_start, self.pos_end,
                "listMin(list) expects a list",
                exec_ctx,
            ))
        if not args[0].elements:
            return RTResult().failure(RTError(
                self.pos_start, self.pos_end,
                "listMin() called on an empty list",
                exec_ctx,
            ))
        try:
            return RTResult().success(min(args[0].elements, key=self._list_sort_key))
        except Exception as e:
            return RTResult().failure(RTError(
                self.pos_start, self.pos_end,
                f"listMin() failed: {e}",
                exec_ctx,
            ))

    def execute_listMax(self, args, exec_ctx):
        if len(args) != 1 or not isinstance(args[0], List):
            return RTResult().failure(RTError(
                self.pos_start, self.pos_end,
                "listMax(list) expects a list",
                exec_ctx,
            ))
        if not args[0].elements:
            return RTResult().failure(RTError(
                self.pos_start, self.pos_end,
                "listMax() called on an empty list",
                exec_ctx,
            ))
        try:
            return RTResult().success(max(args[0].elements, key=self._list_sort_key))
        except Exception as e:
            return RTResult().failure(RTError(
                self.pos_start, self.pos_end,
                f"listMax() failed: {e}",
                exec_ctx,
            ))

    # ── tuple built-ins ───────────────────────────────────────────────────────

    def execute_tupleCreate(self, args, exec_ctx):
        """tupleCreate(v1, v2, ...) — create a tuple from any number of arguments."""
        return RTResult().success(LynxTuple(args))

    def execute_tupleGet(self, args, exec_ctx):
        if len(args) != 2 or not isinstance(args[0], LynxTuple) or not isinstance(args[1], Number):
            return RTResult().failure(RTError(
                self.pos_start, self.pos_end,
                "tupleGet(tuple, idx) expects a tuple and an integer index",
                exec_ctx,
            ))
        t = args[0]
        idx = int(args[1].value)
        if idx < -len(t.elements) or idx >= len(t.elements):
            return RTResult().failure(RTError(
                self.pos_start, self.pos_end,
                f"tupleGet() index {idx} out of range for tuple of length {len(t.elements)}",
                exec_ctx,
            ))
        return RTResult().success(t.elements[idx])

    def execute_tupleLen(self, args, exec_ctx):
        if len(args) != 1 or not isinstance(args[0], LynxTuple):
            return RTResult().failure(RTError(
                self.pos_start, self.pos_end,
                "tupleLen(tuple) expects a tuple",
                exec_ctx,
            ))
        return RTResult().success(Number(len(args[0].elements)))

    def execute_tupleContains(self, args, exec_ctx):
        if len(args) != 2 or not isinstance(args[0], LynxTuple):
            return RTResult().failure(RTError(
                self.pos_start, self.pos_end,
                "tupleContains(tuple, val) expects a tuple and a value",
                exec_ctx,
            ))
        target = str(args[1])
        found = any(str(e) == target for e in args[0].elements)
        return RTResult().success(Number(1 if found else 0, is_bool=True))

    def execute_tupleIndex(self, args, exec_ctx):
        if len(args) != 2 or not isinstance(args[0], LynxTuple):
            return RTResult().failure(RTError(
                self.pos_start, self.pos_end,
                "tupleIndex(tuple, val) expects a tuple and a value",
                exec_ctx,
            ))
        target = str(args[1])
        for i, e in enumerate(args[0].elements):
            if str(e) == target:
                return RTResult().success(Number(i))
        return RTResult().success(Number(-1))

    def execute_tupleSlice(self, args, exec_ctx):
        if (len(args) != 3 or not isinstance(args[0], LynxTuple)
                or not isinstance(args[1], Number) or not isinstance(args[2], Number)):
            return RTResult().failure(RTError(
                self.pos_start, self.pos_end,
                "tupleSlice(tuple, start, stop) expects a tuple and two integer indices",
                exec_ctx,
            ))
        start = int(args[1].value)
        stop = int(args[2].value)
        return RTResult().success(LynxTuple(args[0].elements[start:stop]))

    def execute_tupleToList(self, args, exec_ctx):
        if len(args) != 1 or not isinstance(args[0], LynxTuple):
            return RTResult().failure(RTError(
                self.pos_start, self.pos_end,
                "tupleToList(tuple) expects a tuple",
                exec_ctx,
            ))
        return RTResult().success(List(list(args[0].elements)))

    def execute_listToTuple(self, args, exec_ctx):
        if len(args) != 1 or not isinstance(args[0], List):
            return RTResult().failure(RTError(
                self.pos_start, self.pos_end,
                "listToTuple(list) expects a list",
                exec_ctx,
            ))
        return RTResult().success(LynxTuple(args[0].elements))

    def execute_tupleConcat(self, args, exec_ctx):
        if (len(args) != 2 or not isinstance(args[0], LynxTuple)
                or not isinstance(args[1], LynxTuple)):
            return RTResult().failure(RTError(
                self.pos_start, self.pos_end,
                "tupleConcat(t1, t2) expects two tuples",
                exec_ctx,
            ))
        return RTResult().success(LynxTuple(args[0].elements + args[1].elements))

    def execute_tupleCount(self, args, exec_ctx):
        if len(args) != 2 or not isinstance(args[0], LynxTuple):
            return RTResult().failure(RTError(
                self.pos_start, self.pos_end,
                "tupleCount(tuple, val) expects a tuple and a value",
                exec_ctx,
            ))
        target = str(args[1])
        count = sum(1 for e in args[0].elements if str(e) == target)
        return RTResult().success(Number(count))

    def execute_tupleFirst(self, args, exec_ctx):
        if len(args) != 1 or not isinstance(args[0], LynxTuple):
            return RTResult().failure(RTError(
                self.pos_start, self.pos_end,
                "tupleFirst(tuple) expects a tuple",
                exec_ctx,
            ))
        if not args[0].elements:
            return RTResult().failure(RTError(
                self.pos_start, self.pos_end,
                "tupleFirst() called on an empty tuple",
                exec_ctx,
            ))
        return RTResult().success(args[0].elements[0])

    def execute_tupleLast(self, args, exec_ctx):
        if len(args) != 1 or not isinstance(args[0], LynxTuple):
            return RTResult().failure(RTError(
                self.pos_start, self.pos_end,
                "tupleLast(tuple) expects a tuple",
                exec_ctx,
            ))
        if not args[0].elements:
            return RTResult().failure(RTError(
                self.pos_start, self.pos_end,
                "tupleLast() called on an empty tuple",
                exec_ctx,
            ))
        return RTResult().success(args[0].elements[-1])

    def execute_tupleJsonArray(self, args, exec_ctx):
        import json as _json
        if len(args) != 1 or not isinstance(args[0], LynxTuple):
            return RTResult().failure(RTError(
                self.pos_start, self.pos_end,
                "tupleJsonArray(tuple) expects a tuple",
                exec_ctx,
            ))
        try:
            items = [e.value if isinstance(e, (Number, String)) else str(e)
                     for e in args[0].elements]
            return RTResult().success(String(_json.dumps(items)))
        except Exception as e:
            return RTResult().failure(RTError(
                self.pos_start, self.pos_end,
                f"tupleJsonArray() failed: {e}",
                exec_ctx,
            ))

    # ── /tuple built-ins ──────────────────────────────────────────────────────

    # ------------------------------------------------------------------ async built-ins

    def execute_asyncRun(self, args, exec_ctx):
        """asyncRun(coro) — run a coroutine (from an async function call) synchronously
        via asyncio.run().  Use this in main() to drive async code."""
        import asyncio
        if len(args) != 1 or not isinstance(args[0], CoroutineValue):
            return RTResult().failure(RTError(
                self.pos_start, self.pos_end,
                "asyncRun(coro) expects a single coroutine argument "
                "(the result of calling an 'async' function)",
                exec_ctx,
            ))
        try:
            coro_res = asyncio.run(args[0].coro)
        except Exception as e:
            return RTResult().failure(RTError(
                self.pos_start, self.pos_end,
                f"asyncRun() raised an exception: {type(e).__name__}: {e}",
                exec_ctx,
            ))
        if isinstance(coro_res, RTResult):
            if coro_res.error:
                return RTResult().failure(coro_res.error)
            return RTResult().success(coro_res.value if coro_res.value is not None else Number.null)
        return RTResult().success(Number.null)

    def execute_asyncGather(self, args, exec_ctx):
        """asyncGather(coro1, coro2, ...) — return a new coroutine that, when awaited,
        runs all supplied coroutines concurrently and returns a list of their results."""
        for i, arg in enumerate(args):
            if not isinstance(arg, CoroutineValue):
                return RTResult().failure(RTError(
                    self.pos_start, self.pos_end,
                    f"asyncGather() argument {i + 1} is not a coroutine "
                    "(expected the result of calling an 'async' function)",
                    exec_ctx,
                ))

        import asyncio

        coros = [arg.coro for arg in args]

        async def _gather():
            results = await asyncio.gather(*coros)
            elements = []
            for r in results:
                if isinstance(r, RTResult):
                    if r.error:
                        return r  # propagate first error
                    elements.append(r.value if r.value is not None else Number.null)
                else:
                    elements.append(Number.null)
            return RTResult().success(List(elements))

        return RTResult().success(CoroutineValue(_gather()))

    def execute_asyncSleep(self, args, exec_ctx):
        """asyncSleep(seconds) — return a coroutine that, when awaited,
        sleeps for the given number of seconds (float allowed)."""
        import asyncio
        if len(args) != 1 or not isinstance(args[0], Number):
            return RTResult().failure(RTError(
                self.pos_start, self.pos_end,
                "asyncSleep(seconds) expects a single numeric argument",
                exec_ctx,
            ))
        seconds = args[0].value

        async def _sleep():
            await asyncio.sleep(seconds)
            return RTResult().success(Number.null)

        return RTResult().success(CoroutineValue(_sleep()))

    # ------------------------------------------------------------------ /async built-ins


BuiltInFunction.print = BuiltInFunction("print")
BuiltInFunction.println = BuiltInFunction("println")
BuiltInFunction.input = BuiltInFunction("input")
BuiltInFunction.inputln = BuiltInFunction("inputln")
BuiltInFunction.rawPy = BuiltInFunction("rawPy")
BuiltInFunction.rawPyx = BuiltInFunction("rawPyx")
BuiltInFunction.strOf = BuiltInFunction("strOf")
BuiltInFunction.intOf = BuiltInFunction("intOf")
BuiltInFunction.floatOf = BuiltInFunction("floatOf")
BuiltInFunction.returnType = BuiltInFunction("returnType")
BuiltInFunction.returnLength = BuiltInFunction("returnLength")
BuiltInFunction.seqFromTo = BuiltInFunction("seqFromTo")
BuiltInFunction.range = BuiltInFunction("range")
BuiltInFunction.cleanRawPyxCache = BuiltInFunction("cleanRawPyxCache")
BuiltInFunction.listJsonArray = BuiltInFunction("listJsonArray")
BuiltInFunction.listJsonObject = BuiltInFunction("listJsonObject")
BuiltInFunction.splitStr = BuiltInFunction("splitStr")
BuiltInFunction.listFlatten = BuiltInFunction("listFlatten")
BuiltInFunction.listUnique = BuiltInFunction("listUnique")
BuiltInFunction.listPush = BuiltInFunction("listPush")
BuiltInFunction.listPop = BuiltInFunction("listPop")
BuiltInFunction.listGet = BuiltInFunction("listGet")
BuiltInFunction.listSet = BuiltInFunction("listSet")
BuiltInFunction.listSlice = BuiltInFunction("listSlice")
BuiltInFunction.listContains = BuiltInFunction("listContains")
BuiltInFunction.listJoin = BuiltInFunction("listJoin")
BuiltInFunction.listIndex = BuiltInFunction("listIndex")
BuiltInFunction.listRemove = BuiltInFunction("listRemove")
BuiltInFunction.anyOf = BuiltInFunction("anyOf")
BuiltInFunction.allOf = BuiltInFunction("allOf")
BuiltInFunction.sumOf = BuiltInFunction("sumOf")
BuiltInFunction.sortList = BuiltInFunction("sortList")
BuiltInFunction.reverseList = BuiltInFunction("reverseList")
BuiltInFunction.listMin = BuiltInFunction("listMin")
BuiltInFunction.listMax = BuiltInFunction("listMax")
BuiltInFunction.asyncRun = BuiltInFunction("asyncRun")
BuiltInFunction.asyncGather = BuiltInFunction("asyncGather")
BuiltInFunction.asyncSleep = BuiltInFunction("asyncSleep")
BuiltInFunction.tupleCreate = BuiltInFunction("tupleCreate")
BuiltInFunction.tupleGet = BuiltInFunction("tupleGet")
BuiltInFunction.tupleLen = BuiltInFunction("tupleLen")
BuiltInFunction.tupleContains = BuiltInFunction("tupleContains")
BuiltInFunction.tupleIndex = BuiltInFunction("tupleIndex")
BuiltInFunction.tupleSlice = BuiltInFunction("tupleSlice")
BuiltInFunction.tupleToList = BuiltInFunction("tupleToList")
BuiltInFunction.listToTuple = BuiltInFunction("listToTuple")
BuiltInFunction.tupleConcat = BuiltInFunction("tupleConcat")
BuiltInFunction.tupleCount = BuiltInFunction("tupleCount")
BuiltInFunction.tupleFirst = BuiltInFunction("tupleFirst")
BuiltInFunction.tupleLast = BuiltInFunction("tupleLast")
BuiltInFunction.tupleJsonArray = BuiltInFunction("tupleJsonArray")

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
    """Namespace for 'local' functions defined in the current function scope.

    Intentionally only searches the *direct* symbol table (no parent walk).
    A nested global executing inside an outer global must not be able to
    reach locals that were defined in the outer global's scope.
    """

    def __init__(self, symbol_table):
        super().__init__()
        self.symbol_table = symbol_table

    def get_attr(self, name):
        # Direct lookup only — do NOT walk the parent chain.
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


# ── embedPy — experimental Python bridge ──────────────────────────────────────
# Lets Lynxer code call Python functions/modules with Lynx syntax instead of
# a rawPy block.  Example:
#   embedPy.requests.get("https://example.com")   →  calls requests.get(...)
#   embedPy.len("hello")                           →  calls builtins.len("hello")
#   str status = embedPy.os.path.exists("/tmp");   →  typed variable, "True"/"False"
# ──────────────────────────────────────────────────────────────────────────────


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
    # Number.null / Null singleton → None
    return None


def _python_to_lynx(py_val, context=None, pos_start=None, pos_end=None):
    """Convert a plain Python value → the nearest Lynxer Value equivalent."""
    if py_val is None:
        return Number.null
    if isinstance(py_val, bool):
        # bool check must come before int (bool is a subtype of int)
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
    # Anything else (Response objects, class instances, modules…) → EmbedPyObject
    obj = EmbedPyObject(py_val)
    if context:
        obj.set_context(context)
    if pos_start:
        obj.set_pos(pos_start, pos_end)
    return obj


class EmbedPyObject(Value):
    """Wraps an arbitrary Python object returned from an embedPy call.

    Supports further attribute access (``obj.attr``) and calling if callable.
    When printed or used as a string, falls back to Python's str().
    """

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
        # Non-callable attribute (constant, string, int…) — convert directly
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
    """The root ``embedPy`` namespace.

    ``embedPy.name`` first checks Python builtins (len, str, int, …) then
    tries to import a module with that name.  On failure, a clear error is
    raised instead of silently returning null.
    """

    def get_attr(self, name):
        import builtins as _builtins
        import importlib as _importlib
        import types as _types

        # 1. Python builtins (len, str, int, print, open, …)
        builtin = getattr(_builtins, name, None)
        if builtin is not None and callable(builtin):
            fn = EmbedPyCallable(builtin, name)
            fn.set_context(self.context).set_pos(self.pos_start, self.pos_end)
            return fn, None

        # 2. Try importing as a module (requests, os, json, math, …)
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


# ── end embedPy ────────────────────────────────────────────────────────────────


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
    """Namespace of all class blueprints for a given scope.

    Accessible as ``global.class`` (main program) or
    ``global.<module>.class`` (imported module).
    """

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
    """Static singleton class object.

    Fields and methods live directly on this object — there are no
    separate instances.  Access pattern::

        global.class.ClassName()              # run init() if defined
        global.class.ClassName.varName        # read field
        str global.class.ClassName.varName = "x"  # set field
        global.class.ClassName.methodName()  # call method
    """

    def __init__(self, name, field_defs, methods):
        """
        field_defs : list of (type_str, name_str, value_node, is_const)
                     value_node is evaluated lazily at class-definition time.
        methods    : dict of method_name -> Function
        """
        super().__init__()
        self.name = name
        self._field_defs = field_defs   # kept for repr / introspection
        self._methods = methods
        self._fields = {}               # name -> {"type": str, "value": Value, "const": bool}

    # ---- attribute access ----

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

    # ---- callable: global.class.ClassName() ----

    def execute(self, args):
        """Call the class.  Runs init() if defined, otherwise is a no-op."""
        res = RTResult()
        if args:
            return res.failure(RTError(
                self.pos_start, self.pos_end,
                f"Class '{self.name}' constructor takes no arguments "
                f"(define an 'init' method for custom initialisation)",
                self.context,
            ))
        if "init" in self._methods:
            bound = BoundMethod(self._methods["init"], self)
            bound.set_pos(self.pos_start, self.pos_end).set_context(self.context)
            call_res = bound.execute([])
            if call_res.error:
                return call_res
        return res.success(Number.null)

    # ---- value protocol ----

    def copy(self):
        # Reference semantics — mutations must be visible everywhere.
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


class BoundMethod(Value):
    """A class method bound to its owning ClassBlueprint.

    When called, injects ``this`` = the class blueprint into the
    method's execution context so that ``this.field`` etc. work.
    """

    def __init__(self, func, class_obj):
        super().__init__()
        self.func = func
        self.class_obj = class_obj   # ClassBlueprint

    def execute(self, args):
        res = RTResult()
        interpreter = SHARED_INTERPRETER
        exec_ctx = self.func.generate_new_context()
        exec_ctx.symbol_table.set("this", self.class_obj)
        res.register(
            self.func.check_and_populate_args(
                self.func.param_names, args, exec_ctx, self.func.param_types
            )
        )
        if res.should_return():
            return res
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
        c = BoundMethod(self.func, self.class_obj)
        c.set_pos(self.pos_start, self.pos_end)
        c.set_context(self.context)
        return c

    def __repr__(self):
        return f"<bound method {self.func.name} of class {self.class_obj.name}>"


class VarGroup(Value):
    """Runtime representation of a vargroup value.

    Uses reference semantics: copy() returns self so that mutations made
    through addVarGroup / removeVarGroup / dot-assignment are visible to
    all references that hold the same object.
    """

    def __init__(self, name):
        super().__init__()
        self.name = name
        # Ordered dict: field_name -> {"type": str, "value": Value}
        self._fields = {}

    # ---- attribute access ----

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

    # ---- value protocol ----

    def copy(self):
        # Reference semantics: return self so mutations are always visible.
        return self

    def __repr__(self):
        parts = []
        for k, info in self._fields.items():
            prefix = "const " if info.get("const") else ""
            parts.append(f"{prefix}{info['type']} {k} = {info['value']}")
        return f"vargroup {self.name} " + "{ " + ", ".join(parts) + " }"


# context


class Context:
    def __init__(self, display_name, parent=None, parent_entry_pos=None):
        self.display_name = display_name
        self.parent = parent
        self.parent_entry_pos = parent_entry_pos
        self.symbol_table = None
        self.current_function = None      # the Function/AsyncFunction currently executing
        self.current_global_path = None   # path list for the executing global, e.g. ['a', 'b']


# symbol table


class SymbolTable:
    def __init__(self, parent=None):
        self.symbols = {}
        self.constants = set()
        self.types = {}
        self.parent = parent

    def get(self, name):
        table = self
        while table:
            if name in table.symbols:
                return table.symbols[name]
            table = table.parent
        return None

    def set(self, name, value, is_const=False, decl_type=None):
        self.symbols[name] = value
        if is_const:
            self.constants.add(name)
        if decl_type is not None:
            self.types[name] = decl_type

    def update_existing(self, name, value):
        """Walk the parent chain to find the declaring scope and update it there.
        Falls back to the current table if the variable is not found anywhere.
        Returns the table where the update was performed."""
        table = self
        while table:
            if name in table.symbols:
                table.symbols[name] = value
                return table
            table = table.parent
        # Variable not found — set in current scope (should not happen for well-typed code)
        self.symbols[name] = value
        return self

    def is_const(self, name):
        table = self
        while table:
            if name in table.symbols:
                return name in table.constants
            table = table.parent
        return False

    def get_type(self, name):
        table = self
        while table:
            if name in table.symbols:
                return table.types.get(name)
            table = table.parent
        return None

    def remove(self, name):
        del self.symbols[name]


# ── importPy shared module registry ───────────────────────────────────────────
# Populated by importPy(){"os","sys",...} inside global setup(){}.
# Injected into every rawPy{} and rawPyx{} exec namespace so users never need
# to write 'import os' inside individual blocks.
_rawpy_global_modules: dict = {}

# ── global call hierarchy helpers ─────────────────────────────────────────────

def _can_call_global(caller_path, callee_path):
    """Return True if a global at *caller_path* is allowed to call one at *callee_path*.

    Rules (hierarchical call restriction):
    - Recursion (same path) is always allowed.
    - Globals with a different top-level name are always callable (different tree).
    - Within the same tree, a caller may only call its own ancestors (upward calls).
      Sideways calls to siblings, cousins, or downward calls to descendants are
      forbidden to keep the hierarchy clean.
    """
    if not caller_path or not callee_path:
        return True
    # Recursion
    if caller_path == callee_path:
        return True
    # Different top-level root — always OK
    if caller_path[0] != callee_path[0]:
        return True
    # Same root: callee must be a strict prefix of caller (i.e. an ancestor)
    if len(callee_path) < len(caller_path):
        return caller_path[:len(callee_path)] == callee_path
    # callee is at the same depth or deeper — not an ancestor
    return False


def _get_current_global_path(context):
    """Walk the context chain to find the nearest current_global_path."""
    c = context
    while c is not None:
        if c.current_global_path is not None:
            return c.current_global_path
        c = c.parent
    return None


def _preregister_nested_globals(parent_func, block_node, context):
    """Scan the top-level statements of *block_node* for nested global FuncDefNodes
    and eagerly register them in *parent_func.inner_globals* so that calls like
    ``global.outer.inner()`` work without first executing ``global.outer()``.

    Only the *direct* children of the block are scanned (not nested inside if/for/while).
    Globals inside control-flow branches are registered lazily at runtime when the
    parent executes.  Recurses for deeper nesting (global.a.b.c).
    """
    for stmt in block_node.statements:
        if not isinstance(stmt, FuncDefNode):
            continue
        is_global_def = (
            stmt.kind_tok.value == "global"
            or (stmt.kind_tok.type == TT_IDENTIFIER and stmt.kind_tok.value == "global")
        )
        if not is_global_def:
            continue
        child_name = stmt.var_name_tok.value
        param_names = [p[1].value for p in stmt.param_toks]
        param_types = [p[0].value if p[0] else None for p in stmt.param_toks]
        if stmt.is_async:
            child_func = AsyncFunction(child_name, stmt.body_block, param_names, param_types, is_global=True)
        else:
            child_func = Function(child_name, stmt.body_block, param_names, param_types, is_global=True)
        child_func.set_context(context)
        child_func.set_pos(stmt.pos_start, stmt.pos_end)
        parent_path = parent_func.global_path or [parent_func.name]
        child_func.global_path = parent_path + [child_name]
        parent_func.inner_globals[child_name] = child_func
        # Recurse for deeper nesting
        _preregister_nested_globals(child_func, stmt.body_block, context)


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
        val = Number(1 if node.value else 0, is_bool=True)
        return RTResult().success(
            val.set_context(context).set_pos(node.pos_start, node.pos_end)
        )

    def visit_NoneNode(self, node, context):
        return RTResult().success(
            Null().set_context(context).set_pos(node.pos_start, node.pos_end)
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
        decl_type = node.type_tok.value if node.type_tok else None
        value = res.register(self.visit(node.value_node, context))
        if res.should_return():
            return res
        # Auto-convert a list literal [a, b, c] to a LynxTuple when declared as 'tuple'
        if decl_type == "tuple" and isinstance(value, List):
            value = LynxTuple(value.elements)
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
        context.symbol_table.set(
            var_name, value, is_const=node.is_const, decl_type=decl_type
        )
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
        # Auto-convert a list literal [a, b, c] to a LynxTuple when the target is 'tuple'
        if decl_type == "tuple" and isinstance(value, List):
            value = LynxTuple(value.elements)
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
        # Update in the declaring scope so that assignments inside sub-contexts
        # (e.g. for-loop) are visible in the outer scope.
        context.symbol_table.update_existing(var_name, value)
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
        elif op.type == TT_AMP:
            result, error = left.bit_anded_by(right)
        elif op.type == TT_PIPE:
            result, error = left.bit_ored_by(right)
        elif op.type == TT_CARET:
            result, error = left.bit_xored_by(right)
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
        elif node.op_tok.matches(TT_KEYWORD, "not"):
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
            # continue/restart: still run the update step, then loop
            should_continue = body_res.loop_should_continue

            if not should_break:
                upd_res = RTResult()
                upd_res.register(self.visit(node.update_node, for_ctx))
                if upd_res.error:
                    return upd_res

            if should_break:
                break
            # should_continue just means skip the rest of the body (already done)

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

        # Run the try block in isolation — do not propagate its error outward
        try_res = RTResult()
        try_res.register(self.visit(node.try_block, context))

        if try_res.error:
            # A runtime error occurred — execute the catch block instead.
            # Run the catch block in the same context so that assignments
            # inside it affect the surrounding scope, just like if/else blocks.
            if node.catch_var_tok:
                var_name = node.catch_var_tok.value

                # Enforce const invariant — cannot rebind a const variable
                if context.symbol_table.is_const(var_name):
                    return res.failure(RTError(
                        node.catch_var_tok.pos_start,
                        node.catch_var_tok.pos_end,
                        f"Cannot bind catch variable '{var_name}': "
                        f"it is declared as const",
                        context,
                    ))

                # Enforce type invariant — existing typed variable must be str or any
                existing_type = context.symbol_table.get_type(var_name)
                if existing_type is not None and existing_type not in ("str", "any"):
                    return res.failure(RTError(
                        node.catch_var_tok.pos_start,
                        node.catch_var_tok.pos_end,
                        f"Cannot bind catch variable '{var_name}' as 'str': "
                        f"'{var_name}' is already declared as '{existing_type}'",
                        context,
                    ))

                # Bind the error message as a str in the current scope
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

        # No error — propagate any control-flow signals from the try block
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

    def visit_FuncDefNode(self, node, context):
        res = RTResult()
        func_name = node.var_name_tok.value
        param_names = [p[1].value for p in node.param_toks]
        param_types = [p[0].value if p[0] else None for p in node.param_toks]
        is_global = node.kind_tok.value == "global" or (
            node.kind_tok.type == TT_IDENTIFIER and node.kind_tok.value == "global"
        )
        if node.is_async:
            func_value = AsyncFunction(func_name, node.body_block, param_names, param_types, is_global)
        else:
            func_value = Function(func_name, node.body_block, param_names, param_types, is_global)
        func_value.set_context(context).set_pos(node.pos_start, node.pos_end)

        if is_global:
            # Determine global_path and register as a nested global if applicable
            parent_fn = context.current_function
            if parent_fn is not None and parent_fn.is_global:
                # Nested global: register on parent function and build path
                parent_path = parent_fn.global_path or [parent_fn.name]
                func_value.global_path = parent_path + [func_name]
                parent_fn.inner_globals[func_name] = func_value
            else:
                # Top-level global
                func_value.global_path = [func_name]

            # Pre-register any nested globals declared directly inside this
            # function's body so they are accessible via global.a.b() without
            # needing to call global.a() first.
            _preregister_nested_globals(func_value, node.body_block, context)

        context.symbol_table.set(func_name, func_value)

        # If this is a 'local' function defined inside another function, register on
        # the parent function so local.outerFunc.innerFunc() chains work.
        if not is_global and context.current_function is not None:
            context.current_function.inner_locals[func_name] = func_value

        return res.success(func_value)

    def visit_AsyncLocalDefNode(self, node, context):
        res = RTResult()
        func_name = node.name_tok.value
        param_names = [p[1].value for p in node.param_toks]
        param_types = [p[0].value if p[0] else None for p in node.param_toks]
        func_value = AsyncFunction(func_name, node.body, param_names, param_types)
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

    # ------------------------------------------------------------------ async visitor path

    async def async_visit(self, node, context):
        """Dispatch to async_visit_<NodeType> if available, else fall back to sync visit."""
        method_name = f"async_visit_{type(node).__name__}"
        method = getattr(self, method_name, None)
        if method is not None:
            return await method(node, context)
        # Leaf / simple nodes (NumberNode, StringNode, BoolNode, NoneNode, VarAccessNode, etc.)
        return self.visit(node, context)

    async def async_visit_BlockNode(self, node, context):
        res = RTResult()
        for stmt in node.statements:
            res.register(await self.async_visit(stmt, context))
            if res.should_return():
                return res
        return res.success(Number.null)

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

    async def async_visit_VarDeclNode(self, node, context):
        res = RTResult()
        var_name = node.var_name_tok.value
        decl_type = node.type_tok.value if node.type_tok else None
        value = res.register(await self.async_visit(node.value_node, context))
        if res.should_return():
            return res
        # Auto-convert list literal to LynxTuple when declared as 'tuple'
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
        context.symbol_table.set(var_name, value, is_const=node.is_const, decl_type=decl_type)
        return res.success(value)

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
        # Auto-convert list literal to LynxTuple when the target is 'tuple'
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
        elif op.type == TT_AMP:
            result, error = left.bit_anded_by(right)
        elif op.type == TT_PIPE:
            result, error = left.bit_ored_by(right)
        elif op.type == TT_CARET:
            result, error = left.bit_xored_by(right)
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
        elif node.op_tok.matches(TT_KEYWORD, "not"):
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
            # continue/restart just skips the rest of body (already handled)

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

    async def async_visit_CallNode(self, node, context):
        res = RTResult()
        args = []
        value_to_call = res.register(await self.async_visit(node.node_to_call, context))
        if res.should_return():
            return res
        value_to_call = value_to_call.copy().set_pos(node.pos_start, node.pos_end)

        # enforce global.funcName() call syntax for user-defined global functions
        if (isinstance(node.node_to_call, VarAccessNode)
                and isinstance(value_to_call, (Function, AsyncFunction))
                and value_to_call.is_global):
            return res.failure(RTError(
                node.pos_start, node.pos_end,
                f"Global function '{value_to_call.name}' must be called as "
                f"'global.{value_to_call.name}(...)' not '{value_to_call.name}(...)'",
                context,
            ))

        # enforce local.funcName() call syntax for user-defined local functions
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
            args.append(res.register(await self.async_visit(arg_node, context)))
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
            f"Value of type '{type(obj).__name__}' does not support attribute access",
            context,
        ))

    async def async_visit_FuncDefNode(self, node, context):
        return self.visit_FuncDefNode(node, context)

    async def async_visit_AsyncLocalDefNode(self, node, context):
        return self.visit_AsyncLocalDefNode(node, context)

    async def async_visit_AsyncDotCallNode(self, node, context):
        # Inside an async body: async.name(args) returns a CoroutineValue so the caller
        # can either `await` it or pass it to asyncGather.
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
        # Return the CoroutineValue — let await / asyncGather consume it
        return res.success(call_res.value)

    async def async_visit_DotAssignNode(self, node, context):
        res = RTResult()
        obj = res.register(await self.async_visit(node.obj_node, context))
        if res.should_return():
            return res

        if not isinstance(obj, (VarGroup, ClassBlueprint)):
            return res.failure(
                RTError(
                    node.pos_start,
                    node.pos_end,
                    "Dot-assignment target must be a vargroup or a class field "
                    "(use: type global.class.ClassName.field = value)",
                    context,
                )
            )

        attr_name = node.attr_name_tok.value

        if attr_name in obj._fields:
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
        # Field initializers do not support await (per language spec)
        return self.visit_VarGroupDeclNode(node, context)

    async def async_visit_AddVarGroupNode(self, node, context):
        # Field initializers do not support await (per language spec)
        return self.visit_AddVarGroupNode(node, context)

    async def async_visit_RemoveVarGroupNode(self, node, context):
        return self.visit_RemoveVarGroupNode(node, context)

    # ------------------------------------------------------------------ /async visitor path

    def visit_CallNode(self, node, context):
        res = RTResult()
        args = []

        value_to_call = res.register(self.visit(node.node_to_call, context))
        if res.should_return():
            return res
        value_to_call = value_to_call.copy().set_pos(node.pos_start, node.pos_end)

        # enforce global.funcName() call syntax for user-defined global functions
        if (isinstance(node.node_to_call, VarAccessNode)
                and isinstance(value_to_call, (Function, AsyncFunction))
                and value_to_call.is_global):
            return res.failure(RTError(
                node.pos_start, node.pos_end,
                f"Global function '{value_to_call.name}' must be called as "
                f"'global.{value_to_call.name}(...)' not '{value_to_call.name}(...)'",
                context,
            ))

        # enforce local.funcName() call syntax for user-defined local functions
        if (isinstance(node.node_to_call, VarAccessNode)
                and isinstance(value_to_call, (Function, AsyncFunction))
                and not value_to_call.is_global):
            return res.failure(RTError(
                node.pos_start, node.pos_end,
                f"Local function '{value_to_call.name}' must be called as "
                f"'local.{value_to_call.name}(...)' not '{value_to_call.name}(...)'",
                context,
            ))

        # enforce global call hierarchy restriction for nested globals
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
                    f"may only call its own ancestors (upward calls only). "
                    f"Sideways and downward calls within the same tree are not allowed.",
                    context,
                ))

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

    # ------------------------------------------------------------------ vargroup visitors

    def _build_vargroup(self, name, fields, context):
        # build a VarGroup from fields; caller registers it
        res = RTResult()
        vg = VarGroup(name)
        for field_tuple in fields:
            # fields are 4-tuples: (type, name_tok, value_node, is_const)
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
                # value_node is a VarGroupDeclNode — build nested VarGroup
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
            self._build_vargroup(node.name_tok.value, node.fields, context)
        )
        if res.should_return():
            return res
        context.symbol_table.set(node.name_tok.value, vg, is_const=node.is_const, decl_type="vargroup")
        return res.success(vg)

    def visit_ClassDefNode(self, node, context):
        """Register a class blueprint in the nearest ClassRegistry."""
        res = RTResult()

        # Build method Functions from method FuncDefNodes
        methods = {}
        for method_node in node.method_nodes:
            func_name = method_node.var_name_tok.value
            param_names = [p[1].value for p in method_node.param_toks]
            param_types = [p[0].value if p[0] else None for p in method_node.param_toks]
            func = Function(func_name, method_node.body_block, param_names, param_types, is_global=False)
            func.set_context(context).set_pos(method_node.pos_start, method_node.pos_end)
            methods[func_name] = func

        # field_defs: (type_str, name_str, value_node, is_const)
        field_defs = [
            (fd[0], fd[1].value, fd[2], fd[3])
            for fd in node.field_defs
        ]

        blueprint = ClassBlueprint(node.name_tok.value, field_defs, methods)
        blueprint.set_context(context).set_pos(node.pos_start, node.pos_end)

        # Evaluate field default values, enforce declared type, populate blueprint
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

        # Register in the ClassRegistry stored under "class" in this scope
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

        if not isinstance(obj, (VarGroup, ClassBlueprint)):
            return res.failure(
                RTError(
                    node.pos_start,
                    node.pos_end,
                    "Dot-assignment target must be a vargroup or a class field "
                    "(use: type global.class.ClassName.field = value)",
                    context,
                )
            )

        attr_name = node.attr_name_tok.value

        # The type written at the assignment site must match the field's declared type
        if attr_name in obj._fields:
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

    # ------------------------------------------------------------------ /vargroup visitors

    def visit_ImportPyNode(self, node, context):
        """Pre-import Python modules into _rawpy_global_modules so every
        rawPy{} / rawPyx{} block in this run can use them without an explicit
        'import x' statement at the top of each block.
        """
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
        # Inject modules pre-imported via importPy(){"os","sys",...} in setup()
        py_ns.update(_rawpy_global_modules)
        tbl = context.symbol_table
        while tbl is not None:
            for name, val in tbl.symbols.items():
                if name not in py_ns:
                    if isinstance(val, Number):
                        # Preserve bool type: Lynxer bools become Python bools
                        # so they round-trip correctly after exec.
                        py_ns[name] = bool(val.value) if val.is_bool else val.value
                    elif isinstance(val, String):
                        py_ns[name] = val.value
                    elif isinstance(val, LynxTuple):
                        # Expose as a read-only Python tuple of primitive values
                        py_ns[name] = tuple(
                            e.value if isinstance(e, (Number, String)) else str(e)
                            for e in val.elements
                        )
                    elif isinstance(val, List):
                        # Expose as a read-only Python list of primitive values
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
                # Update in the declaring scope so nested-scope rawPy blocks
                # (e.g. inside a for/while loop) write back to the right place.
                context.symbol_table.update_existing(name, new_val)

        return res.success(Number.null)

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
            # Cython compilation unavailable or interrupted — fall back to exec
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
                # Update in the declaring scope so nested-scope rawPyx blocks
                # (e.g. inside a for/while loop) write back to the right place.
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

        # Accept explicit .lynxc extension; otherwise default to .lynx
        explicit_bytecode = filename.endswith(".lynxc")
        if not filename.endswith(".lynx") and not explicit_bytecode:
            filename += ".lynx"

        module_name = os.path.splitext(os.path.basename(filename))[0]

        # Idempotent: if this module is already loaded, skip re-importing
        existing = global_symbol_table.get(module_name)
        if isinstance(existing, Module):
            return res.success(Number.null)

        file_val = global_symbol_table.get("__file__")
        base_dir = os.path.dirname(file_val.value) if file_val else ""
        filepath = os.path.join(base_dir, filename) if base_dir else filename

        # Resolve path: for .lynx imports, a compiled .lynxc beside the source
        # takes precedence; fall back to stdlib if neither exists locally.
        use_bytecode = explicit_bytecode
        if not explicit_bytecode:
            lynxc_path = os.path.splitext(filepath)[0] + ".lynxc"
            if os.path.exists(lynxc_path):
                filepath = lynxc_path
                use_bytecode = True
            elif not os.path.exists(filepath):
                stdlib_path = os.path.join(STDLIB_DIR, filename)
                if os.path.exists(stdlib_path):
                    filepath = stdlib_path

        if not os.path.exists(filepath):
            return res.failure(
                RTError(
                    node.pos_start,
                    node.pos_end,
                    f"Module \"{module_name}\" not found — checked '{filepath}' and stdlib/",
                    context,
                )
            )

        module_table = SymbolTable(global_symbol_table)
        module_table.set("true", Number.true)
        module_table.set("false", Number.false)
        module_table.set("print", BuiltInFunction.print)
        module_table.set("println", BuiltInFunction.println)
        module_table.set("input", BuiltInFunction.input)
        module_table.set("inputln", BuiltInFunction.inputln)
        module_table.set("rawPy", BuiltInFunction.rawPy)
        module_table.set("rawPyx", BuiltInFunction.rawPyx)
        module_table.set("strOf", BuiltInFunction.strOf)
        module_table.set("intOf", BuiltInFunction.intOf)
        module_table.set("floatOf", BuiltInFunction.floatOf)
        module_table.set("returnType", BuiltInFunction.returnType)
        module_table.set("returnLength", BuiltInFunction.returnLength)
        module_table.set("seqFromTo", BuiltInFunction.seqFromTo)
        module_table.set("range", BuiltInFunction.range)
        module_table.set("cleanRawPyxCache", BuiltInFunction.cleanRawPyxCache)
        module_table.set("listJsonArray", BuiltInFunction.listJsonArray)
        module_table.set("listJsonObject", BuiltInFunction.listJsonObject)
        module_table.set("splitStr", BuiltInFunction.splitStr)
        module_table.set("listFlatten", BuiltInFunction.listFlatten)
        module_table.set("listUnique", BuiltInFunction.listUnique)
        module_table.set("listPush", BuiltInFunction.listPush)
        module_table.set("listPop", BuiltInFunction.listPop)
        module_table.set("listGet", BuiltInFunction.listGet)
        module_table.set("listSet", BuiltInFunction.listSet)
        module_table.set("listSlice", BuiltInFunction.listSlice)
        module_table.set("listContains", BuiltInFunction.listContains)
        module_table.set("listJoin", BuiltInFunction.listJoin)
        module_table.set("listIndex", BuiltInFunction.listIndex)
        module_table.set("listRemove", BuiltInFunction.listRemove)
        module_table.set("anyOf", BuiltInFunction.anyOf)
        module_table.set("allOf", BuiltInFunction.allOf)
        module_table.set("sumOf", BuiltInFunction.sumOf)
        module_table.set("sortList", BuiltInFunction.sortList)
        module_table.set("reverseList", BuiltInFunction.reverseList)
        module_table.set("listMin", BuiltInFunction.listMin)
        module_table.set("listMax", BuiltInFunction.listMax)
        module_table.set("tupleCreate", BuiltInFunction.tupleCreate)
        module_table.set("tupleGet", BuiltInFunction.tupleGet)
        module_table.set("tupleLen", BuiltInFunction.tupleLen)
        module_table.set("tupleContains", BuiltInFunction.tupleContains)
        module_table.set("tupleIndex", BuiltInFunction.tupleIndex)
        module_table.set("tupleSlice", BuiltInFunction.tupleSlice)
        module_table.set("tupleToList", BuiltInFunction.tupleToList)
        module_table.set("listToTuple", BuiltInFunction.listToTuple)
        module_table.set("tupleConcat", BuiltInFunction.tupleConcat)
        module_table.set("tupleCount", BuiltInFunction.tupleCount)
        module_table.set("tupleFirst", BuiltInFunction.tupleFirst)
        module_table.set("tupleLast", BuiltInFunction.tupleLast)
        module_table.set("tupleJsonArray", BuiltInFunction.tupleJsonArray)
        # Class registry for this module (accessible as global.<module>.class)
        module_table.set("class", ClassRegistry())

        if use_bytecode:
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
        if not filename.endswith(".lynx") and not explicit_bytecode:
            filename += ".lynx"

        module_name = os.path.splitext(os.path.basename(filename))[0]

        # Idempotent: skip if this alias is already loaded as a module
        existing = global_symbol_table.get(alias_name)
        if isinstance(existing, Module):
            return res.success(Number.null)

        file_val = global_symbol_table.get("__file__")
        base_dir = os.path.dirname(file_val.value) if file_val else ""
        filepath = os.path.join(base_dir, filename) if base_dir else filename

        use_bytecode = explicit_bytecode
        if not explicit_bytecode:
            lynxc_path = os.path.splitext(filepath)[0] + ".lynxc"
            if os.path.exists(lynxc_path):
                filepath = lynxc_path
                use_bytecode = True
            elif not os.path.exists(filepath):
                stdlib_path = os.path.join(STDLIB_DIR, filename)
                if os.path.exists(stdlib_path):
                    filepath = stdlib_path

        if not os.path.exists(filepath):
            return res.failure(RTError(
                node.pos_start, node.pos_end,
                f"Module \"{module_name}\" not found — checked '{filepath}' and stdlib/",
                context,
            ))

        module_table = SymbolTable(global_symbol_table)
        module_table.set("true", Number.true)
        module_table.set("false", Number.false)
        module_table.set("print", BuiltInFunction.print)
        module_table.set("println", BuiltInFunction.println)
        module_table.set("input", BuiltInFunction.input)
        module_table.set("inputln", BuiltInFunction.inputln)
        module_table.set("rawPy", BuiltInFunction.rawPy)
        module_table.set("rawPyx", BuiltInFunction.rawPyx)
        module_table.set("strOf", BuiltInFunction.strOf)
        module_table.set("intOf", BuiltInFunction.intOf)
        module_table.set("floatOf", BuiltInFunction.floatOf)
        module_table.set("returnType", BuiltInFunction.returnType)
        module_table.set("returnLength", BuiltInFunction.returnLength)
        module_table.set("seqFromTo", BuiltInFunction.seqFromTo)
        module_table.set("range", BuiltInFunction.range)
        module_table.set("cleanRawPyxCache", BuiltInFunction.cleanRawPyxCache)
        module_table.set("listJsonArray", BuiltInFunction.listJsonArray)
        module_table.set("listJsonObject", BuiltInFunction.listJsonObject)
        module_table.set("splitStr", BuiltInFunction.splitStr)
        module_table.set("listFlatten", BuiltInFunction.listFlatten)
        module_table.set("listUnique", BuiltInFunction.listUnique)
        module_table.set("listPush", BuiltInFunction.listPush)
        module_table.set("listPop", BuiltInFunction.listPop)
        module_table.set("listGet", BuiltInFunction.listGet)
        module_table.set("listSet", BuiltInFunction.listSet)
        module_table.set("listSlice", BuiltInFunction.listSlice)
        module_table.set("listContains", BuiltInFunction.listContains)
        module_table.set("listJoin", BuiltInFunction.listJoin)
        module_table.set("listIndex", BuiltInFunction.listIndex)
        module_table.set("listRemove", BuiltInFunction.listRemove)
        module_table.set("anyOf", BuiltInFunction.anyOf)
        module_table.set("allOf", BuiltInFunction.allOf)
        module_table.set("sumOf", BuiltInFunction.sumOf)
        module_table.set("sortList", BuiltInFunction.sortList)
        module_table.set("reverseList", BuiltInFunction.reverseList)
        module_table.set("listMin", BuiltInFunction.listMin)
        module_table.set("listMax", BuiltInFunction.listMax)
        module_table.set("tupleCreate", BuiltInFunction.tupleCreate)
        module_table.set("tupleGet", BuiltInFunction.tupleGet)
        module_table.set("tupleLen", BuiltInFunction.tupleLen)
        module_table.set("tupleContains", BuiltInFunction.tupleContains)
        module_table.set("tupleIndex", BuiltInFunction.tupleIndex)
        module_table.set("tupleSlice", BuiltInFunction.tupleSlice)
        module_table.set("tupleToList", BuiltInFunction.tupleToList)
        module_table.set("listToTuple", BuiltInFunction.listToTuple)
        module_table.set("tupleConcat", BuiltInFunction.tupleConcat)
        module_table.set("tupleCount", BuiltInFunction.tupleCount)
        module_table.set("tupleFirst", BuiltInFunction.tupleFirst)
        module_table.set("tupleLast", BuiltInFunction.tupleLast)
        module_table.set("tupleJsonArray", BuiltInFunction.tupleJsonArray)
        module_table.set("class", ClassRegistry())

        if use_bytecode:
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
global_symbol_table.set("println", BuiltInFunction.println)
global_symbol_table.set("inputln", BuiltInFunction.inputln)
global_symbol_table.set("input", BuiltInFunction.input)
global_symbol_table.set("rawPy", BuiltInFunction.rawPy)
global_symbol_table.set("rawPyx", BuiltInFunction.rawPyx)
global_symbol_table.set("strOf", BuiltInFunction.strOf)
global_symbol_table.set("intOf", BuiltInFunction.intOf)
global_symbol_table.set("floatOf", BuiltInFunction.floatOf)
global_symbol_table.set("returnType", BuiltInFunction.returnType)
global_symbol_table.set("returnLength", BuiltInFunction.returnLength)
global_symbol_table.set("seqFromTo", BuiltInFunction.seqFromTo)
global_symbol_table.set("range", BuiltInFunction.range)
global_symbol_table.set("cleanRawPyxCache", BuiltInFunction.cleanRawPyxCache)
global_symbol_table.set("listJsonArray", BuiltInFunction.listJsonArray)
global_symbol_table.set("listJsonObject", BuiltInFunction.listJsonObject)
global_symbol_table.set("splitStr", BuiltInFunction.splitStr)
global_symbol_table.set("listFlatten", BuiltInFunction.listFlatten)
global_symbol_table.set("listUnique", BuiltInFunction.listUnique)
global_symbol_table.set("listPush", BuiltInFunction.listPush)
global_symbol_table.set("listPop", BuiltInFunction.listPop)
global_symbol_table.set("listGet", BuiltInFunction.listGet)
global_symbol_table.set("listSet", BuiltInFunction.listSet)
global_symbol_table.set("listSlice", BuiltInFunction.listSlice)
global_symbol_table.set("listContains", BuiltInFunction.listContains)
global_symbol_table.set("listJoin", BuiltInFunction.listJoin)
global_symbol_table.set("listIndex", BuiltInFunction.listIndex)
global_symbol_table.set("listRemove", BuiltInFunction.listRemove)
global_symbol_table.set("anyOf", BuiltInFunction.anyOf)
global_symbol_table.set("allOf", BuiltInFunction.allOf)
global_symbol_table.set("sumOf", BuiltInFunction.sumOf)
global_symbol_table.set("sortList", BuiltInFunction.sortList)
global_symbol_table.set("reverseList", BuiltInFunction.reverseList)
global_symbol_table.set("listMin", BuiltInFunction.listMin)
global_symbol_table.set("listMax", BuiltInFunction.listMax)
global_symbol_table.set("asyncRun", BuiltInFunction.asyncRun)
global_symbol_table.set("asyncGather", BuiltInFunction.asyncGather)
global_symbol_table.set("asyncSleep", BuiltInFunction.asyncSleep)
global_symbol_table.set("tupleCreate", BuiltInFunction.tupleCreate)
global_symbol_table.set("tupleGet", BuiltInFunction.tupleGet)
global_symbol_table.set("tupleLen", BuiltInFunction.tupleLen)
global_symbol_table.set("tupleContains", BuiltInFunction.tupleContains)
global_symbol_table.set("tupleIndex", BuiltInFunction.tupleIndex)
global_symbol_table.set("tupleSlice", BuiltInFunction.tupleSlice)
global_symbol_table.set("tupleToList", BuiltInFunction.tupleToList)
global_symbol_table.set("listToTuple", BuiltInFunction.listToTuple)
global_symbol_table.set("tupleConcat", BuiltInFunction.tupleConcat)
global_symbol_table.set("tupleCount", BuiltInFunction.tupleCount)
global_symbol_table.set("tupleFirst", BuiltInFunction.tupleFirst)
global_symbol_table.set("tupleLast", BuiltInFunction.tupleLast)
global_symbol_table.set("tupleJsonArray", BuiltInFunction.tupleJsonArray)
global_symbol_table.set("embedPy", EmbedPyNamespace())

SHARED_INTERPRETER = Interpreter()


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

    interpreter = SHARED_INTERPRETER
    context = Context("<program>")
    context.symbol_table = global_symbol_table
    global_symbol_table.set("__file__", String(os.path.abspath(fn)))
    global_symbol_table.set("global", Namespace(global_symbol_table))
    # Fresh class registry for this run (accessible as global.class)
    global_symbol_table.set("class", ClassRegistry())

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

    interpreter = SHARED_INTERPRETER
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


# ── Bytecode compile / run ────────────────────────────────────────────────────

def compile_to_bytecode(fn, text):
    """Parse *text* (source of *fn*) and write a ``.lynxc`` file beside it.

    Returns ``(out_path, None)`` on success or ``(None, error)`` on failure.
    The ``.lynxc`` file is a binary pickle prefixed with ``BYTECODE_MAGIC``.
    """
    lexer = Lexer(fn, text)
    tokens, error = lexer.make_tokens()
    if error:
        return None, error

    parser = Parser(tokens)
    ast = parser.parse()
    if ast.error:
        return None, ast.error

    out_path = os.path.splitext(os.path.abspath(fn))[0] + ".lynxc"
    data = {
        "version": BYTECODE_VERSION,
        "source":  os.path.abspath(fn),
        "node":    ast.node,
    }
    with open(out_path, "wb") as f:
        f.write(BYTECODE_MAGIC)
        pickle.dump(data, f, protocol=pickle.HIGHEST_PROTOCOL)

    return out_path, None


def _load_bytecode(fn):
    """Read and unpickle a ``.lynxc`` file.  Returns the data dict."""
    with open(fn, "rb") as f:
        magic = f.read(len(BYTECODE_MAGIC))
        if magic != BYTECODE_MAGIC:
            raise ValueError(
                f"'{fn}' is not a valid Lynxer bytecode file "
                f"(bad magic bytes: {magic!r})"
            )
        return pickle.load(f)


def run_bytecode(fn):
    """Load and execute a pre-compiled ``.lynxc`` bytecode file.

    Returns ``(value, error)`` — the same contract as :func:`run`.
    """
    try:
        data = _load_bytecode(fn)
    except Exception as e:
        # Wrap in a plain Python exception; shell.py will print it.
        raise RuntimeError(str(e)) from e

    node = data["node"]
    interpreter = SHARED_INTERPRETER
    context = Context("<program>")
    context.symbol_table = global_symbol_table
    global_symbol_table.set("__file__", String(os.path.abspath(fn)))
    global_symbol_table.set("global", Namespace(global_symbol_table))
    global_symbol_table.set("class", ClassRegistry())

    result = interpreter.visit(node, context)
    return result.value, result.error


def run_bytecode_file(fn, symbol_table):
    """Load and execute a ``.lynxc`` file as a module (used by ``import()``).

    Returns ``None`` on success or an ``Error`` on failure — the same
    contract as :func:`run_file`.
    """
    try:
        data = _load_bytecode(fn)
    except Exception as e:
        # Surface as a plain string; visit_ImportNode wraps it in RTError.
        raise RuntimeError(str(e)) from e

    node = data["node"]
    interpreter = SHARED_INTERPRETER
    context = Context(f"<import:{os.path.basename(fn)}>")
    context.symbol_table = symbol_table
    symbol_table.set("__file__", String(os.path.abspath(fn)))

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
