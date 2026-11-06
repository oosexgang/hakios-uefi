# HakiOS UEFI

The UEFI firmware HakiOS phones start with: [Project Silicium's Mu-Silicium](https://github.com/Project-Silicium/Mu-Silicium)
at a fixed commit, with HakiOS's changes on top, and the platform of each phone from its device repository.

| What | Where |
|---|---|
| the Mu-Silicium commit it is made on | `BASE` |
| HakiOS's changes to Silicium's files | `hakios.patch` |
| HakiOS's new files (the XNU loader in the boot manager) | `files/` |
| the pictures it shows | `branding/banner.py` makes them |
| a phone's platform (memory map, drivers list, its device tree) | `devices/<name>/uefi/files/` in the device's repository |

## What HakiOS changes

| Part | Change |
|---|---|
| `BootShim/BootShim.S` | copies the `XNUP` container behind the UEFI image to `0xF0000000`, where the loader finds the kernel |
| `BootManagerLib` | starts the XNU loader from that container (`XnuLoader.c`) instead of showing a boot menu |
| `BootLogo.bmp`, `NoBootOS.bmp`, `MassStorage.bmp` | the site's mark (a lowercase h and the red dot) white on black, in place of Silicium's: what the phone shows while it starts is ours, every time |
| `PcdFirmwareVendor` | `HakiOS` (what the firmware and its SMBIOS tables say they are) |
| `Sec.c` | the first log line: `HakiOS UEFI (Mu-Silicium) for <maker> <model>` |

Mu-Silicium's own code keeps its licence and its authors' notices; this is a fork of it in the open, as its licence allows.

## U-Boot

`u-boot/` is the other firmware HakiOS starts its loader with: [Das U-Boot](https://source.denx.de/u-boot/u-boot) at the
release in `u-boot/BASE`, with HakiOS's changes, and each device's files (`devices/<name>/u-boot/files/`: its device tree,
defconfig and environment). It is open source throughout: no vendor driver is needed, and nothing is flashed: the phone's ABL
starts it from RAM (`fastboot boot`) and it starts the loader from memory. The common repository's `docs/u-boot.md` says how it
runs and what has been checked.

| What | Where |
|---|---|
| the U-Boot release it is made on | `u-boot/BASE` |
| HakiOS's changes to U-Boot's files (Kconfig, Makefiles, the EFI setup) | `u-boot/hakios.patch` |
| the hakios command: the loader and its payload from ABL's ramdisk or U-Boot's fastboot, the payload to `0xF0000000`, `bootefi` | `u-boot/files/cmd/hakios.c` |
| Qualcomm's `EFI_USB_DEVICE_PROTOCOL` on U-Boot's USB gadget stack: the loader's own fastboot (`xl`) under U-Boot | `u-boot/files/lib/efi_loader/efi_qcom_usb.c` |
| what the host does, on the phone's screen, each line marked `[oosexgang]` (`hakios_status=0` turns it off) | `efi_qcom_usb.c`, `cmd/hakios.c` |
| U-Boot's DWC3 and fastboot fixed for this: bulk transfers start on queue, a stall is cleared with no transfer started, a response is dequeued only in flight | `u-boot/hakios.patch` |

```sh
uefi/u-boot/apply.sh             # u-boot/ at the top: BASE, hakios.patch, files/, every device's U-Boot files
uefi/u-boot/build.sh redwood     # u-boot/build-redwood/u-boot.bin (CROSS_COMPILE: an AArch64 GCC)
uefi/u-boot/export.sh            # the reverse, after changing something in u-boot/
```

## The pictures

```sh
python3 uefi/branding/banner.py /tmp/banner     # BootLogo.bmp, NoBootOS.bmp, MassStorage.bmp
```

They are the website's mark (hakios-web: a lowercase h and a red dot) white on black, drawn from its shapes and smoothed, with a
5x7 pixel font for the two captions, as 8-bit BMPs with a 256-colour palette (greys and reds), the format and sizes Silicium's are (300x300, 300x300, 500x500). Nothing to install; change
the text or the colours in the file and run `uefi/apply.sh` again.

## Make the tree

```sh
uefi/apply.sh            # Mu-Silicium/ at the top: BASE, hakios.patch, files/, the pictures, every device's platform
uefi/apply.sh redwood    # ... with one device's platform only
uefi/export.sh           # the reverse, after changing something in Mu-Silicium/
```

## The vendor's drivers are not in the repository

A phone's UEFI is made of open code (Project Mu, EDK2) and of drivers that Qualcomm and Xiaomi wrote for
that phone: display, storage, USB, secure boot and more. They are proprietary and have not been
licensed for redistribution, so a device repository carries neither them (`devices/<name>/uefi/files/Binaries/<name>/*`) nor
the UEFI image built with them (`prebuilt/SILICIUM_UEFI.fd`). `tools/provenance-check.py` fails the
build if they come back; it runs on every push, and `tools/provenance.allow` says why each binary that
*is* here may be.

Without that image `mkxnuimg.py` stops with a message that points here. To get one, make both from
**your own phone**:

1. Get your phone's UEFI firmware: the vendor's `xbl` firmware, which is on the phone (with root, the
   `xbl` partition) and in the vendor's firmware package for your model and region.
   `devices/redwood/uefi/files/Binaries/redwood/README.md` records the version the port was made on (`OS1.0.3.0.UMSMIXM`):
   another version may need other patches.
2. Extract the drivers from it with [UEFIReader](https://github.com/WOA-Project/UEFIReader) into
   `devices/<name>/uefi/files/Binaries/<name>/`. Its README says no more than what the tool is for ("generate .inf
   payloads ... out of an existing UEFI volume", as of 2026-10-26); Mu-Silicium's documentation for a
   device describes the layout it expects. **The authors of this repository have not repeated this
   extraction since the drivers were taken out of the tree: if you do it, say what worked in an
   issue.**
3. Build as below. `mkxnuimg.py` then takes the image from `Mu-Silicium/`.

What you extract is the vendor's, from your own device: keep it on your disk and never commit it.

## Build

Packages as in Mu-Silicium's `setup_env.sh` (Arch: `git base-devel lld nasm clang llvm
aarch64-linux-gnu-gcc python python-pip python-virtualenv`).

```sh
uefi/apply.sh redwood                  # clones Mu-Silicium (with submodules, ~3 GB), HakiOS's changes, the redwood platform
python3 -m venv venv && venv/bin/pip install -r Mu-Silicium/pip-requirements.txt
cd Mu-Silicium
source ../venv/bin/activate
python3 build_uefi.py -d redwood -r RELEASE     # -> Build/redwoodPkg/RELEASE_CLANGPDB/FV/SILICIUM_UEFI.fd
```

The `mu_nasm` dependency comes through NuGet and wants Mono; without Mono, point it at the
system nasm:

```sh
D=Mu_Basecore/BaseTools/Bin/mu_nasm_extdep
mkdir -p $D/Linux-x86-64
echo 'version: 20016.1.1' > $D/extdep_state.yaml
ln -s /usr/bin/nasm /usr/bin/ndisasm $D/Linux-x86-64/
```

After a build, `mkxnuimg.py` (called by `./xl enter`) takes the UEFI from `Mu-Silicium/`, and
`tools/update-prebuilt.sh` refreshes `devices/redwood/prebuilt`. `uefi/export.sh`
is the reverse: it saves the changes from `Mu-Silicium/` back.

## What has been checked

27 October 2026: `uefi/apply.sh`'s steps, run on a checkout of Mu-Silicium at `BASE`, give the same tree as the earlier redwood
port, with the HakiOS pictures, vendor and log line in it. **The firmware has not been built or started with these changes yet**:
the build needs the vendor's drivers of a phone (above). The pictures are in the format and sizes of Silicium's own.
