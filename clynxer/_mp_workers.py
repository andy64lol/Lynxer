"""
Top-level worker callables for lynxer/stdlib/multiprocessing.lynx.

Functions must live at module top-level so Python's multiprocessing can
pickle and send them to worker processes.  This module is also used by
the ThreadPoolExecutor path (where pickling is not required, but the same
helpers keep the code DRY).
"""
import subprocess


def run_cmd(cmd: str) -> str:
    """Run *cmd* in a shell; return stripped stdout, or stderr on failure."""
    try:
        proc = subprocess.run(
            cmd, shell=True,
            capture_output=True, text=True, timeout=60, check=True
        )
        out = proc.stdout.strip() if proc.returncode == 0 else proc.stderr.strip()
        # Replace internal newlines so the caller's \n delimiter stays unambiguous
        return out.replace("\n", " ")
    except Exception as exc:  # noqa: BLE001
        return str(exc).replace("\n", " ")


def run_cmd_template(args):
    """Run a template command; *args* is (template_str, item_str)."""
    template, item = args
    return run_cmd(template.replace("{}", str(item)))


def run_cmd_silent(cmd: str) -> int:
    """Run *cmd* in a shell silently; return the exit code."""
    try:
        proc = subprocess.run(
            cmd, shell=True,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            timeout=60,
            check=True
        )
        return proc.returncode
    except Exception:  # noqa: BLE001
        return 1
