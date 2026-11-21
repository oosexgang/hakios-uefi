# HakiOS U-Boot EFI

U-Boot starts the HakiOS EFI loader and provides the board console, USB recovery transport, and image-loading commands. Shared U-Boot changes live in `u-boot/`; board definitions and local firmware belong to `platform/` and `local-firmware/`.

There are no device profiles in this repository yet. Add and test a board profile before building an image for a phone. One image cannot initialize every phone: DRAM setup, secure boot, device trees, storage, display, and USB controllers vary by board.

## Add a device

Read [the board porting guide](docs/porting-a-board.md), copy `platform/profile.env.example`, and add the target's U-Boot overlay under `<profile-root>/<vendor>/<device>/u-boot/files/`. Keep vendor firmware under ignored `local-firmware/<device>/`.

`HAKIOS_PROFILE_ROOT` defaults to this repository's `platform/` directory. Set it to a device repository's profile directory to keep board files in that repository.

## Build

After adding a profile and its upstream U-Boot defconfig:

```sh
source ./env.sh
export HAKIOS_PROFILE_ROOT=/path/to/device-repo/uefi-profiles
PROFILE=vendor/device
DEVICE=${PROFILE##*/}
source "$HAKIOS_PROFILE_ROOT/$PROFILE/profile.env"
u-boot/build.sh "$PROFILE"
python3 u-boot/pack.py \
  --u-boot "build/u-boot-$DEVICE/u-boot.bin" \
  --loader "$KERNEL_ROOT/arch/arm64/boot/loader.efi" \
  --dtb "$DEVICE_FIRMWARE/$FIRMWARE_DIR/$PROFILE_DTB" \
  -o "build/hakios-$DEVICE.img"
```

The output is an Android boot image for a supported bootloader's RAM-boot command. It does not flash device partitions. See the porting guide for profile requirements, USB protocol support, build checks, and hardware validation.
