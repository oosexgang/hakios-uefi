#!/bin/bash
# Local sibling-workspace paths; this file contains no credentials or firmware data.
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
WORKSPACE_ROOT=$(cd "$HERE/.." && pwd)
export KERNEL_ROOT=${KERNEL_ROOT:-$WORKSPACE_ROOT/poco-x5-pro}
export DEVICE_REPO=${DEVICE_REPO:-$WORKSPACE_ROOT/poco-x5-pro-device}
export HAKIOS_DEVICES=${HAKIOS_DEVICES:-$DEVICE_REPO}
export DEVICE_FIRMWARE=${DEVICE_FIRMWARE:-$HERE/local-firmware}
export REDWOOD_FIRMWARE=${REDWOOD_FIRMWARE:-$HERE/local-firmware/redwood}
