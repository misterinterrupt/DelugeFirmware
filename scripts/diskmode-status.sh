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
        usbdir="$p/../../.."
        [ -f "$usbdir/idVendor" ] || continue
        if [ "$(cat "$usbdir/idVendor")" = "$VID" ] && [ "$(cat "$usbdir/idProduct")" = "$PID" ]; then
            basename "$d"
            return 0
        fi
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

# --- firmware counters via J-Link -----------------------------------------
if [ "${1:-}" = "--no-jlink" ]; then
    echo "=== end (J-Link skipped) ==="
    exit 0
fi

if ! command -v JLinkExe >/dev/null 2>&1; then
    echo "FW:   JLinkExe not found - skipping firmware counters"
    echo "=== end ==="
    exit 0
fi

if [ ! -f "$ELF" ]; then
    echo "FW:   ELF not found at $ELF - skipping firmware counters"
    echo "=== end ==="
    exit 0
fi

# Resolve a symbol's address; tolerates .lto_priv suffixes on statics.
addr_of() {
    "$NM" "$ELF" | awk -v s="$1" '$3 ~ s"(\\.|$)" {print $1; exit}' | tr 'a-f' 'A-F'
}

# name  size(b)  description
COUNTERS="
g_usb_dbg_pump      4  PCD task events processed
g_usb_dbg_reset     4  bus resets seen
g_usb_dbg_ctrt      4  control-transfer interrupts
g_usb_dbg_gdesc     4  GET_DESCRIPTOR handled
g_usb_dbg_brdy0     4  EP0 data arrivals
g_usb_dbg_bemp0     4  EP0 transmissions completed
g_usb_dbg_stall0    4  EP0 stalls raised
g_usb_dbg_cr_shrt   4  ctrl reads ok (short)
g_usb_dbg_cr_end    4  ctrl reads ok (full)
g_usb_dbg_cr_writing 4 ctrl reads partial
g_usb_dbg_cr_fifoerr 4 ctrl reads FIFO-ERROR (should stay 0)
g_sd_sector_count   4  SD capacity (sectors)
g_pmsc_state        1  BOT state
g_scsi_status       1  last SCSI status
g_pmsc_ejected      1  ejected flag
"

cmdfile=$(mktemp /tmp/dmstatus.XXXXXX.jlink)
outprefix="DMST"
: >"$cmdfile"
names=""
while read -r sym sz desc; do
    [ -n "${sym:-}" ] || continue
    a=$(addr_of "$sym")
    if [ -z "$a" ]; then
        echo "FW:   symbol $sym not in ELF (stale build?)"
        continue
    fi
    names="$names $sym"
    if [ "$sz" = "1" ]; then
        echo "mem8 $a 1" >>"$cmdfile"
    else
        echo "mem32 $a 1" >>"$cmdfile"
    fi
done <<EOF
$COUNTERS
EOF
echo "quit" >>"$cmdfile"

jlink_out=$(timeout 45 JLinkExe -device R7S721001 -if SWD -speed 1000 -AutoConnect 1 -NoGui 1 \
    -CommanderScript "$cmdfile" 2>/dev/null)
rm -f "$cmdfile"

vals=""
for name in $names; do
    a=$(addr_of "$name")
    v=$(printf '%s\n' "$jlink_out" | awk -v a="$a" '$1 == a && / = / {print $NF; exit}')
    vals="$vals $name=$v"
done
echo "FW:  raw:$vals"

# --- human decoding --------------------------------------------------------
get() {
    printf '%s\n' "$vals" | tr ' ' '\n' | sed -n "s/^$1=//p" | head -1
}
hex2dec() {
    [ -n "$1" ] && echo $((16#$1)) || echo "?"
}

state_names=("" "WAIT_CBW" "DATA_IN" "DATA_OUT" "SEND_CSW")
st=$(get g_pmsc_state)
sti=$(hex2dec "$st")
stn="?"
[ -n "${state_names[$sti]:-}" ] && stn="${state_names[$sti]}"

sect=$(hex2dec "$(get g_sd_sector_count)")
fifoerr=$(hex2dec "$(get g_usb_dbg_cr_fifoerr)")
resets=$(hex2dec "$(get g_usb_dbg_reset)")

echo "FW:   BOT state=$stn ($sti)  scsi_status=$(hex2dec "$(get g_scsi_status)")  ejected=$(hex2dec "$(get g_pmsc_ejected)")"
echo "FW:   sd_sectors=$sect  bus_resets=$resets  fifo_errors=$fifoerr"
echo "FW:   ep0: ok=$(($(hex2dec "$(get g_usb_dbg_cr_shrt)") + $(hex2dec "$(get g_usb_dbg_cr_end)")))  fifoerr=$fifoerr  stall=$(hex2dec "$(get g_usb_dbg_stall0)")  bemp0=$(hex2dec "$(get g_usb_dbg_bemp0)")"

echo "--- verdict hints ---"
if [ -z "$usb_line" ]; then
    echo "- Device not on USB: check cable, and that disk mode is actually entered (screen shows counters)."
elif [ -z "${dev:-}" ]; then
    echo "- Enumerated but no block device: usb-storage has not completed a LUN scan; check bus_resets growth."
elif [ -z "$fstype" ]; then
    echo "- Disk present without FS signature: sector reads failing; compare bus_resets growth over ~10s."
    echo "- You can re-run this script to see if counters advance."
elif [ -z "$mnt" ]; then
    echo "- Ready but unmounted: run  udisksctl mount -b /dev/$dev  (or click it in the file explorer)."
else
    echo "- READY: $fstype mounted at $mnt"
fi
echo "=== end ==="
