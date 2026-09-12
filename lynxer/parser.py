"""The Lynxer recursive-descent parser."""

from __future__ import annotations

from typing import Any

from .error import (
    Error,
    InvalidSyntaxError,
    warn_legacy_syntax,
    warn_legacy_syntax_position,
    warning_message,
)
from .lexer import (
    INTER_ESCAPE_MARK,
    INTERPOLATION_BUILTINS,
    TT_AMP,
    TT_BITWISE_NAND,
    TT_BITWISE_NOR,
    TT_BITWISE_XNOR,
    TT_CARET,
    TT_CHAR,
    TT_COMMA,
    TT_DIV,
    TT_DIVEQ,
    TT_DOCSTRING,
    TT_DOT,
    TT_EOF,
    TT_EQ,
    TT_EQEQ,
    TT_EXEC_BLOCK,
    TT_FLOAT,
    TT_FLOORDIV,
    TT_FLOORDIVEQ,
    TT_GT,
    TT_GTE,
    TT_IDENTIFIER,
    TT_INT,
    TT_INTER_STRING,
    TT_KEYWORD,
    TT_LBRACE,
    TT_LBRACKET,
    TT_LOGICAL_AND,
    TT_LOGICAL_NAND,
    TT_LOGICAL_NOR,
    TT_LOGICAL_NOT,
    TT_LOGICAL_OR,
    TT_LPAREN,
    TT_LT,
    TT_LTE,
    TT_MINUS,
    TT_MINUSEQ,
    TT_MOD,
    TT_MODEQ,
    TT_MUL,
    TT_MULEQ,
    TT_NE,
    TT_PIPE,
    TT_PLUS,
    TT_PLUSEQ,
    TT_POW,
    TT_POWEQ,
    TT_RAWPY_BLOCK,
    TT_RAWPYX_BLOCK,
    TT_RBRACE,
    TT_RBRACKET,
    TT_ROOT,
    TT_ROOTEQ,
    TT_RPAREN,
    TT_SEMICOLON,
    TT_SHL,
    TT_SHR,
    TT_STRING,
    TT_TILDE,
    TYPE_KEYWORDS,
    Lexer,
    Position,
    Token,
)
from .lynxerAst import (
    AddVarGroupNode,
    AsyncDotCallNode,
    AsyncLocalDefNode,
    AwaitNode,
    BinOpNode,
    BlockNode,
    BoolNode,
    BreakNode,
    CallNode,
    CaseNode,
    CharNode,
    ClassDefNode,
    CodeBlockLiteralNode,
    CodeBlockRefNode,
    ContinueNode,
    DefaultNode,
    DotAccessNode,
    DotAssignNode,
    DoWhileNode,
    EnumDefNode,
    ExecBlockNode,
    ExecCallNode,
    ExecFileNode,
    ForeverNode,
    ForNode,
    FuncDefNode,
    IfNode,
    ImportAsNode,
    ImportNode,
    ImportPyNode,
    InterpolatedStringNode,
    IterateNode,
    ListElementNode,
    ListNode,
    NewNode,
    NoneNode,
    NumberNode,
    PatternNode,
    ProgramNode,
    RawPyBlockNode,
    RawPyxBlockNode,
    RemoveVarGroupNode,
    ReturnNode,
    SharedNode,
    StringNode,
    StructDefNode,
    SwitchNode,
    TryCatchNode,
    TupleNode,
    UnaryOpNode,
    VarAccessNode,
    VarAssignNode,
    VarDeclNode,
    VarGroupDeclNode,
    WhileNode,
    block_contains_break,
)
from .parser_result import ParseResult

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
        self._file_func_names = {}
        self._declared_function_names = set()
        self._enum_names = {}
        self._require_main = True
        self._inter_allowed = False  # whether inter"..." may appear right here
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

    def claim_file_func_name(self, name_tok):
        """Reserve a direct-call ``func`` name for this source file.

        ``func`` declarations are deliberately source-wide.  Keeping the
        declaration token lets duplicate-name diagnostics point at the
        original declaration as well as the conflicting one.
        """
        previous = self._file_func_names.get(name_tok.value)
        if previous is not None:
            return InvalidSyntaxError(
                name_tok.pos_start,
                name_tok.pos_end,
                f"Duplicate 'func' declaration '{name_tok.value}'. "
                "A func name may be declared only once in a Lynxer file "
                f"(first declared at line {previous.pos_start.ln + 1}).",
            )
        if name_tok.value in self._declared_function_names:
            return InvalidSyntaxError(
                name_tok.pos_start,
                name_tok.pos_end,
                f"'func {name_tok.value}()' conflicts with another function "
                "declared in this Lynxer file. Direct-call function names must "
                "be unique and cannot shadow existing functions.",
            )
        if name_tok.value in self._enum_names:
            return InvalidSyntaxError(
                name_tok.pos_start,
                name_tok.pos_end,
                f"'func {name_tok.value}()' conflicts with the 'enum' "
                "declaration of the same name. Enum names and function names "
                "share one namespace.",
            )
        if name_tok.value in {"setup", "main"}:
            return InvalidSyntaxError(
                name_tok.pos_start,
                name_tok.pos_end,
                f"'func {name_tok.value}()' is reserved for the program "
                "lifecycle and must be declared with its required global form.",
            )
        self._file_func_names[name_tok.value] = name_tok
        self._declared_function_names.add(name_tok.value)
        return None

    def claim_function_name_against_file_funcs(self, name_tok):
        """Reject a later ordinary function that would shadow a ``func``."""
        if name_tok.value in self._file_func_names:
            return InvalidSyntaxError(
                name_tok.pos_start,
                name_tok.pos_end,
                f"Function '{name_tok.value}' conflicts with the file-wide "
                "'func' declaration of the same name.",
            )
        if name_tok.value in self._enum_names:
            return InvalidSyntaxError(
                name_tok.pos_start,
                name_tok.pos_end,
                f"Function '{name_tok.value}' conflicts with the 'enum' "
                "declaration of the same name. Enum names and function names "
                "share one namespace.",
            )
        self._declared_function_names.add(name_tok.value)
        return None

    def claim_enum_name(self, name_tok):
        """Reserve an ``enum`` name for this source file.

        Enum names are source-wide, like ``func`` names. Keeping the
        declaration token lets duplicate-name diagnostics point at the
        original declaration as well as the conflicting one.
        """
        previous = self._enum_names.get(name_tok.value)
        if previous is not None:
            return InvalidSyntaxError(
                name_tok.pos_start,
                name_tok.pos_end,
                f"Duplicate 'enum' declaration '{name_tok.value}'. "
                "An enum name may be declared only once in a Lynxer file "
                f"(first declared at line {previous.pos_start.ln + 1}).",
            )
        if (
            name_tok.value in self._file_func_names
            or name_tok.value in self._declared_function_names
        ):
            return InvalidSyntaxError(
                name_tok.pos_start,
                name_tok.pos_end,
                f"'enum {name_tok.value}' conflicts with a function "
                "declaration of the same name. Enum names and function names "
                "share one namespace.",
            )
        self._enum_names[name_tok.value] = name_tok
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
            if (
                self.current_tok.matches(TT_KEYWORD, "async")
                and next_tok is not None
                and next_tok.matches(TT_KEYWORD, "func")
            ):
                return res.failure(InvalidSyntaxError(
                    self.current_tok.pos_start,
                    next_tok.pos_end,
                    "'async func' declarations are not allowed. "
                    "File-wide 'func' declarations must be top-level; "
                    "async functions may call them but cannot declare them.",
                ))
            is_file_func_kw = self.current_tok.matches(TT_KEYWORD, "func")
            is_func_kw = (
                self.current_tok.matches(TT_KEYWORD, "global")
                or (
                    self.current_tok.type == TT_IDENTIFIER
                    and self.current_tok.value == "global"
                    and next_tok is not None
                    and next_tok.type == TT_IDENTIFIER
                )
            )
            if is_file_func_kw:
                if main_seen:
                    return res.failure(InvalidSyntaxError(
                        self.current_tok.pos_start,
                        self.current_tok.pos_end,
                        "A 'func' declaration must appear before 'global main(){}'. "
                        "No declarations are allowed after main.",
                    ))
                node = res.register(self.parse_func_def())
                if res.error:
                    return res
                globals_list.append(node)
                any_other_seen = True
            elif is_func_kw:
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
                    self._declared_function_names.add(func_name_tok.value)
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
                    self._declared_function_names.add(func_name_tok.value)
                    main_seen = True
                else:
                    if main_seen:
                        fname = func_name_tok.value if func_name_tok else "..."
                        return res.failure(InvalidSyntaxError(
                            self.current_tok.pos_start, self.current_tok.pos_end,
                            f"'global {fname}' must be declared "
                            f"before 'global main(){{}}'. No declarations are allowed after main.",
                        ))
                    collision = self.claim_function_name_against_file_funcs(func_name_tok)
                    if collision is not None:
                        return res.failure(collision)
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
            elif self.current_tok.matches(TT_KEYWORD, "enum"):
                if main_seen:
                    return res.failure(InvalidSyntaxError(
                        self.current_tok.pos_start, self.current_tok.pos_end,
                        "Enum definitions must appear before 'global main(){}'. "
                        "No declarations are allowed after main.",
                    ))
                node = res.register(self.parse_enum_def())
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

    def parse_enum_def(self):
        """Parse ``enum Name = [Variant(type field), Empty] { ... }``."""
        res = ParseResult()
        pos_start = self.current_tok.pos_start.copy()
        res.register_advancement()
        self.advance()
        if self.current_tok.type != TT_IDENTIFIER:
            return res.failure(InvalidSyntaxError(
                self.current_tok.pos_start, self.current_tok.pos_end,
                "Expected an enum name after 'enum'",
            ))
        name_tok = self.current_tok
        name_error = self.claim_enum_name(name_tok)
        if name_error:
            return res.failure(name_error)
        res.register_advancement()
        self.advance()
        if self.current_tok.type != TT_EQ:
            return res.failure(InvalidSyntaxError(
                self.current_tok.pos_start, self.current_tok.pos_end,
                "Expected '=' after enum name",
            ))
        res.register_advancement()
        self.advance()
        if self.current_tok.type != TT_LBRACKET:
            return res.failure(InvalidSyntaxError(
                self.current_tok.pos_start, self.current_tok.pos_end,
                "Expected '[' before enum variants",
            ))
        res.register_advancement()
        self.advance()
        variants = []
        seen = set()
        while self.current_tok.type != TT_RBRACKET:
            if self.current_tok.type != TT_IDENTIFIER:
                return res.failure(InvalidSyntaxError(
                    self.current_tok.pos_start, self.current_tok.pos_end,
                    "Expected an enum variant name",
                ))
            variant_tok = self.current_tok
            if variant_tok.value in seen:
                return res.failure(InvalidSyntaxError(
                    variant_tok.pos_start, variant_tok.pos_end,
                    f"Duplicate enum variant '{variant_tok.value}'",
                ))
            seen.add(variant_tok.value)
            res.register_advancement()
            self.advance()
            fields = []
            if self.current_tok.type == TT_LPAREN:
                res.register_advancement()
                self.advance()
                while self.current_tok.type != TT_RPAREN:
                    if not self.is_type_name():
                        return res.failure(InvalidSyntaxError(
                            self.current_tok.pos_start, self.current_tok.pos_end,
                            "Expected a payload type and name in enum variant",
                        ))
                    type_name = self.current_tok.value
                    res.register_advancement()
                    self.advance()
                    if self.current_tok.type != TT_IDENTIFIER:
                        return res.failure(InvalidSyntaxError(
                            self.current_tok.pos_start, self.current_tok.pos_end,
                            "Expected an enum payload field name",
                        ))
                    field_tok = self.current_tok
                    fields.append((type_name, field_tok.value))
                    res.register_advancement()
                    self.advance()
                    if self.current_tok.type == TT_COMMA:
                        res.register_advancement()
                        self.advance()
                    elif self.current_tok.type != TT_RPAREN:
                        return res.failure(InvalidSyntaxError(
                            self.current_tok.pos_start, self.current_tok.pos_end,
                            "Expected ',' or ')' in enum payload",
                        ))
                res.register_advancement()
                self.advance()
            variants.append((variant_tok.value, fields))
            if self.current_tok.type == TT_COMMA:
                res.register_advancement()
                self.advance()
            elif self.current_tok.type != TT_RBRACKET:
                return res.failure(InvalidSyntaxError(
                    self.current_tok.pos_start, self.current_tok.pos_end,
                    "Expected ',' or ']' after enum variant",
                ))
        res.register_advancement()
        self.advance()
        if self.current_tok.type != TT_LBRACE:
            return res.failure(InvalidSyntaxError(
                self.current_tok.pos_start, self.current_tok.pos_end,
                "Expected '{' after enum variants",
            ))
        body = res.register(self.parse_block(
            allow_local_funcs=True, allow_function_defs=False
        ))
        if res.error:
            return res
        return res.success(EnumDefNode(name_tok, variants, body, pos_start, body.pos_end))

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
            or self.current_tok.matches(TT_KEYWORD, "func")
        ):
            return res.failure(
                InvalidSyntaxError(
                    self.current_tok.pos_start,
                    self.current_tok.pos_end,
                    "Expected 'global', 'local', or 'func'"
                    + (" after 'async'" if is_async else ""),
                )
            )
        is_file_func = self.current_tok.matches(TT_KEYWORD, "func")
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
        if is_file_func:
            collision = self.claim_file_func_name(name_tok)
            if collision is not None:
                return res.failure(collision)
        elif self.current_tok.type == TT_IDENTIFIER or self.current_tok.type == TT_KEYWORD:
            collision = self.claim_function_name_against_file_funcs(name_tok)
            if collision is not None:
                return res.failure(collision)
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
        ) or kind_tok.value == "func"

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

        if self.current_tok.matches(TT_KEYWORD, "func"):
            return res.failure(InvalidSyntaxError(
                self.current_tok.pos_start,
                self.current_tok.pos_end,
                "'func' declarations are only allowed at the top level of a "
                "Lynxer file; local function bodies cannot declare file-wide funcs",
            ))

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
            if peek1 and peek1.matches(TT_KEYWORD, "func"):
                return res.failure(InvalidSyntaxError(
                    self.current_tok.pos_start,
                    peek1.pos_end,
                    "'async func' declarations are not allowed. "
                    "File-wide 'func' declarations must be top-level; "
                    "async functions may call them but cannot declare them.",
                ))
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

        if (
            type_tok.value == "codeblock"
            and isinstance(value, CodeBlockLiteralNode)
            and self.current_tok.type == TT_LBRACKET
        ):
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

        if (
            type_tok.value == "codeblock"
            and isinstance(value, CodeBlockLiteralNode)
            and self.current_tok.type == TT_LBRACKET
        ):
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
                has_break=block_contains_break(body),
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

    def _switch_pattern_equal(self, left, right):
        """Compare patterns without treating source locations as semantics.

        Literal expressions are intentionally compared structurally.  That
        catches repeated literal syntax such as ``case(2)``/``case(2)`` while
        avoiding evaluation during parsing (``case(1 + 1)`` versus
        ``case(2)`` remains a valid runtime-equivalence edge case).
        """
        if not isinstance(left, PatternNode) or not isinstance(right, PatternNode):
            return False
        if left.kind != right.kind:
            return False
        if left.kind in {"wildcard", "binding"}:
            return left.kind == right.kind and (
                left.kind == "wildcard" or left.value == right.value
            )
        if left.kind == "literal":
            return self._switch_ast_equal(left.value, right.value)
        if left.kind == "enum":
            return (
                left.value == right.value
                and len(left.items) == len(right.items)
                and all(
                    self._switch_pattern_equal(a, b)
                    for a, b in zip(left.items, right.items)
                )
            )
        if left.kind in {"list", "tuple"}:
            return len(left.items) == len(right.items) and all(
                self._switch_pattern_equal(a, b)
                for a, b in zip(left.items, right.items)
            )
        return False

    def _switch_ast_equal(self, left, right):
        """Compare literal AST values while ignoring positions."""
        if type(left) is not type(right):
            return False
        if isinstance(left, Token):
            return left.type == right.type and left.value == right.value
        if isinstance(left, (str, int, float, bool, type(None))):
            return left == right
        if isinstance(left, list):
            return len(left) == len(right) and all(
                self._switch_ast_equal(a, b) for a, b in zip(left, right)
            )
        if not hasattr(left, "__dict__") or not hasattr(right, "__dict__"):
            return left == right
        left_attrs = {
            key for key in vars(left) if key not in {"pos_start", "pos_end"}
        }
        right_attrs = {
            key for key in vars(right) if key not in {"pos_start", "pos_end"}
        }
        if left_attrs != right_attrs:
            return False
        return all(
            self._switch_ast_equal(getattr(left, key), getattr(right, key))
            for key in left_attrs
        )

    @staticmethod
    def _switch_pattern_is_universal(pattern):
        """Whether a top-level pattern matches every scrutinee value."""
        return (
            isinstance(pattern, PatternNode)
            and pattern.kind in {"wildcard", "binding"}
        )

    def _validate_switch_patterns(self, cases, res):
        """Reject duplicate and obviously unreachable case patterns.

        This is deliberately conservative: only structural duplicates and a
        top-level wildcard/binding are diagnosed.  Proving equivalence of
        arbitrary expressions or nested type patterns would require evaluating
        user code during parsing and would be less safe than a runtime match.
        """
        seen = []
        for case in cases:
            if not isinstance(case, CaseNode):
                continue
            for previous in seen:
                if self._switch_pattern_is_universal(previous):
                    return res.failure(InvalidSyntaxError(
                        case.pos_start,
                        case.pos_end,
                        "Unreachable switch pattern: a previous wildcard or "
                        "binding pattern matches every value.",
                    ))
                if self._switch_pattern_equal(previous, case.match_node):
                    return res.failure(InvalidSyntaxError(
                        case.pos_start,
                        case.pos_end,
                        "Duplicate switch pattern: this case is already "
                        "covered by an earlier case.",
                    ))
            seen.append(case.match_node)
        return None

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

        pattern_error = self._validate_switch_patterns(cases, res)
        if pattern_error is not None:
            return pattern_error

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
        match_node = res.register(self.parse_switch_pattern())
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

    def parse_switch_pattern(self):
        """Parse scalar, wildcard, sequence, and enum switch patterns."""
        res = ParseResult()
        tok = self.current_tok
        if tok.type == TT_IDENTIFIER and tok.value == "_":
            res.register_advancement()
            self.advance()
            return res.success(PatternNode(
                "wildcard", pos_start=tok.pos_start, pos_end=tok.pos_end
            ))
        next_tok = self.peek(1)
        if (
            tok.type == TT_IDENTIFIER
            and not (
                next_tok is not None
                and next_tok.type in (TT_DOT, TT_LPAREN)
            )
        ):
            res.register_advancement()
            self.advance()
            return res.success(PatternNode(
                "binding", value=tok.value,
                pos_start=tok.pos_start, pos_end=tok.pos_end,
            ))

        # Qualified enum pattern: Result.Ok(...) or Result.Ok.
        dot_tok = self.peek(1)
        variant_tok = self.peek(2)
        if (
            tok.type == TT_IDENTIFIER
            and dot_tok is not None and dot_tok.type == TT_DOT
            and variant_tok is not None and variant_tok.type == TT_IDENTIFIER
        ):
            enum_name = tok.value
            after_variant_tok = self.peek(3)
            if after_variant_tok is not None and after_variant_tok.type == TT_LPAREN:
                res.register_advancement(); self.advance()
                res.register_advancement(); self.advance()
                res.register_advancement(); self.advance()
                res.register_advancement(); self.advance()
                items = []
                while self.current_tok.type != TT_RPAREN:
                    items.append(res.register(self.parse_switch_pattern()))
                    if res.error:
                        return res
                    if self.current_tok.type == TT_COMMA:
                        res.register_advancement(); self.advance()
                    elif self.current_tok.type != TT_RPAREN:
                        return res.failure(InvalidSyntaxError(
                            self.current_tok.pos_start, self.current_tok.pos_end,
                            "Expected ',' or ')' in enum switch pattern",
                        ))
                res.register_advancement(); self.advance()
                return res.success(PatternNode(
                    "enum", value=(enum_name, variant_tok.value), items=items,
                    pos_start=tok.pos_start, pos_end=self.current_tok.pos_end,
                ))
            res.register_advancement(); self.advance()
            res.register_advancement(); self.advance()
            res.register_advancement(); self.advance()
            return res.success(PatternNode(
                "enum", value=(enum_name, variant_tok.value), items=[],
                pos_start=tok.pos_start, pos_end=variant_tok.pos_end,
            ))

        if tok.type in (TT_LBRACKET, TT_LPAREN):
            opening = tok.type
            closing = TT_RBRACKET if opening == TT_LBRACKET else TT_RPAREN
            res.register_advancement()
            self.advance()
            items = []
            while self.current_tok.type != closing:
                if self.current_tok.type == TT_IDENTIFIER and self.current_tok.value == "_":
                    item = res.register(self.parse_switch_pattern())
                else:
                    # Sequence patterns accept the normal typed literal form
                    # as well as untyped scalar literals.
                    if self.is_type_keyword() and self.peek(1) is not None:
                        res.register_advancement()
                        self.advance()
                    item = res.register(self.parse_switch_pattern())
                if res.error:
                    return res
                items.append(item)
                if self.current_tok.type == TT_COMMA:
                    res.register_advancement(); self.advance()
                elif self.current_tok.type != closing:
                    return res.failure(InvalidSyntaxError(
                        self.current_tok.pos_start, self.current_tok.pos_end,
                        "Expected ',' or closing delimiter in switch pattern",
                    ))
            pos_end = self.current_tok.pos_end.copy()
            res.register_advancement()
            self.advance()
            kind = "list" if opening == TT_LBRACKET else "tuple"
            return res.success(PatternNode(
                kind, items=items, pos_start=tok.pos_start, pos_end=pos_end
            ))

        literal = res.register(self.parse_expr())
        if res.error:
            return res
        return res.success(PatternNode(
            "literal", value=literal, pos_start=literal.pos_start, pos_end=literal.pos_end
        ))

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
                    inter_allowed = (
                        isinstance(atom, VarAccessNode)
                        and atom.var_name_tok.value in INTERPOLATION_BUILTINS
                    )

                    if self.current_tok.type != TT_RPAREN:
                        outer_inter = self._inter_allowed
                        self._inter_allowed = inter_allowed
                        arg_nodes.append(res.register(self.parse_expr()))
                        if res.error:
                            return res

                        while self.current_tok.type == TT_COMMA:
                            res.register_advancement()
                            self.advance()
                            arg_nodes.append(res.register(self.parse_expr()))
                            if res.error:
                                return res
                        self._inter_allowed = outer_inter

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

        elif tok.type == TT_INTER_STRING:
            return self.parse_inter_string()

        elif tok.matches(TT_KEYWORD, "inter"):
            next_tok = self.peek(1)
            return res.failure(
                InvalidSyntaxError(
                    tok.pos_start,
                    next_tok.pos_end if next_tok else tok.pos_end,
                    "Expected a string literal after 'inter' "
                    '(e.g. inter"Hello, {name}!")',
                )
            )

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

    def _inter_body_offset(self, tok):
        """Return the offset of the first body character inside the quotes."""
        text = tok.pos_start.ftxt or ""
        start = tok.pos_start.idx
        end = tok.pos_end.idx if tok.pos_end.idx > start else len(text)
        quote = text.find('"', start, end)
        if quote == -1:
            return len("inter") + 1
        return quote - start + 1

    def _inter_position(self, tok, offset):
        """Return a file position for *offset* characters into an inter body."""
        pos = tok.pos_start.copy()
        delta = self._inter_body_offset(tok) + offset
        pos.idx += delta
        pos.col += delta
        return pos

    def parse_inter_expression(self, tok, source, offset):
        """Parse one ``{...}`` body of an ``inter"..."`` literal."""
        res = ParseResult()
        lexer = Lexer(tok.pos_start.fn, source)
        inner_tokens, error = lexer.make_tokens()
        if error:
            return res.failure(
                InvalidSyntaxError(
                    self._inter_position(tok, offset),
                    self._inter_position(tok, offset + len(source)),
                    f"Invalid expression inside inter\"...\": {error.details}",
                )
            )

        # The inner lexer measured positions against the body text; move them
        # back onto the real file so diagnostics point at the right place.
        delta = self._inter_body_offset(tok) + offset
        for inner_tok in inner_tokens:
            for pos in (inner_tok.pos_start, inner_tok.pos_end):
                pos.idx += tok.pos_start.idx + delta
                pos.col += tok.pos_start.col + delta
                pos.ftxt = tok.pos_start.ftxt

        inner_parser = Parser(inner_tokens)
        node = res.register(inner_parser.parse_expr())
        if res.error:
            return res

        if inner_parser.current_tok.type != TT_EOF:
            return res.failure(
                InvalidSyntaxError(
                    inner_parser.current_tok.pos_start,
                    inner_parser.current_tok.pos_end,
                    "Unexpected text inside inter\"...\" interpolation; "
                    "expected a single value or path",
                )
            )
        return res.success(node)

    def parse_inter_string(self):
        """Parse ``inter"Hello, {name}!"`` into an interpolated string node."""
        res = ParseResult()
        tok = self.current_tok
        res.register_advancement()
        self.advance()

        if not self._inter_allowed:
            return res.failure(
                InvalidSyntaxError(
                    tok.pos_start,
                    tok.pos_end,
                    "inter\"...\" interpolation is only allowed in "
                    "print, println, input, and inputln",
                )
            )

        raw = tok.value if isinstance(tok.value, str) else ""
        literal_parts = []
        value_nodes = []
        buffer: list[str] = []
        expr_source = None
        expr_offset = 0
        index = 0

        while index < len(raw):
            char = raw[index]
            if char == INTER_ESCAPE_MARK:
                index += 1
                if index < len(raw):
                    if expr_source is None:
                        buffer.append(raw[index])
                    else:
                        expr_source += raw[index]
                index += 1
                continue
            if char == "{":
                if expr_source is not None:
                    return res.failure(
                        InvalidSyntaxError(
                            self._inter_position(tok, index),
                            self._inter_position(tok, index + 1),
                            "Nested '{' inside inter\"...\"; write '\\{' for a "
                            "literal brace",
                        )
                    )
                literal_parts.append("".join(buffer))
                buffer = []
                expr_source = ""
                expr_offset = index + 1
                index += 1
                continue
            if char == "}":
                if expr_source is None:
                    return res.failure(
                        InvalidSyntaxError(
                            self._inter_position(tok, index),
                            self._inter_position(tok, index + 1),
                            "Unmatched '}' inside inter\"...\"; write '\\}' for "
                            "a literal brace",
                        )
                    )
                source = expr_source.strip()
                if not source:
                    return res.failure(
                        InvalidSyntaxError(
                            self._inter_position(tok, expr_offset - 1),
                            self._inter_position(tok, index + 1),
                            "Empty '{}' inside inter\"...\"; expected a variable "
                            "or value path",
                        )
                    )
                node = res.register(
                    self.parse_inter_expression(tok, source, expr_offset)
                )
                if res.error:
                    return res
                value_nodes.append(node)
                expr_source = None
                index += 1
                continue
            if expr_source is None:
                buffer.append(char)
            else:
                expr_source += char
            index += 1

        if expr_source is not None:
            return res.failure(
                InvalidSyntaxError(
                    self._inter_position(tok, expr_offset - 1),
                    tok.pos_end,
                    "Missing '}' to close the interpolation in inter\"...\"",
                )
            )

        literal_parts.append("".join(buffer))
        return res.success(
            InterpolatedStringNode(tok, literal_parts, value_nodes)
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

