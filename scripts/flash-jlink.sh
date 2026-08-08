#!/bin/bash
#
# flash-jlink.sh — flash the built Deluge firmware to hardware via J-Link,
# fully headless (no GUI popups / dialogs).
#
#   ./scripts/flash-jlink.sh              # flashes build/Release/deluge.elf
#   ./scripts/flash-jlink.sh debug        # flashes build/Debug/deluge.elf
#   ./scripts/flash-jlink.sh /path/x.elf  # flashes an explicit ELF
#
# Requires JLinkExe on PATH (from scripts/setup-jlink-sudo.sh) and udev rule
# installed (passwordless USB access).
#
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

BUILD="${1:-release}"
case "$BUILD" in
    release|debug|relwithdebinfo)
        ELF="build/$(tr '[:lower:]' '[:upper:]' <<<"${BUILD:0:1}")${BUILD:1}/deluge.elf"
        ;;
    *)
        ELF="$BUILD"   # treat arg as an explicit path
        ;;
esac

if [ ! -f "$ELF" ]; then
    echo "ERROR: firmware not found: $ELF"
    echo "       Build first with: ./dbt build release"
    exit 1
fi

if ! command -v JLinkExe >/dev/null 2>&1; then
    echo "ERROR: JLinkExe not on PATH. Run scripts/setup-jlink-sudo.sh first."
    exit 1
fi

# Extract ELF entry point address using readelf if available
READELF="arm-none-eabi-readelf"
if ! command -v "$READELF" >/dev/null 2>&1; then
    READELF="$(dirname "$(which JLinkExe)")/../arm-none-eabi-gcc/bin/arm-none-eabi-readelf"
fi

ENTRY_HEX="0x2007b5c0"
if command -v "$READELF" >/dev/null 2>&1; then
    EXTRACTED="$("$READELF" -h "$ELF" 2>/dev/null | grep "Entry point address:" | awk '{print $4}')"
    if [ -n "$EXTRACTED" ]; then
        ENTRY_HEX="$EXTRACTED"
    fi
fi

# Headless J-Link command file: load ELF, set PC to entry point, run.
CMDFILE="$(mktemp)"
trap 'rm -f "$CMDFILE"' EXIT
cat > "$CMDFILE" <<EOF
device R7S721020
if SWD
speed 4000
connect
r
h
ms 100
loadfile $ELF
SetPC $ENTRY_HEX
g
exit
EOF

echo "==> Flashing: $ELF"
JLinkExe -autoconnect 1 -exitonerror 1 -CommandFile "$CMDFILE" < /dev/null

echo "==> Done. Deluge should now be running the new firmware."

