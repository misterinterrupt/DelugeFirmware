#!/usr/bin/env bash
# Watch for the Deluge USB disk to enumerate and gain a filesystem signature.
#
# Polls every couple of seconds. The moment the disk appears it notifies you;
# when a filesystem signature shows up it notifies again and (with --mount)
# tries to mount it via udisks. Keeps running until a filesystem is mounted
# or Ctrl-C. No J-Link use, so it is free to run continuously.
#
# Usage: scripts/diskmode-watch.sh [--mount]

set -u

VID=16d0
PID=0ce3
INTERVAL=2
AUTOMOUNT=0
[ "${1:-}" = "--mount" ] && AUTOMOUNT=1

notify() {
    echo "[watch] $1"
    command -v notify-send >/dev/null 2>&1 && notify-send "Deluge disk mode" "$1"
}

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

fs_of() {
    udevadm info -q property -n "/dev/$1" 2>/dev/null | sed -n 's/^ID_FS_TYPE=//p'
}

mountpoint_of() {
    lsblk -no MOUNTPOINT "/dev/$1" 2>/dev/null | grep -v '^$' | head -1
}

echo "[watch] polling every ${INTERVAL}s for $VID:$PID (Ctrl-C to stop)"
notified_enum=0
notified_fs=""

while true; do
    dev=$(find_deluge_sd || true)

    if [ -z "${dev:-}" ]; then
        if [ "$notified_enum" = "1" ]; then
            notify "Disk disappeared"
            notified_enum=0
            notified_fs=""
        fi
        sleep "$INTERVAL"
        continue
    fi

    if [ "$notified_enum" = "0" ]; then
        size=$(cat "/sys/block/$dev/size" 2>/dev/null || echo ?)
        notify "Deluge enumerated as /dev/$dev ($((size / 2048)) MiB)"
        notified_enum=1
    fi

    fstype=$(fs_of "$dev")
    if [ -n "$fstype" ] && [ "$notified_fs" != "$fstype" ]; then
        notified_fs="$fstype"
        notify "Filesystem ready: $fstype on /dev/$dev"
        if [ "$AUTOMOUNT" = "1" ]; then
            mnt=$(udisksctl mount -b "/dev/$dev" 2>&1)
            echo "[watch] mount attempt: $mnt"
            case "$mnt" in
                *Mounted*|*"is already mounted"*) notify "Mounted - you are good"; exit 0 ;;
            esac
        fi
    fi

    mnt=$(mountpoint_of "$dev")
    if [ -n "$mnt" ]; then
        notify "Mounted at $mnt"
        exit 0
    fi

    sleep "$INTERVAL"
done
