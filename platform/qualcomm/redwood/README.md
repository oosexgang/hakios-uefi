# Qualcomm SM7325: POCO X5 Pro 5G (`redwood`)

This is the first HakiOS phone profile. It uses the upstream Qualcomm phone path and adds the Redwood device tree, menu, USB console/fastboot, and EFI loader handoff.

## Hardware status

- U-Boot image built on macOS and accepted by stock fastboot through `fastboot boot`.
- HakiOS EFI loader and USB recovery path have been exercised on the phone.
- Darwin 27 displayed the Apple logo. A complete userspace boot is not confirmed.
- Latest recorded USB check found no attached fastboot device; verify again before hardware operations.

## Build inputs

Place the boot-chain device tree at `local-firmware/redwood/uefi/device-tree.dtb`. This local directory is ignored by Git. Board DTS and defconfig sources live here; the shared kernel/board data package remains in `poco-x5-pro-device`.

Build with `u-boot/build.sh qualcomm/redwood`; package it using the loader and DTB paths from `profile.env`. The default boot environment is compiled into U-Boot, so the package has no separate Redwood environment file. The image can be RAM-booted with `fastboot boot`; it does not flash a partition. Do not reuse its memory map or reserved regions on another board.

## Bring-up notes

The next hardware pass should collect a serial/console log from the complete userspace boot, confirm the device returns to stock fastboot, and exercise reset, storage enumeration, charging/battery reporting, touch, and all eight CPU cores. GPU/Metal claims also need a rendered workload test on the target OS.
