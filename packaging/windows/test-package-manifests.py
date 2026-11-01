#!/usr/bin/env python3
"""Verify Windows package manifests are rendered from release archive hashes."""

from __future__ import annotations

import hashlib
import json
import subprocess
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
RENDERER = ROOT / "packaging/windows/render-package-manifests.py"
VERSION = "9.8.7"
BASE_URL = "https://downloads.example.test/lynxer"


class PackageManifestTests(unittest.TestCase):
    def test_manifests_match_release_archives(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary)
            archives: dict[str, Path] = {}
            hashes: dict[str, str] = {}
            for architecture in ("amd64", "arm64"):
                archive = work / f"lynxer-windows-{architecture}.zip"
                with zipfile.ZipFile(archive, "w") as package:
                    package.writestr("lynxer.exe", f"{architecture} binary")
                archives[architecture] = archive
                hashes[architecture] = hashlib.sha256(archive.read_bytes()).hexdigest()

            output = work / "manifests"
            subprocess.run(
                [
                    sys.executable,
                    str(RENDERER),
                    "--version",
                    VERSION,
                    "--amd64",
                    str(archives["amd64"]),
                    "--arm64",
                    str(archives["arm64"]),
                    "--output",
                    str(output),
                    "--base-url",
                    BASE_URL,
                ],
                check=True,
                capture_output=True,
                text=True,
            )

            urls = {
                architecture: (
                    f"{BASE_URL}/v{VERSION}/lynxer-windows-{architecture}.zip"
                )
                for architecture in archives
            }
            winget_dir = (
                output / "winget/manifests/a/andy64lol/Lynxer" / VERSION
            )
            version_manifest = (winget_dir / "andy64lol.Lynxer.yaml").read_text()
            self.assertIn(f"PackageVersion: {VERSION}", version_manifest)
            installer_manifest = (
                winget_dir / "andy64lol.Lynxer.installer.yaml"
            ).read_text()
            for architecture, winget_arch in (("amd64", "x64"), ("arm64", "arm64")):
                self.assertIn(f"  - Architecture: {winget_arch}", installer_manifest)
                self.assertIn(f"InstallerUrl: {urls[architecture]}", installer_manifest)
                self.assertIn(
                    f"InstallerSha256: {hashes[architecture]}",
                    installer_manifest,
                )

            scoop = json.loads((output / "scoop/lynxer.json").read_text())
            self.assertEqual(scoop["version"], VERSION)
            self.assertEqual(scoop["architecture"]["64bit"]["url"], urls["amd64"])
            self.assertEqual(scoop["architecture"]["arm64"]["url"], urls["arm64"])
            self.assertEqual(
                scoop["architecture"]["64bit"]["hash"],
                f"sha256:{hashes['amd64']}",
            )
            self.assertEqual(
                scoop["architecture"]["arm64"]["hash"],
                f"sha256:{hashes['arm64']}",
            )

            namespace = {
                "n": "http://schemas.microsoft.com/packaging/2015/06/nuspec.xsd"
            }
            nuspec = ET.parse(output / "chocolatey/lynxer.nuspec").getroot()
            metadata = nuspec.find("n:metadata", namespace)
            self.assertIsNotNone(metadata)
            if metadata is None:
                self.fail("Chocolatey nuspec is missing package metadata")
            self.assertEqual(metadata.findtext("n:version", namespaces=namespace), VERSION)
            installer = (
                output / "chocolatey/tools/chocolateyinstall.ps1"
            ).read_text()
            self.assertIn(f"$url64 = '{urls['amd64']}'", installer)
            self.assertIn(f"$checksum64 = '{hashes['amd64']}'", installer)


if __name__ == "__main__":
    unittest.main()
