#!/usr/bin/env python3
import sys
import os

_here = os.path.dirname(os.path.abspath(__file__))
_parent = os.path.dirname(_here)
if _parent not in sys.path:
    sys.path.insert(0, _parent)

from lynxer.lynxer import run  # noqa: E402

VERSION = "0.1.2"

HELP = """\
Lynxer {version} — a statically-flavoured, C-style scripting language.

Usage:
    lynxer <file.lynx>          Run a Lynxer program
    lynxer --help               Show this message
    lynxer --version            Show version

Examples:
    lynxer examples/example.lynx
    lynxer my_program.lynx
""".format(version=VERSION)


def main(argv=None):
    if argv is None:
        argv = sys.argv[1:]

    if not argv or argv[0] in ("-h", "--help"):
        print(HELP)
        return 0

    if argv[0] in ("-v", "--version"):
        print(f"Lynxer {VERSION}")
        return 0

    filepath = argv[0]

    if not os.path.exists(filepath):
        print(f"lynxer: file not found: '{filepath}'", file=sys.stderr)
        return 1

    if not filepath.endswith(".lynx"):
        print(
            f"lynxer: warning: '{filepath}' does not have a .lynx extension",
            file=sys.stderr,
        )

    try:
        with open(filepath, "r") as fh:
            source = fh.read()
    except OSError as exc:
        print(f"lynxer: cannot read '{filepath}': {exc}", file=sys.stderr)
        return 1

    _, error = run(filepath, source)
    if error:
        print(error.as_string(), file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())
