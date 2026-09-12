"""Abstract-syntax-tree nodes produced by the Lynxer parser."""

from __future__ import annotations

from typing import Any

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

class InterpolatedStringNode:
    """An ``inter"Hello, {name}!"`` literal.

    ``literal_parts`` always holds one more entry than ``value_nodes``: the
    text before, between, and after each ``{...}`` interpolation.
    """
    def __init__(self, tok, literal_parts, value_nodes):
        self.tok = tok
        self.literal_parts = literal_parts
        self.value_nodes = value_nodes
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

def uses_shared_parameters(function_value):
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

class PatternNode:
    """A switch pattern.

    Patterns deliberately remain AST data instead of being evaluated as normal
    expressions.  This lets ``_`` and bindings work without requiring those
    names to exist in the surrounding symbol table.
    """
    def __init__(self, kind, value=None, items=None, pos_start=None, pos_end=None):
        self.kind = kind
        self.value: Any = value
        self.items = items or []
        self.pos_start = pos_start
        self.pos_end = pos_end

class EnumDefNode:
    def __init__(self, name_tok, variants, body, pos_start, pos_end):
        self.name_tok = name_tok
        self.variants = variants
        self.body = body
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

def block_contains_break(block_node):
    """Return whether a block contains a ``break`` statement."""
    if isinstance(block_node, IfNode):
        if block_contains_break(block_node.then_block):
            return True
        return (
            block_node.else_block is not None
            and block_contains_break(block_node.else_block)
        )

    for stmt in block_node.statements:
        if isinstance(stmt, BreakNode):
            return True
        if isinstance(stmt, IfNode):
            if block_contains_break(stmt.then_block):
                return True
            if stmt.else_block is not None and block_contains_break(stmt.else_block):
                return True
        elif isinstance(stmt, TryCatchNode):
            if block_contains_break(stmt.try_block):
                return True
            if stmt.catch_block is not None and block_contains_break(stmt.catch_block):
                return True
    return False


# Compatibility aliases for older extensions importing the original helpers.
_uses_shared_parameters = uses_shared_parameters
_block_contains_break = block_contains_break

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

