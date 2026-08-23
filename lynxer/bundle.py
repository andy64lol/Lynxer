"""Build a standalone executable for a Lynxer source program."""

from __future__ import annotations

import shutil
import subprocess
import sys
import platform
from pathlib import Path

from .bytecode import compile_to_bytecode


def _linux_architecture() -> str:
    """Return the normalized architecture PyInstaller will build for."""
    machine = platform.machine().lower()
    aliases = {
        "x86_64": "x86_64",
        "amd64": "x86_64",
        "aarch64": "arm64",
        "arm64": "arm64",
    }
    architecture = aliases.get(machine)
    if sys.platform.startswith("linux") and architecture is None:
        raise RuntimeError(
            f"Linux bundling is supported on x86_64 and arm64 hosts; "
            f"detected '{machine or 'unknown'}'"
        )
    return architecture or machine or "unknown"


def _launcher_source(bytecode_name: str) -> str:
    return f"""#!/usr/bin/env python3
import os
import sys

from lynxer.bytecode import run_bytecode


def main():
    bytecode = os.path.join(getattr(sys, "_MEIPASS", os.path.dirname(__file__)), "bytecode", {bytecode_name!r})
    try:
        _, error = run_bytecode(bytecode)
    except Exception as exc:
        print(f"lynxer: could not run bundled program: {{exc}}", file=sys.stderr)
        return 1
    if error:
        print(error.as_string(), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
"""


def bundle_program(source_path: str, output_name: str | None = None) -> Path:
    """Compile *source_path* and package it as a one-file executable."""
    _linux_architecture()
    source = Path(source_path).expanduser().resolve()
    if not source.is_file():
        raise RuntimeError(f"file not found: '{source_path}'")
    if source.suffix != ".lynx":
        raise RuntimeError("bundling requires a .lynx source file")

    root = Path(__file__).resolve().parent.parent
    build_root = root / "build"
    bytecode_root = build_root / "bytecode"
    launcher_root = build_root / "launchers"
    dist_root = root / "dist"
    bytecode_root.mkdir(parents=True, exist_ok=True)
    launcher_root.mkdir(parents=True, exist_ok=True)
    dist_root.mkdir(parents=True, exist_ok=True)

    compiled_path, error = compile_to_bytecode(str(source), source.read_text(encoding="utf-8"))
    if error is not None or compiled_path is None:
        raise RuntimeError(error.as_string() if error else "could not compile source")

    compiled = Path(compiled_path)
    bytecode_name = f"{source.stem}.lynxc"
    bytecode_path = bytecode_root / bytecode_name
    shutil.move(str(compiled), bytecode_path)

    name = output_name or source.stem
    if Path(name).name != name or not name:
        raise RuntimeError("output name must be a simple executable name")
    launcher = launcher_root / f"{name}.py"
    launcher.write_text(_launcher_source(bytecode_name), encoding="utf-8")

    pyinstaller = shutil.which("pyinstaller") or sys.executable
    command = [
        pyinstaller,
        "--onefile",
        "--clean",
        "--noconfirm",
        "--name",
        name,
        "--distpath",
        str(dist_root),
        "--workpath",
        str(build_root / "pyinstaller"),
        "--specpath",
        str(build_root),
        "--paths",
        str(root),
        "--hidden-import",
        "lynxer.cpp",
        "--add-data",
        f"{bytecode_path}:bytecode",
        "--add-data",
        f"{root / 'lynxer' / 'stdlib'}:stdlib",
        "--add-data",
        f"{root / 'lynxer' / 'warnings.txt'}:lynxer",
        str(launcher),
    ]
    if pyinstaller == sys.executable:
        command[0:2] = [sys.executable, "-m"]
        command.insert(2, "PyInstaller")

    result = subprocess.run(command, cwd=root, check=False)
    if result.returncode != 0:
        raise RuntimeError("PyInstaller failed while creating the bundled executable")
    executable = dist_root / name
    if not executable.is_file():
        raise RuntimeError(f"PyInstaller completed without creating '{executable}'")
    return executable