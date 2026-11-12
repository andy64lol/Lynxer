#!/usr/bin/env python3
"""Drive `lynxer --cli-ide` through a pseudo-terminal.

The terminal IDE needs a TTY on stdin and stdout, so a plain pipe cannot
exercise it. This script allocates a pty, opens a scratch .lynx file, types a
character, saves, lints and quits, then checks the screen and the file. It
also covers tree navigation, the open-files list, 1-9 buffer switching, focus
combos, save-all prompts when closing files or folders, non-text file
rejection and the editing keys at the line boundaries (Backspace, Delete and
the arrow keys) in separate pty sessions.

Usage: cli_ide_pty_test.py <lynxer-binary> <scratch-file>
"""

import fcntl
import os
import re
import select
import struct
import subprocess
import sys
import termios
import time
import unicodedata


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


def strip_ansi(text):
    """Drop the colour and cursor escapes so the text can be matched."""
    return re.sub(r"\x1b\[[0-9;?]*[A-Za-z]", "", text)


def display_width(text):
    """The terminal columns a string takes, counting wide characters twice."""
    width = 0
    for char in text:
        if unicodedata.east_asian_width(char) in ("W", "F"):
            width += 2
        elif unicodedata.combining(char):
            continue
        else:
            width += 1
    return width


def check_tree_navigation(interpreter):
    """Arrows browse files/folders and Enter opens a file or expands a folder."""
    import shutil
    import tempfile

    root = tempfile.mkdtemp(prefix="lynxer-ide-tree-")
    master = slave = None
    process = None
    try:
        os.mkdir(os.path.join(root, "sub"))
        with open(os.path.join(root, "sub", "inner.lynx"), "w") as handle:
            handle.write("fn main(){ println(\"inner\"); }\n")
        with open(os.path.join(root, "top.lynx"), "w") as handle:
            handle.write("fn main(){ println(\"top\"); }\n")

        import pty
        master, slave = pty.openpty()
        fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", 24, 100, 0, 0))
        process = subprocess.Popen(
            [interpreter, "--cli-ide", root],
            stdin=slave, stdout=slave, stderr=slave, close_fds=True,
        )
        os.close(slave)
        slave = None

        chunks = []
        def step(data, expect=None, label=""):
            chunks.clear()
            os.write(master, data)
            drain(master, 0.5, chunks)
            text = b"".join(chunks).decode("utf-8", "replace")
            if expect is not None and expect not in text:
                print(f"{label}: missing {expect!r}", file=sys.stderr)
                return False
            return True

        drain(master, 1.2, chunks)
        screen = b"".join(chunks).decode("utf-8", "replace")
        if "sub" not in screen or "top.lynx" not in screen:
            print("tree did not list the folder and file", file=sys.stderr)
            return 1
        if "inner.lynx" in screen:
            print("collapsed folder already showed its children", file=sys.stderr)
            return 1

        # Enter on a folder expands it (and steps inside).
        if not step(b"\r", "inner.lynx", "Enter opens folder"):
            return 1
        # Left jumps back to the parent, then collapses it.
        if not step(b"\x1b[D", "Folder: sub", "Left to parent"):
            return 1
        chunks.clear()
        os.write(master, b"\x1b[D")
        drain(master, 0.5, chunks)
        collapsed = b"".join(chunks).decode("utf-8", "replace")
        if "inner.lynx" in collapsed:
            print("Left did not collapse the folder", file=sys.stderr)
            return 1
        # Right expands and steps into the folder; Down/Up walk the rows.
        if not step(b"\x1b[C", "inner.lynx", "Right arrow"):
            return 1
        if not step(b"\x1b[B", "top.lynx", "Down arrow"):
            return 1
        if not step(b"\x1b[A", None, "Up arrow"):
            return 1
        # Enter on the selected file opens it in the editor.
        chunks.clear()
        os.write(master, b"\r")
        drain(master, 0.5, chunks)
        opened = b"".join(chunks).decode("utf-8", "replace")
        if "inner.lynx" not in opened or "Opened" not in opened:
            print("Enter did not open the selected file", file=sys.stderr)
            return 1

        # Collapsing an expanded folder while a buffer is dirty asks to save
        # all changes first; N discards and then the folder closes.
        if not step(b"x", None, "type to make dirty"):
            return 1
        if not step(b"\x10\x1b[D", "Folder: sub", "^P/Left selects parent"):
            return 1
        if not step(b"\r", "Save all changes?", "folder collapse prompts"):
            return 1
        if not step(b"n\r", "Closed folder: sub", "N collapses folder"):
            return 1
        collapsed_screen = b"".join(chunks).decode("utf-8", "replace")
        # The chunks contain the frame drawn while the prompt was still open,
        # followed by the frame after the collapse; only inspect the latter.
        last_frame = collapsed_screen[collapsed_screen.rfind("\x1b[1;1H"):]
        if "[+] sub" not in last_frame or "[-] sub" in last_frame:
            print("folder did not collapse after discarding changes",
                  file=sys.stderr)
            return 1
        if not step(b"\x13", "Saved", "save dirty buffer"):
            return 1

        os.write(master, b"\x18")
        try:
            code = process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
            print("tree IDE did not exit after Ctrl+X", file=sys.stderr)
            return 1
        process = None
        if code != 0:
            print(f"tree IDE exited with {code}", file=sys.stderr)
            return 1
        return 0
    finally:
        if process is not None:
            process.kill()
            process.wait()
        if slave is not None:
            os.close(slave)
        if master is not None:
            os.close(master)
        shutil.rmtree(root, ignore_errors=True)


def check_buffers_and_files(interpreter):
    """FILES list, 1-9 switching, focus combos, toggle-close, save-all, cap."""
    import shutil
    import tempfile

    root = tempfile.mkdtemp(prefix="lynxer-ide-buffers-")
    master = slave = None
    process = None
    try:
        names = ["alpha.lynx", "beta.lynx", "gamma.lynx", "delta.lynx",
                 "epsilon.lynx", "zeta.lynx", "eta.lynx", "theta.lynx",
                 "iota.lynx", "kappa.lynx"]
        for name in names:
            with open(os.path.join(root, name), "w") as handle:
                handle.write('fn main(){ println("ok"); }\n')
        with open(os.path.join(root, "image.png"), "wb") as handle:
            handle.write(b"\x89PNG\r\n\x1a\n" + b"\x00" * 64)
        with open(os.path.join(root, "blob"), "wb") as handle:
            handle.write(b"\x00\x01\x02binary\x00")

        import pty
        master, slave = pty.openpty()
        fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", 26, 110, 0, 0))
        process = subprocess.Popen(
            [interpreter, "--cli-ide", root],
            stdin=slave, stdout=slave, stderr=slave, close_fds=True,
        )
        os.close(slave)
        slave = None

        chunks = []
        def step(data, expect=None, label="", absent=None):
            chunks.clear()
            os.write(master, data)
            drain(master, 0.45, chunks)
            text = b"".join(chunks).decode("utf-8", "replace")
            if expect is not None and expect not in text:
                print(f"{label}: missing {expect!r}", file=sys.stderr)
                return False
            if absent is not None and absent in text:
                print(f"{label}: unexpectedly found {absent!r}", file=sys.stderr)
                return False
            return True

        def open_prompt(path, label):
            chunks.clear()
            os.write(master, b"\x0f")
            drain(master, 0.25, chunks)
            os.write(master, path.encode())
            drain(master, 0.25, chunks)
            os.write(master, b"\r")
            drain(master, 0.45, chunks)
            text = b"".join(chunks).decode("utf-8", "replace")
            if "Opened" not in text:
                print(f"{label}: did not open {path}", file=sys.stderr)
                return False
            return True

        drain(master, 1.2, chunks)
        screen = b"".join(chunks).decode("utf-8", "replace")
        if "FILES" not in screen or "alpha.lynx" not in screen:
            print("open-files list or tree missing", file=sys.stderr)
            return 1
        if "(none" not in screen:
            print("FILES list should start empty", file=sys.stderr)
            return 1

        # Non-text files are marked B and refused. Tree order: files sorted,
        # so alpha, beta, ..., then blob and image.png; 10 text files first.
        # Use ^O with explicit paths to be independent of tree order.
        if not step(b"\x0f" + os.path.join(root, "image.png").encode() + b"\r",
                    "Cannot open non-text file", "reject .png", absent="Opened"):
            return 1
        if not step(b"\x0f" + os.path.join(root, "blob").encode() + b"\r",
                    "Cannot open non-text file", "reject extensionless binary",
                    absent="Opened"):
            return 1
        if not step(b"\x05", "Editor focused", "^E focuses editor"):
            return 1

        # Two files in the FILES list, numbered 1 and 2.
        if not open_prompt(os.path.join(root, "alpha.lynx"), "open alpha"):
            return 1
        if not open_prompt(os.path.join(root, "beta.lynx"), "open beta"):
            return 1
        if not step(b"\x1b1", "File 1: alpha.lynx", "Alt+1 switches buffer"):
            return 1
        if not step(b"\x10", "1-9 File", "^P focuses sidebar"):
            return 1
        if not step(b"2", "File 2: beta.lynx", "plain 2 switches in sidebar"):
            return 1
        if not step(b"\x05", "Editor focused", "^E returns to editor"):
            return 1
        if "1 alpha.lynx" not in b"".join(chunks).decode("utf-8", "replace"):
            print("FILES list does not show the numbered buffers", file=sys.stderr)
            return 1

        # With three buffers and the middle one active, closing the first from
        # the tree must keep the same file active instead of jumping to the
        # last one. Toggle-close is clean here, so it happens immediately.
        if not open_prompt(os.path.join(root, "gamma.lynx"), "open gamma"):
            return 1
        if not step(b"\x1b2", "File 2: beta.lynx", "Alt+2 selects beta"):
            return 1
        if not step(b"\x10\x1b[H\r", "Closed alpha.lynx", "toggle-close first"):
            return 1
        if not step(b"\x05", "Editor focused", "^E after close"):
            return 1
        after_close = b"".join(chunks).decode("utf-8", "replace")
        if "[1/2]" not in after_close or "beta.lynx" not in after_close:
            print("closing a background buffer changed the active file",
                  file=sys.stderr)
            return 1

        # Dirty buffer -> ^W raises the save-all prompt; N discards.
        if not step(b"\x05z", None, "type to make dirty"):
            return 1
        if not step(b"\x17", "Save all changes?", "^W save-all prompt"):
            return 1
        if not step(b"n\r", "Closed beta.lynx", "N closes without saving"):
            return 1

        # Cap at nine simultaneous files.
        for name in names[:9]:
            if not open_prompt(os.path.join(root, name), f"open {name}"):
                return 1
        if not step(b"\x0f" + os.path.join(root, "kappa.lynx").encode() + b"\r",
                    "Only 9 files can be open", "10th file refused",
                    absent="Opened kappa"):
            return 1

        os.write(master, b"\x18")
        try:
            code = process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
            print("buffer IDE did not exit after Ctrl+X", file=sys.stderr)
            return 1
        process = None
        if code != 0:
            print(f"buffer IDE exited with {code}", file=sys.stderr)
            return 1
        return 0
    finally:
        if process is not None:
            process.kill()
            process.wait()
        if slave is not None:
            os.close(slave)
        if master is not None:
            os.close(master)
        shutil.rmtree(root, ignore_errors=True)


def check_editing_keys(interpreter):
    """Backspace, Delete and the arrow keys edit at the line boundaries."""
    import shutil
    import tempfile

    root = tempfile.mkdtemp(prefix="lynxer-ide-edit-")
    master = slave = None
    process = None
    try:
        path = os.path.join(root, "edit.lynx")
        with open(path, "w") as handle:
            handle.write('fn main(){\n  alpha();\n}\n')

        import pty
        master, slave = pty.openpty()
        fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", 30, 100, 0, 0))
        process = subprocess.Popen(
            [interpreter, "--cli-ide", path],
            stdin=slave, stdout=slave, stderr=slave, close_fds=True,
        )
        os.close(slave)
        slave = None

        chunks = []

        def step(data, label):
            chunks.clear()
            os.write(master, data)
            drain(master, 0.45, chunks)
            frame = strip_ansi(b"".join(chunks).decode("utf-8", "replace"))
            if not frame:
                print(f"{label}: the IDE stopped drawing", file=sys.stderr)
                return None
            return frame

        def expect(frame, needle, label):
            if needle not in frame:
                print(f"{label}: missing {needle!r}", file=sys.stderr)
                return False
            return True

        drain(master, 1.2, chunks)
        screen = strip_ansi(b"".join(chunks).decode("utf-8", "replace"))
        if "fn main(){" not in screen:
            print("edit IDE did not open the file", file=sys.stderr)
            return 1

        # Backspace reaches the terminal as DEL (127) on most terminals and as
        # BS (8) on others; both must erase the character before the caret.
        frame = step(b"Z", "type")
        if frame is None or not expect(frame, "Zfn main(){", "typing"):
            return 1
        frame = step(b"\x7f", "backspace")
        if frame is None:
            return 1
        if "Zfn main(){" in frame:
            print("Backspace did not erase the character before the caret",
                  file=sys.stderr)
            return 1
        if not expect(frame, "fn main(){", "backspace"):
            return 1
        frame = step(b"\x08", "backspace")
        if frame is None:
            return 1
        if not expect(frame, "fn main(){", "backspace BS"):
            return 1

        # Backspace at the start of a line joins it with the line above.
        frame = step(b"\x1b[B\x1b[H", "down and home")
        if frame is None or not expect(frame, "Ln 2, Col 1", "Down/Home"):
            return 1
        frame = step(b"\x7f", "backspace at line start")
        if frame is None:
            return 1
        if not expect(frame, "fn main(){  alpha();", "backspace did not join"):
            return 1
        if not expect(frame, "Ln 1, Col 11", "join caret"):
            return 1

        # Left at the start of a line jumps to the end of the line above.
        frame = step(b"\x1b[B", "down")
        if frame is None or not expect(frame, "Ln 2, Col 2", "Down"):
            return 1
        frame = step(b"\x1b[H", "home")
        if frame is None or not expect(frame, "Ln 2, Col 1", "Home"):
            return 1
        frame = step(b"\x1b[D", "left at line start")
        if frame is None:
            return 1
        if not expect(frame, "Ln 1, Col 21", "Left did not cross the line"):
            return 1

        # Delete removes the character under the caret...
        frame = step(b"\x1b[H", "home")
        if frame is None or not expect(frame, "Ln 1, Col 1", "Home"):
            return 1
        frame = step(b"\x1b[3~", "delete")
        if frame is None:
            return 1
        if not expect(frame, "n main(){  alpha();", "Delete did not erase"):
            return 1
        # ...and joins the next line at the end of a line.
        frame = step(b"\x1b[F", "end")
        if frame is None or not expect(frame, "Ln 1, Col 20", "End"):
            return 1
        frame = step(b"\x1b[3~", "delete at line end")
        if frame is None:
            return 1
        if not expect(frame, "n main(){  alpha();}", "Delete did not join"):
            return 1

        # The buffer is dirty, so Ctrl+X asks to save first; N discards.
        os.write(master, b"\x18")
        drain(master, 0.4, chunks)
        os.write(master, b"n\r")
        try:
            code = process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
            print("edit IDE did not exit after Ctrl+X", file=sys.stderr)
            return 1
        process = None
        if code != 0:
            print(f"edit IDE exited with {code}", file=sys.stderr)
            return 1
        return 0
    finally:
        if process is not None:
            process.kill()
            process.wait()
        if slave is not None:
            os.close(slave)
        if master is not None:
            os.close(master)
        shutil.rmtree(root, ignore_errors=True)


def check_wrapped_lines(interpreter):
    """A long line wraps over several rows, each labelled with its line."""
    import math
    import shutil
    import tempfile

    root = tempfile.mkdtemp(prefix="lynxer-ide-wrap-")
    master = slave = None
    process = None
    try:
        path = os.path.join(root, "wrap.lynx")
        body = "w" * 200
        with open(path, "w") as handle:
            handle.write("first\n" + body + "\nlast\n")

        import pty
        master, slave = pty.openpty()
        fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", 30, 100, 0, 0))
        process = subprocess.Popen(
            [interpreter, "--cli-ide", path],
            stdin=slave, stdout=slave, stderr=slave, close_fds=True,
        )
        os.close(slave)
        slave = None

        chunks = []

        def step(data, label):
            chunks.clear()
            os.write(master, data)
            drain(master, 0.45, chunks)
            raw = b"".join(chunks).decode("utf-8", "replace")
            if not raw:
                print(f"{label}: the IDE stopped drawing", file=sys.stderr)
                return None, []
            rows = re.split(r"\x1b\[\d+;\d+H", raw)
            return raw, rows

        drain(master, 1.2, chunks)
        screen = b"".join(chunks).decode("utf-8", "replace")
        rows = re.split(r"\x1b\[\d+;\d+H", screen)
        plain = [strip_ansi(row) for row in rows]

        # Every row of the long line repeats its line number in the gutter.
        wrapped = [row for row in plain if "   2 | " in row]
        if len(wrapped) < 2:
            print(f"the long line did not wrap: {len(wrapped)} row(s)",
                  file=sys.stderr)
            return 1
        text = "".join(row.split("   2 | ", 1)[1] for row in wrapped).strip()
        if text != body:
            print("the wrapped rows do not carry the whole line", file=sys.stderr)
            return 1
        width = len(wrapped[0].split("   2 | ", 1)[1].rstrip())
        if len(wrapped) != math.ceil(len(body) / width):
            print(f"unexpected wrap: {len(wrapped)} rows of {width} columns",
                  file=sys.stderr)
            return 1

        # Down walks the wrapped rows of the line, so the line number in the
        # status bar only changes when the caret leaves the line.
        raw, rows = step(b"\x1b[B", "down")
        if raw is None:
            return 1
        plain = [strip_ansi(row) for row in rows]
        if not any("Ln 2, Col 1" in row for row in plain):
            print("Down did not reach the long line", file=sys.stderr)
            return 1
        # Every remaining row of the line keeps the same line number.
        for _ in range(len(wrapped) - 1):
            raw, rows = step(b"\x1b[B", "down")
            if raw is None:
                return 1
            plain = [strip_ansi(row) for row in rows]
            if not any("Ln 2, Col 1" in row for row in plain):
                print("Down left the wrapped line", file=sys.stderr)
                return 1
        # The caret is drawn on the continuation row of the same line.
        if not any("   2 | " in row and "\x1b[7m" in row for row in rows):
            print("the caret is not drawn on the wrapped row", file=sys.stderr)
            return 1
        raw, rows = step(b"\x1b[B", "down")
        if raw is None:
            return 1
        plain = [strip_ansi(row) for row in rows]
        if not any("Ln 3, Col 1" in row for row in plain):
            print("Down did not leave the wrapped line", file=sys.stderr)
            return 1

        os.write(master, b"\x18")
        drain(master, 0.4, chunks)
        try:
            code = process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
            print("wrap IDE did not exit after Ctrl+X", file=sys.stderr)
            return 1
        process = None
        if code != 0:
            print(f"wrap IDE exited with {code}", file=sys.stderr)
            return 1
        return 0
    finally:
        if process is not None:
            process.kill()
            process.wait()
        if slave is not None:
            os.close(slave)
        if master is not None:
            os.close(master)
        shutil.rmtree(root, ignore_errors=True)


def check_long_names(interpreter):
    """A long name scrolls in the tree and keeps the columns aligned."""
    import shutil
    import tempfile

    root = tempfile.mkdtemp(prefix="lynxer-ide-long-")
    master = slave = None
    process = None
    try:
        long_name = ("a-directory-with-a-name-far-too-long-for-the-tree-pane"
                     "-of-the-terminal-ide")
        os.mkdir(os.path.join(root, long_name))
        with open(os.path.join(root, long_name, "inner.lynx"), "w") as handle:
            handle.write("fn main(){}\n")
        # A wide name and one with a control character in it.
        with open(os.path.join(root, "日本語のファイル名.lynx"), "w") as handle:
            handle.write("fn main(){}\n")
        with open(os.path.join(root, "tab\tname.lynx"), "w") as handle:
            handle.write("fn main(){}\n")

        import pty
        master, slave = pty.openpty()
        fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", 30, 100, 0, 0))
        process = subprocess.Popen(
            [interpreter, "--cli-ide", root],
            stdin=slave, stdout=slave, stderr=slave, close_fds=True,
        )
        os.close(slave)
        slave = None

        chunks = []

        def step(data, label):
            chunks.clear()
            os.write(master, data)
            drain(master, 0.45, chunks)
            raw = b"".join(chunks).decode("utf-8", "replace")
            if not raw:
                print(f"{label}: the IDE stopped drawing", file=sys.stderr)
                return None, []
            return raw, re.split(r"\x1b\[\d+;\d+H", raw)

        def separator_columns(rows):
            columns = []
            for row in rows:
                plain = strip_ansi(row)
                pos = plain.find("|")
                if pos >= 0:
                    columns.append(display_width(plain[:pos]))
            return columns

        drain(master, 1.2, chunks)
        raw = b"".join(chunks).decode("utf-8", "replace")
        rows = re.split(r"\x1b\[\d+;\d+H", raw)
        if not rows:
            print("the IDE did not draw its first frame", file=sys.stderr)
            return 1
        columns = separator_columns(rows)
        if len(set(columns)) > 1:
            print(f"the tree column is not aligned: {sorted(set(columns))}",
                  file=sys.stderr)
            return 1

        # A control character in a name is shown as a caret, never written
        # to the terminal as an escape sequence.
        plain = [strip_ansi(row) for row in rows]
        if not any("^I" in row for row in plain):
            print("the tab in a file name was not escaped", file=sys.stderr)
            return 1
        if not any("日本語" in row for row in plain):
            print("the wide file name is missing", file=sys.stderr)
            return 1

        # The long name is cut off with an ellipsis and scrolls with the
        # arrow keys.
        plain = [strip_ansi(row) for row in rows]
        if not any("…" in row for row in plain):
            print("the long name was not marked as cut off", file=sys.stderr)
            return 1
        before = [row for row in plain if "…" in row]
        raw, rows = step(b"\x1b[C", "scroll right")
        if raw is None:
            return 1
        plain = [strip_ansi(row) for row in rows]
        after = [row for row in plain if "…" in row]
        if before == after:
            print("Right did not scroll the long name", file=sys.stderr)
            return 1
        columns = separator_columns(rows)
        if len(set(columns)) > 1:
            print(f"the tree column is not aligned after scrolling: "
                  f"{sorted(set(columns))}", file=sys.stderr)
            return 1

        # Scrolling back returns to the start of the name.
        raw, rows = step(b"\x1b[D", "scroll left")
        if raw is None:
            return 1
        plain = [strip_ansi(row) for row in rows]
        if [row for row in plain if "…" in row] != before:
            print("Left did not scroll the long name back", file=sys.stderr)
            return 1

        os.write(master, b"\x18")
        drain(master, 0.4, chunks)
        try:
            code = process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
            print("long-name IDE did not exit after Ctrl+X", file=sys.stderr)
            return 1
        process = None
        if code != 0:
            print(f"long-name IDE exited with {code}", file=sys.stderr)
            return 1
        return 0
    finally:
        if process is not None:
            process.kill()
            process.wait()
        if slave is not None:
            os.close(slave)
        if master is not None:
            os.close(master)
        shutil.rmtree(root, ignore_errors=True)


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

        # ^G opens the combo-only help overlay; Esc closes it.
        os.write(master, b"\x07")
        drain(master, 0.4, chunks)
        if "KEY BINDINGS" not in b"".join(chunks).decode("utf-8", "replace"):
            print("^G did not open the help overlay", file=sys.stderr)
            return 1
        os.write(master, b"\x1b")
        drain(master, 0.4, chunks)

        # Ctrl+Alt+C opens the command menu. Down arrows navigate it and Enter
        # runs the selected command (the second entry is "Open file").
        os.write(master, b"\x1b\x03")
        drain(master, 0.4, chunks)
        if "COMMANDS" not in b"".join(chunks).decode("utf-8", "replace"):
            print("Ctrl+Alt+C did not open the command menu", file=sys.stderr)
            return 1
        os.write(master, b"\x1b[B")  # Down
        drain(master, 0.4, chunks)
        os.write(master, b"\r")      # Enter
        drain(master, 0.4, chunks)
        if "Open file" not in b"".join(chunks).decode("utf-8", "replace"):
            print("arrow keys / Enter did not run a menu command", file=sys.stderr)
            return 1
        os.write(master, b"\x1b")    # cancel the prompt
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
        os.write(master, b"\x13")  # Ctrl+S save
        drain(master, 0.4, chunks)
        os.write(master, b"\x0c")  # Ctrl+L lint
        drain(master, 1.5, chunks)
        os.write(master, b"\x18")  # Ctrl+X exit
        drain(master, 0.6, chunks)

        try:
            code = process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
            print("terminal IDE did not exit after Ctrl+X", file=sys.stderr)
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
            print("^L did not run the linter", file=sys.stderr)
            return 1

    finally:
        if slave is not None:
            os.close(slave)
        os.close(master)

    tree_result = check_tree_navigation(interpreter)
    if tree_result != 0:
        return tree_result

    buffers_result = check_buffers_and_files(interpreter)
    if buffers_result != 0:
        return buffers_result

    editing_result = check_editing_keys(interpreter)
    if editing_result != 0:
        return editing_result

    wrapping_result = check_wrapped_lines(interpreter)
    if wrapping_result != 0:
        return wrapping_result

    names_result = check_long_names(interpreter)
    if names_result != 0:
        return names_result

    print("terminal IDE pty smoke test passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
