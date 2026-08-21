"""Public package exports.

The interpreter and built-in registry depend on each other during startup.
Keep package exports lazy so importing a submodule such as
``lynxer.builtins`` does not eagerly start the interpreter first.
"""

__all__ = [
    "run",
    "run_file",
    "BYTECODE_MAGIC",
    "BYTECODE_VERSION",
    "compile_to_bytecode",
    "run_bytecode",
    "run_bytecode_file",
]


def __getattr__(name):
    if name in {"run", "run_file"}:
        from . import lynxer as runtime
        return getattr(runtime, name)
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
