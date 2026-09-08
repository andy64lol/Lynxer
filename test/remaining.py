#!/usr/bin/env python3
"""Regression coverage for language/runtime additions.

Checks that are not tied to a ``testN.lynx`` fixture live here: ownership,
switch and enum semantics, compiler optimization, native interop, and stdlib
modules that need no device.
"""

from __future__ import annotations

import contextlib
import io
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from lynxer.bytecode import (
    BYTECODE_VERSION,
    _optimize_program,
    compile_to_bytecode,
    load_bytecode,
    run_bytecode,
)
from lynxer.lynxer import BinOpNode, Lexer, NumberNode, Parser, Token, run


def execute(source: str, filename: str) -> tuple[str, object]:
    output = io.StringIO()
    with contextlib.redirect_stdout(output):
        _, error = run(filename, source)
    return output.getvalue(), error


def require_output(source: str, expected: str, name: str) -> None:
    output, error = execute(source, f"<{name}>")
    assert error is None, f"{name}: {error.as_string() if error else error}"
    assert output == expected, f"{name}: {output!r} != {expected!r}"


def require_error(source: str, fragment: str, name: str) -> None:
    _, error = execute(source, f"<{name}>")
    assert error is not None, f"{name}: expected an error"
    assert fragment in error.as_string(), f"{name}: {error.as_string()}"


def parse_program(source: str):
    """Parse *source* and return its program node."""
    tokens, error = Lexer("<fold>", source).make_tokens()
    assert error is None, error.as_string()
    result = Parser(tokens).parse()
    assert result.error is None, result.error.as_string()
    return result.node


def collect(node, node_type, found=None):
    """Return every node of *node_type* at or below *node*."""
    if found is None:
        found = []
    if isinstance(node, node_type):
        found.append(node)
    for attr, value in list(vars(node).items()):
        if attr in ("pos_start", "pos_end"):
            continue
        if isinstance(value, list):
            candidates = value
        elif hasattr(value, "__dict__") and not isinstance(value, Token):
            candidates = [value]
        else:
            continue
        for candidate in candidates:
            if hasattr(candidate, "__dict__"):
                collect(candidate, node_type, found)
    return found


def native_available() -> bool:
    """Return whether the compiled native extension can be imported."""
    try:
        import lynxer.cpp  # noqa: F401
    except Exception:  # noqa: BLE001
        return False
    return True


# ---------------------------------------------------------------------------
# Ownership
# ---------------------------------------------------------------------------

def test_mutable_ownership() -> None:
    if not native_available():
        print("SKIP  mutable ownership: native extension not built")
        return
    require_output(
        """global setup(){}
global main(){
    int source = 7;
    any destination = "old";
    varTransferMutate(source, destination);
    println(destination);
    any borrower = none;
    varBorrowMutate(destination, borrower);
    borrower = 9;
    println(destination);
    println(beingBorrowed(destination));
    varEndBorrow(borrower);
    println(borrowing(borrower));
}""",
        "7\n9\ntrue\nfalse\n",
        "mutable ownership",
    )


def test_read_only_borrow_rejection() -> None:
    if not native_available():
        print("SKIP  read-only borrow rejection: native extension not built")
        return
    require_error(
        """global setup(){}
global main(){
    any source = 7;
    any borrower = none;
    varBorrow(source, borrower);
    borrower = 9;
}""",
        "read-only",
        "read-only borrow rejection",
    )


# ---------------------------------------------------------------------------
# Switch patterns
# ---------------------------------------------------------------------------

def test_nested_sequence_pattern() -> None:
    require_output(
        """global setup(){}
global main(){
    list value = [int 1, list [int 2, int 3]];
    switch(value){
        case([int 1, [int 2, _]]){ println("nested"); }
        default(){ println("wrong"); }
    }
}""",
        "nested\n",
        "nested sequence pattern",
    )


def test_enum_pattern_binding() -> None:
    require_output(
        """global setup(){}
enum result = [Ok(int value), Err(str message)]{}
global main(){
    any value = result.Ok(42);
    switch(value){
        case(result.Ok(number)){ println(number); }
        default(){ println("wrong"); }
    }
}""",
        "42\n",
        "enum pattern binding",
    )


def test_empty_enum_variant() -> None:
    require_output(
        """global setup(){}
enum status = [Ready, Failed(str reason)]{}
global main(){
    any value = status.Ready;
    switch(value){
        case(status.Ready){ println("ready"); }
        default(){ println("wrong"); }
    }
}""",
        "ready\n",
        "empty enum variant",
    )


def test_enum_payload_validation() -> None:
    require_error(
        """global setup(){}
enum result = [Ok(int value)]{}
global main(){ any value = result.Ok("bad"); }""",
        "declared as 'int'",
        "enum payload validation",
    )


def test_switch_binding_shadowing_is_an_error() -> None:
    """A pattern binding may not silently degrade into an equality check."""
    require_error(
        """global setup(){}
enum result = [Ok(int value)]{}
global main(){
    int seen = 5;
    any value = result.Ok(1);
    switch(value){ case(result.Ok(seen)){ println(seen); } }
}""",
        "shadows an existing variable",
        "switch binding shadowing",
    )


def test_switch_binding_name_is_reusable() -> None:
    """Two switches may reuse a pattern name; only real variables conflict."""
    require_output(
        """global setup(){}
enum result = [Ok(int value)]{}
global main(){
    any a = result.Ok(1);
    switch(a){ case(result.Ok(v)){ println(v); } }
    any b = result.Ok(2);
    switch(b){ case(result.Ok(v)){ println(v); } }
}""",
        "1\n2\n",
        "reused switch binding name",
    )


def test_switch_duplicate_pattern_is_rejected() -> None:
    require_error(
        """global setup(){}
global main(){
    int value = 1;
    switch(value){
        case(1){ println("first"); }
        case(1){ println("second"); }
    }
}""",
        "Duplicate switch pattern",
        "duplicate switch pattern",
    )


def test_switch_unreachable_pattern_is_rejected() -> None:
    require_error(
        """global setup(){}
global main(){
    int value = 1;
    switch(value){
        case(_){ println("everything"); }
        case(1){ println("never"); }
    }
}""",
        "Unreachable switch pattern",
        "unreachable switch pattern",
    )


# ---------------------------------------------------------------------------
# Enum declaration diagnostics
# ---------------------------------------------------------------------------

def test_duplicate_enum_name() -> None:
    require_error(
        """global setup(){}
enum result = [Ok(int value)]{}
enum result = [Err(str message)]{}
global main(){}""",
        "Duplicate 'enum' declaration 'result'",
        "duplicate enum name",
    )


def test_enum_conflicts_with_func() -> None:
    require_error(
        """global setup(){}
enum result = [Ok(int value)]{}
func result(){}
global main(){}""",
        "conflicts with the 'enum' declaration",
        "enum/func collision",
    )


def test_func_conflicts_with_enum() -> None:
    require_error(
        """global setup(){}
func result(){}
enum result = [Ok(int value)]{}
global main(){}""",
        "conflicts with a function declaration",
        "func/enum collision",
    )


# ---------------------------------------------------------------------------
# Compiler optimization
# ---------------------------------------------------------------------------

_FOLDABLE_SOURCE = """global setup(){}
global main(){ println(2 + 3 * 4); }
"""

_DIVIDE_SOURCE = """global setup(){}
global main(){ int value = 1 / 0; }
"""

# Folding this would build a multi-million-bit integer at compile time, for
# code the program may never even execute.
_HUGE_POWER_SOURCE = """global setup(){}
global main(){ int value = 9 ** 9 ** 9; }
"""

_DIFFERENTIAL_SOURCE = """global setup(){}
global main(){
    println(2 + 3 * 4);
    println("a" + "b");
    println(10 / 4);
    println(7 % 3);
    println(2 == 2);
}
"""


def test_constant_folding() -> None:
    node = parse_program(_FOLDABLE_SOURCE)
    optimized = _optimize_program(node)
    assert not collect(optimized, BinOpNode), "every binop should have folded"
    numbers = [n.tok.value for n in collect(optimized, NumberNode)]
    assert numbers == [14], numbers


def test_optimizer_keeps_runtime_errors() -> None:
    """Folding must not turn a failing program into a successful one."""
    node = parse_program(_DIVIDE_SOURCE)
    optimized = _optimize_program(node)
    assert collect(optimized, BinOpNode), "1 / 0 must not be folded away"
    require_error(_DIVIDE_SOURCE, "Division by zero", "unfolded division by zero")


def test_optimizer_skips_huge_powers() -> None:
    """Folding must not spend compile time on an enormous exponentiation."""
    node = parse_program(_HUGE_POWER_SOURCE)
    optimized = _optimize_program(node)
    assert collect(optimized, BinOpNode), "9 ** 9 ** 9 must not be folded"


def test_optimized_and_unoptimized_agree() -> None:
    expected = "14\nab\n2.5\n1\ntrue\n"
    require_output(_DIFFERENTIAL_SOURCE, expected, "differential source run")

    with tempfile.TemporaryDirectory(prefix="lynxer-opt-") as directory:
        root = Path(directory)
        outputs = {}
        for label, optimize in (("optimized", True), ("unoptimized", False)):
            source_path = root / f"{label}.lynx"
            source_path.write_text(_DIFFERENTIAL_SOURCE, encoding="utf-8")
            bytecode_path, error = compile_to_bytecode(
                str(source_path), _DIFFERENTIAL_SOURCE,
                optimize=optimize, use_cache=False,
            )
            assert error is None, error.as_string() if error else error
            assert bytecode_path is not None
            output = io.StringIO()
            with contextlib.redirect_stdout(output):
                _, runtime_error = run_bytecode(bytecode_path)
            assert runtime_error is None, (
                runtime_error.as_string() if runtime_error else runtime_error
            )
            outputs[label] = output.getvalue()

    assert outputs["optimized"] == expected, outputs["optimized"]
    assert outputs["unoptimized"] == expected, outputs["unoptimized"]
    assert outputs["optimized"] == outputs["unoptimized"]


def test_bytecode_roundtrip() -> None:
    with tempfile.TemporaryDirectory(prefix="lynxer-remaining-") as directory:
        source_path = Path(directory) / "features.lynx"
        source = """global setup(){}
enum result = [Ok(int value), Err(str message)]{}
global main(){
    list value = [int 1, int 2];
    switch(value){ case([int 1, _]){ println("bytecode"); } }
    any item = result.Ok(3);
    switch(item){ case(result.Ok(v)){ println(v); } }
}"""
        source_path.write_text(source, encoding="utf-8")
        bytecode_path, error = compile_to_bytecode(
            str(source_path), source, use_cache=False
        )
        assert error is None, error.as_string() if error else error
        assert bytecode_path is not None
        assert load_bytecode(bytecode_path)["version"] == BYTECODE_VERSION
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            _, runtime_error = run_bytecode(bytecode_path)
        assert runtime_error is None, runtime_error.as_string() if runtime_error else runtime_error
        assert output.getvalue() == "bytecode\n3\n"


# ---------------------------------------------------------------------------
# Native interop
# ---------------------------------------------------------------------------

def test_native_thread_join_all() -> None:
    if not native_available():
        print("SKIP  nativeThreadJoinAll: native extension not built")
        return
    require_output(
        """global setup(){}
global main(){
    nativeThreadJoinAll();
    println("joined");
}""",
        "joined\n",
        "nativeThreadJoinAll",
    )


def test_ffi_callback_signatures() -> None:
    """``ffiCallback`` accepts real signatures; docs once claimed one only."""
    if not native_available():
        print("SKIP  ffiCallback signatures: native extension not built")
        return
    import lynxer.cpp as cpp

    class _Target:
        def execute(self, *args):
            return 0

    target = _Target()
    for signature in (
        "cdecl:int32(int32,int32)",
        "cdecl:int32(int32)",
        "cdecl:int64(int32,int32,int32)",
        "cdecl:void(int32)",
        "cdecl:float64(float64)",
        "cdecl:uint8(int32,int32)",
    ):
        callback = cpp.ffiCallback(signature, target)
        assert callback is not None, f"ffiCallback rejected {signature!r}"
        cpp.ffiFreeCallback(callback)


# ---------------------------------------------------------------------------
# sound stdlib
# ---------------------------------------------------------------------------

def test_sound_module() -> None:
    """The standalone sound module is headless-safe on invalid handles."""
    # Every public function is called with an invalid handle so a broken
    # module-internal call surfaces here instead of only under a real device.
    require_output(
        """global setup(){ import("sound"); }
global main(){
    println(global.sound.loadSound("does-not-exist.wav"));
    println(global.sound.loadSoundStreaming("does-not-exist.wav"));
    println(global.sound.playSound(-1));
    println(global.sound.playSoundOnce(-1));
    println(global.sound.loopSound(-1));
    println(global.sound.stopSound(-1));
    println(global.sound.pauseSound(-1));
    println(global.sound.resumeSound(-1));
    println(global.sound.setSoundVolume(-1, 0.5));
    println(global.sound.isSoundPlaying(-1));
    println(global.sound.getSoundLength(-1));
    println(global.sound.releaseSound(-1));
    println(global.sound.soundCount());
}""",
        "-1\n-1\nfalse\nfalse\nfalse\nfalse\nfalse\nfalse\nfalse\nfalse\n0\nfalse\n0\n",
        "sound module",
    )


# ---------------------------------------------------------------------------
# Struct layout
# ---------------------------------------------------------------------------

def test_struct_packing() -> None:
    """A trailing alignment argument clamps padding for every field."""
    if not native_available():
        print("SKIP  struct packing: native extension not built")
        return
    require_output(
        """global setup(){}
global main(){
    println(memoryStructSize("int8 a, int32 b"));
    println(memoryStructSize("int8 a, int32 b", 1));
    println(memoryStructFieldOffset("int8 a, int32 b", "b"));
    println(memoryStructFieldOffset("int8 a, int32 b", "b", 1));
    println(memoryStructAlignment("int8 a, int32 b", 1));
    int handle = memoryStructAllocate("int8 a, int32 b", 1);
    memoryStructSet(handle, "a", 7);
    memoryStructSet(handle, "b", 12345);
    println(memoryStructGet(handle, "a"));
    println(memoryStructGet(handle, "b"));
}""",
        "8\n5\n4\n1\n1\n7\n12345\n",
        "struct packing",
    )


def test_struct_packing_rejects_bad_alignment() -> None:
    if not native_available():
        print("SKIP  struct packing alignment: native extension not built")
        return
    require_error(
        """global setup(){}
global main(){ int size = memoryStructSize("int8 a, int32 b", 3); }""",
        "power of two",
        "bad struct alignment",
    )


TESTS = [
    test_mutable_ownership,
    test_read_only_borrow_rejection,
    test_nested_sequence_pattern,
    test_enum_pattern_binding,
    test_empty_enum_variant,
    test_enum_payload_validation,
    test_switch_binding_shadowing_is_an_error,
    test_switch_binding_name_is_reusable,
    test_switch_duplicate_pattern_is_rejected,
    test_switch_unreachable_pattern_is_rejected,
    test_duplicate_enum_name,
    test_enum_conflicts_with_func,
    test_func_conflicts_with_enum,
    test_constant_folding,
    test_optimizer_keeps_runtime_errors,
    test_optimizer_skips_huge_powers,
    test_optimized_and_unoptimized_agree,
    test_bytecode_roundtrip,
    test_native_thread_join_all,
    test_ffi_callback_signatures,
    test_sound_module,
    test_struct_packing,
    test_struct_packing_rejects_bad_alignment,
]


def main() -> int:
    for test in TESTS:
        test()
    print("PASS  remaining language/runtime regressions")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
