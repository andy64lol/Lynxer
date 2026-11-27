# AUR packaging

Arch User Repository packaging for Lynxer.

## `lynxer` (source)

[`lynxer/PKGBUILD`](lynxer/PKGBUILD) builds Lynxer from the `v<version>` release
tarball with `make buildLynxer` and installs it with the project's own
`lynxer --install` layout (`/usr/lib/lynxer` + a `/usr/bin/lynxer` symlink).

Build it locally:

```console
$ cd packaging/aur/lynxer
$ makepkg -f            # add -s to install missing build deps
# or, without installing:
$ makepkg -f --nodeps
```

The build compiles every stdlib backend (C++ and Rust), so it is **memory
hungry** — a small box with ~8 GB RAM can OOM during the `-O2` C++ compile.

From the repository root, `make cleanArch` removes makepkg's `src/`/`pkg/`
trees, generated package and tarball outputs, and the `dist/payload` /
`dist/packages` staging directories used by `make buildLynxerArch`;
`make cleanAll` includes it.

The CI Arch packages do not use this PKGBUILD: `build-arch-package-*.yml`
wrap the binaries produced by the Linux build workflows using the
`package()`-only template at `.github/arch-package/PKGBUILD.in`.

### Publishing to the AUR

The AUR is git-over-SSH; publishing needs an AUR account with a registered SSH
key (<https://aur.archlinux.org> → *My Account* → SSH Public Key). Then:

```console
$ git clone ssh://aur@aur.archlinux.org/lynxer.git
$ cp packaging/aur/lynxer/PKGBUILD packaging/aur/lynxer/.SRCINFO lynxer/
$ cd lynxer
$ makepkg --printsrcinfo > .SRCINFO   # regenerate after editing the PKGBUILD
$ git add PKGBUILD .SRCINFO
$ git commit -m "lynxer <version>"
$ git push
```

The first push creates the package; users then install with
`yay -S lynxer` / `paru -S lynxer`.

### Version bumps

Update `pkgver`, refresh the tarball `sha256sums`, reset `pkgrel=1`, regenerate
`.SRCINFO`, commit, push.

## `lynxer-bin` (binary)

[`lynxer-bin/PKGBUILD`](lynxer-bin/PKGBUILD) unpacks the released
`lynxer-linux-<arch>.zip` (interpreter + `stdlib/` + config + `liblynxer.so` +
headers) and installs the binary to `/usr/bin/lynxer` with the runtime under
`/usr/lib/lynxer` — the layout the interpreter resolves on its own. It
`provides`/`conflicts` `lynxer`, so users pick one.

This is the light option (no build): most users should prefer it over the source
package. Publishing it works exactly like the source package below.

## `wynxer` (source)

[`wynxer/PKGBUILD`](wynxer/PKGBUILD) builds the Wynxer web language from the
pinned commit with `make buildWynxer` and installs `/usr/bin/wynxer` plus its
docs and example project under `/usr/share/doc/wynxer`. Node.js is an optional
runtime dependency, needed only for `wynxer --add` / `--remove`.

## `bobcat` (source)

[`bobcat/PKGBUILD`](bobcat/PKGBUILD) builds both Bobcat IDEs with
`make buildBobcat` and installs `/usr/bin/bobcat-cli` and
`/usr/bin/bobcat-ide`. Because Bobcat is only a frontend, it **depends on
`lynxer` and `wynxer`** (plus the X11/Wayland libraries the iced GUI needs).
The GUI IDE's LTO link makes this a slow build; `check()` runs only the fast
CLI test suite.

Both packages pin the same commit as `lynxer` so all three are built from one
consistent tree. Check them locally with:

```console
$ cd packaging/aur/wynxer && makepkg -f --nodeps
$ cd ../bobcat && makepkg -f --nodeps
```

`make cleanArch` removes the generated `src/`, `pkg/` and `*.pkg.tar.*` files.
