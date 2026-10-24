#!/usr/bin/env python3
"""Render winget, Scoop and Chocolatey manifests from the release zips."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

PACKAGE_ID = "andy64lol.Lynxer"
PUBLISHER = "andy64lol"
DESCRIPTION = "Lynxer programming language interpreter and standard library."
HOMEPAGE = "https://github.com/andy64lol/Lynxer"
LICENSE = "MIT"
DEFAULT_BASE_URL = "https://github.com/andy64lol/Lynxer/releases/download"


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def write(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def winget(output: Path, version: str, urls: dict[str, str],
           hashes: dict[str, str]) -> None:
    root = output / "winget" / "manifests" / "a" / PUBLISHER / "Lynxer" / version
    write(
        root / f"{PACKAGE_ID}.yaml",
        f"""PackageIdentifier: {PACKAGE_ID}
PackageVersion: {version}
DefaultLocale: en-US
ManifestType: version
ManifestVersion: 1.6.0
""",
    )
    installers = []
    for architecture, key in (("x64", "amd64"), ("arm64", "arm64")):
        installers.append(
            f"""  - Architecture: {architecture}
    InstallerUrl: {urls[key]}
    InstallerSha256: {hashes[key]}
    InstallerType: zip
    NestedInstallerType: portable
    NestedInstallerFiles:
      - RelativeFilePath: lynxer.exe
        PortableCommandAlias: lynxer
"""
        )
    write(
        root / f"{PACKAGE_ID}.installer.yaml",
        f"""PackageIdentifier: {PACKAGE_ID}
PackageVersion: {version}
InstallerLocale: en-US
Platform:
  - Windows.Desktop
MinimumOSVersion: 10.0.0.0
Installers:
{''.join(installers)}ManifestType: installer
ManifestVersion: 1.6.0
""",
    )
    write(
        root / f"{PACKAGE_ID}.locale.en-US.yaml",
        f"""PackageIdentifier: {PACKAGE_ID}
PackageVersion: {version}
PackageLocale: en-US
Publisher: {PUBLISHER}
PublisherUrl: {HOMEPAGE}
PackageName: Lynxer
PackageUrl: {HOMEPAGE}
License: {LICENSE}
LicenseUrl: {HOMEPAGE}/blob/main/LICENSE
ShortDescription: {DESCRIPTION}
Moniker: lynxer
Tags:
  - programming-language
  - interpreter
ManifestType: defaultLocale
ManifestVersion: 1.6.0
""",
    )


def scoop(output: Path, version: str, urls: dict[str, str],
          hashes: dict[str, str]) -> None:
    manifest = {
        "version": version,
        "description": DESCRIPTION,
        "homepage": HOMEPAGE,
        "license": LICENSE,
        "architecture": {
            "64bit": {
                "url": urls["amd64"],
                "hash": f"sha256:{hashes['amd64']}",
            },
            "arm64": {
                "url": urls["arm64"],
                "hash": f"sha256:{hashes['arm64']}",
            },
        },
        "bin": "lynxer.exe",
        "checkver": {"github": HOMEPAGE},
        "autoupdate": {
            "architecture": {
                "64bit": {
                    "url": f"{DEFAULT_BASE_URL}/v$version/lynxer-windows-amd64.zip"
                },
                "arm64": {
                    "url": f"{DEFAULT_BASE_URL}/v$version/lynxer-windows-arm64.zip"
                },
            }
        },
    }
    write(output / "scoop" / "lynxer.json", json.dumps(manifest, indent=2) + "\n")


def chocolatey(output: Path, version: str, urls: dict[str, str],
               hashes: dict[str, str]) -> None:
    root = output / "chocolatey"
    write(
        root / "lynxer.nuspec",
        f"""<?xml version="1.0" encoding="utf-8"?>
<package xmlns="http://schemas.microsoft.com/packaging/2015/06/nuspec.xsd">
  <metadata>
    <id>lynxer</id>
    <version>{version}</version>
    <title>Lynxer</title>
    <authors>{PUBLISHER}</authors>
    <projectUrl>{HOMEPAGE}</projectUrl>
    <license type="expression">{LICENSE}</license>
    <requireLicenseAcceptance>false</requireLicenseAcceptance>
    <description>{DESCRIPTION}</description>
    <tags>programming-language interpreter lynxer</tags>
  </metadata>
  <files>
    <file src="tools\\**" target="tools" />
  </files>
</package>
""",
    )
    write(
        root / "tools" / "chocolateyinstall.ps1",
        f"""$ErrorActionPreference = 'Stop'

$toolsDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$url64 = '{urls['amd64']}'
$checksum64 = '{hashes['amd64']}'

Install-ChocolateyZipPackage -PackageName 'lynxer' `
  -Url64bit $url64 -UnzipLocation $toolsDir `
  -Checksum64 $checksum64 -ChecksumType64 'sha256'

Install-BinFile -Name 'lynxer' -Path (Join-Path $toolsDir 'lynxer.exe')
""",
    )


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", required=True)
    parser.add_argument("--amd64", required=True, type=Path)
    parser.add_argument("--arm64", required=True, type=Path)
    parser.add_argument("--output", type=Path,
                        default=Path("packaging/windows/manifests"))
    parser.add_argument("--base-url", default=DEFAULT_BASE_URL)
    args = parser.parse_args()

    zips = {"amd64": args.amd64, "arm64": args.arm64}
    hashes = {name: sha256(path) for name, path in zips.items()}
    urls = {
        name: f"{args.base_url}/v{args.version}/lynxer-windows-{name}.zip"
        for name in zips
    }
    winget(args.output, args.version, urls, hashes)
    scoop(args.output, args.version, urls, hashes)
    chocolatey(args.output, args.version, urls, hashes)
    print(f"Rendered manifests for {args.version} under {args.output}")


if __name__ == "__main__":
    main()
