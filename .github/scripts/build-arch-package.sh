#!/usr/bin/env bash
set -euo pipefail

# Wrap the Lynxer binary built by build-lynxer-linux-{amd64,arm64}.yml.
# Nothing is compiled here: the payload is downloaded by the workflow into
# /src/dist/payload/<arch> and installed verbatim into the Arch package.

arch="$(uname -m)"
expected_arch="${LYNXER_EXPECTED_ARCH:-$arch}"

if [[ "$arch" != "$expected_arch" ]]; then
  printf 'Expected %s, but the build environment is %s.\n' "$expected_arch" "$arch" >&2
  exit 1
fi

build_dir=/tmp/lynxer-arch-package
payload_dir="/src/dist/payload/$arch"
pkbuild_template=/src/.github/arch-package/PKGBUILD.in

# actions/upload-artifact@v4 may flatten the common lynxer/ prefix or keep it.
payload_root="$payload_dir"
if [[ -f "$payload_dir/lynxer/lynxer.config" ]]; then
  payload_root="$payload_dir/lynxer"
fi
if [[ ! -f "$payload_root/lynxer.config" ]]; then
  printf 'No Lynxer payload found at %s.\n' "$payload_dir" >&2
  exit 1
fi
if [[ ! -f "$pkbuild_template" ]]; then
  printf 'PKGBUILD template not found at %s.\n' "$pkbuild_template" >&2
  exit 1
fi

# zstd is needed for makepkg's default .pkg.tar.zst output; the runtime libs
# let us execute the packaged interpreter as part of the version guard.
build_packages=(base-devel zstd libffi gcc-libs alsa-lib bzip2)
pacman -Syu --noconfirm
pacman -S --needed --noconfirm "${build_packages[@]}"

for tool in makepkg fakeroot sed install readelf tar; do
  if ! command -v "$tool" >/dev/null 2>&1; then
    printf 'Required packaging tool %s is missing.\n' "$tool" >&2
    exit 1
  fi
done

if ! id builder >/dev/null 2>&1; then
  useradd --create-home --shell /bin/bash builder
fi

payload_version="$(sed -n 's/^version *= *//p' "$payload_root/lynxer.config" | head -n1)"
source_version="$(sed -n 's/^version *= *//p' /src/lynxer/lynxer.config | head -n1)"
if [[ -z "$payload_version" || "$payload_version" != "$source_version" ]]; then
  printf 'Payload version (%s) does not match the checkout (%s).\n' \
    "$payload_version" "$source_version" >&2
  exit 1
fi
if [[ "$payload_version" == "0.1.9b1" ]]; then
  printf 'Forbidden Lynxer version detected in payload; aborting.\n' >&2
  exit 1
fi
if [[ ! "$payload_version" =~ ^[0-9A-Za-z.+-]+$ ]]; then
  printf 'Unsafe Lynxer version string: %s\n' "$payload_version" >&2
  exit 1
fi

rm -rf "$build_dir"
install -d -o builder -g builder "$build_dir"
install -d -o builder -g builder "$build_dir/src"
install -d -o builder -g builder "$build_dir/payload"
cp -a "$payload_root"/. "$build_dir/payload/"
install -m 0644 /src/LICENSE "$build_dir/LICENSE"
install -m 0644 /src/README.md "$build_dir/README.md"

for required in lynxer stdlib lynxer.config liblynxer.so lynxer.h ffi_abi.h; do
  if [[ ! -e "$build_dir/payload/$required" ]]; then
    printf 'Payload is missing %s.\n' "$required" >&2
    exit 1
  fi
done

sed "s/__PKGVER__/$payload_version/g" "$pkbuild_template" > "$build_dir/PKGBUILD"
if grep -q '__PKGVER__' "$build_dir/PKGBUILD"; then
  printf 'Failed to substitute the package version.\n' >&2
  exit 1
fi
chown -R builder:builder "$build_dir"

# --nodeps: no compilation happens, so runtime dependencies need not be
# installed in the container. Packaging alone needs makepkg + fakeroot.
su builder -s /bin/bash -c "cd '$build_dir' && makepkg --noconfirm --nodeps"

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

if [[ "${packages[0]}" != *"$payload_version"* ]]; then
  printf 'Built package name does not contain version %s: %s\n' \
    "$payload_version" "${packages[0]}" >&2
  exit 1
fi

# Version control: a lynxer.config text file can claim anything (that is how
# 0.1.9b1 shipped inside a 0.1.9b1h package). Only the interpreter's own
# --version output is authoritative, so unpack the finished package and run
# that exact binary before publishing it.
check_dir="$(mktemp -d)"
case "${packages[0]}" in
  *.zst) tar --zstd -xf "${packages[0]}" -C "$check_dir" ;;
  *)     tar -xf "${packages[0]}" -C "$check_dir" ;;
esac

installed_binary="$check_dir/usr/lib/lynxer/lynxer"
if ! cmp -s "$build_dir/payload/lynxer" "$installed_binary"; then
  printf 'Packaged binary is not byte-for-byte the build artifact.\n' >&2
  exit 1
fi
if ! cmp -s "$build_dir/payload/lynxer.config" "$check_dir/usr/lib/lynxer/lynxer.config"; then
  printf 'Packaged lynxer.config does not match the build artifact.\n' >&2
  exit 1
fi

installed_version="$("$installed_binary" --version)"
expected_version="Lynxer $payload_version"
if [[ "$installed_version" != "$expected_version" ]]; then
  printf 'Packaged Lynxer version %q does not match expected %q.\n' \
    "$installed_version" "$expected_version" >&2
  exit 1
fi
if [[ "$installed_version" == "Lynxer 0.1.9b1" ]]; then
  printf 'Forbidden Lynxer version detected in package; aborting.\n' >&2
  exit 1
fi
printf 'Packaged Lynxer version: %s\n' "$installed_version"

if [[ "$arch" == "x86_64" ]]; then
  isa="$(readelf -n "$installed_binary" | sed -n 's/.*x86 ISA needed: //p' | head -n1)"
  if printf '%s\n' "$isa" | grep -qE 'x86-64-v[234]'; then
    printf 'Packaged binary requires an x86-64 ISA level above baseline: %s\n' "$isa" >&2
    exit 1
  fi
fi
rm -rf "$check_dir"

output_dir="/src/dist/packages/$arch"
install -d "$output_dir"
rm -f "$output_dir"/lynxer-*.pkg.tar.*
install -m 0644 "${packages[0]}" "$output_dir/"
printf 'Built package: %s\n' "$output_dir/$(basename "${packages[0]}")"

# Install the freshly built package on this Arch system and smoke-test it, so
# the CI proves the package installs cleanly (dependencies included) and the
# installed interpreter resolves its stdlib/config regardless of cwd.
shopt -s nullglob
install_candidates=("$output_dir"/lynxer-*.pkg.tar.*)
shopt -u nullglob
if (( ${#install_candidates[@]} != 1 )); then
  printf 'Expected one Lynxer package to install, found %s.\n' "${#install_candidates[@]}" >&2
  exit 1
fi
pacman -U --noconfirm "${install_candidates[0]}"
installed_version="$(/usr/bin/lynxer --version)"
printf 'Installed Lynxer version: %s\n' "$installed_version"
expected_installed_version="Lynxer $payload_version"
if [[ "$installed_version" != "$expected_installed_version" ]]; then
  printf 'Installed Lynxer version %q does not match expected %q.\n' \
    "$installed_version" "$expected_installed_version" >&2
  exit 1
fi
if [[ "$installed_version" == "Lynxer 0.1.9b1" ]]; then
  printf 'Forbidden Lynxer version detected after install; aborting.\n' >&2
  exit 1
fi
printf 'fn setup(){ import("math"); }\nfn main(){ println(math.sqrt(9)); }\n' > /tmp/lynxer-package-smoke.lynx
smoke_output="$(cd /tmp && /usr/bin/lynxer /tmp/lynxer-package-smoke.lynx)"
printf 'Installed package smoke output: %s\n' "$smoke_output"
if [[ "$smoke_output" != "3" ]]; then
  printf 'Installed Lynxer did not run the smoke program correctly.\n' >&2
  rm -f /tmp/lynxer-package-smoke.lynx
  exit 1
fi
if ! /usr/bin/lynxer --list-stdlibs | grep -Fxq '  math'; then
  printf 'Installed Lynxer did not find its stdlib.\n' >&2
  rm -f /tmp/lynxer-package-smoke.lynx
  exit 1
fi
rm -f /tmp/lynxer-package-smoke.lynx
printf 'Installed Arch package smoke test passed.\n'
