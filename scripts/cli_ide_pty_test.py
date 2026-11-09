#!/usr/bin/env python3
"""Drive `lynxer --cli-ide` through a pseudo-terminal.

The terminal IDE needs a TTY on stdin and stdout, so a plain pipe cannot
exercise it. This script allocates a pty, opens a scratch .lynx file, types a
character, saves, lints and quits, then checks the screen and the file.

Usage: cli_ide_pty_test.py <lynxer-binary> <scratch-file>
"""

import fcntl
import os
import select
import struct
import subprocess
import sys
import termios
import time


def drain(master, seconds, chunks):
    deadline = time.time() + seconds
    while time.time() < deadline:
        readable, _, _ = select.select([master], [], [], 0.1)
        if not readable:
            continue
        try:
            data = os.read(master, 65536)
        except OSError:
            return
        if not data:
            return
        chunks.append(data)


def main():
    if len(sys.argv) != 3:
        print("usage: cli_ide_pty_test.py <lynxer> <scratch-file>", file=sys.stderr)
        return 2

    interpreter, source = sys.argv[1], sys.argv[2]

    master = slave = None
    try:
        import pty
        master, slave = pty.openpty()
    except (ImportError, OSError) as exc:
        print(f"skipped: no pseudo-terminal available ({exc})")
        return 0

    # A realistic terminal size; the IDE needs at least 40x10.
    fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", 30, 100, 0, 0))

    process = subprocess.Popen(
        [interpreter, "--cli-ide", source],
        stdin=slave,
        stdout=slave,
        stderr=slave,
        close_fds=True,
    )
    os.close(slave)
    slave = None

    chunks = []
    try:
        drain(master, 1.5, chunks)
        screen = b"".join(chunks).decode("utf-8", "replace")

        if "LYNXER" not in screen or "IDE" not in screen:
            print("terminal IDE did not render its header", file=sys.stderr)
            print(repr(screen[:400]), file=sys.stderr)
            return 1

        if os.path.basename(source) not in screen:
            print("terminal IDE did not open the requested file", file=sys.stderr)
            return 1

        # F1 opens the help overlay; Esc closes it.
        os.write(master, b"\x1b[11~")
        drain(master, 0.4, chunks)
        if "KEY BINDINGS" not in b"".join(chunks).decode("utf-8", "replace"):
            print("F1 did not open the help overlay", file=sys.stderr)
            return 1
        os.write(master, b"\x1b")
        drain(master, 0.4, chunks)

        # Ctrl+O opens a prompt; Esc cancels it.
        os.write(master, b"\x0f")
        drain(master, 0.4, chunks)
        if "Open file" not in b"".join(chunks).decode("utf-8", "replace"):
            print("Ctrl+O did not open the file prompt", file=sys.stderr)
            return 1
        os.write(master, b"\x1b")
        drain(master, 0.4, chunks)

        # Type a character, save it, lint, then quit.
        os.write(master, b"x")
        drain(master, 0.4, chunks)
        os.write(master, b"\x13")  # Ctrl+S
        drain(master, 0.4, chunks)
        os.write(master, b"\x1b[17~")  # F6 lint
        drain(master, 1.5, chunks)
        os.write(master, b"\x11")  # Ctrl+Q
        drain(master, 0.6, chunks)

        try:
            code = process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
            print("terminal IDE did not exit after Ctrl+Q", file=sys.stderr)
            return 1

        if code != 0:
            print(f"terminal IDE exited with {code}", file=sys.stderr)
            return 1

        with open(source, "r", encoding="utf-8") as handle:
            saved = handle.read()

        if not saved.startswith("x"):
            print(
                f"Ctrl+S did not persist the edit: {saved[:80]!r}",
                file=sys.stderr,
            )
            return 1

        full = b"".join(chunks).decode("utf-8", "replace")
        if "Lint finished" not in full:
            print("F6 did not run the linter", file=sys.stderr)
            return 1

    finally:
        if slave is not None:
            os.close(slave)
        os.close(master)

    print("terminal IDE pty smoke test passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
