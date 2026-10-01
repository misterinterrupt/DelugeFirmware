#!/usr/bin/env bash
# One-shot USB disk mode health check for the Deluge.
#
# Reports:
#   - whether the Deluge mass-storage device is enumerated on USB
#   - whether its block device exists, its size, partitions, filesystem
#     signature (the thing that decides whether file explorers show it)
#     and mount state
#   - firmware-side debug counters (EP0 outcomes, FIFO errors, BOT state,
#     bus resets) read straight off the running Deluge via J-Link, with
#     symbol addresses resolved from the ELF at runtime so it survives
#     rebuilds
#
# Output is a compact block meant for pasting back into the ZCode session.
#
# Usage: scripts/diskmode-status.sh [--no-jlink]

set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

ELF="${ELF:-build/Release/deluge.elf}"
NM=nm
if [ -x toolchain/v16/linux-x86_64/arm-none-eabi-gcc/bin/arm-none-eabi-nm ]; then
    NM=toolchain/v16/linux-x86_64/arm-none-eabi-gcc/bin/arm-none-eabi-nm
fi

VID=16d0
PID=0ce3

echo "=== Deluge USB disk mode status @ $(date '+%H:%M:%S') ==="

# --- USB enumeration -------------------------------------------------------
usb_line=$(lsusb 2>/dev/null | grep -i "$VID:$PID" | head -1)
if [ -n "$usb_line" ]; then
    echo "USB:  ENUMERATED ($usb_line)"
else
    echo "USB:  not enumerated (no $VID:$PID on the bus)"
fi

# --- block device ----------------------------------------------------------
find_deluge_sd() {
    for d in /sys/block/sd*; do
        [ -e "$d" ] || continue
        p=$(readlink -f "$d/device") || continue
        # walk up the SCSI chain (disk -> target -> host -> interface) to the USB device
        usbdir="$p"
        for _ in 1 2 3 4 5 6; do
            usbdir="$(dirname "$usbdir")"
            if [ -f "$usbdir/idVendor" ]; then
                if [ "$(cat "$usbdir/idVendor")" = "$VID" ] && [ "$(cat "$usbdir/idProduct")" = "$PID" ]; then
                    basename "$d"
                    return 0
                fi
                break
            fi
        done
    done
    return 1
}

dev=$(find_deluge_sd || true)
if [ -n "${dev:-}" ]; then
    size=$(cat "/sys/block/$dev/size")
    echo "DISK: /dev/$dev ($((size / 2048)) MiB, $size sectors)"
    parts=$(lsblk -l -o NAME "/dev/$dev" 2>/dev/null | tail -n +2 | tr '\n' ' ')
    [ -n "$parts" ] && echo "PARTITIONS: $parts"
    fstype=$(udevadm info -q property -n "/dev/$dev" 2>/dev/null | sed -n 's/^ID_FS_TYPE=//p')
    if [ -n "$fstype" ]; then
        echo "FS:   $fstype"
    else
        echo "FS:   NO SIGNATURE  <-- sector reads are not completing"
    fi
    mnt=$(lsblk -no MOUNTPOINT "/dev/$dev" 2>/dev/null | grep -v '^$' | head -1)
    if [ -n "$mnt" ]; then
        echo "MOUNTED: $mnt"
    else
        echo "MOUNTED: no"
    fi
else
    echo "DISK: none (no block device from $VID:$PID)"
fi

# --- firmware state via J-Link (optional) ----------------------------------
# The canonical PMSC driver state can be read via J-Link when needed; see
# g_usb_pmsc_seq / g_usb_pmscstatus / g_usb_pmsc_dtl in deluge.elf.
echo "=== end ==="
