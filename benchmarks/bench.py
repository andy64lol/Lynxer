#!/usr/bin/env python3
"""Measure Lynxer interpreter, optimizer, and compiled startup timings.

The measurements are intentionally process-level: they include executable
startup, parsing, optimization (when enabled), and execution. Compare runs on
the same machine and build; do not treat them as cross-machine scores.
"""

from __future__ import annotations

import argparse
import json
import os
import platform
import statistics
import subprocess
import tempfile
import time
from pathlib import Path


def measure(command: list[str], runs: int, warmups: int) -> list[float]:
    samples: list[float] = []
    for index in range(warmups + runs):
        started = time.perf_counter()
        result = subprocess.run(command, capture_output=True, check=False)
        elapsed = time.perf_counter() - started
        if result.returncode != 0:
            stderr = result.stderr.decode(errors="replace")
            raise RuntimeError(
                f"command failed ({result.returncode}): {' '.join(command)}\n{stderr}"
            )
        if index >= warmups:
            samples.append(elapsed * 1000.0)
    return samples


def summarize(samples: list[float]) -> dict[str, float | list[float]]:
    ordered = sorted(samples)
    p90_index = min(len(ordered) - 1, int(0.9 * len(ordered)))
    return {
        "median_ms": statistics.median(samples),
        "p90_ms": ordered[p90_index],
        "samples_ms": [round(sample, 3) for sample in samples],
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--lynxer", default="lynxer/lynxer",
                        help="built Lynxer executable")
    parser.add_argument("--runs", type=int, default=9,
                        help="measured process launches per case")
    parser.add_argument("--warmups", type=int, default=2,
                        help="discarded launches per case")
    parser.add_argument("--loop-count", type=int, default=100_000,
                        help="iterations in the arithmetic workload")
    parser.add_argument("--json", type=Path,
                        help="also write the result document to this path")
    args = parser.parse_args()
    if args.runs < 1 or args.warmups < 0 or args.loop_count < 1:
        parser.error("runs and loop-count must be positive; warmups cannot be negative")

    executable = Path(args.lynxer).expanduser().resolve()
    if not executable.is_file():
        parser.error(f"Lynxer executable not found: {executable}")

    workload = f"""global setup() {{}}
global main() {{
    int index = 0;
    int total = 0;
    while (index < {args.loop_count}) {{
        total = total + index * 3 + (8 * 7);
        index = index + 1;
    }}
    println(total);
}}
"""
    startup = "global setup() {}\nglobal main() {}\n"
    command = [str(executable)]
    compiled_suffix = ".exe" if os.name == "nt" else ""

    with tempfile.TemporaryDirectory(prefix="lynxer-bench-") as temp_name:
        temp = Path(temp_name)
        workload_path = temp / "workload.lynx"
        startup_path = temp / "startup.lynx"
        workload_path.write_text(workload, encoding="utf-8")
        startup_path.write_text(startup, encoding="utf-8")
        compiled_workload = temp / f"workload-compiled{compiled_suffix}"
        compiled_startup = temp / f"startup-compiled{compiled_suffix}"

        subprocess.run(
            command + ["--compile", str(workload_path), "-o", str(compiled_workload)],
            capture_output=True, check=True,
        )
        subprocess.run(
            command + ["--compile", str(startup_path), "-o", str(compiled_startup)],
            capture_output=True, check=True,
        )

        cases = {
            "interpreter_optimized": command + [str(workload_path)],
            "interpreter_no_opt": command + ["--no-opt", str(workload_path)],
            "compiled_workload": [str(compiled_workload)],
            "interpreter_empty_startup": command + [str(startup_path)],
            "compiled_empty_startup": [str(compiled_startup)],
        }
        results = {
            name: summarize(measure(run_command, args.runs, args.warmups))
            for name, run_command in cases.items()
        }

    def median(name: str) -> float:
        value = results[name]["median_ms"]
        assert isinstance(value, float)
        return value

    optimized = median("interpreter_optimized")
    unoptimized = median("interpreter_no_opt")
    source_start = median("interpreter_empty_startup")
    compiled_start = median("compiled_empty_startup")
    report = {
        "lynxer": str(executable),
        "version": subprocess.run(
            command + ["--version"], capture_output=True, text=True, check=False
        ).stdout.strip(),
        "platform": platform.platform(),
        "runs": args.runs,
        "warmups": args.warmups,
        "loop_count": args.loop_count,
        "metrics": results,
        "comparisons": {
            "optimizer_speedup": (unoptimized / optimized) if optimized else None,
            "compiled_empty_startup_speedup": (
                source_start / compiled_start if compiled_start else None
            ),
        },
    }
    rendered = json.dumps(report, indent=2)
    print(rendered)
    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(rendered + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
