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
GitHub Actions builds it fine.

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

## `lynxer-bin` (not yet possible)

A binary package would be much lighter for users. It is **not offered yet**
because the GitHub release asset (`lynxer-linux-<arch>.zip`) contains only the
`lynxer` executable — the stdlib modules are missing, so an installed copy
cannot `import("math")`. A `lynxer-bin` package can be added once the release
ships the whole install tree (interpreter + `stdlib/` + `liblynxer.so` +
headers + `lynxer.config`).
