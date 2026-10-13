#!/usr/bin/env python3
"""Bundle one built Arch package into a downloadable ZIP."""

from pathlib import Path
import re
import sys
from zipfile import ZIP_STORED, ZipFile


def main() -> int:
    if len(sys.argv) != 2 or sys.argv[1] not in {"x86_64", "aarch64"}:
        print("Usage: zip-arch-package.py <x86_64|aarch64>", file=sys.stderr)
        return 2

    arch = sys.argv[1]
    repo_root = Path(__file__).resolve().parents[2]
    package_dir = repo_root / "dist" / "packages" / arch
    package_pattern = re.compile(
        rf"^lynxer-[0-9][^-]*-[0-9]+-{re.escape(arch)}\.pkg\.tar\.zst$"
    )
    packages = sorted(
        path for path in package_dir.glob("*.pkg.tar.zst") if package_pattern.fullmatch(path.name)
    )

    if len(packages) != 1:
        print(
            f"Expected one Lynxer {arch} package in {package_dir}, found {len(packages)}.",
            file=sys.stderr,
        )
        return 1

    package = packages[0]
    zip_path = repo_root / "dist" / f"{package.name.removesuffix('.pkg.tar.zst')}.zip"
    with ZipFile(zip_path, "w", compression=ZIP_STORED) as archive:
        archive.write(package, arcname=package.name)

    print(f"Created {zip_path} containing {package.name}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
