#!/bin/bash
# Build U-Boot for a board profile on macOS or Linux.
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
	echo "build.sh: profile must be vendor/device, got '$PROFILE'" >&2
	exit 2
fi
PROFILE_DIR="$PROFILE_ROOT/$PROFILE"
if [ ! -f "$PROFILE_DIR/profile.env" ]; then
	echo "build.sh: no buildable profile at platform/$PROFILE/profile.env" >&2
	exit 2
fi
source "$PROFILE_DIR/profile.env"
DEVICE=${PROFILE#*/}
UB=${U_BOOT:-$UEFI_ROOT/build/u-boot-$DEVICE-src}
OUT=${U_BOOT_OUT:-$UEFI_ROOT/build/u-boot-$DEVICE}
JOBS=${JOBS:-}

U_BOOT="$UB" "$HERE/apply.sh" "$PROFILE"

if [ -z "${CROSS_COMPILE:-}" ]; then
	for prefix in aarch64-elf- aarch64-linux-gnu- aarch64-none-elf-; do
		if command -v "${prefix}gcc" >/dev/null 2>&1; then
			CROSS_COMPILE=$prefix
			break
		fi
	done
fi
if [ -z "${CROSS_COMPILE:-}" ]; then
	echo "build.sh: install an AArch64 GCC toolchain or set CROSS_COMPILE" >&2
	exit 1
fi

if [ "$(uname -s)" = Darwin ]; then
	BREW=${HOMEBREW_PREFIX:-$(brew --prefix)}
	TOOLS="$UEFI_ROOT/build/u-boot-host-tools"
	mkdir -p "$TOOLS"
	GMAKE=${GNU_MAKE:-$(command -v gmake || true)}
	GBASH=${GNU_BASH:-$BREW/bin/bash}
	NCPU=$(sysctl -n hw.ncpu)
	PYTHON=${PYTHON3:-$(command -v python3.12 || command -v python3)}
	OPENSSL=${OPENSSL_PREFIX:-$(brew --prefix openssl@3)}
	[ -x "$GMAKE" ] || { echo "build.sh: GNU make is required (brew install make)" >&2; exit 1; }
	[ -x "$GBASH" ] || { echo "build.sh: GNU Bash 5 is required (brew install bash)" >&2; exit 1; }
	[ -n "$PYTHON" ] && [ -x "$PYTHON" ] || { echo "build.sh: Python 3 is required" >&2; exit 1; }
	[ -d "$OPENSSL/include/openssl" ] || { echo "build.sh: OpenSSL 3 headers are required (brew install openssl@3)" >&2; exit 1; }
	cat > "$TOOLS/make" <<EOF
#!/bin/sh
exec "$GMAKE" "\$@"
EOF
	cat > "$TOOLS/bash" <<EOF
#!/bin/sh
exec "$GBASH" "\$@"
EOF
	cat > "$TOOLS/nproc" <<EOF
#!/bin/sh
echo "$NCPU"
EOF
	ln -sf "$PYTHON" "$TOOLS/python3"
	chmod +x "$TOOLS/make" "$TOOLS/bash" "$TOOLS/nproc"
	export PATH="$TOOLS:$PATH"
	export HOSTCFLAGS="${HOSTCFLAGS:-} -I$OPENSSL/include"
	export HOSTLDFLAGS="${HOSTLDFLAGS:-} -L$OPENSSL/lib"
	export PKG_CONFIG_PATH="$OPENSSL/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
	JOBS=${JOBS:-$NCPU}
else
	JOBS=${JOBS:-$(nproc)}
fi

MAKE=${MAKE:-make}
"$MAKE" -C "$UB" -s O="$OUT" CROSS_COMPILE="$CROSS_COMPILE" "$UBOOT_DEFCONFIG"
"$MAKE" -C "$UB" -s O="$OUT" CROSS_COMPILE="$CROSS_COMPILE" -j"$JOBS"

IMAGE="$OUT/u-boot.bin"
[ -s "$IMAGE" ] || { echo "build.sh: U-Boot did not produce $IMAGE" >&2; exit 1; }
if [ "$(dd if="$IMAGE" bs=1 count=4 skip=56 2>/dev/null)" != "ARMd" ]; then
	echo "build.sh: $IMAGE has no AArch64 Linux image header" >&2
	exit 1
fi
ls -lh "$IMAGE"
echo "build.sh: profile $PROFILE built"
echo "build.sh: U-Boot: $IMAGE"
echo "build.sh: DTB:    ${DEVICE_FIRMWARE:-$UEFI_ROOT/local-firmware}/$FIRMWARE_DIR/$PROFILE_DTB"
