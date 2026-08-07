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

# Headless J-Link command file: load ELF, reset, run.
CMDFILE="$(mktemp)"
trap 'rm -f "$CMDFILE"' EXIT
cat > "$CMDFILE" <<EOF
loadfile $ELF
r
g
exit
EOF

echo "==> Flashing: $ELF"
# -device R7S721020: specify target device
# -if SWD         : target interface
# -speed 4000     : clock speed (kHz)
# -autoconnect 1  : connect to probe and target automatically
# -exitonerror 1  : abort on any error
# < /dev/null     : essential on Linux to prevent stdin pollution from interrupting autoconnect
JLinkExe -device R7S721020 -if SWD -speed 4000 -autoconnect 1 -exitonerror 1 -CommandFile "$CMDFILE" < /dev/null

echo "==> Done. Deluge should now be running the new firmware."

