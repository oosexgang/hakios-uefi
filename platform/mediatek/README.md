# MediaTek phone profiles

The common HakiOS U-Boot EFI layer is designed to be shared with MediaTek, but no MediaTek phone profile is implemented or tested yet. Existing upstream support for MediaTek development boards does not establish that a phone can boot this stack.

Each phone needs its own profile under this directory, named for the device codename. Record the exact SoC and upstream U-Boot revision, BootROM/preloader path, DDR initialization, secure-boot state, DRAM and reserved regions, storage, USB transport, display, reset behavior, and EFI handoff.

Start with non-destructive RAM boot and a serial log. Do not reuse the Redwood device tree or memory map. Do not flash a boot-critical partition until the profile has a repeatable recovery path and successful cold-boot tests.
