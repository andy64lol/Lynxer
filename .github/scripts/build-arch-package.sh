#!/usr/bin/env bash
set -euo pipefail

source_dir=/src/packaging/aur/lynxer
build_dir=/tmp/lynxer-arch-package
arch="$(uname -m)"
expected_arch="${LYNXER_EXPECTED_ARCH:-$arch}"

if [[ "$arch" != "$expected_arch" ]]; then
  printf 'Expected %s, but the build environment is %s.\n' "$expected_arch" "$arch" >&2
  exit 1
fi

if [[ ! -f "$source_dir/PKGBUILD" ]]; then
  printf 'PKGBUILD not found at %s.\n' "$source_dir/PKGBUILD" >&2
  exit 1
fi

build_packages=(
  base-devel
  rust
  pkgconf
  cmake
  clang
  curl
  git
  libglvnd
  libx11
  libxext
  libxrandr
  libxi
  libxinerama
  libxcursor
  libxxf86vm
  libffi
  alsa-lib
  bzip2
)

pacman -Syu --noconfirm
pacman -S --needed --noconfirm "${build_packages[@]}"

missing_packages="$(pacman -T "${build_packages[@]}" || true)"
if [[ -n "$missing_packages" ]]; then
  printf 'Required Arch packages are still missing:\n%s\n' "$missing_packages" >&2
  exit 1
fi

if ! id builder >/dev/null 2>&1; then
  useradd --create-home --shell /bin/bash builder
fi

rm -rf "$build_dir"
install -d -o builder -g builder "$build_dir"
install -o builder -g builder "$source_dir/PKGBUILD" "$build_dir/PKGBUILD"

su builder -s /bin/bash -c "cd '$build_dir' && makepkg --cleanbuild --noconfirm"

shopt -s nullglob
packages=(
  "$build_dir"/lynxer-[0-9]*-[0-9]*-"${arch}".pkg.tar.zst
  "$build_dir"/lynxer-[0-9]*-[0-9]*-"${arch}".pkg.tar.xz
)
shopt -u nullglob

if (( ${#packages[@]} != 1 )); then
  printf 'Expected one Lynxer %s package, found %s.\n' "$arch" "${#packages[@]}" >&2
  printf 'Build output:\n' >&2
  find "$build_dir" -maxdepth 1 -type f -name '*.pkg.tar.*' -print >&2
  exit 1
fi

output_dir="/src/dist/packages/$arch"
install -d "$output_dir"
install -m 0644 "${packages[0]}" "$output_dir/"
printf 'Built package: %s\n' "$output_dir/$(basename "${packages[0]}")"
