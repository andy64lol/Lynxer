"""Lynxer bytecode serialization, loading, and execution.

The bytecode file contains a compressed pickle of the parsed AST.  Runtime
classes are imported lazily so this module can be imported by ``lynxer.py``
without creating a circular import during interpreter startup.
"""

from __future__ import annotations
import os
import pickle
import zlib
from typing import Any


BYTECODE_MAGIC = b"LYNXC\x00"
BYTECODE_VERSION = 3


def _runtime() -> Any:
    """Return the interpreter module only when a bytecode operation needs it."""
    from . import lynxer

    return lynxer


def _read_bytecode(fn: str) -> tuple[dict[str, Any], int, int]:
    """Read, decompress, validate, and unpickle a ``.lynxc`` file."""
    with open(fn, "rb") as bytecode_file:
        magic = bytecode_file.read(len(BYTECODE_MAGIC))
        if magic != BYTECODE_MAGIC:
            raise ValueError(
                f"'{fn}' is not a valid Lynxer bytecode file "
                f"(bad magic bytes: {magic!r})"
            )
        compressed = bytecode_file.read()

    try:
        raw = zlib.decompress(compressed)
    except zlib.error as exc:
        raise ValueError(
            f"'{fn}' bytecode is corrupt or was compiled with an older "
            f"Lynxer version (decompression failed: {exc}). "
            "Recompile the source file to fix this."
        ) from exc

    data = pickle.loads(raw)
    if not isinstance(data, dict):
        raise ValueError(f"'{fn}' does not contain a valid Lynxer bytecode payload")

    file_version = data.get("version")
    if file_version != BYTECODE_VERSION:
        raise ValueError(
            f"'{fn}' was compiled with bytecode version {file_version} but this "
            f"Lynxer runtime expects version {BYTECODE_VERSION}.  "
            "Recompile the source file with "
            "'lynxer --compile <source.lynx>' to generate an up-to-date .lynxc file."
        )

    return data, len(raw), len(compressed)


def load_bytecode(fn: str) -> dict[str, Any]:
    """Load and validate a bytecode file, returning its payload."""
    data, _, _ = _read_bytecode(fn)
    return data


def read_bytecode(fn: str) -> tuple[dict[str, Any], int, int]:
    """Load bytecode and return ``(payload, decompressed_size, stored_size)``."""
    return _read_bytecode(fn)


def compile_to_bytecode(fn: str, text: str) -> tuple[str | None, Any]:
    """Parse and compile *text* to a ``.lynxc`` bytecode file."""
    runtime = _runtime()
    lexer = runtime.Lexer(fn, text)
    tokens, error = lexer.make_tokens()
    if error:
        return None, error

    parser = runtime.Parser(tokens)
    ast = parser.parse()
    if ast.error:
        return None, ast.error

    out_path = os.path.splitext(os.path.abspath(fn))[0] + ".lynxc"
    data = {
        "version": BYTECODE_VERSION,
        "source": os.path.abspath(fn),
        "node": ast.node,
    }
    payload = zlib.compress(
        pickle.dumps(data, protocol=pickle.HIGHEST_PROTOCOL),
        level=zlib.Z_BEST_COMPRESSION,
    )
    with open(out_path, "wb") as bytecode_file:
        bytecode_file.write(BYTECODE_MAGIC)
        bytecode_file.write(payload)

    return out_path, None


def run_bytecode(fn: str) -> tuple[Any, Any]:
    """Load and execute a pre-compiled ``.lynxc`` file."""
    runtime = _runtime()
    runtime._main_override = None
    runtime._forever_delay = 0.02
    runtime._forever_warning_suppressed = False
    runtime._setup_in_progress = False

    try:
        data = load_bytecode(fn)
    except Exception as exc:
        raise RuntimeError(str(exc)) from exc

    node = data["node"]
    interpreter = runtime.SHARED_INTERPRETER
    context = runtime.Context("<program>")
    context.symbol_table = runtime.global_symbol_table
    runtime.global_symbol_table.set("__file__", runtime.String(os.path.abspath(fn)))
    runtime.global_symbol_table.set(
        "global", runtime.Namespace(runtime.global_symbol_table)
    )
    runtime.global_symbol_table.set("class", runtime.ClassRegistry())

    result = interpreter.visit(node, context)
    return result.value, result.error


def run_bytecode_file(fn: str, symbol_table: Any) -> Any:
    """Load and execute a compiled module in an existing symbol table."""
    runtime = _runtime()
    try:
        data = load_bytecode(fn)
    except Exception as exc:
        raise RuntimeError(str(exc)) from exc

    node = data["node"]
    interpreter = runtime.SHARED_INTERPRETER
    context = runtime.Context(f"<import:{os.path.basename(fn)}>")
    context.symbol_table = symbol_table
    symbol_table.set("__file__", runtime.String(os.path.abspath(fn)))

    for decl in node.globals_list:
        result = runtime.RTResult()
        result.register(interpreter.visit(decl, context))
        if result.error:
            return result.error

    if node.setup_func:
        previous_setup_state = runtime._setup_in_progress
        runtime._setup_in_progress = True
        try:
            result = runtime.RTResult()
            result.register(interpreter.visit(node.setup_func.body_block, context))
            if result.error:
                return result.error
        finally:
            runtime._setup_in_progress = previous_setup_state

    return None
