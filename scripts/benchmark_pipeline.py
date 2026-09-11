#!/usr/bin/env python3
"""Benchmark Lynxer pipeline stages.

This intentionally excludes builtins implementation work.  It reports the
current Python lexer/parser/runtime timings and, when the native extension is
available, compares compiled-bytecode execution through the C++ VM.
"""

from __future__ import annotations

import argparse
import contextlib
import io
import os
import statistics
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
import sys

if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from lynxer import run
from lynxer.bytecode import compile_to_bytecode, run_bytecode
from lynxer.lexer import Lexer
from lynxer.parser import Parser


SOURCE = """\
global setup(){}
global square(int value){
    return value * value;
}
global main(){
    int total = 0;
    for(int i = 0; i < 100; i = i + 1){
        total = total + global.square(i);
    }
}
"""


def measure(function, iterations: int) -> list[float]:
    samples = []
    for _ in range(iterations):
        start = time.perf_counter_ns()
        function()
        samples.append((time.perf_counter_ns() - start) / 1_000_000)
    return samples


def report(name: str, samples: list[float]) -> None:
    print(
        f"{name:28} median={statistics.median(samples):9.3f} ms "
        f"mean={statistics.mean(samples):9.3f} ms "
        f"min={min(samples):9.3f} ms"
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("-n", "--iterations", type=int, default=25)
    args = parser.parse_args()
    if args.iterations < 1:
        parser.error("--iterations must be positive")

    with tempfile.TemporaryDirectory(prefix="lynxer-benchmark-") as directory:
        source_path = Path(directory) / "benchmark.lynx"
        source_path.write_text(SOURCE, encoding="utf-8")

        tokens, error = Lexer(str(source_path), SOURCE).make_tokens()
        if error:
            raise RuntimeError(error.as_string())
        parsed = Parser(tokens).parse()
        if parsed.error:
            raise RuntimeError(parsed.error.as_string())

        bytecode_path, error = compile_to_bytecode(
            str(source_path), SOURCE, use_cache=False
        )
        if error:
            raise RuntimeError(error.as_string())

        def lex():
            result, lex_error = Lexer(str(source_path), SOURCE).make_tokens()
            if lex_error:
                raise RuntimeError(lex_error.as_string())
            return result

        def parse():
            result_tokens, lex_error = Lexer(str(source_path), SOURCE).make_tokens()
            if lex_error:
                raise RuntimeError(lex_error.as_string())
            result = Parser(result_tokens).parse()
            if result.error:
                raise RuntimeError(result.error.as_string())
            return result.node

        def execute_source():
            with contextlib.redirect_stdout(io.StringIO()):
                value, runtime_error = run(str(source_path), SOURCE)
            if runtime_error:
                raise RuntimeError(runtime_error.as_string())
            return value

        def compile_source():
            output, compile_error = compile_to_bytecode(
                str(source_path), SOURCE, use_cache=False
            )
            if compile_error:
                raise RuntimeError(compile_error.as_string())
            return output

        def execute_bytecode():
            with contextlib.redirect_stdout(io.StringIO()):
                value, runtime_error = run_bytecode(str(bytecode_path))
            if runtime_error:
                raise RuntimeError(runtime_error.as_string())
            return value

        print(f"iterations: {args.iterations}")
        print(f"source: {len(SOURCE)} bytes")
        report("lexer (Python)", measure(lex, args.iterations))
        report("parser (Python)", measure(parse, args.iterations))
        report("runtime source (Python)", measure(execute_source, args.iterations))
        report("bytecode compile (Python)", measure(compile_source, args.iterations))

        try:
            import lynxer.bytecode_vm  # noqa: F401
        except ImportError:
            print("bytecode runtime (C++): unavailable")
        else:
            report("bytecode runtime (C++)", measure(execute_bytecode, args.iterations))

        print()
        print("Native lexer/parser/runtime: not enabled yet.")
        return 0


if __name__ == "__main__":
    raise SystemExit(main())