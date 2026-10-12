#!/usr/bin/env bash
set -euo pipefail

build_deps=(
  base-devel clang curl git rust pkgconf cmake
  libffi alsa-lib bzip2 libglvnd
  libx11 libxext libxrandr libxi libxinerama libxcursor libxxf86vm
)

pacman -Syu --noconfirm
pacman -S --noconfirm --needed "${build_deps[@]}"

missing_deps=$(pacman -T "${build_deps[@]}" || true)
if [[ -n "$missing_deps" ]]; then
  printf 'Arch build dependencies still missing after installation:\n%s\n' "$missing_deps" >&2
  exit 1
fi

useradd --create-home builder
chown -R builder:builder /src/packaging/aur/lynxer
su builder -s /bin/bash -c 'cd /src/packaging/aur/lynxer && makepkg --noconfirm'
ls -la /src/packaging/aur/lynxer/*.pkg.tar.zst
