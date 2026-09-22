#!/usr/bin/env python3
"""Capture and verify Lynxer's Stage 1 behavior corpus.

The corpus stores the exact command-line result for every runnable
``test/*.lynx`` fixture and an import smoke test for every stdlib module.
Interactive fixtures are represented as explicit skips so the corpus never
blocks waiting for a window, input, or an unbounded loop.

Usage:

    python scripts/golden_corpus.py --update
    python scripts/golden_corpus.py
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
import tempfile
from dataclasses import asdict, dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SHELL = ROOT / "lynxer" / "shell.py"
CORPUS_DIR = ROOT / "test" / "golden"
MANIFEST = CORPUS_DIR / "manifest.json"
TIMEOUT_SECONDS = 15


@dataclass(frozen=True)
class Case:
    name: str
    kind: str
    source: str | None
    command_source: str | None = None
    skip_reason: str | None = None


@dataclass(frozen=True)
class Result:
    name: str
    kind: str
    source: str | None
    status: str
    returncode: int | None
    stdout: str
    stderr: str
    command_source: str | None = None
    skip_reason: str | None = None


def _relative(path: Path) -> str:
    return path.relative_to(ROOT).as_posix()


def discover_cases() -> list[Case]:
    cases: list[Case] = []
    for source in sorted((ROOT / "test").glob("*.lynx")):
        text = source.read_text(encoding="utf-8")
        reason = None
        if "global.game.run(" in text:
            reason = "interactive game fixture; it waits for a window to close"
        elif "forever(" in text:
            reason = "unbounded fixture; it is intentionally not run by the corpus"
        cases.append(
            Case(
                name=f"test/{source.name}",
                kind="fixture",
                source=_relative(source),
                skip_reason=reason,
            )
        )

    for source in sorted((ROOT / "lynxer" / "stdlib").glob("*.lynx")):
        cases.append(
            Case(
                name=f"stdlib/{source.stem}",
                kind="stdlib-import",
                source=_relative(source),
            )
        )
    return cases


def _stdlib_smoke_source(module: str) -> str:
    return (
        "global setup(){\n"
        f'    import("{module}");\n'
        "}\n"
        "global main(){}\n"
    )


def _normalise_output(value: str, temporary_root: Path) -> str:
    """Remove generated workspace paths while preserving program output."""
    return value.replace(str(temporary_root), "<golden-stdlib>").replace(
        str(ROOT), "<project>"
    )


def _normalise_case_output(case: Case, value: str, temporary_root: Path) -> str:
    """Canonicalize only outputs whose fixture intentionally observes raw memory."""
    value = _normalise_output(value, temporary_root)
    if case.name == "test/test22.lynx":
        # The fixture checks zero-size allocation behavior. Allocator addresses
        # and the integer read from a zero-byte copy are inherently process-
        # dependent, so preserve the labels while comparing stable semantics.
        value = re.sub(
            r"(?m)^(memoryAllocate(?:Zeroed)?\([^\n]+:\s*)\d+$",
            r"\1<address>",
            value,
        )
        value = re.sub(
            r"(After memoryCopy\(\.\.\., 0\):\n)-?\d+",
            r"\1<unspecified-int>",
            value,
        )
    return value


def run_case(case: Case, temporary_root: Path) -> Result:
    if case.skip_reason:
        return Result(
            name=case.name,
            kind=case.kind,
            source=case.source,
            status="skipped",
            returncode=None,
            stdout="",
            stderr="",
            command_source=case.command_source,
            skip_reason=case.skip_reason,
        )

    command_source = case.source
    generated_path: Path | None = None
    if case.kind == "stdlib-import":
        module = Path(case.source or "").stem
        generated_path = temporary_root / f"{module}.lynx"
        generated_path.write_text(
            _stdlib_smoke_source(module),
            encoding="utf-8",
        )
        command_source = str(generated_path)

    assert command_source is not None
    try:
        completed = subprocess.run(
            [sys.executable, str(SHELL), command_source],
            cwd=ROOT,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=TIMEOUT_SECONDS,
            check=False,
        )
    except subprocess.TimeoutExpired as exc:
        stdout = exc.stdout or ""
        stderr = exc.stderr or ""
        if isinstance(stdout, bytes):
            stdout = stdout.decode("utf-8", "replace")
        if isinstance(stderr, bytes):
            stderr = stderr.decode("utf-8", "replace")
        return Result(
            name=case.name,
            kind=case.kind,
            source=case.source,
            status="timeout",
            returncode=None,
            stdout=_normalise_case_output(case, stdout, temporary_root),
            stderr=_normalise_case_output(case, stderr, temporary_root),
            command_source=case.source,
        )

    return Result(
        name=case.name,
        kind=case.kind,
        source=case.source,
        status="completed",
        returncode=completed.returncode,
        stdout=_normalise_case_output(case, completed.stdout, temporary_root),
        stderr=_normalise_case_output(case, completed.stderr, temporary_root),
        command_source=case.source,
    )


def _result_path(name: str) -> Path:
    safe_name = name.replace("/", "__")
    return CORPUS_DIR / f"{safe_name}.json"


def _read_result(path: Path) -> Result:
    return Result(**json.loads(path.read_text(encoding="utf-8")))


def update_corpus(cases: list[Case]) -> int:
    CORPUS_DIR.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="lynxer-golden-") as directory:
        temporary_root = Path(directory)
        results = [run_case(case, temporary_root) for case in cases]

    for result in results:
        _result_path(result.name).write_text(
            json.dumps(asdict(result), indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
        marker = "SKIP" if result.status == "skipped" else "CAPTURE"
        print(f"{marker:7} {result.name}")

    manifest = {
        "format": 1,
        "cases": [asdict(case) for case in cases],
    }
    MANIFEST.write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(f"Wrote {len(results)} cases to {CORPUS_DIR.relative_to(ROOT)}/")
    return 0


def check_corpus(cases: list[Case]) -> int:
    if not MANIFEST.exists():
        print(
            f"Missing {MANIFEST.relative_to(ROOT)}; run with --update first.",
            file=sys.stderr,
        )
        return 2

    expected_manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    actual_manifest = {
        "format": 1,
        "cases": [asdict(case) for case in cases],
    }
    if expected_manifest.get("format") != actual_manifest["format"]:
        print("Golden corpus format is incompatible; run with --update.", file=sys.stderr)
        return 2
    if expected_manifest.get("cases") != actual_manifest["cases"]:
        print("Golden case manifest changed; run with --update.", file=sys.stderr)
        return 1

    failures = 0
    with tempfile.TemporaryDirectory(prefix="lynxer-golden-") as directory:
        temporary_root = Path(directory)
        for case in cases:
            path = _result_path(case.name)
            if not path.exists():
                print(f"MISSING {case.name}: {path.relative_to(ROOT)}")
                failures += 1
                continue
            expected = _read_result(path)
            actual = run_case(case, temporary_root)
            if expected != actual:
                print(f"DIFF    {case.name}")
                failures += 1
            else:
                print(f"PASS    {case.name}")

    if failures:
        print(f"Golden corpus check failed: {failures} case(s).", file=sys.stderr)
        return 1
    print(f"Golden corpus check passed: {len(cases)} cases.")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--update",
        action="store_true",
        help="capture the current implementation as the new baseline",
    )
    args = parser.parse_args()
    cases = discover_cases()
    return update_corpus(cases) if args.update else check_corpus(cases)


if __name__ == "__main__":
    raise SystemExit(main())