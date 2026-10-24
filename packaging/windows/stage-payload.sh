#!/bin/sh
# Stage the files Inno Setup packages. Run from the repository root after
# `make buildLynxer`.
set -eu

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
src="$root/lynxer"
dst="$root/build/windows-payload"

rm -rf "$dst"
mkdir -p "$dst/stdlib"

for file in lynxer.exe lynxer.dll lynxer.dll.a lynxer.h ffi_abi.h lynxer.config; do
    if [ -f "$src/$file" ]; then
        cp "$src/$file" "$dst/"
    fi
done
cp "$src"/stdlib/* "$dst/stdlib/"
if [ -d "$src/runtime" ]; then
    cp "$src"/runtime/* "$dst/"
fi
echo "Staged $dst"
