# U-Boot integration

`files/` and `patches/` contain shared HakiOS commands, the EFI USB transport, and fixes for U-Boot's gadget transfer path. Device-specific defconfigs and device trees are applied from `platform/<vendor>/<device>/u-boot/files/`.

Build requires a profile. The scripts intentionally have no default board:

```sh
source ./env.sh
u-boot/build.sh vendor/device
```

The profile selects an upstream defconfig. `pack.py` combines that U-Boot binary, the HakiOS EFI loader, a device tree, and optionally an XNUP payload into an Android boot image. Example packaging commands are in [the board porting guide](../docs/porting-a-board.md).

The HakiOS EFI USB protocol is vendor-neutral. The U-Boot implementation uses the generic gadget API; each board still needs a working USB device-controller driver and correct device-tree setup. A loader that predates this protocol can use the Qualcomm compatibility fallback, but that does not add support for other vendor firmware.
