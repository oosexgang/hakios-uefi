# Port HakiOS U-Boot to a device

HakiOS has one shared U-Boot command layer and EFI loader handoff. It does not have a universal phone image: a device profile must provide the correct U-Boot target, memory map, device tree, boot chain, storage setup, and USB device controller.

## 1. Check the boot path first

Before adding files, identify the exact device codename, SoC, RAM layout, bootloader version, and recovery path. Confirm that the stock bootloader accepts a RAM boot image. A phone that needs a proprietary DDR training stage, signed vendor image, or board-specific BootROM exploit is not supported by adding a defconfig alone.

Begin with a serial console and the upstream U-Boot board closest to the target. Record how the stock bootloader passes the device tree and initramfs, how to return to its recovery mode, and whether secure boot permits the image. Do not write GPT or boot-critical partitions during first bring-up.

## 2. Create a profile

Create a profile directory. Keep it in this repository's `platform/` directory or, preferably, in the device repository; point `HAKIOS_PROFILE_ROOT` at the directory containing vendor profiles. Copy the template:

```sh
mkdir -p "$HAKIOS_PROFILE_ROOT/<vendor>/<device>/u-boot/files"
cp platform/profile.env.example "$HAKIOS_PROFILE_ROOT/<vendor>/<device>/profile.env"
```

Set the variables to the real board values:

```sh
UBOOT_DEFCONFIG=vendor_device_defconfig
FIRMWARE_DIR=<device>
PROFILE_DTB=uefi/device-tree.dtb
```

`FIRMWARE_DIR` is relative to `DEVICE_FIRMWARE` (by default `local-firmware/`). Keep proprietary firmware and extracted device-tree binaries there; never commit them. Commit only source, reproducible configuration, and documentation that can be redistributed.

## 3. Add the board files

Put the board's defconfig, environment, and device-tree source in its own overlay. The overlay is copied over the pinned U-Boot source by `u-boot/apply.sh <vendor>/<device>`.

Typical layout:

```text
<profile-root>/<vendor>/<device>/
  profile.env
  README.md
  u-boot/files/
    configs/<vendor>_<device>_defconfig
    board/<vendor>/<device>.env
    dts/upstream/src/arm64/<soc>/<device>.dts
```

Set the default device tree in the defconfig and use the shared environment file with `CONFIG_ENV_DEFAULT_ENV_TEXT_FILE="board/hakios.env"`. Enable `CMD_HAKIOS`, `CMD_BOOTEFI`, the selected USB gadget controller, USB ACM if the board supports it, and `EFI_HAKIOS_USB_DEVICE`. Add hardware-specific environment settings in the profile only when needed. Do not copy another device's addresses, reserved memory ranges, USB port number, or environment as defaults.

Put code shared by a SoC family in this repository's `platform/<vendor>/common/u-boot/files/`. Keep board pins, regulators, reserved regions, controller selection, and reset behavior in the device overlay.

## 4. Enable the shared USB path

The HakiOS loader and U-Boot use the HakiOS EFI USB protocol GUID `0ebd8a1f-88b5-4b1e-a659-8d79f2942b31`. U-Boot's implementation uses its generic USB gadget API, so it can work with different device-controller drivers. The board still needs a functioning U-Boot gadget controller, correct device mode/pinctrl setup, and a host-compatible USB cable.

The loader in the kernel repository must include the same protocol definition and probe this GUID. It retains a Qualcomm-protocol fallback for stock Qualcomm UEFI. If a device uses a different transport or cannot use U-Boot's gadget framework, add and document a transport adapter; do not label the device supported until transfers work on hardware.

## 5. Boot from an SD card

The shared environment includes an optional SD-card path. It reads `/hakios.hkub` from a FAT partition and passes the container to `hakios load`. This is supported only on profiles that enable the target's SD/MMC host, pinctrl and regulator setup, block layer, `CONFIG_CMD_MMC`, and `CONFIG_CMD_FAT`. U-Boot's `mmc` command selects a device; `fatload` reads a file into RAM. See the [U-Boot MMC command](https://docs.u-boot.org/en/stable/usage/cmd/mmc.html) and [FAT load command](https://docs.u-boot.org/en/v2025.01/usage/cmd/fatload.html) references.

Use a FAT32 first partition, for example 1 GiB, and copy `hakios.hkub` into its root directory. That is enough for the loader and the current payload limit; the rest of the card may be left unused or put in a separate data partition. HakiOS does not automatically mount or use that second partition: the OS needs its own SD driver and storage configuration.

Build both the usual RAM-boot image and the standalone container:

```sh
python3 u-boot/pack.py \
  --u-boot "build/u-boot-$DEVICE/u-boot.bin" \
  --loader "$KERNEL_ROOT/arch/arm64/boot/loader.efi" \
  --dtb "$DEVICE_FIRMWARE/$FIRMWARE_DIR/$PROFILE_DTB" \
  --payload /path/to/device.xnup \
  --hkub-output build/hakios.hkub \
  -o "build/hakios-$DEVICE.img"
```

Copy `build/hakios.hkub` to the SD card's FAT32 root. In the board environment file, set `hakios_sd_dev` to the device number shown by `mmc list` (commonly, but not always, `0`), `hakios_sd_part=1`, and `hakios_sd_addr_r` to a RAM range reserved for this file by the board's memory map. The common loader caps the read at 64 MiB. That full range must be available and must not overlap U-Boot, firmware, the device tree, or reserved regions. Never copy an address from another phone. Enable `CONFIG_CMD_MMC`, `CONFIG_MMC`, the correct controller/PHY and `CONFIG_CMD_FAT`; then verify `mmc list`, `mmc dev`, and `fatls mmc <dev>:1 /` on the actual board. Select **Boot HakiOS from SD card** in the menu, or let the default boot flow try it after the boot-image RAM container.

For removable media, allocate space on the card itself. Do not resize Android's internal GPT or reserve 64 GiB of phone storage for this boot flow. A second partition is only a storage layout choice; it does not make that space available to the OS without driver and mount support.

## 6. Build and package

Install GNU make, GNU Bash, Python 3, OpenSSL 3, and an AArch64 GCC toolchain. Set `KERNEL_ROOT` to the kernel checkout and `DEVICE_FIRMWARE` to the local firmware root:

```sh
source ./env.sh
PROFILE=<vendor>/<device>
DEVICE=${PROFILE##*/}
source "$HAKIOS_PROFILE_ROOT/$PROFILE/profile.env"
u-boot/build.sh "$PROFILE"

python3 u-boot/pack.py \
  --u-boot "build/u-boot-$DEVICE/u-boot.bin" \
  --loader "$KERNEL_ROOT/arch/arm64/boot/loader.efi" \
  --dtb "$DEVICE_FIRMWARE/$FIRMWARE_DIR/$PROFILE_DTB" \
  --payload /path/to/device.xnup \
  -o "build/hakios-$DEVICE.img"
```

Omit `--payload` to start the loader without a preloaded XNUP image. Use the device's own DTB. The resulting Android boot image is for the stock bootloader's RAM-boot command; it is not a flashing package.

## 7. Test before calling it supported

Keep a test log in the profile README. Test in this order:

1. Build the upstream target and verify the AArch64 boot-image header.
2. Start U-Boot with RAM boot and capture a serial log.
3. Check the displayed memory map and confirm the loader and payload reservations do not overlap firmware or device memory.
4. Verify SD-card detection, FAT file reads, USB fastboot/ACM transfers, then return to stock recovery.
5. Start the EFI loader with and without a payload; check reset, watchdog, and repeated cold boots.
6. Validate the target OS separately. Reaching a logo is not a successful OS boot.

Record the exact U-Boot revision, defconfig, device-tree source, firmware version, commands, successful checks, failures, and recovery steps. Never claim support for a whole vendor family from one phone test.
