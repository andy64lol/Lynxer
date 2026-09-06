"""Install and uninstall the frozen Lynxer executable on Linux."""

from __future__ import annotations

import os
import shutil
import signal
import stat
import subprocess
import sys
import tempfile
import time
from typing import Iterable

INSTALL_PATH = "/usr/bin/lynxer"
_TERM_WAIT_SECONDS = 3.0
_POLL_SECONDS = 0.05


def _is_root() -> bool:
    return hasattr(os, "geteuid") and os.geteuid() == 0


def _is_elf(path: str) -> bool:
    try:
        with open(path, "rb") as executable:
            return executable.read(4) == b"\x7fELF"
    except OSError:
        return False


def executable_path() -> str:
    """Return the current frozen executable or raise for source execution."""
    if not getattr(sys, "frozen", False):
        raise RuntimeError(
            "installation requires the compiled Lynxer ELF executable; "
            "build it first with 'make build' or 'make buildLite'"
        )

    path = os.path.realpath(sys.executable)
    if not _is_elf(path):
        raise RuntimeError(f"the current executable is not an ELF binary: '{path}'")
    return path


def _proc_executable(pid: int) -> str | None:
    try:
        path = os.readlink(f"/proc/{pid}/exe")
    except OSError:
        return None
    # Linux may append this marker after an executable has been unlinked.
    return os.path.realpath(path.removesuffix(" (deleted)"))


def _matching_pids(path: str) -> list[int]:
    """Find processes executing exactly *path*, excluding this process."""
    target = os.path.realpath(path)
    current_pid = os.getpid()
    pids: list[int] = []

    try:
        entries = os.listdir("/proc")
    except OSError:
        return pids

    for entry in entries:
        if not entry.isdigit():
            continue
        pid = int(entry)
        if pid == current_pid:
            continue
        executable = _proc_executable(pid)
        if executable == target:
            pids.append(pid)
    return pids


def _signal_pids(pids: Iterable[int], sig: int) -> None:
    for pid in pids:
        try:
            os.kill(pid, sig)
        except ProcessLookupError:
            pass
        except PermissionError as exc:
            raise RuntimeError(f"could not signal Lynxer process {pid}: {exc}") from exc


def stop_installed_processes(path: str = INSTALL_PATH) -> list[int]:
    """Stop other processes running the exact executable at *path*."""
    pids = _matching_pids(path)
    if not pids:
        return []

    _signal_pids(pids, signal.SIGTERM)
    deadline = time.monotonic() + _TERM_WAIT_SECONDS
    remaining = pids
    while remaining and time.monotonic() < deadline:
        time.sleep(_POLL_SECONDS)
        remaining = [pid for pid in remaining if os.path.exists(f"/proc/{pid}")]

    if remaining:
        _signal_pids(remaining, signal.SIGKILL)
        kill_deadline = time.monotonic() + _TERM_WAIT_SECONDS
        while remaining and time.monotonic() < kill_deadline:
            time.sleep(_POLL_SECONDS)
            remaining = [pid for pid in remaining if os.path.exists(f"/proc/{pid}")]

    if remaining:
        joined = ", ".join(str(pid) for pid in remaining)
        raise RuntimeError(f"could not stop Lynxer process(es): {joined}")
    return pids


def install_executable(
    source: str | None = None,
    target: str = INSTALL_PATH,
) -> str:
    """Install *source* as an executable at *target*."""
    source_path = os.path.realpath(source or executable_path())
    target_path = os.path.abspath(target)

    if not _is_elf(source_path):
        raise RuntimeError(f"source is not an ELF executable: '{source_path}'")

    target_dir = os.path.dirname(target_path)
    if not os.path.isdir(target_dir):
        raise RuntimeError(f"installation directory does not exist: '{target_dir}'")

    stopped = stop_installed_processes(target_path)
    temporary_path: str | None = None
    try:
        fd, temporary_path = tempfile.mkstemp(
            prefix=".lynxer-install-", dir=target_dir
        )
        with os.fdopen(fd, "wb") as destination, open(source_path, "rb") as source_file:
            shutil.copyfileobj(source_file, destination)
            destination.flush()
            os.fsync(destination.fileno())
        os.chmod(temporary_path, stat.S_IRUSR | stat.S_IWUSR | stat.S_IXUSR |
                 stat.S_IRGRP | stat.S_IXGRP | stat.S_IROTH | stat.S_IXOTH)
        os.replace(temporary_path, target_path)
        temporary_path = None
    finally:
        if temporary_path is not None:
            try:
                os.unlink(temporary_path)
            except FileNotFoundError:
                pass

    if stopped:
        print(f"Stopped {len(stopped)} existing Lynxer process(es).")
    return target_path


def uninstall_executable(target: str = INSTALL_PATH) -> bool:
    """Stop processes using *target* and remove the installed executable."""
    target_path = os.path.abspath(target)
    try:
        mode = os.stat(target_path).st_mode
    except FileNotFoundError:
        return False
    except OSError as exc:
        raise RuntimeError(f"could not inspect '{target_path}': {exc}") from exc

    if not stat.S_ISREG(mode):
        raise RuntimeError(f"refusing to remove non-regular file '{target_path}'")

    stopped = stop_installed_processes(target_path)
    try:
        os.unlink(target_path)
    except OSError as exc:
        raise RuntimeError(f"could not remove '{target_path}': {exc}") from exc

    if stopped:
        print(f"Stopped {len(stopped)} existing Lynxer process(es).")
    return True


def _sudo_reexec(action: str) -> int:
    sudo = shutil.which("sudo")
    if sudo is None:
        print("Error: sudo is required for Lynxer installation.", file=sys.stderr)
        return 1

    result = subprocess.run([sudo, sys.executable, action], check=False)
    return result.returncode


def installer_main(action: str) -> int:
    """Run an install action, elevating through sudo when necessary."""
    if action not in ("--install", "--uninstall"):
        print(f"Error: unsupported installer action '{action}'.", file=sys.stderr)
        return 1

    if not _is_root():
        if action == "--install":
            try:
                executable_path()
            except RuntimeError as exc:
                print(f"Error: {exc}", file=sys.stderr)
                return 1
        return _sudo_reexec(action)

    try:
        if action == "--install":
            path = install_executable()
            print(f"Installed Lynxer as {path}.")
        else:
            if uninstall_executable():
                print(f"Uninstalled Lynxer from {INSTALL_PATH}.")
            else:
                print(f"Lynxer is not installed at {INSTALL_PATH}.")
    except (OSError, RuntimeError) as exc:
        print(f"Error: {exc}", file=sys.stderr)
        return 1
    return 0


def main() -> int:
    if len(sys.argv) != 2:
        print("Usage: install.py --install|--uninstall", file=sys.stderr)
        return 1
    return installer_main(sys.argv[1])


if __name__ == "__main__":
    raise SystemExit(main())
