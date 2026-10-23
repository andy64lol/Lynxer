#!/usr/bin/env python3
"""Exercise the interactive `--debug` commands against a known fixture."""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def check(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(f"debugger test failed: {message}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--lynxer", default=str(ROOT / "lynxer" / "lynxer"))
    parser.add_argument("--fixture", default=str(
        ROOT / "lynxer" / "examples" / "debugger.lynx"))
    args = parser.parse_args()

    commands = "\n".join([
        "break 9",
        "continue",
        "locals",
        "bt",
        "print total",
        "continue",
        "",
    ])
    result = subprocess.run(
        [args.lynxer, "--debug", args.fixture],
        input=commands, capture_output=True, text=True, check=False,
    )
    output = result.stdout + result.stderr
    check(result.returncode == 0,
          f"debugger exited {result.returncode}:\n{output}")
    check(f"stopped at {args.fixture}:13" in output,
          f"missing initial stop at main:\n{output}")
    check("breakpoint set at 9" in output,
          f"breakpoint was not set:\n{output}")
    check(f"stopped at {args.fixture}:9" in output,
          f"did not stop at the breakpoint:\n{output}")
    check("int total = 5" in output,
          f"locals did not include the computed total:\n{output}")
    check("int b = 3" in output and "int a = 2" in output,
          f"locals did not include the parameters:\n{output}")
    check("#0 add at" in output and "#1 main at" in output,
          f"backtrace is missing a frame:\n{output}")
    check("(lynxer) 5" in output,
          f"program did not continue to completion:\n{output}")
    print("debugger test passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
