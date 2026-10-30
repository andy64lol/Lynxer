#!/usr/bin/env python3
"""Bundle a Windows release into one zip: portable program plus installer.

The zip root holds the same layout as the old binary zip (`lynxer.exe`,
`lynxer.dll`, headers, `lynxer.config`, `stdlib/`, runtime DLLs) so the winget
`portable` nested installer keeps working, and the Inno Setup installer is added
beside it. One download per architecture is therefore both the portable program
and the installer.
"""

from __future__ import annotations

import argparse
import sys
import zipfile
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--arch", required=True, choices=("amd64", "arm64"))
    parser.add_argument("--payload", default="build/windows-payload",
                        help="directory staged by stage-payload.sh")
    parser.add_argument("--installer", required=True,
                        help="compiled Inno Setup .exe")
    parser.add_argument("--output", required=True,
                        help="destination zip, e.g. dist/lynxer-windows-amd64.zip")
    args = parser.parse_args()

    payload = Path(args.payload)
    installer = Path(args.installer)
    output = Path(args.output)

    if not payload.is_dir():
        print(f"error: payload directory not found: {payload}", file=sys.stderr)
        return 1
    if not installer.is_file():
        print(f"error: installer not found: {installer}", file=sys.stderr)
        return 1

    files = sorted(path for path in payload.rglob("*") if path.is_file())
    if not files:
        print(f"error: payload directory is empty: {payload}", file=sys.stderr)
        return 1

    output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(output, "w", zipfile.ZIP_DEFLATED) as archive:
        for path in files:
            archive.write(path, path.relative_to(payload))
        archive.write(installer, installer.name)

    print(f"Created {output} ({len(files) + 1} file(s), {args.arch})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
