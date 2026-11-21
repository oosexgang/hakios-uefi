# HakiOS U-Boot EFI

U-Boot starts the HakiOS EFI loader and supplies a console, boot menu, and image-loading commands. The kernel repository owns the EFI loader and kernel; this repository owns U-Boot integration and device profiles.

Build and hardware-specific files are kept out of the kernel tree. Local vendor firmware and device trees stay under the ignored `local-firmware/` directory and are never committed.

## Build a board

```sh
source ./env.sh
u-boot/build.sh qualcomm/redwood
python3 u-boot/pack.py \
  --u-boot build/u-boot-redwood/u-boot.bin \
  --loader "$KERNEL_ROOT/arch/arm64/boot/loader.efi" \
  --dtb "$REDWOOD_FIRMWARE/uefi/device-tree.dtb" \
  -o build/hakios-redwood.img
```

The produced Android boot image is intended for `fastboot boot`. It does not write device partitions. See [board profiles](platform/README.md) for adding a device and the current validation status.

## Scope

The HakiOS image reader and EFI handoff are shared across profiles. Hardware initialization remains board-specific: there is no universal phone UEFI image. Redwood is the only phone profile currently tested; MediaTek and other SoCs need device profiles and hardware validation before they are supported.
