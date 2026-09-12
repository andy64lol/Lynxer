"""Public package exports.

The interpreter and built-in registry depend on each other during startup.
Keep package exports lazy so importing a submodule such as
``lynxer.builtins`` does not eagerly start the interpreter first.
"""

# The import cycles below (``__init__`` <-> ``bytecode``/``lynxer``) only
# exist for the type checker: both submodules are imported lazily by
# ``__getattr__`` at runtime, which is what keeps interpreter startup cheap.
# pyright: reportImportCycles=false

from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from .bytecode import (
        BYTECODE_MAGIC,
        BYTECODE_VERSION,
        compile_to_bytecode,
        run_bytecode,
        run_bytecode_file,
    )
    from .lynxer import run, run_file


__all__ = [
    "BYTECODE_MAGIC",
    "BYTECODE_VERSION",
    "compile_to_bytecode",
    "run",
    "run_bytecode",
    "run_bytecode_file",
    "run_file",
]


def __getattr__(name):
    if name in {"run", "run_file"}:
        # The compatibility facade in ``lynxer.py`` re-exports the pipeline
        # types, but the executable entry points live in ``runtime.py``.
        # Import the owner explicitly here; ``from . import lynxer`` routes
        # through the facade and makes the lazy package export fail.
        from .runtime import run, run_file

        return run if name == "run" else run_file
    if name in {
        "BYTECODE_MAGIC",
        "BYTECODE_VERSION",
        "compile_to_bytecode",
        "run_bytecode",
        "run_bytecode_file",
    }:
        from . import bytecode
        return getattr(bytecode, name)
    raise AttributeError(f"module {__name__!r} has no attribute {name!r}")
