#!/bin/bash
# HakiOS's U-Boot: Das U-Boot at the release in BASE, with HakiOS's changes on top, and the U-Boot files of each device
# that has some (devices/<name>/u-boot/files/: its device tree, defconfig and environment). The result is u-boot/ at the
# top of the repository, which u-boot/build.sh builds and tools/mkubootimg.py packs.
#
#   uefi/u-boot/apply.sh [DEVICE...]      default: every device with a devices/<name>/u-boot/files/
#
# What HakiOS adds to U-Boot (hakios.patch, files/):
#   cmd/hakios.c     the hakios command: the HakiOS loader and its payload from memory (ABL's ramdisk or the fastboot
#                    buffer), the payload to 0xF0000000, the loader started with bootefi
set -e
HERE=$(cd "$(dirname "$0")" && pwd)     # not resolved: uefi/ may be a link to a checkout elsewhere
ROOT=$(dirname "$(dirname "$HERE")")
UB=${U_BOOT:-$ROOT/u-boot}
[ -e "$UB" ] && { echo "u-boot/apply.sh: $UB already exists (u-boot/export.sh saves what is in it; move it away for a fresh one)"; exit 1; }
git clone -q --depth 1 -b "$(cat "$HERE/BASE")" https://source.denx.de/u-boot/u-boot.git "$UB"
git -C "$UB" apply --whitespace=nowarn "$HERE/hakios.patch"
cp -r "$HERE/files/." "$UB/"
devices=("$@")
if [ ${#devices[@]} -eq 0 ]; then
	for d in "$ROOT"/devices/*/u-boot/files; do [ -d "$d" ] && devices+=("$(basename "$(dirname "$(dirname "$d")")")"); done
fi
for d in "${devices[@]}"; do
	[ -d "$ROOT/devices/$d/u-boot/files" ] || { echo "u-boot/apply.sh: devices/$d has no u-boot/files/" >&2; exit 1; }
	cp -r "$ROOT/devices/$d/u-boot/files/." "$UB/"
	echo "u-boot/apply.sh: the files of $d"
done
echo "u-boot/apply.sh: done; build with: uefi/u-boot/build.sh <device>"
