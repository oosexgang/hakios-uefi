#!/bin/bash
# Prepare pinned U-Boot with shared HakiOS changes and one board profile.
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
UEFI_ROOT=$(cd "$HERE/.." && pwd)
PROFILE_ROOT=${HAKIOS_PROFILE_ROOT:-$UEFI_ROOT/platform}
PROFILE=${1:-}
if [ -z "$PROFILE" ]; then
	echo "usage: $0 <vendor/device>" >&2
	exit 2
fi
if [[ ! "$PROFILE" =~ ^[a-z0-9_-]+/[a-z0-9_-]+$ ]]; then
	echo "apply.sh: profile must be vendor/device, got '$PROFILE'" >&2
	exit 2
fi
PROFILE_DIR="$PROFILE_ROOT/$PROFILE"
if [ ! -f "$PROFILE_DIR/profile.env" ]; then
	echo "apply.sh: no buildable profile at platform/$PROFILE/profile.env" >&2
	exit 2
fi
# Profile files only contain assignments maintained in this repository.
source "$PROFILE_DIR/profile.env"
DEVICE=${PROFILE#*/}
UB=${U_BOOT:-$UEFI_ROOT/build/u-boot-$DEVICE-src}
TAG=$(cat "$HERE/BASE")
PIN=$(cat "$HERE/BASE_COMMIT")
URL=${U_BOOT_URL:-https://source.denx.de/u-boot/u-boot.git}

if [ ! -d "$UB/.git" ]; then
	if [ -e "$UB" ]; then
		echo "apply.sh: $UB exists but is not a Git checkout" >&2
		exit 1
	fi
	mkdir -p "$(dirname "$UB")"
	git clone --quiet --depth 1 --branch "$TAG" "$URL" "$UB"
fi

actual=$(git -C "$UB" rev-parse HEAD)
if [ "$actual" != "$PIN" ]; then
	echo "apply.sh: expected U-Boot $TAG at $PIN, found $actual in $UB" >&2
	exit 1
fi

if git -C "$UB" apply --check "$HERE/patches/hakios.patch" 2>/dev/null; then
	git -C "$UB" apply --whitespace=nowarn "$HERE/patches/hakios.patch"
elif ! grep -q 'config CMD_HAKIOS' "$UB/cmd/Kconfig" || ! grep -q 'EFI_HAKIOS_USB_DEVICE' "$UB/lib/efi_loader/Kconfig"; then
	echo "apply.sh: patch does not apply cleanly to the pinned source" >&2
	exit 1
fi

cp -R "$HERE/files/." "$UB/"
FAMILY_DIR="$UEFI_ROOT/platform/${PROFILE%%/*}/common/u-boot/files"
if [ -d "$FAMILY_DIR" ]; then
	cp -R "$FAMILY_DIR/." "$UB/"
fi
BOARD_DIR="$PROFILE_DIR/u-boot/files"
if [ -d "$BOARD_DIR" ]; then
	cp -R "$BOARD_DIR/." "$UB/"
fi
echo "apply.sh: U-Boot $TAG prepared for $PROFILE at $UB"
