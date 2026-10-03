#!/usr/bin/env bash
# Watchpoint trap for the memory-poisoning scribbler (v3: un-killable).
#
# Captures so far:
#  - round 6/7: currentSong/currentParamManager poisoned with Q31 products
#    (currentSong=0x0f8110a0, deterministic across reboots); watchpoints on those
#    globals never fired -> writer stores THROUGH poisoned pointers.
#  - round 10: "Exported -1 of -1 clips" -> counters sprayed with 0xFFFFFFFF.
#  - round 12: processStarted byte = 255 (0xFF) with counters 0/0, audio task
#    captured live stuck in renderAudioForStemExport (the freeze). NO export was
#    started by the user; the state was forged by the spray.
#
# This version:
#  - watches stemExport.processStarted (the byte the round-12 spray hit) and
#    stemExport.numStemsExported - code only ever writes 0/1 and 0/positive there.
#  - resolves all addresses up front from the on-disk elf and inlines raw hex into
#    the gdb script, so a missing/renamed symbol can no longer abort the capture
#    (that killed rounds 7 and 12 after their first stop).
#  - dumps pc/instruction/backtrace/breakpoint-hit-counts at EVERY stop, up to 60.
#
# How to use:
#   1. Power-cycle the Deluge, load your song, start it.
#   2. Run:  ./scripts/watch_scribble.sh   (attaches, arms, resumes)
#   3. Reproduce: visual menu -> FILTERS > LPF -> hold KIT -> turn SELECT;
#      also audition a kit row with the menu open, then edit a slot.
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

# Resolve addresses from the on-disk elf up front. A missing symbol resolves to
# the string "SKIP" and every use of it is omitted from the generated gdb script.
resolve() {
	local expr="$1"
	local out
	out=$("$GDB" --batch -ex "p/x $expr" "$ELF" 2>/dev/null | grep -o '0x[0-9a-fA-F]*' | head -1)
	if [ -n "$out" ]; then printf '%s' "$out"; else printf 'SKIP'; fi
}

ADDR_PS=$(resolve '&stemExport.processStarted')
ADDR_NUM=$(resolve '&stemExport.numStemsExported')
ADDR_TOTAL=$(resolve '&stemExport.totalNumStemsToExport')
ADDR_CANARY=$(resolve '&scribbleCanaryTrips')
ADDR_SONG=$(resolve '&currentSong')

{
	echo "=== ADDRESS RESOLUTION (from $ELF) ==="
	echo "stemExport.processStarted : $ADDR_PS"
	echo "stemExport.numStemsExported : $ADDR_NUM"
	echo "stemExport.totalNumStemsToExport : $ADDR_TOTAL"
	echo "scribbleCanaryTrips : $ADDR_CANARY"
	echo "currentSong : $ADDR_SONG"
	toolchain/v16/linux-x86_64/arm-none-eabi-gcc/bin/arm-none-eabi-nm "$ELF" 2>/dev/null | grep -q scribbleCanaryTrips \
		|| echo "WARNING: on-disk elf lacks scribbleCanaryTrips - it may NOT match the flashed build; symbolize with care"
} >"$REPORT"

GDBCMD=$(mktemp)
{
	echo "set pagination off"
	echo "set confirm off"
	echo "set can-use-hw-watchpoints 1"
	echo "target remote :2331"
	echo 'echo \n\n=== ARMING WATCHPOINTS ===\n'
	[ "$ADDR_PS" != SKIP ] && echo "watch *(unsigned char *)$ADDR_PS"
	[ "$ADDR_NUM" != SKIP ] && echo "watch *(unsigned int *)$ADDR_NUM"
	echo 'echo \nWatchpoints armed. Reproduce on the device now.\n'
	echo "set \$n = 0"
	echo "while \$n < 60"
	echo "  continue"
	echo "  set \$n = \$n + 1"
	echo '  echo \n\n=== WATCHPOINT STOP ===\n'
	echo "  p/x \$pc"
	echo "  x/i \$pc"
	echo "  info breakpoints"
	echo "  bt 12"
	[ "$ADDR_PS" != SKIP ] && echo "  x/bx $ADDR_PS"
	[ "$ADDR_NUM" != SKIP ] && echo "  x/wx $ADDR_NUM"
	[ "$ADDR_TOTAL" != SKIP ] && echo "  x/wx $ADDR_TOTAL"
	[ "$ADDR_CANARY" != SKIP ] && echo "  x/wx $ADDR_CANARY"
	[ "$ADDR_SONG" != SKIP ] && echo "  x/wx $ADDR_SONG"
	# Catch condition uses raw addresses only - no symbol resolution to fail on.
	if [ "$ADDR_NUM" != SKIP ] && [ "$ADDR_TOTAL" != SKIP ]; then
		echo "  if (*(int *)$ADDR_NUM == -1) || (*(int *)$ADDR_TOTAL == -1)"
		echo '    echo \n\n=== SCRIBBLER CAUGHT WRITING -1 ===\n'
		echo "    bt"
		echo "    info registers"
		echo "    x/12iw \$pc-8"
		echo "    p/x \$cpsr"
		echo "    loop_break"
		echo "  end"
	fi
	if [ "$ADDR_PS" != SKIP ]; then
		echo "  if (*(unsigned char *)$ADDR_PS == 255)"
		echo '    echo \n\n=== SCRIBBLER CAUGHT WRITING 0xFF TO processStarted ===\n'
		echo "    bt"
		echo "    info registers"
		echo "    x/12iw \$pc-8"
		echo "    p/x \$cpsr"
		echo "    loop_break"
		echo "  end"
	fi
	echo "end"
	echo 'echo \n\n=== FINAL STATE (hit limit or catch) ===\n'
	echo "bt"
	echo "info registers"
	echo "x/12iw \$pc-8"
	echo "detach"
	echo "quit"
} >"$GDBCMD"

"$GDBSERVER" -device "$DEVICE" -if "$IFACE" -speed "$SPEED" -port 2331 -nogui >/dev/null 2>&1 &
SERVER_PID=$!
sleep 2

echo "Reproduce on the device now (visual menu -> FILTERS > LPF -> hold KIT -> turn SELECT; or audition a row then edit)..."
"$GDB" -q -x "$GDBCMD" "$ELF" >>"$REPORT" 2>&1
STATUS=$?

kill "$SERVER_PID" 2>/dev/null
rm -f "$GDBCMD"

echo "Done. Report: $REPORT"
exit $STATUS
