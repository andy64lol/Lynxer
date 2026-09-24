#!/usr/bin/env python3
"""Golden tests for Lynxer's CLI surface and diagnostic text.

Each case in ``lynxer/golden/cases.json`` names the argv to run, the exit code
and the exact stdout/stderr the interpreter must produce. This pins Lynxer's
*own* output -- source-located error strings and CLI messages -- rather than the
Python implementation's exception text, which Lynxer has deliberately diverged
from. See ``lynxer/docs/parity.md``.

A trailing newline is ignored on both sides so an editor's final newline never
causes a false failure. Only the standard library is needed. Run it against the
built interpreter:

    python3 lynxer/scripts/check_golden.py --lynxer ./lynxer/lynxer
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--lynxer",
        default="./lynxer/lynxer",
        help="path to the Lynxer executable (default: ./lynxer/lynxer)",
    )
    parser.add_argument(
        "--cases",
        default=str(ROOT / "lynxer" / "golden" / "cases.json"),
        help="path to the golden case manifest",
    )
    return parser.parse_args()


def show(value: str) -> str:
    return repr(value) if value else "<empty>"


def main() -> int:
    args = parse_args()
    given = Path(args.lynxer)
    binary = given if given.is_absolute() else (ROOT / given)
    if not binary.is_file():
        print(f"check_golden: interpreter not found: {binary}", file=sys.stderr)
        return 1

    manifest = Path(args.cases)
    try:
        cases = json.loads(manifest.read_text(encoding="utf-8"))
    except FileNotFoundError:
        print(f"check_golden: manifest not found: {manifest}", file=sys.stderr)
        return 1
    except json.JSONDecodeError as error:
        print(f"check_golden: invalid manifest {manifest}: {error}", file=sys.stderr)
        return 1

    checked = 0
    failures = 0
    for name, case in cases.items():
        checked += 1
        expected_exit = case.get("exit", 0)
        # A case may create a scratch file from a fixture, so a case that
        # rewrites a file (--format) can be tested without touching the
        # repository. `{scratch}` in an argument expands to its path.
        scratch = case.get("scratch")
        scratch_path = None
        if scratch:
            scratch_path = ROOT / scratch
            source_path = ROOT / case["sourceFile"]
            scratch_path.write_text(
                source_path.read_text(encoding="utf-8"), encoding="utf-8"
            )
        args = [
            argument.replace("{scratch}", scratch or "")
            for argument in case.get("args", [])
        ]
        try:
            completed = subprocess.run(
                [str(binary), *args],
                cwd=ROOT,
                input=case.get("stdin", ""),
                capture_output=True,
                text=True,
            )
        finally:
            if scratch_path is not None:
                scratch_path.unlink(missing_ok=True)

        problems = []
        if completed.returncode != expected_exit:
            problems.append(
                f"exit code: expected {expected_exit}, got {completed.returncode}"
            )
        for stream, actual in (
            ("stdout", completed.stdout),
            ("stderr", completed.stderr),
        ):
            if stream in case:
                if case[stream].rstrip("\n") != actual.rstrip("\n"):
                    problems.append(
                        f"{stream}: expected {show(case[stream])}, got {show(actual)}"
                    )
            elif actual.strip():
                problems.append(f"{stream}: expected no output, got {show(actual)}")

        if problems:
            failures += 1
            print(f"golden case failed: {name}")
            for problem in problems:
                print(f"  {problem}")

    print(f"golden: {checked} case(s) checked, {failures} error(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
