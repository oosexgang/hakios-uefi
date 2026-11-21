#!/bin/bash
# Local sibling-workspace paths; this file contains no credentials or firmware data.
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
WORKSPACE_ROOT=$(cd "$HERE/.." && pwd)
export KERNEL_ROOT=${KERNEL_ROOT:-$WORKSPACE_ROOT/xnu-kernel}
export DEVICE_REPO=${DEVICE_REPO:-$WORKSPACE_ROOT/devices}
export HAKIOS_DEVICES=${HAKIOS_DEVICES:-$DEVICE_REPO}
export DEVICE_FIRMWARE=${DEVICE_FIRMWARE:-$HERE/local-firmware}
export HAKIOS_PROFILE_ROOT=${HAKIOS_PROFILE_ROOT:-$HERE/platform}
