# Board profiles

The HakiOS command, HKUB image reader, and EFI handoff are shared. A profile selects an upstream U-Boot defconfig and device tree, then adds board-specific files under `u-boot/files/`. Optional vendor-family code belongs under `platform/<vendor>/common/u-boot/files/`.

Build a profile with `u-boot/build.sh <vendor>/<device>`. A profile is considered buildable only when it has `profile.env`, a valid upstream U-Boot defconfig, and the matching local DTB. The profile's firmware directory is ignored by Git; do not check vendor firmware into this repository.

## Current coverage

| Family | Profile | Validation |
| --- | --- | --- |
| Qualcomm Snapdragon | `qualcomm/redwood` | POCO X5 Pro SM7325 build and RAM boot observed; complete Darwin userspace boot remains unconfirmed. |
| MediaTek | None yet | U-Boot support depends on a named phone's boot chain, DDR setup, device tree, and USB transport. |
| Other AArch64 SoCs | None yet | Add a board profile when upstream U-Boot can start on that device. |

This is a reusable U-Boot framework, not one binary that can boot every phone. DDR initialization, secure boot, display, storage, and USB vary by board. A device is supported only after its profile has been built and tested on hardware.

To add a phone, copy `profile.env.example`, put its U-Boot overlay under `platform/<vendor>/<device>/u-boot/files/`, add only shared family code to `platform/<vendor>/common/`, and record the exact SoC, boot chain, firmware provenance, test status, and recovery method in the device README.
