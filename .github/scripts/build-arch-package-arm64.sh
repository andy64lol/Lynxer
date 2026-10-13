#!/usr/bin/env bash
set -euo pipefail

rootfs="${RUNNER_TEMP:?RUNNER_TEMP is required}/lynxer-archlinuxarm-rootfs"
rootfs_archive="${RUNNER_TEMP}/ArchLinuxARM-aarch64-latest.tar.gz"
rootfs_url=https://ca.us.mirror.archlinuxarm.org/os/ArchLinuxARM-aarch64-latest.tar.gz

cleanup() {
  for mount_path in run sys proc dev src; do
    target="$rootfs/$mount_path"
    if mountpoint -q "$target"; then
      sudo umount -R "$target" || true
    fi
  done
  sudo rm -rf "$rootfs" "$rootfs_archive"
}
trap cleanup EXIT

sudo mkdir -p "$rootfs"
curl --fail --location --retry 3 --retry-delay 2 "$rootfs_url" --output "$rootfs_archive"
sudo bsdtar -xpf "$rootfs_archive" -C "$rootfs"

sudo rm -f "$rootfs/etc/resolv.conf"
sudo cp --dereference /etc/resolv.conf "$rootfs/etc/resolv.conf"
sudo mkdir -p "$rootfs/src" "$rootfs/dev" "$rootfs/proc" "$rootfs/sys" "$rootfs/run"

sudo mount --bind "$GITHUB_WORKSPACE" "$rootfs/src"
sudo mount --rbind /dev "$rootfs/dev"
sudo mount --make-rslave "$rootfs/dev"
sudo mount -t proc proc "$rootfs/proc"
sudo mount --rbind /sys "$rootfs/sys"
sudo mount --make-rslave "$rootfs/sys"
sudo mount --bind /run "$rootfs/run"

# Arch Linux ARM's generic root filesystem requires its own pacman keyring setup.
sudo chroot "$rootfs" /bin/bash -e -c 'pacman-key --init && pacman-key --populate archlinuxarm'
sudo chroot "$rootfs" /usr/bin/env LYNXER_EXPECTED_ARCH=aarch64 /bin/bash /src/.github/scripts/build-arch-package.sh
