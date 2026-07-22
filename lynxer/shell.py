#!/usr/bin/env python3
"""
Development shim — run from anywhere:
    python lynxer/shell.py <file.lynx>
    cd lynxer && python shell.py <file.lynx>
"""
import sys
import os

# Resolve repo root (parent of the lynxer/ package directory) and add it to
# sys.path so that `from lynxer.lynxer import run` always works.
_here   = os.path.dirname(os.path.abspath(__file__))   # .../lynxer/
_parent = os.path.dirname(_here)                         # repo root
if _parent not in sys.path:
    sys.path.insert(0, _parent)

from lynxer import run  # noqa: E402


def main():
    argv = sys.argv[1:]
    if not argv or argv[0] in ('-h', '--help'):
        print("Usage: python shell.py <file.lynx>")
        return 0
    if argv[0] in ('-v', '--version'):
        print("Lynxer 0.1.3")
        return 0

    filepath = argv[0]
    if not os.path.isabs(filepath):
        filepath = os.path.join(os.getcwd(), filepath)

    if not os.path.exists(filepath):
        print(f"shell.py: file not found: '{argv[0]}'", file=sys.stderr)
        return 1

    with open(filepath, 'r') as fh:
        source = fh.read()

    _, error = run(filepath, source)
    if error:
        print(error.as_string(), file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
