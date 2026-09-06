"""Build a standalone executable for a Lynxer source program."""

from __future__ import annotations

import shutil
import subprocess
import sys
from pathlib import Path

from .bytecode import compile_to_bytecode
from .syscalls import host_architecture, require_supported_platform


def _linux_architecture() -> str:
    """Return the normalized architecture PyInstaller will build for."""
    require_supported_platform()
    return host_architecture()


def _launcher_source(bytecode_name: str, expected_architecture: str) -> str:
    return f"""#!/usr/bin/env python3
import os
import sys

from lynxer.bytecode import run_bytecode
from lynxer.syscalls import SYSCALL_TABLE, require_supported_platform, unavailable

EXPECTED_ARCHITECTURE = {expected_architecture!r}


def main():
    try:
        architecture = require_supported_platform()
    except Exception as exc:
        print(f"lynxer: bundled executable cannot run its Linux runtime: {{exc}}", file=sys.stderr)
        return 1
    if architecture != EXPECTED_ARCHITECTURE:
        print(
            "lynxer: bundled executable was built for "
            f"{{EXPECTED_ARCHITECTURE}} but is running on {{architecture}}",
            file=sys.stderr,
        )
        return 1
    missing_syscalls = unavailable()
    if len(missing_syscalls) == len(SYSCALL_TABLE):
        print(
            "lynxer: bundled syscall tables are unavailable; "
            "rebuild with the system-calls package included",
            file=sys.stderr,
        )
        return 1

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


def _runtime_hook_source() -> str:
    return """import os
import sys

root = getattr(sys, "_MEIPASS", os.path.dirname(__file__))
stdlib = os.path.join(root, "stdlib")
if os.path.isdir(stdlib):
    os.environ["LYNXER_STDLIB"] = stdlib
"""


def bundle_program(source_path: str, output_name: str | None = None) -> Path:
    """Compile *source_path* and package it as a one-file executable."""
    architecture = _linux_architecture()
    source = Path(source_path).expanduser().resolve()
    if not source.is_file():
        raise RuntimeError(f"file not found: '{source_path}'")
    if source.suffix != ".lynx":
        raise RuntimeError("bundling requires a .lynx source file")

    root = Path(__file__).resolve().parent.parent
    build_root = root / "build"
    bytecode_root = build_root / "bytecode"
    launcher_root = build_root / "launchers"
    hook_root = build_root / "hooks"
    dist_root = root / "dist"
    bytecode_root.mkdir(parents=True, exist_ok=True)
    launcher_root.mkdir(parents=True, exist_ok=True)
    hook_root.mkdir(parents=True, exist_ok=True)
    dist_root.mkdir(parents=True, exist_ok=True)

    compiled_path, error = compile_to_bytecode(str(source), source.read_text(encoding="utf-8"))
    if error is not None or compiled_path is None:
        raise RuntimeError(error.as_string() if error else "could not compile source")

    compiled = Path(compiled_path)
    bytecode_name = f"{source.stem}.lynxc"
    bytecode_path = bytecode_root / bytecode_name
    shutil.move(str(compiled), bytecode_path)
    # Native libraries are runtime inputs, not Python imports.  Stage them
    # beside the bytecode so relative imports keep working after PyInstaller
    # extracts a one-file executable to its temporary _MEIPASS directory.
    from .bytecode import load_bytecode
    payload = load_bytecode(str(bytecode_path))
    native_dependencies = payload.get("native_dependencies", [])
    staged_native = []
    for dependency in native_dependencies:
        dependency_path = (source.parent / dependency).resolve()
        if not dependency_path.is_file():
            raise RuntimeError(
                f"native dependency '{dependency}' declared by {source.name} was not found "
                f"(expected {dependency_path})"
            )
        target = bytecode_root / dependency_path.name
        shutil.copy2(dependency_path, target)
        staged_native.append(target)

    name = output_name or source.stem
    if Path(name).name != name or not name:
        raise RuntimeError("output name must be a simple executable name")
    launcher = launcher_root / f"{name}.py"
    launcher.write_text(
        _launcher_source(bytecode_name, architecture),
        encoding="utf-8",
    )
    runtime_hook = hook_root / f"{name}_runtime_hook.py"
    runtime_hook.write_text(_runtime_hook_source(), encoding="utf-8")

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
        "--hidden-import",
        "lynxer.syscalls",
        "--collect-submodules",
        "system_calls",
        "--collect-all",
        "system_calls",
        "--runtime-hook",
        str(runtime_hook),
        "--add-data",
        f"{bytecode_path}:bytecode",
        "--add-data",
        f"{root / 'lynxer' / 'stdlib'}:stdlib",
        "--add-data",
        f"{root / 'lynxer' / 'warnings.txt'}:lynxer",
        str(launcher),
    ]
    for native_path in staged_native:
        command.extend(["--add-data", f"{native_path}:bytecode"])
    if pyinstaller == sys.executable:
        command[0:2] = [sys.executable, "-m"]
        command.insert(2, "PyInstaller")

    result = subprocess.run(command, cwd=root, check=False, capture_output=True, text=True)
    if result.returncode != 0:
        details = "\n".join(
            line
            for line in [result.stdout.strip(), result.stderr.strip()]
            if line
        )
        if details:
            raise RuntimeError(
                "PyInstaller failed while creating the bundled executable:\n"
                + details[-4000:]
            )
        raise RuntimeError("PyInstaller failed while creating the bundled executable")
    executable = dist_root / name
    if not executable.is_file():
        raise RuntimeError(f"PyInstaller completed without creating '{executable}'")
    return executable
