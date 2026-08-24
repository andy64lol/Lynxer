"""Lynxer bytecode serialization, loading, and execution.

The bytecode file contains a compressed pickle of the parsed AST.  Runtime
classes are imported lazily so this module can be imported by ``lynxer.py``
without creating a circular import during interpreter startup.
"""

from __future__ import annotations
import io
import os
import pickle
import re
import zlib
from typing import Any


BYTECODE_MAGIC = b"LYNXC\x00"
BYTECODE_VERSION = 5
MAX_BYTECODE_FILE_SIZE = 64 * 1024 * 1024
MAX_BYTECODE_PAYLOAD_SIZE = 256 * 1024 * 1024
_NATIVE_IMPORT_RE = re.compile(
    r"""(?:import|importAs)\s*\(\s*["']([^"']+\.(?:so|dylib|dll))["']""",
    re.IGNORECASE,
)


class _SafeUnpickler(pickle.Unpickler):
    """Unpickle only the AST types produced by Lynxer's compiler.

    Pickle is executable by design.  A ``.lynxc`` file is user-controlled
    input, so the normal ``pickle.loads`` entry point is not appropriate here.
    The compiler serialises syntax nodes, tokens, and positions only; runtime
    values and arbitrary Python globals are intentionally not accepted.
    """

    _ALLOWED_BUILTINS = {
        "bool",
        "bytes",
        "complex",
        "dict",
        "float",
        "frozenset",
        "int",
        "list",
        "set",
        "str",
        "tuple",
    }

    def find_class(self, module: str, name: str) -> Any:
        if module == "builtins" and name in self._ALLOWED_BUILTINS:
            return getattr(__import__(module), name)

        if module == "lynxer.lynxer":
            runtime = _runtime()
            allowed = {
                class_name
                for class_name, value in vars(runtime).items()
                if isinstance(value, type)
                and (class_name.endswith("Node") or class_name in {"Position", "Token"})
            }
            if name in allowed:
                return getattr(runtime, name)

        raise pickle.UnpicklingError(
            f"bytecode contains a disallowed Python object: {module}.{name}"
        )


def _runtime() -> Any:
    """Return the interpreter module only when a bytecode operation needs it."""
    from . import lynxer

    return lynxer


def _read_bytecode(fn: str) -> tuple[dict[str, Any], int, int]:
    """Read, decompress, validate, and unpickle a ``.lynxc`` file."""
    try:
        file_size = os.path.getsize(fn)
    except OSError as exc:
        raise ValueError(f"could not read '{fn}': {exc}") from exc
    if file_size > MAX_BYTECODE_FILE_SIZE:
        raise ValueError(
            f"'{fn}' is too large ({file_size:,} bytes); "
            f"the maximum supported bytecode file is {MAX_BYTECODE_FILE_SIZE:,} bytes"
        )

    try:
        with open(fn, "rb") as bytecode_file:
            magic = bytecode_file.read(len(BYTECODE_MAGIC))
            if magic != BYTECODE_MAGIC:
                raise ValueError(
                    f"'{fn}' is not a valid Lynxer bytecode file "
                    f"(bad magic bytes: {magic!r})"
                )
            compressed = bytecode_file.read()
    except OSError as exc:
        raise ValueError(f"could not read '{fn}': {exc}") from exc

    try:
        decompressor = zlib.decompressobj()
        raw = decompressor.decompress(compressed, MAX_BYTECODE_PAYLOAD_SIZE + 1)
        if len(raw) > MAX_BYTECODE_PAYLOAD_SIZE or decompressor.unconsumed_tail:
            raise ValueError(
                f"'{fn}' expands beyond the maximum supported bytecode payload "
                f"of {MAX_BYTECODE_PAYLOAD_SIZE:,} bytes"
            )
        raw += decompressor.flush()
        if len(raw) > MAX_BYTECODE_PAYLOAD_SIZE:
            raise ValueError(
                f"'{fn}' expands beyond the maximum supported bytecode payload "
                f"of {MAX_BYTECODE_PAYLOAD_SIZE:,} bytes"
            )
        if decompressor.unused_data:
            raise ValueError(f"'{fn}' contains trailing or oversized compressed data")
    except (ValueError, zlib.error) as exc:
        raise ValueError(
            f"'{fn}' bytecode is corrupt or was compiled with an older "
            f"Lynxer version (decompression failed: {exc}). "
            "Recompile the source file to fix this."
        ) from exc

    try:
        data = _SafeUnpickler(io.BytesIO(raw)).load()
    except (pickle.UnpicklingError, EOFError, AttributeError, ImportError, IndexError, TypeError) as exc:
        raise ValueError(
            f"'{fn}' does not contain a safe, valid Lynxer bytecode payload: {exc}"
        ) from exc
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

    if "node" not in data:
        raise ValueError(f"'{fn}' does not contain a compiled Lynxer program")
    runtime = _runtime()
    if not isinstance(data["node"], runtime.ProgramNode):
        raise ValueError(f"'{fn}' does not contain a valid compiled Lynxer program")

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
        "native_dependencies": sorted(set(_NATIVE_IMPORT_RE.findall(text))),
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


def run_bytecode(fn: str, suppress_deprecation_warnings=False) -> tuple[Any, Any]:
    """Load and execute a pre-compiled ``.lynxc`` file."""
    runtime = _runtime()
    runtime.reset_runtime_state()
    setattr(runtime, "_main_override", None)
    setattr(runtime, "_forever_delay", 0.02)
    setattr(runtime, "_forever_warning_suppressed", False)
    setattr(runtime, "_deprecation_warning_suppressed", bool(suppress_deprecation_warnings))
    runtime._pending_deprecation_warnings.clear()
    setattr(runtime, "_setup_in_progress", False)

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
        setattr(runtime, "_setup_in_progress", True)
        try:
            result = interpreter.run_setup(node.setup_func, context)
            if result.error:
                return result.error
        finally:
            setattr(runtime, "_setup_in_progress", previous_setup_state)

    return None
