"""The Lynxer interpreter, execution contexts, and program entry points."""

from __future__ import annotations

import os
import sys
import textwrap
from typing import Any

from . import error as _error
from .bytecode import run_bytecode_file
from .error import (
    _flush_deprecation_warnings,
    warn_forever_no_break,
    warn_legacy_syntax_position,
    warning_message,
)
from .lexer import (
    TT_AMP,
    TT_BITWISE_NAND,
    TT_BITWISE_NOR,
    TT_BITWISE_XNOR,
    TT_CARET,
    TT_DIV,
    TT_EQEQ,
    TT_FLOORDIV,
    TT_GT,
    TT_GTE,
    TT_IDENTIFIER,
    TT_KEYWORD,
    TT_LOGICAL_AND,
    TT_LOGICAL_NAND,
    TT_LOGICAL_NOR,
    TT_LOGICAL_NOT,
    TT_LOGICAL_OR,
    TT_LT,
    TT_LTE,
    TT_MINUS,
    TT_MOD,
    TT_MUL,
    TT_NE,
    TT_PIPE,
    TT_PLUS,
    TT_POW,
    TT_ROOT,
    TT_SHL,
    TT_SHR,
    TT_TILDE,
    Lexer,
    Position,
)
from .lynxerAst import (
    DefaultNode,
    DoWhileNode,
    ForeverNode,
    ForNode,
    FuncDefNode,
    IfNode,
    IterateNode,
    ListNode,
    PatternNode,
    SwitchNode,
    TryCatchNode,
    VarAccessNode,
    WhileNode,
    _uses_shared_parameters,
)
from .parser import Parser
from .values import (
    AsyncFunction,
    BoundMethod,
    Char,
    ClassBlueprint,
    ClassInstance,
    ClassRegistry,
    CodeBlockValue,
    Context,
    CoroutineValue,
    ExecutionState,
    EmbedPyNamespace,
    EnumType,
    EnumValue,
    Function,
    List,
    LynxTuple,
    Module,
    Namespace,
    Null,
    Number,
    RTError,
    RTResult,
    String,
    StructBlueprint,
    SymbolTable,
    VarGroup,
    _build_exec_bindings,
    _exec_codeblock_variable_names,
    type_matches,
    value_type_name,
)

_SOURCE_DIR = os.path.dirname(os.path.abspath(__file__))
STDLIB_DIR = os.path.join(_SOURCE_DIR, "stdlib")
_cython_inline_fn: Any = None
def _get_cython_inline() -> Any:
    """Lazily import Cython's inline compiler (needs setuptools' distutils shim)."""
    global _cython_inline_fn
    if _cython_inline_fn is None:
        # Import by name so Pyright can analyze Lynxer without requiring the
        # optional Cython package to be installed in its analysis environment.
        from importlib import import_module

        import setuptools  # noqa: F401 — patches distutils for Cython on py3.12+

        _cython_inline_fn = import_module("Cython.Build.Inline").cython_inline
    return _cython_inline_fn
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
# State shared by one interpreter execution.  Contexts carry this object into
# values and builtins instead of importing this module to find live globals.
_execution_state = ExecutionState()
execution_state = _execution_state

# importPy shared module registry
_rawpy_global_modules = _execution_state.rawpy_global_modules

# Python callbacks registered by a standard-library module (for example the
# Arcade game loop) need a way back into the currently running Lynxer program.
# rawPy already runs with the active interpreter context, so the bridge is
# installed lazily for the lifetime of each rawPy block and can safely be
# captured by a host callback.
def _python_to_lynxer_callback_value(value):
    """Convert the small set of values host event loops pass to callbacks."""
    if isinstance(value, bool):
        return Number(1 if value else 0, is_bool=True)
    if isinstance(value, int):
        return Number(value)
    if isinstance(value, float):
        return Number(value)
    if isinstance(value, str):
        return String(value)
    return String(str(value))


def _lynxer_callback_dispatcher(context):
    """Return a Python callable that invokes a Lynxer function by name.

    Names may be plain function names or dotted global paths such as
    ``global.update``.  The dispatcher intentionally accepts only Lynxer
    functions; arbitrary Python objects are never exposed through this hook.
    """
    def dispatch(callback_name, *python_args):
        if not isinstance(callback_name, str) or not callback_name.strip():
            raise RuntimeError("game callback name must be a non-empty string")

        parts = [part for part in callback_name.split(".") if part]
        if parts and parts[0] == "global":
            parts = parts[1:]
            target = context.symbol_table.get("global")
        else:
            target = context.symbol_table.get(parts.pop(0)) if parts else None

        for part in parts:
            if target is None or not hasattr(target, "get_attr"):
                target = None
                break
            target, error = target.get_attr(part)
            if error:
                target = None
                break

        if not isinstance(target, Function):
            raise RuntimeError(  # noqa: TRY004
                f"game callback '{callback_name}' is not a synchronous Lynxer function"
            )

        args = [
            _python_to_lynxer_callback_value(value)
            for value in python_args
        ]
        result = target.execute(args)
        if result.error:
            raise RuntimeError(result.error.as_string())
        value = result.value
        if isinstance(value, Number):
            return bool(value.value) if value.is_bool else value.value
        if isinstance(value, (String, Char)):
            return value.value
        return None

    return dispatch


# Lynxer module registry.  Module names are intentionally global: importing
# two different files with the same basename is ambiguous even when their
# directories differ.
_lynx_modules = _execution_state.lynx_modules
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
        raise NotImplementedError(
            f"No visit_{type(node).__name__} method defined"
        )

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

    def visit_InterpolatedStringNode(self, node, context):
        res = RTResult()
        parts = []

        for index, literal in enumerate(node.literal_parts):
            parts.append(literal)
            if index >= len(node.value_nodes):
                continue
            value = res.register(self.visit(node.value_nodes[index], context))
            if res.should_return():
                return res
            parts.append(str(value))

        return res.success(
            String("".join(parts))
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
        ownership_error = context.symbol_table.ownership_error(var_name, "read")
        if ownership_error:
            return res.failure(
                RTError(
                    node.pos_start,
                    node.pos_end,
                    ownership_error,
                    context,
                )
            )
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
        if var_name in context.symbol_table.symbols or var_name in context.symbol_table.aliases:
            ownership_error = context.symbol_table.ownership_error(var_name, "write to")
            if ownership_error:
                return res.failure(RTError(
                    node.pos_start,
                    node.pos_end,
                    ownership_error,
                    context,
                ))
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
        ownership_error = context.symbol_table.ownership_error(var_name, "write to")
        if ownership_error and ownership_error != f"'{var_name}' is not defined":
            return res.failure(
                RTError(
                    node.pos_start,
                    node.pos_end,
                    ownership_error,
                    context,
                )
            )
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
        context.symbol_table.mark_reinitialized(var_name)
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
            time.sleep(context.require_execution_state().forever_delay)

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

    def _match_switch_pattern(self, pattern, value, context, bindings):
        if isinstance(pattern, PatternNode):
            if pattern.kind == "wildcard":
                return True, None
            if pattern.kind == "binding":
                name = pattern.value
                existing = context.symbol_table.get(name)
                if (
                    existing is not None
                    and name not in context.symbol_table._pattern_bindings
                ):
                    return False, RTError(
                        pattern.pos_start,
                        pattern.pos_end,
                        f"switch binding '{name}' shadows an existing variable. "
                        "Rename the binding or remove the case; a pattern "
                        "binding cannot silently compare against an existing "
                        "value.",
                        context,
                    )
                bindings[name] = value.copy()
                return True, None
            if pattern.kind == "literal":
                result = self.visit(pattern.value, context)
                if result.error:
                    return False, result.error
                equal, error = value.get_comparison_eq(result.value)
                return bool(equal and equal.is_true()), error
            if pattern.kind in {"list", "tuple"}:
                expected_type = List if pattern.kind == "list" else LynxTuple
                if not isinstance(value, expected_type):
                    return False, None
                if len(value.elements) != len(pattern.items):
                    return False, None
                for child, actual in zip(pattern.items, value.elements):
                    matched, error = self._match_switch_pattern(
                        child, actual, context, bindings
                    )
                    if error or not matched:
                        return matched, error
                return True, None
            if pattern.kind == "enum":
                if not isinstance(value, EnumValue):
                    return False, None
                enum_name, variant_name = pattern.value
                if (
                    (enum_name is not None and value.enum_name != enum_name)
                    or value.variant_name != variant_name
                    or len(value.payload) != len(pattern.items)
                ):
                    return False, None
                for child, actual in zip(pattern.items, value.payload):
                    matched, error = self._match_switch_pattern(
                        child, actual, context, bindings
                    )
                    if error or not matched:
                        return matched, error
                return True, None
        equal, error = value.get_comparison_eq(pattern)
        return bool(equal and equal.is_true()), error

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

            bindings = {}
            matches, error = self._match_switch_pattern(
                case.match_node, switch_value, context, bindings
            )
            if error:
                return res.failure(error)
            if matches:
                for binding_name, binding_value in bindings.items():
                    context.symbol_table.set(
                        binding_name, binding_value,
                        decl_type=value_type_name(binding_value),
                    )
                    context.symbol_table._pattern_bindings.add(binding_name)
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
        ) or node.kind_tok.value == "func"
        is_file_func = node.kind_tok.value == "func"
        if node.is_async:
            func_value = AsyncFunction(
                func_name,
                node.body_block,
                param_names,
                param_types,
                is_global,
                code_block_names,
                param_defaults,
                is_file_func,
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
                is_file_func,
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
        except Exception as e:  # noqa: BLE001
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
        if var_name in context.symbol_table.symbols or var_name in context.symbol_table.aliases:
            ownership_error = context.symbol_table.ownership_error(var_name, "write to")
            if ownership_error:
                return res.failure(RTError(
                    node.pos_start,
                    node.pos_end,
                    ownership_error,
                    context,
                ))
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
        ownership_error = context.symbol_table.ownership_error(var_name, "write to")
        if ownership_error and ownership_error != f"'{var_name}' is not defined":
            return res.failure(
                RTError(
                    node.pos_start,
                    node.pos_end,
                    ownership_error,
                    context,
                )
            )
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
        context.symbol_table.mark_reinitialized(var_name)
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

            bindings = {}
            matches, error = self._match_switch_pattern(
                case.match_node, switch_value, context, bindings
            )
            if error:
                return res.failure(error)
            if matches:
                for binding_name, binding_value in bindings.items():
                    context.symbol_table.set(
                        binding_name, binding_value,
                        decl_type=value_type_name(binding_value),
                    )
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
            await asyncio.sleep(context.require_execution_state().forever_delay)

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
                and not value_to_call.is_file_func
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

        ownership_query = (
            isinstance(node.node_to_call, VarAccessNode)
            and node.node_to_call.var_name_tok.value in {"borrowing", "beingBorrowed"}
        )
        ownership_target = (
            isinstance(node.node_to_call, VarAccessNode)
            and node.node_to_call.var_name_tok.value in {
                "varTransfer",
                "varTransferMutate",
                "varBorrow",
                "varBorrowMutate",
                "varSwapAll",
                "varSwapVal",
            }
        )
        for index, arg_node in enumerate(node.arg_nodes):
            if (
                (
                    ownership_query
                    or (
                        ownership_target
                        and (
                            node.node_to_call.var_name_tok.value
                            in {"varSwapAll", "varSwapVal"}
                            or index == 1
                        )
                    )
                )
                and isinstance(arg_node, VarAccessNode)
            ):
                arg_name = arg_node.var_name_tok.value
                arg_value = context.symbol_table.get(arg_name)
                if arg_value is None:
                    return res.failure(RTError(
                        arg_node.pos_start,
                        arg_node.pos_end,
                        f"'{arg_name}' is not defined",
                        context,
                    ))
                arg_value = arg_value.copy().set_pos(
                    arg_node.pos_start, arg_node.pos_end
                ).set_context(context)
            else:
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

    async def async_visit_EnumDefNode(self, node, context):
        return self.visit_EnumDefNode(node, context)

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
                and not value_to_call.is_file_func
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

        ownership_query = (
            isinstance(node.node_to_call, VarAccessNode)
            and node.node_to_call.var_name_tok.value in {"borrowing", "beingBorrowed"}
        )
        ownership_target = (
            isinstance(node.node_to_call, VarAccessNode)
            and node.node_to_call.var_name_tok.value in {
                "varTransfer",
                "varTransferMutate",
                "varBorrow",
                "varBorrowMutate",
                "varSwapAll",
                "varSwapVal",
            }
        )
        for index, arg_node in enumerate(node.arg_nodes):
            if (
                (
                    ownership_query
                    or (
                        ownership_target
                        and (
                            node.node_to_call.var_name_tok.value
                            in {"varSwapAll", "varSwapVal"}
                            or index == 1
                        )
                    )
                )
                and isinstance(arg_node, VarAccessNode)
            ):
                arg_name = arg_node.var_name_tok.value
                arg_value = context.symbol_table.get(arg_name)
                if arg_value is None:
                    return res.failure(RTError(
                        arg_node.pos_start,
                        arg_node.pos_end,
                        f"'{arg_name}' is not defined",
                        context,
                    ))
                arg_value = arg_value.copy().set_pos(
                    arg_node.pos_start, arg_node.pos_end
                ).set_context(context)
            else:
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

    def visit_EnumDefNode(self, node, context):
        enum = EnumType(node.name_tok.value, node.variants)
        enum.set_context(context).set_pos(node.pos_start, node.pos_end)
        context.symbol_table.set(node.name_tok.value, enum, decl_type=node.name_tok.value)
        # Associated code is parsed and retained for forward compatibility.  It
        # is intentionally not executed at declaration time.
        return RTResult().success(Number.null)

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
        # Keep the callback bridge on the real Python builtins module so a
        # function created in this isolated exec namespace can call it later,
        # after this rawPy block has returned.
        import builtins as _host_builtins
        # setattr, because this attribute is injected dynamically and is not
        # part of the builtins module's declared interface.
        setattr(  # noqa: B010
            _host_builtins,
            "_lx_invoke_lynxer",
            _lynxer_callback_dispatcher(context),
        )
        tbl = context.symbol_table
        while tbl is not None:
            for name, val in tbl.symbols.items():
                if name not in py_ns:
                    if isinstance(val, Number):
                        py_ns[name] = bool(val.value) if val.is_bool else val.value
                    elif isinstance(val, (Char, String)):
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
            exec(textwrap.dedent(node.code), py_ns)  # noqa: S102
        except Exception as e:  # noqa: BLE001
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
            elif isinstance(val, (int, float)):
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
        except BaseException:  # noqa: BLE001
            py_ns = {"__builtins__": __builtins__}
            py_ns.update(cy_locals)
            try:
                exec(textwrap.dedent(node.code), py_ns)  # noqa: S102
            except Exception as e:  # noqa: BLE001
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
            elif isinstance(val, (int, float)):
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
        target_table = context.symbol_table
        existing_entry = _lynx_modules.get(module_name)
        file_val = target_table.get("__file__")
        base_dir = os.path.dirname(file_val.value) if isinstance(file_val, String) else ""
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

        if existing_entry is not None:
            existing_path, existing_module = existing_entry
            if os.path.realpath(filepath) != existing_path:
                return res.failure(RTError(
                    node.pos_start,
                    node.pos_end,
                    f"Module name collision: '{module_name}' is already loaded "
                    f"from '{existing_path}', not '{filepath}'",
                    context,
                ))
            target_table.set(module_name, existing_module)
            global_symbol_table.set(module_name, existing_module)
            return res.success(Number.null)

        module_table = SymbolTable(target_table)
        _register_builtins(module_table)
        module_table.set("class", ClassRegistry())
        module_table.set("global", Namespace(module_table))
        module = Module(module_name, module_table)
        module.set_pos(node.pos_start, node.pos_end).set_context(context)
        _lynx_modules[module_name] = (os.path.realpath(filepath), module)

        if use_bytecode == "native":
            try:
                from .builtins import _load_native_module, populate_native_module_table
                _, native_state = _load_native_module(filepath, imported=True)
                populate_native_module_table(native_state, module_table)
                error = None
            except Exception as e:  # noqa: BLE001
                error = RTError(
                    node.pos_start, node.pos_end,
                    f'Failed to load native module "{filename}": {e}',
                    context,
                )
        elif use_bytecode:
            try:
                error = run_bytecode_file(filepath, module_table)
            except Exception as e:  # noqa: BLE001
                _lynx_modules.pop(module_name, None)
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
                with open(filepath, "r", encoding="utf-8") as f:
                    script = f.read()
            except Exception as e:  # noqa: BLE001
                _lynx_modules.pop(module_name, None)
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
            _lynx_modules.pop(module_name, None)
            return res.failure(
                RTError(
                    node.pos_start,
                    node.pos_end,
                    f'Error in imported file "{filename}":\n{error.as_string()}',
                    context,
                )
            )

        target_table.set(module_name, module)
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
        target_table = context.symbol_table
        file_val = target_table.get("__file__")
        base_dir = os.path.dirname(file_val.value) if isinstance(file_val, String) else ""
        filepath, use_bytecode, resolve_error = _module_path(filename, base_dir)

        if filepath is None:
            return res.failure(RTError(
                node.pos_start, node.pos_end,
                f"Could not import module \"{module_name}\": {resolve_error}",
                context,
            ))

        existing_entry = _lynx_modules.get(module_name)
        if existing_entry is not None:
            existing_path, existing_module = existing_entry
            if os.path.realpath(filepath) != existing_path:
                return res.failure(RTError(
                    node.pos_start,
                    node.pos_end,
                    f"Module name collision: '{module_name}' is already loaded "
                    f"from '{existing_path}', not '{filepath}'",
                    context,
                ))
            target_table.set(alias_name, existing_module)
            global_symbol_table.set(alias_name, existing_module)
            return res.success(Number.null)

        module_table = SymbolTable(target_table)
        _register_builtins(module_table)
        module_table.set("class", ClassRegistry())
        module_table.set("global", Namespace(module_table))
        module = Module(module_name, module_table)
        module.set_pos(node.pos_start, node.pos_end).set_context(context)
        _lynx_modules[module_name] = (os.path.realpath(filepath), module)

        if use_bytecode == "native":
            try:
                from .builtins import _load_native_module, populate_native_module_table
                _, native_state = _load_native_module(filepath, imported=True)
                populate_native_module_table(native_state, module_table)
                error = None
            except Exception as e:  # noqa: BLE001
                error = RTError(
                    node.pos_start, node.pos_end,
                    f'Failed to load native module "{filename}": {e}',
                    context,
                )
        elif use_bytecode:
            try:
                error = run_bytecode_file(filepath, module_table)
            except Exception as e:  # noqa: BLE001
                _lynx_modules.pop(module_name, None)
                return res.failure(RTError(
                    node.pos_start, node.pos_end,
                    f'Failed to load bytecode "{filename}": {e}',
                    context,
                ))
        else:
            try:
                with open(filepath, "r", encoding="utf-8") as f:
                    script = f.read()
            except Exception as e:  # noqa: BLE001
                _lynx_modules.pop(module_name, None)
                return res.failure(RTError(
                    node.pos_start, node.pos_end,
                    f'Failed to importAs "{filename}": {e}',
                    context,
                ))
            error = run_file(filepath, script, module_table)

        if error:
            _lynx_modules.pop(module_name, None)
            return res.failure(RTError(
                node.pos_start, node.pos_end,
                f'Error in imported file "{filename}":\n{error.as_string()}',
                context,
            ))

        target_table.set(alias_name, module)
        global_symbol_table.set(alias_name, module)
        return res.success(Number.null)

    def visit_ProgramNode(self, node, context):
        res = RTResult()

        exec_state = context.execution_state
        if exec_state is None:
            raise RuntimeError(
                "visit_ProgramNode requires a Context with an execution state"
            )

        for decl in node.globals_list:
            res.register(self.visit(decl, context))
            if res.error:
                return res

        if node.main_func is not None:
            res.register(self.visit(node.main_func, context))
            if res.error:
                return res

        if node.setup_func:
            previous_setup_state = exec_state.setup_in_progress
            exec_state.setup_in_progress = True
            try:
                setup_res = self.run_setup(node.setup_func, context)
                if setup_res.error:
                    return setup_res
            finally:
                exec_state.setup_in_progress = previous_setup_state

        entry_name = (
            exec_state.main_override
            if exec_state.main_override
            else "main"
        )
        entry_fn = context.symbol_table.get(entry_name)
        if entry_fn is None:
            if exec_state.main_override:
                return res.failure(RTError(
                    node.pos_start, node.pos_end,
                    f"overrideMain: no global function named "
                    f"'{exec_state.main_override}' found. "
                    f"Make sure 'global "
                    f"{exec_state.main_override}(){{}}' is declared "
                    "in the file.",
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
    """Install built-ins after the value and interpreter layers are ready."""
    from .builtins import BuiltInFunction, register_builtins

    globals()["BuiltInFunction"] = BuiltInFunction
    register_builtins(symbol_table, _execution_state)

# global symbol table

def _new_global_symbol_table():
    table = SymbolTable()
    _execution_state.global_symbol_table = table
    table.set("true", Number.true)
    table.set("false", Number.false)
    _register_builtins(table)
    table.set("embedPy", EmbedPyNamespace())
    return table


global_symbol_table = _new_global_symbol_table()

SHARED_INTERPRETER = Interpreter()
_execution_state.interpreter = SHARED_INTERPRETER


def reset_runtime_state():
    """Start a clean top-level runtime for an independent program run."""
    global global_symbol_table
    global_symbol_table = _new_global_symbol_table()
    _rawpy_global_modules.clear()
    _lynx_modules.clear()


def _interpreter_error(fn, text, context_name, exc):
    """Turn an unexpected host exception into a normal Lynxer error."""
    context = Context(context_name)
    start = Position(0, 0, 0, fn, text)
    details = str(exc).strip() or type(exc).__name__
    return RTError(start, start.copy(), f"Interpreter failure: {details}", context)


# run

def _join_outstanding_native_threads():
    """Wait for native threads the program started but never joined.

    A worker calls back into Python, so it must finish while the interpreter is
    still alive; leaving one behind races the teardown and aborts the process.
    """
    module = sys.modules.get("lynxer.cpp") or sys.modules.get("cpp")
    if module is None:
        return
    try:
        module.nativeThreadJoinAll()
    except Exception:  # noqa: BLE001, S110
        pass


def run(fn, text, suppress_deprecation_warnings=False):
    reset_runtime_state()
    _execution_state.main_override = None
    _execution_state.forever_delay = 0.02
    _error._forever_warning_suppressed = False
    _error._deprecation_warning_suppressed = bool(suppress_deprecation_warnings)
    _error._pending_deprecation_warnings.clear()
    _execution_state.setup_in_progress = False
    _error._deprecation_warning_deferred = True

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
        _error._deprecation_warning_deferred = False

    interpreter = SHARED_INTERPRETER
    context = Context("<program>", execution_state=_execution_state)
    context.symbol_table = global_symbol_table
    global_symbol_table.set("__file__", String(os.path.abspath(fn)))
    global_symbol_table.set("global", Namespace(global_symbol_table))
    global_symbol_table.set("class", ClassRegistry())

    try:
        result = interpreter.visit(ast.node, context)
    except Exception as exc:  # noqa: BLE001
        _flush_deprecation_warnings()
        _join_outstanding_native_threads()
        return None, _interpreter_error(fn, text, "<program>", exc)
    _flush_deprecation_warnings()
    _join_outstanding_native_threads()
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
    context = Context(
        f"<import:{os.path.basename(fn)}>",
        execution_state=_execution_state,
    )
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
            previous_setup_state = _execution_state.setup_in_progress
            _execution_state.setup_in_progress = True
            try:
                r = interpreter.run_setup(node.setup_func, context)
                if r.error:
                    return r.error
            finally:
                _execution_state.setup_in_progress = previous_setup_state

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
    except Exception as exc:  # noqa: BLE001
        return _interpreter_error(fn, text, f"<import:{os.path.basename(fn)}>", exc)

    return None

