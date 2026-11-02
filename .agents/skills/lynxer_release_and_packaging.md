---
name: lynxer-release-and-packaging
description: Version, bundle, package and release Lynxer across the dist tarball, AUR packages, Windows installer and CI workflows with explicit safety checks.
---

# Lynxer release and packaging

Use this skill only for version bumps, release bundles, package metadata, or CI
packaging changes. Release actions publish artifacts and can be irreversible:
never tag, push, upload, or publish without explicit user permission.

## Version source

The version lives in `lynxer/lynxer.config`:

```text
version = 0.1.9b1h
```

`make dist` reads it to name the bundle. Update the config first, then any
packaging metadata that repeats the version.

## Portable bundle

```bash
make buildLynxer
make dist
```

Output: `dist/lynxer-<version>-<os>-<arch>.tar.gz`.

The bundle contains the interpreter, `stdlib/`, `lynxer.config`, and on Linux
the embedding runtime and public headers. `dist/` is gitignored. Verify the
archive by extracting it into a temporary directory and running the bundled
interpreter with a small program that imports a stdlib module.

## AUR packages

There are two packages under `packaging/aur/`:

- `lynxer` — builds from source.
- `lynxer-bin` — installs the released binary bundle.

Read `packaging/aur/README.md` and the current `PKGBUILD` before editing; the
source package pins a commit and uses `sha256sums=('SKIP')`, while the README may
describe a tarball flow. Treat the `PKGBUILD` as the source of truth.

Typical local checks:

```bash
cd packaging/aur/lynxer
makepkg -f --nodeps
makepkg --printsrcinfo > .SRCINFO
```

On a version bump:

1. Update `pkgver`, reset `pkgrel=1`, and refresh the pinned commit or checksums
   as the package requires.
2. Regenerate `.SRCINFO` with `makepkg --printsrcinfo`.
3. Build the package locally if dependencies are available.
4. Do not push to the AUR without explicit permission.

The source package compiles every stdlib backend and is memory hungry. On this
7.6 GiB host, avoid parallel package builds and close other heavy processes.

## Windows packaging

Follow `packaging/windows/README.md`:

```bash
make buildLynxer
packaging/windows/stage-payload.sh
MSYS2_ARG_CONV_EXCL="/D" iscc /DMyArch=amd64 packaging/windows/lynxer.iss
python3 packaging/windows/make-release-zip.py \
  --arch amd64 \
  --installer dist/lynxer-<version>-windows-amd64-setup.exe \
  --output dist/lynxer-windows-amd64.zip
```

Use `--arch arm64` and `/DMyArch=arm64` for the ARM64 artifacts. The release zip
keeps the portable layout at its root so the winget portable installer still
finds `lynxer.exe`.

Signing uses release secrets that are not committed. Do not attempt to sign,
upload, or publish without the user providing the credentials and asking for it.

## CI workflows

The workflows under `.github/workflows/` build:

- Linux AMD64 and ARM64 (`make buildLynxer`, `make testLynxer
  LYNXER_SKIP_DISPLAY=1`, plus the ARM64 syscall fixture);
- Windows AMD64 and ARM64 (MSYS2/CLANGARM64, release zip upload);
- Arch package builds for AMD64 and ARM64.

When changing a workflow, keep the existing artifact names and upload paths
stable unless the change explicitly requires a new layout.

## Release checklist

- [ ] Version updated in `lynxer/lynxer.config`.
- [ ] Docs and `syntax.lynx` reflect the release.
- [ ] `make check` passes on the host architecture.
- [ ] `make dist` succeeds and the bundle runs after extraction.
- [ ] AUR `PKGBUILD`/`.SRCINFO` updated if the package version changed.
- [ ] Windows artifacts built and signed only with authorized credentials.
- [ ] Artifact checksums or signatures recorded where the release process needs
      them.
- [ ] No push, tag, upload, or package publication without explicit permission.

## Safety

- `make cleanLynxer`, `make cleanAll`, and `rm -rf dist/...` delete build output;
  ask before running them.
- Never rewrite Git history or force-push.
- Never commit `.env`, signing secrets, or local credentials.
- Report exactly which artifacts were produced and which checks were skipped.
