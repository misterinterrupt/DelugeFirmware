#!/usr/bin/env bash
# Watchpoint trap for the pointer-poisoning scribble.
#
# The crash report showed currentSong / soundEditor.currentParamManager /
# currentSound overwritten with Q31-arithmetic garbage (0x0f8xxxxx). This script arms
# hardware watchpoints on those globals and catches the exact writing instruction.
#
# How to use:
#   1. Power-cycle the Deluge (clear the parked fault state), load your song, start it.
#   2. Run:  ./scripts/watch_scribble.sh
#      (it attaches, halts the CPU briefly, arms watchpoints, then resumes)
#   3. On the device: open the visual menu, go to FILTERS > Low-Pass Filter,
#      tap KIT to latch RESONANCE, and turn SELECT until the trap fires
#      (the script exits on its own and writes the report).
#   4. Send back scripts/watch_report.txt
#
set -u

cd "$(dirname "$0")/.."

ELF="build/Release/deluge.elf"
GDB="toolchain/v16/linux-x86_64/arm-none-eabi-gcc/bin/arm-none-eabi-gdb"
GDBSERVER="${JLINK_SERVER:-/opt/SEGGER/JLink/JLinkGDBServer}"
DEVICE="${JLINK_DEVICE:-R7S721001}"
IFACE="${JLINK_IF:-swd}"
SPEED="${JLINK_SPEED:-30000}"
REPORT="scripts/watch_report.txt"

[ -x "$GDB" ] || { echo "gdb not found: $GDB" >&2; exit 1; }
[ -x "$GDBSERVER" ] || { echo "JLinkGDBServer not found: $GDBSERVER" >&2; exit 1; }
[ -f "$ELF" ] || { echo "elf not found: $ELF" >&2; exit 1; }

GDBCMD=$(mktemp)
cat >"$GDBCMD" <<'EOF'
set pagination off
set confirm off
set can-use-hw-watchpoints 1
target remote :2331
echo \n\n=== ARMING WATCHPOINTS ===\n
watch *(unsigned int *)&currentSong
watch *(unsigned int *)&soundEditor.currentParamManager
echo \nWatchpoints armed. Now reproduce on the device (menu -> FILTERS -> LPF -> KIT/RESO -> turn SELECT).\n
set $n = 0
while $n < 40
  continue
  set $n = $n + 1
  echo \n\n=== WATCHPOINT STOP ===\n
  bt 10
  p currentSong
  p soundEditor.currentParamManager
  p soundEditor.currentSound
  if ((unsigned long) soundEditor.currentParamManager > 0x08000000) && ((unsigned long) soundEditor.currentParamManager < 0x20000000)
    echo \n\n=== POISON WRITER CAUGHT ===\n
    bt
    info registers
    x/12iw $pc-8
    p/x $cpsr
    loop_break
  end
end
echo \n\n=== FINAL STATE (hit limit or trap) ===\n
bt
info registers
x/12iw $pc-8
detach
quit
EOF

"$GDBSERVER" -device "$DEVICE" -if "$IFACE" -speed "$SPEED" -port 2331 -nogui >/dev/null 2>&1 &
SERVER_PID=$!
sleep 2

echo "Reproduce the crash on the device now (FILTERS > LPF > KIT latches RESO > turn SELECT)..."
"$GDB" -q -x "$GDBCMD" "$ELF" >"$REPORT" 2>&1
STATUS=$?

kill "$SERVER_PID" 2>/dev/null
rm -f "$GDBCMD"

echo "Done. Report: $REPORT"
exit $STATUS
