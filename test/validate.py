#!/usr/bin/env python3
"""Broad regression validator for Lynxer.
BTW ALL generated Lynxer files are created below a temp dir and rm
auto. Existing ``test/*.lynx`` fixtures are checked but never edited
or deleted (obviously dude).
"""

from __future__ import annotations

import contextlib
import io
import os
import subprocess
import sys
import tempfile
import re
from pathlib import Path
from typing import Callable


ROOT = Path(__file__).resolve().parents[1]
SHELL = ROOT / "lynxer" / "shell.py"
sys.path.insert(0, str(ROOT))

from lynxer.bytecode import compile_to_bytecode, run_bytecode  # noqa: E402
from lynxer.install import INSTALL_PATH, _is_elf, _matching_pids  # noqa: E402
from lynxer.lynxer import run  # noqa: E402


class ValidationFailure(Exception):
    pass


def run_source(source: str, filename: str = "<validation>") -> tuple[str, object]:
    output = io.StringIO()
    with contextlib.redirect_stdout(output):
        _, error = run(filename, source)
    return output.getvalue(), error


def require_output(source: str, expected: str, name: str) -> None:
    output, error = run_source(source, f"<{name}>")
    if error is not None:
        raise ValidationFailure(f"{name}: runtime error:\n{error.as_string()}")
    if output != expected:
        raise ValidationFailure(
            f"{name}: expected output {expected!r}, received {output!r}"
        )


def require_error(source: str, fragment: str, name: str) -> None:
    _, error = run_source(source, f"<{name}>")
    if error is None:
        raise ValidationFailure(f"{name}: expected an error containing {fragment!r}")
    rendered = error.as_string()
    if fragment not in rendered:
        raise ValidationFailure(
            f"{name}: error did not contain {fragment!r}:\n{rendered}"
        )


def test_scalars_and_operators() -> None:
    require_output(
        """global setup(){}
global main(){
    int a = 7;
    float b = 2.5;
    bool ok = a > 3 and b < 3;
    println(a + 5);
    println(strOf(b));
    println(ok);
}""",
        "12\n2.5\ntrue\n",
        "scalars and operators",
    )


def test_lists_and_tuples() -> None:
    require_output(
        """global setup(){}
global main(){
    list values = [int 1, str "two", list [bool true]];
    tuple pair = (int 3, str "four");
    println(strOf(values));
    println(strOf(pair));
    println(returnLength(values));
    println(tupleLen(pair));
}""",
        "[1, two, [true]]\n(3, four)\n3\n2\n",
        "lists and tuples",
    )
    require_error(
        """global setup(){}
global main(){ list values = [int "wrong"]; }""",
        "declared as 'int'",
        "list type validation",
    )


def test_shared_aliases() -> None:
    require_output(
        """global setup(){}
global main(){
    int x = 42;
    shared int y = x;
    println(y);
    y = 100;
    println(x);
    unshare(y);
    y = 200;
    println(x);
    println(y);
}""",
        "42\n100\n100\n200\n",
        "shared aliases",
    )
    require_error(
        """global setup(){}
global main(){ int x = 1; shared int y = x + 1; }""",
        "must be initialized from another variable",
        "shared initializer validation",
    )
    require_output(
        """global setup(){}
global change(int x){
    shared x;
    x = 100;
}
global main(){
    int y = 42;
    change(y);
    println(y);
}""",
        "100\n",
        "shared function parameter",
    )


def test_control_flow_and_functions() -> None:
    require_output(
        """global setup(){}
global add(int a, int b){ return a + b; }
global main(){
    int total = 0;
    for(int i = 0; i < 4; i += 1){ total += i; }
    println(total);
    println(global.add(2, 3));
}""",
        "6\n5\n",
        "control flow and functions",
    )


def test_runtime_errors() -> None:
    require_error(
        """global setup(){}
global main(){ int value = "not an int"; }""",
        "Type mismatch",
        "runtime type error",
    )
    require_error(
        """global setup(){}
global main(){ int value = 1 / 0; }""",
        "Division by zero",
        "division by zero",
    )
    require_error(
        """global setup(){}
global main(){ println(missing); }""",
        "is not defined",
        "undefined variable",
    )


def test_low_level_memory() -> None:
    source_path = ROOT / "test" / "test20.lynx"
    source = source_path.read_text(encoding="utf-8")
    expected = (
        "-8\n"
        "-1600\n"
        "-320000\n"
        "-6400000000\n"
        "250\n"
        "65000\n"
        "4000000000\n"
        "16000000000\n"
        "1.5\n"
        "2.5\n"
        "4\n"
    )
    output, error = run_source(source, str(source_path))
    if error is not None:
        raise ValidationFailure(
            f"low-level memory fixture failed:\n{error.as_string()}"
        )
    if output != expected:
        raise ValidationFailure(
            f"low-level memory fixture: expected {expected!r}, received {output!r}"
        )

    require_error(
        """global setup(){}
global main(){ memoryWriteUInt8(1, 0, 256); }""",
        "0 to 255",
        "memory byte range validation",
    )
    require_error(
        """global setup(){}
global main(){ memoryReadInt32(-1, 0); }""",
        "non-negative integer arguments",
        "memory address validation",
    )
    require_error(
        """global setup(){}
global main(){ println(sizeof("unit32")); }""",
        "unknown C type",
        "uint32 spelling validation",
    )
    require_output(
        """global setup(){}
global main(){
    int values = memoryBlockAllocate("int32", 2);
    memoryBlockSet(values, 0, 7);
    memoryBlockView(values, "int32", 2);
    memoryViewSet(values, 1, 9);
    println(memoryArrayGet(values, 0));
    println(memoryViewGet(values, 1));
    println(memoryArrayLength(values));
    memoryFree(values);
}""",
        "7\n9\n2\n",
        "native typed memory blocks and views",
    )
    require_output(
        """global setup(){}
global main(){
    int a = memoryBlockAllocate("int8", 1);
    int b = memoryBlockAllocate("uint8", 1);
    int c = memoryBlockAllocate("int16", 1);
    int d = memoryBlockAllocate("uint16", 1);
    int e = memoryBlockAllocate("int32", 1);
    int f = memoryBlockAllocate("uint32", 1);
    int g = memoryBlockAllocate("int64", 1);
    int h = memoryBlockAllocate("uint64", 1);
    int i = memoryBlockAllocate("float32", 1);
    int j = memoryBlockAllocate("float64", 1);
    memoryBlockSet(a, 0, -8); memoryBlockSet(b, 0, 250);
    memoryBlockSet(c, 0, -1600); memoryBlockSet(d, 0, 65000);
    memoryBlockSet(e, 0, -320000); memoryBlockSet(f, 0, 4000000000);
    memoryBlockSet(g, 0, -6400000000); memoryBlockSet(h, 0, 16000000000);
    memoryBlockSet(i, 0, 1.5); memoryBlockSet(j, 0, 2.5);
    println(memoryBlockGet(a, 0)); println(memoryBlockGet(b, 0));
    println(memoryBlockGet(c, 0)); println(memoryBlockGet(d, 0));
    println(memoryBlockGet(e, 0)); println(memoryBlockGet(f, 0));
    println(memoryBlockGet(g, 0)); println(memoryBlockGet(h, 0));
    println(memoryBlockGet(i, 0)); println(memoryBlockGet(j, 0));
    memoryFree(a); memoryFree(b); memoryFree(c); memoryFree(d); memoryFree(e);
    memoryFree(f); memoryFree(g); memoryFree(h); memoryFree(i); memoryFree(j);
}""",
        "-8\n250\n-1600\n65000\n-320000\n4000000000\n"
        "-6400000000\n16000000000\n1.5\n2.5\n",
        "all native typed memory scalar types",
    )
    require_output(
        """global setup(){}
global main(){
    int raw = memoryAllocate(16);
    memoryBlockView(raw, "int32", 4);
    memoryBlockSet(raw, 0, 11);
    memoryArraySet(raw, 1, 22);
    println(memoryViewGet(raw, 0));
    println(memoryArrayGet(raw, 1));
    println(memoryBlockLength(raw));
    memoryFree(raw);
}""",
        "11\n22\n4\n",
        "typed views and array aliases",
    )
    require_output(
        """global setup(){}
global main(){
    int player = memoryStructAllocate("int32 id, float32 x");
    memoryStructSet(player, "id", 42);
    memoryStructSet(player, "x", 2.5);
    println(memoryStructGet(player, "id"));
    println(memoryStructGet(player, "x"));
    println(memoryStructSize("int32 id, float32 x"));
    println(memoryStructFieldOffset("int32 id, float32 x", "x"));
    memoryFree(player);
}""",
        "42\n2.5\n8\n4\n",
        "native struct layouts",
    )
    require_output(
        """global setup(){}
global main(){
    int player = nativeStructAllocate("int32 id, float64 score, uint8 alive");
    nativeStructSet(player, "id", 99);
    nativeStructSet(player, "score", 12.5);
    nativeStructSet(player, "alive", 1);
    println(nativeStructGet(player, "id"));
    println(nativeStructGet(player, "score"));
    println(nativeStructGet(player, "alive"));
    println(nativeStructFieldOffset("int32 id, float64 score, uint8 alive", "score"));
    println(nativeStructFieldSize("int32 id, float64 score, uint8 alive", "alive"));
    memoryFree(player);
}""",
        "99\n12.5\n1\n8\n1\n",
        "native struct aliases and padding",
    )
    require_error(
        """global setup(){}
global main(){
    int values = memoryBlockAllocate("int32", 1);
    memoryBlockGet(values, 1);
}""",
        "out of bounds",
        "typed block bounds validation",
    )
    require_error(
        """global setup(){}
global main(){
    int player = memoryStructAllocate("int32 id");
    memoryStructGet(player, "missing");
}""",
        "struct field is not present",
        "struct field validation",
    )
    require_error(
        """global setup(){}
global main(){
    int values = memoryBlockAllocate("uint8", 1);
    memoryBlockSet(values, 0, 256);
}""",
        "outside the range",
        "typed block range validation",
    )
    require_error(
        """global setup(){}
global main(){
    int values = memoryAllocate(4);
    memoryBlockView(values, "int32", 2);
}""",
        "out of bounds",
        "typed view allocation bounds validation",
    )
    require_error(
        """global setup(){}
global main(){
    int values = memoryBlockAllocate("int32", 1);
    memoryFree(values);
    memoryBlockLength(values);
}""",
        "typed memory block",
        "typed block lifetime validation",
    )
    require_error(
        """global setup(){}
global main(){
    int values = memoryBlockAllocate("int32", 1);
    memoryBlockGet(values, 1);
}""",
        "out of bounds",
        "typed block index validation",
    )
    require_error(
        """global setup(){}
global main(){
    int player = memoryStructAllocate("int32 id");
    memoryStructSet(player, "missing", 1);
}""",
        "struct field is not present",
        "struct field write validation",
    )


def test_imports(temp_root: Path) -> None:
    module = temp_root / "validation_module.lynx"
    module.write_text(
        """global setup(){ int exported = 9; }
global helper(){ return 4; }
""",
        encoding="utf-8",
    )
    source = """global setup(){ importAs("validation_module.lynx", "mod"); }
global main(){{ println(global.mod.exported); println(global.mod.helper()); }}
""".replace("global main(){{", "global main(){").replace("}}", "}")
    old_cwd = Path.cwd()
    os.chdir(temp_root)
    try:
        output, error = run_source(source, str(temp_root / "import_main.lynx"))
    finally:
        os.chdir(old_cwd)
    if error is not None:
        raise ValidationFailure(f"imports: runtime error:\n{error.as_string()}")
    if output != "9\n4\n":
        raise ValidationFailure(f"imports: received {output!r}")


def test_bytecode(temp_root: Path) -> None:
    source_path = temp_root / "bytecode_case.lynx"
    source = """global setup(){}
global main(){
    int x = 42;
    shared int y = x;
    unshare(y);
    y = 100;
    println(x);
    println(y);
}"""
    source_path.write_text(source, encoding="utf-8")
    bytecode_path, error = compile_to_bytecode(str(source_path), source)
    if error is not None or bytecode_path is None:
        raise ValidationFailure(
            f"bytecode compilation failed: {error.as_string() if error else 'unknown error'}"
        )
    output = io.StringIO()
    with contextlib.redirect_stdout(output):
        _, error = run_bytecode(bytecode_path)
    if error is not None:
        raise ValidationFailure(f"bytecode execution failed:\n{error.as_string()}")
    if output.getvalue() != "42\n100\n":
        raise ValidationFailure(f"bytecode: received {output.getvalue()!r}")


def test_cli(temp_root: Path) -> None:
    source_path = temp_root / "cli_case.lynx"
    source_path.write_text(
        """global setup(){}
global main(){ println("cli works"); }
""",
        encoding="utf-8",
    )
    run_result = subprocess.run(
        [sys.executable, str(SHELL), str(source_path)],
        cwd=ROOT,
        capture_output=True,
        text=True,
        check=False,
    )
    if run_result.returncode != 0 or run_result.stdout != "cli works\n":
        raise ValidationFailure(
            f"CLI source execution failed: rc={run_result.returncode}, "
            f"stdout={run_result.stdout!r}, stderr={run_result.stderr!r}"
        )

    compile_result = subprocess.run(
        [sys.executable, str(SHELL), "--compile", str(source_path)],
        cwd=ROOT,
        capture_output=True,
        text=True,
        check=False,
    )
    bytecode_path = source_path.with_suffix(".lynxc")
    if compile_result.returncode != 0 or not bytecode_path.exists():
        raise ValidationFailure(
            f"CLI bytecode compilation failed: rc={compile_result.returncode}, "
            f"stdout={compile_result.stdout!r}, stderr={compile_result.stderr!r}"
        )


def test_installer_safety() -> None:
    if not _is_elf(str(SHELL)):
        pass  # Python source must not be mistaken for an installable binary.
    if os.path.exists(INSTALL_PATH) and not _is_elf(INSTALL_PATH):
        raise ValidationFailure(
            f"installer: existing {INSTALL_PATH} is not an ELF executable"
        )
    if os.geteuid() != 0 and _matching_pids(INSTALL_PATH):
        raise ValidationFailure("installer: found unexpected Lynxer processes")


def test_existing_fixtures() -> None:
    fixture_pattern = re.compile(r"^test(\d+)\.lynx$")
    fixtures = sorted(
        (
            fixture
            for fixture in (ROOT / "test").iterdir()
            if fixture.is_file() and fixture_pattern.match(fixture.name)
        ),
        key=lambda fixture: int(fixture_pattern.match(fixture.name).group(1)),
    )
    for fixture in fixtures:
        source = fixture.read_text(encoding="utf-8")
        if "forever(" in source:
            print(f"SKIP  existing fixture {fixture.name}: contains unbounded forever()")
            continue
        expected_error = re.search(r"^\s*//\s*EXPECT_ERROR:\s*(.+?)\s*$", source, re.MULTILINE)
        _, error = run_source(source, str(fixture))
        if expected_error:
            fragment = expected_error.group(1)
            if error is None or fragment not in error.as_string():
                raise ValidationFailure(
                    f"fixture {fixture.name} expected error containing {fragment!r}, "
                    f"received {error.as_string() if error else 'no error'}"
                )
        elif error is not None:
            raise ValidationFailure(
                f"existing fixture {fixture.name} failed:\n{error.as_string()}"
            )
        print(f"PASS  {fixture.name}")


TESTS: list[tuple[str, Callable[[], None]]] = [
    ("scalars and operators", test_scalars_and_operators),
    ("lists and tuples", test_lists_and_tuples),
    ("shared aliases", test_shared_aliases),
    ("control flow and functions", test_control_flow_and_functions),
    ("runtime errors", test_runtime_errors),
    ("low-level memory", test_low_level_memory),
    ("installer safety", test_installer_safety),
]


def main() -> int:
    passed = 0
    failed = 0
    with tempfile.TemporaryDirectory(prefix="lynxer-validation-") as directory:
        temp_root = Path(directory)
        tests = TESTS + [
            ("imports", lambda: test_imports(temp_root)),
            ("bytecode", lambda: test_bytecode(temp_root)),
            ("CLI", lambda: test_cli(temp_root)),
            ("existing .lynx fixtures", test_existing_fixtures),
        ]
        for name, test in tests:
            try:
                test()
            except Exception as exc:
                failed += 1
                print(f"FAIL  {name}: {exc}", file=sys.stderr)
            else:
                passed += 1
                print(f"PASS  {name}")

    print(f"\nValidation complete: {passed} passed, {failed} failed.")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())