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
    fixtures = sorted((ROOT / "test").glob("*.lynx"))
    for fixture in fixtures:
        source = fixture.read_text(encoding="utf-8")
        if "forever(" in source:
            print(f"SKIP  existing fixture {fixture.name}: contains unbounded forever()")
            continue
        _, error = run_source(source, str(fixture))
        if error is not None:
            raise ValidationFailure(
                f"existing fixture {fixture.name} failed:\n{error.as_string()}"
            )


TESTS: list[tuple[str, Callable[[], None]]] = [
    ("scalars and operators", test_scalars_and_operators),
    ("lists and tuples", test_lists_and_tuples),
    ("shared aliases", test_shared_aliases),
    ("control flow and functions", test_control_flow_and_functions),
    ("runtime errors", test_runtime_errors),
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