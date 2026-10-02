#!/usr/bin/env bash
# Post-mortem crash capture for the visual-menu audio-death crash.
#
# How to use:
#   1. Flash build/Release/deluge.bin (the .elf next to it must stay untouched — it is what
#      this script symbolizes with, so do NOT rebuild between flashing and capturing).
#   2. Reproduce the crash on the device (e.g. synth clip playing, open visual menu,
#      Main Level page, tap KIT to latch PAN, turn SELECT until audio dies).
#      The firmware's fault handler parks the CPU in a spin loop, so the crash state survives.
#   3. With J-Link connected, run:  ./scripts/capture_crash.sh
#   4. Send back scripts/crash_report.txt
#
set -u

cd "$(dirname "$0")/.."

ELF="build/Release/deluge.elf"
GDB="toolchain/v16/linux-x86_64/arm-none-eabi-gcc/bin/arm-none-eabi-gdb"
GDBSERVER="${JLINK_SERVER:-/opt/SEGGER/JLink/JLinkGDBServer}"
DEVICE="${JLINK_DEVICE:-R7S721001}"
IFACE="${JLINK_IF:-swd}"
SPEED="${JLINK_SPEED:-30000}"
REPORT="scripts/crash_report.txt"

[ -x "$GDB" ] || { echo "gdb not found: $GDB" >&2; exit 1; }
[ -x "$GDBSERVER" ] || { echo "JLinkGDBServer not found: $GDBSERVER" >&2; exit 1; }
[ -f "$ELF" ] || { echo "elf not found: $ELF" >&2; exit 1; }

GDBCMD=$(mktemp)
cat >"$GDBCMD" <<'EOF'
set pagination off
set confirm off
target remote :2331
echo \n\n=== REGISTERS (abort-mode banked sp/lr point at the fault context) ===\n
info registers
echo \n\n=== BACKTRACE ===\n
bt
echo \n\n=== CODE AT PC ===\n
x/16iw $pc
echo \n\n=== CODE AT LR-8 (likely faulting site from abort LR) ===\n
x/8iw $lr-8
echo \n\n=== STACK (abort SP) ===\n
x/64wx $sp
echo \n\n=== EDITING CONTEXT GLOBALS ===\n
p currentSong
p soundEditor.currentParamManager
p soundEditor.currentSound
p soundEditor.currentModControllable
p soundEditor.currentSourceIndex
p deluge::gui::ui::visual_menu::visualMenuUI.selectedSlot_
p deluge::gui::ui::visual_menu::visualMenuUI.currentCategoryIndex_
p deluge::gui::ui::visual_menu::visualMenuUI.currentPageIndex_
p/x $cpsr
detach
quit
EOF

"$GDBSERVER" -device "$DEVICE" -if "$IFACE" -speed "$SPEED" -port 2331 -nogui >/dev/null 2>&1 &
SERVER_PID=$!
sleep 2

"$GDB" -q -x "$GDBCMD" "$ELF" >"$REPORT" 2>&1
STATUS=$?

kill "$SERVER_PID" 2>/dev/null
rm -f "$GDBCMD"

if [ $STATUS -eq 0 ]; then
	echo "Captured. Report: $REPORT"
else
	echo "gdb exited $STATUS; partial report may still be in $REPORT" >&2
fi
exit $STATUS
