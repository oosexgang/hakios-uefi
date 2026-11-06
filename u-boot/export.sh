#!/bin/bash
# The reverse of apply.sh: what was changed in u-boot/ back into this directory (hakios.patch, files/) and into each
# device's devices/<name>/u-boot/files/. A file a device brought stays the device's.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)     # not resolved: uefi/ may be a link to a checkout elsewhere
ROOT=$(dirname "$(dirname "$HERE")")
UB=${U_BOOT:-$ROOT/u-boot}
[ -d "$UB/.git" ] || { echo "u-boot/export.sh: no U-Boot tree at $UB" >&2; exit 1; }
for f in $(cd "$HERE/files" && find . -type f); do cp "$UB/$f" "$HERE/files/$f"; done
for d in "$ROOT"/devices/*/u-boot/files; do
	[ -d "$d" ] || continue
	for f in $(cd "$d" && find . -type f); do cp "$UB/$f" "$d/$f"; done
done
# the patch is U-Boot's own files as changed; new files are in files/ (and the devices'), not in it
git -C "$UB" diff > "$HERE/hakios.patch"
echo "u-boot/export.sh: hakios.patch and files/ saved"
