#!/usr/bin/env python3
"""Wait for a Lynxer Linux build run and download its binary artifact.

The Arch package workflows consume the exact binaries produced by
build-lynxer-linux-{amd64,arm64}.yml instead of compiling Lynxer again.
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import time
from pathlib import Path
from typing import TypedDict

POLL_SECONDS = 30
TIMEOUT_SECONDS = 60 * 60
PAYLOAD_FILES = (
    "lynxer",
    "stdlib",
    "lynxer.config",
    "liblynxer.so",
    "lynxer.h",
    "ffi_abi.h",
)


class RunInfo(TypedDict):
    databaseId: int
    headSha: str
    status: str
    conclusion: str | None


def list_runs(workflow: str, commit: str) -> list[RunInfo]:
    result = subprocess.run(
        [
            "gh",
            "run",
            "list",
            "--workflow",
            workflow,
            "--commit",
            commit,
            "--limit",
            "100",
            "--json",
            "databaseId,headSha,status,conclusion",
        ],
        check=True,
        capture_output=True,
        text=True,
    )
    return json.loads(result.stdout)


def find_run(workflow: str, commit: str) -> RunInfo | None:
    runs = list_runs(workflow, commit)
    matching = [run for run in runs if run["headSha"] == commit]
    return max(matching, key=lambda run: run["databaseId"]) if matching else None


def resolve_run_id(workflow: str, commit: str, run_id: str) -> int:
    if run_id:
        return int(run_id)

    deadline = time.monotonic() + TIMEOUT_SECONDS
    while True:
        run = find_run(workflow, commit)
        if run is None:
            print(f"Waiting for a {workflow} run at {commit}...", flush=True)
        elif run["status"] != "completed":
            print(
                f"Waiting for {workflow} run {run['databaseId']}...",
                flush=True,
            )
        elif run["conclusion"] != "success":
            raise RuntimeError(
                f"{workflow} run {run['databaseId']} did not succeed "
                f"({run['conclusion'] or 'no conclusion'})"
            )
        else:
            return run["databaseId"]

        if time.monotonic() >= deadline:
            raise TimeoutError(
                f"Timed out waiting for {workflow} at {commit}"
            )
        time.sleep(POLL_SECONDS)


def payload_root(out: Path) -> Path:
    """upload-artifact may flatten the lynxer/ prefix or keep it."""
    for candidate in (out, out / "lynxer"):
        if (candidate / "lynxer.config").is_file():
            return candidate
    raise RuntimeError(f"No Lynxer payload (lynxer.config) found under {out}")


def read_version(config: Path) -> str:
    for line in config.read_text().splitlines():
        if line.lstrip().startswith("version") and "=" in line:
            return line.split("=", 1)[1].strip()
    return ""


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--workflow", required=True)
    parser.add_argument("--commit", required=True)
    parser.add_argument("--artifact", required=True)
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--run-id", default="")
    args = parser.parse_args()

    run_id = resolve_run_id(args.workflow, args.commit, args.run_id)
    args.out.mkdir(parents=True, exist_ok=True)
    print(f"Downloading {args.artifact} from run {run_id}", flush=True)
    subprocess.run(
        [
            "gh",
            "run",
            "download",
            str(run_id),
            "--name",
            args.artifact,
            "--dir",
            str(args.out),
        ],
        check=True,
    )

    root = payload_root(args.out)
    missing = [name for name in PAYLOAD_FILES if not (root / name).exists()]
    if missing:
        raise RuntimeError(f"Artifact is missing: {', '.join(missing)}")

    version = read_version(root / "lynxer.config")
    if not version:
        raise RuntimeError("Payload lynxer.config has no version")
    print(f"Payload root: {root}")
    print(f"Lynxer version: Lynxer {version}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (
        subprocess.CalledProcessError,
        RuntimeError,
        TimeoutError,
        ValueError,
    ) as error:
        print(f"download-build-artifact: {error}", file=sys.stderr)
        raise SystemExit(1)
