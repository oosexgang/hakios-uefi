# Board profiles

This repository currently contains the shared HakiOS U-Boot/EFI layer and a profile template. It contains no phone-specific profile. A board is supported only after its profile builds and its RAM-boot, recovery, storage, USB, and loader handoff have been checked on the actual hardware.

Start with a device whose bootloader can start upstream U-Boot from RAM. Add `<profile-root>/<vendor>/<device>/profile.env` and its U-Boot overlay. The profile root can live in a separate device repository and is selected with `HAKIOS_PROFILE_ROOT`. Put code shared by a vendor family under this repository's `platform/<vendor>/common/`; keep local vendor firmware under ignored `local-firmware/<device>/`.

See [porting a board](../docs/porting-a-board.md) for the full profile contract and validation record.
