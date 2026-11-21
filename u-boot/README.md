# U-Boot integration

The `files/` and `patches/` directories contain shared HakiOS commands and EFI setup. Board and family overlays are applied from `platform/` by `apply.sh`.

## Build

Install GNU make, GNU Bash, Python 3, OpenSSL 3, and an AArch64 GCC toolchain. Set `KERNEL_ROOT` to the kernel checkout and `REDWOOD_FIRMWARE` to Redwood's local firmware directory:

```sh
source ./env.sh
u-boot/build.sh qualcomm/redwood
python3 u-boot/pack.py \
  --u-boot build/u-boot-redwood/u-boot.bin \
  --loader "$KERNEL_ROOT/arch/arm64/boot/loader.efi" \
  --dtb "$REDWOOD_FIRMWARE/uefi/device-tree.dtb" \
  --env u-boot/env/redwood.env \
  -o build/redwood-u-boot.img
```

`pack.py` also accepts `--payload` to add an XNUP image. Use a device's own profile DTB and U-Boot defconfig. Do not reuse Redwood addresses or firmware on another board.

## U-Boot console

The menu provides loader entry, USB serial console, U-Boot fastboot, shell, and reset. The USB ACM interface is a serial terminal, not an on-screen keyboard. On macOS, connect to `/dev/cu.usbmodem*` at 115200 baud after enabling the serial gadget.

## Validation

Redwood U-Boot was accepted by stock fastboot via RAM boot, and the HakiOS loader reached the Darwin 27 Apple logo. Full userspace startup has not been verified. The MediaTek transport and phone profiles are not implemented.
