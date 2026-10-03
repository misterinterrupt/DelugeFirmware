#!/usr/bin/env bash
# Watchpoint trap for the memory-poisoning scribble.
#
# Round 6/7: currentSong / soundEditor.currentParamManager were overwritten with
# Q31-arithmetic garbage (0x0f8xxxxx) — but watchpoints on those globals never fired,
# so the writer doesn't store to them directly (it writes THROUGH poisoned pointers).
#
# Round 9 gave us a victim that code NEVER legitimately writes -1 to:
# stemExport.numStemsExported / totalNumStemsToExport showed "-1 of -1" on the OLED
# ("Exported -1 of -1 clips" permanent popup, processStarted spontaneously true).
# Those fields are only ever assigned 0 or a positive count, so any write that puts
# 0xFFFFFFFF there IS the scribbler. Watching them should finally name the writer.
#
# How to use:
#   1. Power-cycle the Deluge, load your song, start it.
#   2. Run:  ./scripts/watch_scribble.sh
#   3. On the device: open the visual menu, FILTERS > LPF, hold KIT, turn SELECT.
#      Also worth trying: audition a kit row with the menu open, then edit a slot.
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
watch *(unsigned int *)&stemExport.numStemsExported
watch *(unsigned int *)&stemExport.totalNumStemsToExport
echo \nWatchpoints armed on stemExport counters. Reproduce on the device now.\n
set $n = 0
while $n < 40
  continue
  set $n = $n + 1
  echo \n\n=== WATCHPOINT STOP ===\n
  bt 12
  p stemExport.numStemsExported
  p stemExport.totalNumStemsToExport
  p stemExport.processStarted
  p scribbleCanaryTrips
  p currentSong
  p soundEditor.currentParamManager
  p/x $pc
  if (stemExport.numStemsExported == -1) || (stemExport.totalNumStemsToExport == -1)
    echo \n\n=== SCRIBBLER CAUGHT WRITING -1 ===\n
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

echo "Reproduce on the device now (visual menu -> FILTERS > LPF -> hold KIT -> turn SELECT; or audition a row then edit)..."
"$GDB" -q -x "$GDBCMD" "$ELF" >"$REPORT" 2>&1
STATUS=$?

kill "$SERVER_PID" 2>/dev/null
rm -f "$GDBCMD"

echo "Done. Report: $REPORT"
exit $STATUS
