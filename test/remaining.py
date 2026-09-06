#!/usr/bin/env python3
"""Regression coverage for the v0.1.8 language/runtime additions."""

from __future__ import annotations

import contextlib
import io
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from lynxer.bytecode import compile_to_bytecode, load_bytecode, run_bytecode
from lynxer.lynxer import run


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


def main() -> int:
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
    require_error(
        """global setup(){}
enum result = [Ok(int value)]{}
global main(){ any value = result.Ok("bad"); }""",
        "declared as 'int'",
        "enum payload validation",
    )

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
        assert load_bytecode(bytecode_path)["version"] == 8
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            _, runtime_error = run_bytecode(bytecode_path)
        assert runtime_error is None, runtime_error.as_string() if runtime_error else runtime_error
        assert output.getvalue() == "bytecode\n3\n"

    print("PASS  remaining language/runtime regressions")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())