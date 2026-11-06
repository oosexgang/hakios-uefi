#!/bin/bash
# Build HakiOS's U-Boot for one device: u-boot/ (made by apply.sh) with configs/hakios_<device>_defconfig, into
# u-boot/build-<device>/u-boot.bin: an AArch64 image with Linux's header, which ABL starts like a kernel.
#
#   uefi/u-boot/build.sh DEVICE
#
# CROSS_COMPILE: an AArch64 GCC (aarch64-linux-gnu-, or Arm's aarch64-none-elf-); found on PATH when not given.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)     # not resolved: uefi/ may be a link to a checkout elsewhere
ROOT=$(dirname "$(dirname "$HERE")")
UB=${U_BOOT:-$ROOT/u-boot}
dev=${1:?usage: build.sh DEVICE}
[ -d "$UB" ] || { echo "u-boot/build.sh: no $UB (uefi/u-boot/apply.sh first)" >&2; exit 1; }
[ -f "$UB/configs/hakios_${dev}_defconfig" ] || { echo "u-boot/build.sh: no hakios_${dev}_defconfig in $UB" >&2; exit 1; }
if [ -z "$CROSS_COMPILE" ]; then
	for c in aarch64-linux-gnu- aarch64-none-elf- aarch64-elf-; do
		command -v "${c}gcc" >/dev/null && { CROSS_COMPILE=$c; break; }
	done
	[ -n "$CROSS_COMPILE" ] || { echo "u-boot/build.sh: no AArch64 GCC on PATH (CROSS_COMPILE=...)" >&2; exit 1; }
fi
out=$UB/build-$dev
# On a Mac: U-Boot's makefiles want GNU make 4 (MAKE= one, as gmake or make on PATH: macOS's own is 3.81, which has no
# `undefine`); scripts/check-local-export is a check only and wants bash 4 (macOS's is 3.2), so it is left out; and the
# host tools are built without OpenSSL, which macOS does not ship headers for (the FIT signing tools: nothing here signs).
MAKE=${MAKE:-make}
extra=()
if [ "$(uname)" = Darwin ]; then
	command -v gmake >/dev/null && MAKE=gmake
	"$MAKE" --version | head -1 | grep -q 'GNU Make [4-9]' ||
		{ echo "u-boot/build.sh: U-Boot needs GNU make 4 (MAKE=..., or gmake on PATH); this make is: $("$MAKE" --version | head -1)" >&2; exit 1; }
	extra=(cmd_check_local_export=true NO_SDL=1)
fi
"$MAKE" -C "$UB" -s O="$out" CROSS_COMPILE="$CROSS_COMPILE" "${extra[@]}" "hakios_${dev}_defconfig"
if [ "$(uname)" = Darwin ]; then
	"$UB/scripts/config" --file "$out/.config" -d TOOLS_LIBCRYPTO -d TOOLS_KWBIMAGE
	"$MAKE" -C "$UB" -s O="$out" CROSS_COMPILE="$CROSS_COMPILE" "${extra[@]}" olddefconfig
fi
"$MAKE" -C "$UB" -s O="$out" CROSS_COMPILE="$CROSS_COMPILE" "${extra[@]}" -j"$(nproc)"
ls -la "$out/u-boot.bin"
