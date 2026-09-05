#!/usr/bin/env python3
"""Print and validate the ARM64 syscall database used by Lynxer.

The ``system-calls`` package contains the complete Linux syscall table, while
``lynxer.syscalls.SYSCALL_TABLE`` describes the subset exposed as Lynxer
built-ins.  This script prints both pieces of information together so an
ARM64 build makes architecture-specific changes visible in its test output.
"""

from __future__ import annotations

import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

try:
    import system_calls
except ImportError as exc:  # pragma: no cover - exercised by an incomplete env
    print(
        "ERROR: the 'system-calls' package is required to inspect ARM64 syscalls",
        file=sys.stderr,
    )
    print(f"       {exc}", file=sys.stderr)
    raise SystemExit(1)

from lynxer.syscalls import SYSCALL_TABLE  # noqa: E402


ARCHITECTURE = "arm64"
# Linux ARM64 has ppoll and epoll_pwait, not the legacy poll and epoll_wait
# syscalls.  Lynxer keeps those built-ins in its cross-architecture registry
# and reports them unavailable.
EXPECTED_UNAVAILABLE = {"poll", "epoll_wait"}


def main() -> int:
    database = system_calls.syscalls()
    table = database.load_arch_table(ARCHITECTURE)
    builtins_by_syscall: dict[str, list[str]] = {}
    for builtin, syscall_name in SYSCALL_TABLE.items():
        builtins_by_syscall.setdefault(syscall_name, []).append(builtin)

    print(f"ARM64 syscall database: {len(table)} entries")
    print("number  syscall name             Lynxer expectation")
    print("------  -----------------------  -----------------------------")
    for syscall_name, number in sorted(table.items(), key=lambda item: (item[1], item[0])):
        expected_builtins = builtins_by_syscall.get(syscall_name)
        if expected_builtins:
            expectation = ", ".join(expected_builtins)
        else:
            expectation = "not exposed as a Lynxer built-in"
        print(f"{number:>6}  {syscall_name:<23}  {expectation}")

    print("\nExpected Lynxer ARM64 mappings:")
    failures: list[str] = []
    for builtin, syscall_name in SYSCALL_TABLE.items():
        if syscall_name in table:
            print(f"  {builtin:<40} -> {syscall_name:<23} #{table[syscall_name]}")
        elif syscall_name in EXPECTED_UNAVAILABLE:
            print(f"  {builtin:<40} -> {syscall_name:<23} unavailable (expected)")
        else:
            print(f"  {builtin:<40} -> {syscall_name:<23} MISSING")
            failures.append(f"{builtin} -> {syscall_name}")

    unexpected_available = sorted(EXPECTED_UNAVAILABLE.intersection(table))
    if unexpected_available:
        print("\nUnexpectedly available ARM64 syscalls:")
        for syscall_name in unexpected_available:
            print(f"  {syscall_name} (update EXPECTED_UNAVAILABLE if intentional)")
        failures.extend(f"unexpectedly available: {name}" for name in unexpected_available)

    if failures:
        print("\nARM64 syscall check: FAIL", file=sys.stderr)
        for failure in failures:
            print(f"  {failure}", file=sys.stderr)
        return 1

    print("\nARM64 syscall check: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())