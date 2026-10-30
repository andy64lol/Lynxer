# Windows packaging

Each release ships **one zip per architecture** that contains both the portable
program (`lynxer.exe`, `lynxer.dll`, the import library, public headers,
`lynxer.config`, `stdlib/` and runtime DLLs) and the compiled installer. Both
the AMD64 and ARM64 workflows build the installer and bundle it this way.

## Installer

`lynxer.iss` is an Inno Setup 6.3+ script for a per-user install. It targets
AMD64 by default; pass `/DMyArch=arm64` to build the ARM64 installer. It writes
the same layout as `lynxer --install`:

```
%LOCALAPPDATA%\Programs\Lynxer\
  bin\lynxer.exe
  lib\lynxer\lynxer.exe
  lib\lynxer\lynxer.dll
  lib\lynxer\lynxer.dll.a
  lib\lynxer\lynxer.h
  lib\lynxer\ffi_abi.h
  lib\lynxer\lynxer.config
  lib\lynxer\stdlib\
  lib\lynxer\<runtime DLLs>\
```

It adds `bin` and `lib\lynxer` to the current user's `PATH` (the latter holds
the runtime DLLs and `lynxer.dll`), registers an uninstall entry, and removes
both `PATH` entries on uninstall.

```sh
make buildLynxer
packaging/windows/stage-payload.sh
# AMD64
MSYS2_ARG_CONV_EXCL="/D" iscc /DMyArch=amd64 packaging/windows/lynxer.iss
# ARM64
MSYS2_ARG_CONV_EXCL="/D" iscc /DMyArch=arm64 packaging/windows/lynxer.iss
```

The output is `dist/lynxer-<version>-windows-<arch>-setup.exe`.

## Release zip

`make-release-zip.py` puts the staged payload and the installer in a single zip
whose root keeps the portable layout (so the winget `portable` nested installer
still finds `lynxer.exe`):

```sh
python3 packaging/windows/make-release-zip.py \
  --arch amd64 \
  --installer dist/lynxer-0.1.9b1-windows-amd64-setup.exe \
  --output dist/lynxer-windows-amd64.zip
```

Use `--arch arm64` with the ARM64 installer for the ARM64 zip. The workflows do
this automatically and upload only `lynxer-windows-<arch>.zip`.

## Signing

Sign both the installer and the shipped executables/DLLs before publishing:

```powershell
signtool sign /fd SHA256 /tr http://timestamp.digicert.com /td SHA256 `
  dist\lynxer-0.1.9b1-windows-amd64-setup.exe
signtool sign /fd SHA256 /tr http://timestamp.digicert.com /td SHA256 `
  dist\lynxer-0.1.9b1-windows-arm64-setup.exe
signtool sign /fd SHA256 /tr http://timestamp.digicert.com /td SHA256 `
  build\windows-payload\lynxer.exe build\windows-payload\lynxer.dll
```

The certificate and timestamp credentials are release secrets; they are not
committed to the repository.

## C runtime

The Windows build targets Windows 10 or newer. AMD64 uses the MSYS2 MINGW64
toolchain (`msvcrt.dll` plus the MSYS2 GCC runtime DLLs); ARM64 uses CLANGARM64
(UCRT plus the CLANGARM64 runtime DLLs). The CI workflow runs `ldd` on
`lynxer.exe` and copies the non-system DLLs into `lynxer/runtime`; release zips
must place those DLLs next to `lynxer.exe`. `stage-payload.sh` copies them into
the installer payload automatically.

## Services and scheduled tasks

`lynxer-task.ps1` is the Task Scheduler story for `server` and other background
programs. The program stays a normal console process; the script only registers
it to start at logon (default) or at boot (requires elevation):

```powershell
.\lynxer-task.ps1 install -Script C:\apps\server.lynx -AtLogOn
.\lynxer-task.ps1 status
.\lynxer-task.ps1 run
.\lynxer-task.ps1 uninstall
```

A true Service Control Manager host is not shipped; use the scheduled task or
an external SCM wrapper when a Windows service is required.

## Package managers

`render-package-manifests.py` reads the two release zips, hashes them, and
renders winget, Scoop and Chocolatey manifests under
`packaging/windows/manifests/`:

```sh
python3 packaging/windows/render-package-manifests.py \
  --version 0.1.9b1 \
  --amd64 dist/lynxer-windows-amd64.zip \
  --arm64 dist/lynxer-windows-arm64.zip
```

Each zip also contains its installer; the winget manifest points at the
portable `lynxer.exe` inside the zip, so the extra installer does not change how
winget installs it.

The zips must be signed and uploaded to a release before the manifests can be
submitted. Publishing to winget, Scoop and Chocolatey is tracked in `todo.md`.

Both GitHub Actions workflows (AMD64 and ARM64) install Inno Setup with
Chocolatey, run `stage-payload.sh`, compile `lynxer.iss` for their architecture,
and upload the combined `lynxer-windows-<arch>.zip` artifact. The installers are
still unsigned.
