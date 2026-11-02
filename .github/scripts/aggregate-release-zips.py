#!/usr/bin/env python3
"""Combine the platform release ZIP artifacts from one commit into one ZIP."""

from __future__ import annotations

import json
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from zipfile import ZIP_DEFLATED, ZipFile


WORKFLOWS = {
    "build-lynxer-linux-amd64.yml": "lynxer-linux-amd64",
    "build-lynxer-linux-arm64.yml": "lynxer-linux-arm64",
    "build-lynxer-windows-amd64.yml": "lynxer-windows-amd64",
    "build-lynxer-windows-arm64.yml": "lynxer-windows-arm64",
    "build-arch-package-amd64.yml": "lynxer-arch-x86_64",
    "build-arch-package-arm64.yml": "lynxer-arch-aarch64",
}
POLL_SECONDS = 30
TIMEOUT_SECONDS = 150 * 60


def gh_json(*args: str) -> object:
    result = subprocess.run(
        ["gh", *args],
        check=True,
        capture_output=True,
        text=True,
    )
    return json.loads(result.stdout)


def find_run(workflow: str, commit: str) -> dict | None:
    runs = gh_json(
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
    )
    matching = [run for run in runs if run["headSha"] == commit]
    return max(matching, key=lambda run: run["databaseId"]) if matching else None


def wait_for_runs(commit: str) -> dict[str, int]:
    deadline = time.monotonic() + TIMEOUT_SECONDS
    while True:
        run_ids: dict[str, int] = {}
        pending: list[str] = []
        for workflow, artifact in WORKFLOWS.items():
            run = find_run(workflow, commit)
            if run is None or run["status"] != "completed":
                pending.append(workflow)
                continue
            if run["conclusion"] != "success":
                raise RuntimeError(
                    f"{workflow} run {run['databaseId']} concluded "
                    f"{run['conclusion']!r}; refusing to create an incomplete ZIP"
                )
            run_ids[artifact] = run["databaseId"]

        if not pending:
            return run_ids
        if time.monotonic() >= deadline:
            raise TimeoutError(
                "Timed out waiting for platform ZIP workflows: "
                + ", ".join(pending)
            )
        print("Waiting for platform ZIP workflows: " + ", ".join(pending), flush=True)
        time.sleep(POLL_SECONDS)


def download_artifact(run_id: int, name: str, directory: Path) -> Path:
    subprocess.run(
        [
            "gh",
            "run",
            "download",
            str(run_id),
            "--name",
            name,
            "--dir",
            str(directory),
        ],
        check=True,
    )
    if name.startswith("lynxer-linux-"):
        files = sorted(path for path in directory.rglob("*") if path.is_file())
        if not files:
            raise RuntimeError(f"Artifact {name!r} contains no files")
        archive_path = directory.parent / f"{name}.zip"
        with ZipFile(archive_path, "w", compression=ZIP_DEFLATED) as archive:
            for path in files:
                archive.write(path, arcname=path.relative_to(directory))
        return archive_path

    archives = sorted(directory.rglob("*.zip"))
    if len(archives) != 1:
        raise RuntimeError(
            f"Expected exactly one ZIP in artifact {name!r}, found {len(archives)}"
        )
    return archives[0]


def main() -> int:
    if len(sys.argv) != 2 or not sys.argv[1]:
        print("Usage: aggregate-release-zips.py <commit-sha>", file=sys.stderr)
        return 2

    commit = sys.argv[1]
    run_ids = wait_for_runs(commit)
    output = Path("dist/lynxer-all-platforms.zip")
    output.parent.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory(prefix="lynxer-platform-zips-") as temp:
        temp_path = Path(temp)
        downloaded: list[Path] = []
        for artifact, run_id in run_ids.items():
            archive = download_artifact(run_id, artifact, temp_path / artifact)
            downloaded.append(archive)

        with ZipFile(output, "w", compression=ZIP_DEFLATED) as aggregate:
            for archive in downloaded:
                aggregate.write(archive, arcname=archive.name)

    print(f"Created {output} with {len(downloaded)} platform ZIPs")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (subprocess.CalledProcessError, RuntimeError, TimeoutError) as error:
        print(f"aggregate-release-zips: {error}", file=sys.stderr)
        raise SystemExit(1)
