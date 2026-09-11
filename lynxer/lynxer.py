"""Lynxer compiler and interpreter pipeline.

The implementation lives in the focused submodules:

* :mod:`lynxer.error` -- diagnostics and deprecation-warning machinery
* :mod:`lynxer.lexer` -- tokens, keywords, and the lexer
* :mod:`lynxer.lynxerAst` -- abstract-syntax-tree nodes
* :mod:`lynxer.parser` -- the recursive-descent parser
* :mod:`lynxer.values` -- runtime values, results, contexts, symbol tables
* :mod:`lynxer.runtime` -- the interpreter and program entry points

This module re-exports the public surface so historic imports such as
``from lynxer.lynxer import run, Lexer, Parser`` keep working.
"""

from __future__ import annotations

# Attribute access on this module falls back to the owning submodule. This
# serves two purposes: names that are rebound while the interpreter runs
# (``global_symbol_table``, ``_setup_in_progress``, ...) and names that only
# appear once the builtins module has been registered stay live, and partial
# initialization keeps working when ``lynxer.builtins`` reaches back into
# this module while ``lynxer.runtime`` is still executing -- exactly like the
# pre-split monolith did. The fallback must be defined before the imports
# below so it is active during this module's own initialization.
_SUBMODULES = ("error", "lexer", "lynxerAst", "parser", "values", "runtime")


def __getattr__(name):
    import sys as _sys

    for module_name in _SUBMODULES:
        module = _sys.modules.get(f"lynxer.{module_name}")
        if module is not None and hasattr(module, name):
            return getattr(module, name)
    raise AttributeError(f"module {__name__!r} has no attribute {name!r}")


def __dir__():
    import sys as _sys

    module_names = set(globals())
    for module_name in _SUBMODULES:
        module = _sys.modules.get(f"lynxer.{module_name}")
        if module is not None:
            module_names.update(dir(module))
    return sorted(module_names)


import itertools  # noqa: F401 — historic module attribute
import os  # noqa: F401 — historic module attribute
import string  # noqa: F401 — historic module attribute
import sys  # noqa: F401 — historic module attribute
import textwrap  # noqa: F401 — historic module attribute
import warnings  # noqa: F401 — historic module attribute
from typing import Any, ClassVar  # noqa: F401 — historic module attributes

from .bytecode import (  # noqa: F401 — re-exported for backwards compatibility
    BYTECODE_MAGIC,
    BYTECODE_VERSION,
    compile_to_bytecode,
    run_bytecode,
    run_bytecode_file,
)
from .error import (  # noqa: F401
    Error,
    ExpectedCharError,
    IllegalCharError,
    InvalidSyntaxError,
    LynxerForeverWarning,
    LynxSyntaxDeprecationWarning,
    RTError,
    _emit_deprecation_warning,
    _flush_deprecation_warnings,
    _load_warning_messages,
    _warning_message_paths,
    warn_forever_no_break,
    warn_legacy_syntax,
    warn_legacy_syntax_position,
    warning_message,
)
from .lexer import (  # noqa: F401
    DIGITS,
    INTER_ESCAPE_MARK,
    INTERPOLATION_BUILTINS,
    KEYWORDS,
    LETTERS,
    LETTERS_DIGITS,
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
from .lynxerAst import (  # noqa: F401
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
    DocstringNode,
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
    _block_contains_break,
    _uses_shared_parameters,
)
from .parser import Parser, ParseResult  # noqa: F401
from .values import (  # noqa: F401
    FLOAT_RANGES,
    INTEGER_RANGES,
    NUMERIC_TYPES,
    Address,
    AsyncFunction,
    BaseFunction,
    BoundMethod,
    Char,
    ClassBlueprint,
    ClassInstance,
    ClassRegistry,
    CodeBlockValue,
    Context,
    CoroutineValue,
    EmbedPyCallable,
    EmbedPyModule,
    EmbedPyNamespace,
    EmbedPyObject,
    EnumConstructor,
    EnumType,
    EnumValue,
    Function,
    FunctionAddress,
    List,
    LocalNamespace,
    LynxTuple,
    Module,
    Namespace,
    NativeHandle,
    Null,
    Number,
    ObjectValue,
    RTResult,
    Sentinel,
    String,
    StructBlueprint,
    StructInstance,
    SymbolTable,
    Value,
    VarGroup,
    _build_exec_bindings,
    _exec_codeblock_variable_names,
    _get_cpp,
    _lynx_to_python,
    _python_to_lynx,
    type_matches,
    value_type_name,
)

try:  # circular: lynxer.runtime may be mid-initialization via lynxer.builtins
    from .runtime import (  # noqa: F401
        SHARED_INTERPRETER,
        STDLIB_DIR,
        Interpreter,
        _can_call_global,
        _get_current_global_path,
        _get_cython_inline,
        _interpreter_error,
        _join_outstanding_native_threads,
        _lynx_modules,
        _lynxer_callback_dispatcher,
        _module_path,
        _new_global_symbol_table,
        _preregister_nested_globals,
        _python_to_lynxer_callback_value,
        _rawpy_global_modules,
        _register_builtins,
        reset_runtime_state,
        run,
        run_file,
        stdlib_dir,
    )
except ImportError:  # attribute access falls back to lynxer.runtime
    pass

