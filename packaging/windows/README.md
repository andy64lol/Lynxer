# Windows packaging

The release workflow uploads a zip containing `lynxer.exe`, `lynxer.dll`, the
import library, public headers, `lynxer.config` and the `stdlib/` directory.

## Installer

`lynxer.iss` is an Inno Setup 6 script for a per-user install. It writes the
same layout as `lynxer --install`:

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
iscc packaging/windows/lynxer.iss
```

The output is `dist/lynxer-<version>-windows-amd64-setup.exe`.

## Signing

Sign both the installer and the shipped executables/DLLs before publishing:

```powershell
signtool sign /fd SHA256 /tr http://timestamp.digicert.com /td SHA256 `
  dist\lynxer-0.1.9b1-windows-amd64-setup.exe
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
python3 packaging/windows/render-package-manifests.py   --version 0.1.9b1   --amd64 dist/lynxer-windows-amd64.zip   --arm64 dist/lynxer-windows-arm64.zip
```

The zips must be signed and uploaded to a release before the manifests can be
submitted. Publishing to winget, Scoop and Chocolatey is tracked in `todo.md`.

The AMD64 GitHub Actions workflow installs Inno Setup with Chocolatey, runs
`stage-payload.sh`, compiles `lynxer.iss`, and uploads the installer as the
`lynxer-windows-amd64-installer` artifact. The installer is still unsigned.
