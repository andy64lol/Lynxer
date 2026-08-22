#!/usr/bin/env python3
"""Comprehensive source-tree and executable validator for Lynxer.

The focused regression suite lives in ``test/validate.py``.  This runner adds
interpreter coverage audits, parser/stdlib sweeps, bytecode corruption checks,
and optional validation of a packaged Lynxer executable.
"""

from __future__ import annotations

import contextlib
import io
import os
import pickle
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent
if not list((ROOT / "lynxer").glob("cpp*.so")):
    build = subprocess.run(
        ["make", "buildCpp"],
        cwd=ROOT,
        capture_output=True,
        text=True,
    )
    if build.returncode != 0:
        print(build.stdout, file=sys.stdout)
        print(build.stderr, file=sys.stderr)
        raise SystemExit("could not build the native extension")
sys.path.insert(0, str(ROOT))

from lynxer import builtins, lynxer  # noqa: E402
from lynxer.bytecode import BYTECODE_MAGIC, BYTECODE_VERSION, load_bytecode  # noqa: E402
from lynxer.bytecode import compile_to_bytecode, run_bytecode  # noqa: E402


class ValidationFailure(Exception):
    pass


def check(name, fn):
    try:
        fn()
    except Exception as exc:
        print(f"FAIL  {name}: {exc}", file=sys.stderr)
        return False
    print(f"PASS  {name}")
    return True


def run_source(source: str):
    output = io.StringIO()
    with contextlib.redirect_stdout(output):
        _, error = lynxer.run("<comprehensive-validation>", source)
    if error is not None:
        raise ValidationFailure(error.as_string())
    return output.getvalue()


def validate_builtin_coverage():
    missing = [
        name for name in builtins.BUILTIN_FUNCTION_NAMES
        if not hasattr(builtins.BuiltInFunction, f"execute_{name}")
    ]
    if missing:
        raise ValidationFailure("built-ins without execute handlers: " + ", ".join(missing))
    if len(set(builtins.BUILTIN_FUNCTION_NAMES)) != len(builtins.BUILTIN_FUNCTION_NAMES):
        raise ValidationFailure("built-in registry contains duplicate names")


def validate_source_tree():
    failures = []
    for path in sorted((ROOT / "lynxer").rglob("*.lynx")):
        source = path.read_text(encoding="utf-8")
        tokens, error = lynxer.Lexer(str(path), source).make_tokens()
        if error:
            failures.append(f"{path}: lexer: {error.as_string()}")
            continue
        parsed = lynxer.Parser(tokens).parse()
        if parsed.error:
            failures.append(f"{path}: parser: {parsed.error.as_string()}")
    if failures:
        raise ValidationFailure("\n".join(failures))


def validate_interpreter_smoke():
    output = run_source(
        """global setup(){}
global factorial(int n){
    if(n <= 1){ return 1; }
    return n * global.factorial(n - 1);
}
global main(){
    list values = [int 1, int 2, int 3];
    int total = 0;
    for(int i = 0; i < returnLength(values); i += 1){ total += values[i]; }
    println(global.factorial(6));
    println(total);
}"""
    )
    if output != "720\n6\n":
        raise ValidationFailure(f"unexpected interpreter output: {output!r}")


def validate_bytecode_security():
    source = "global setup(){}\nglobal main(){ println(42); }\n"
    with tempfile.TemporaryDirectory(prefix="lynxer-comprehensive-") as directory:
        source_path = Path(directory) / "case.lynx"
        source_path.write_text(source, encoding="utf-8")
        bytecode_path, error = compile_to_bytecode(str(source_path), source)
        if error or not bytecode_path:
            raise ValidationFailure(error.as_string() if error else "compile failed")
        payload = load_bytecode(bytecode_path)
        if payload.get("version") != BYTECODE_VERSION:
            raise ValidationFailure("compiled bytecode has the wrong version")
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            _, error = run_bytecode(bytecode_path)
        if error or output.getvalue() != "42\n":
            raise ValidationFailure(f"bytecode round-trip failed: {error or output.getvalue()!r}")

        corrupt = Path(directory) / "corrupt.lynxc"
        corrupt.write_bytes(BYTECODE_MAGIC + zlib.compress(pickle.dumps({"bad": True})))
        try:
            load_bytecode(str(corrupt))
        except ValueError:
            pass
        else:
            raise ValidationFailure("corrupt bytecode was accepted")


def validate_cli():
    with tempfile.TemporaryDirectory(prefix="lynxer-cli-") as directory:
        source = Path(directory) / "cli.lynx"
        source.write_text("global setup(){}\nglobal main(){ println(\"ok\"); }\n", encoding="utf-8")
        command = [sys.executable, str(ROOT / "lynxer" / "shell.py")]
        result = subprocess.run(command + [str(source)], cwd=ROOT, capture_output=True, text=True)
        if result.returncode != 0 or result.stdout != "ok\n":
            raise ValidationFailure(f"source CLI failed: {result.returncode}: {result.stderr}")
        result = subprocess.run(command + ["--compile", str(source)], cwd=ROOT, capture_output=True, text=True)
        if result.returncode != 0 or not source.with_suffix(".lynxc").exists():
            raise ValidationFailure(f"bytecode CLI failed: {result.stderr}")


def executable_candidates():
    configured = os.environ.get("LYNXER_EXECUTABLE")
    candidates = [Path(configured)] if configured else []
    candidates += [ROOT / "dist" / "lynxer", ROOT / "lynxer" / "dist" / "lynxer", Path("/usr/bin/lynxer")]
    return [path for path in candidates if path.is_file() and os.access(path, os.X_OK)]


def validate_executable():
    candidates = executable_candidates()
    if not candidates:
        raise ValidationFailure(
            "no executable found; build one first or set LYNXER_EXECUTABLE"
        )
    for executable in candidates:
        version = subprocess.run([str(executable), "--version"], capture_output=True, text=True)
        if version.returncode != 0 or not version.stdout.startswith("Lynxer "):
            raise ValidationFailure(f"{executable}: --version failed")
    print("  checked: " + ", ".join(str(path) for path in candidates))


def main(argv=None):
    argv = sys.argv[1:] if argv is None else argv
    validate_exe = "--validate-executeable" in argv or "--validate-executable" in argv
    tests = [
        ("built-in handler coverage", validate_builtin_coverage),
        ("stdlib source tree", validate_source_tree),
        ("interpreter smoke", validate_interpreter_smoke),
        ("bytecode round-trip and safety", validate_bytecode_security),
        ("source CLI", validate_cli),
    ]
    if validate_exe:
        tests.append(("packaged executable", validate_executable))
    passed = sum(check(name, fn) for name, fn in tests)
    failed = len(tests) - passed
    print(f"\nComprehensive validation complete: {passed} passed, {failed} failed.")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())