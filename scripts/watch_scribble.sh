#!/usr/bin/env bash
# Watchpoint trap for the memory-poisoning scribbler (v4: interactive, clean exits).
#
# Captures so far:
#  - round 6/7: currentSong/currentParamManager poisoned with Q31 products
#    (currentSong=0x0f8110a0, deterministic across reboots); watchpoints on those
#    globals never fired -> writer stores THROUGH poisoned pointers.
#  - round 10: "Exported -1 of -1 clips" -> counters sprayed with 0xFFFFFFFF.
#  - round 12: processStarted byte = 255 with counters 0/0, audio task caught live
#    stuck in renderAudioForStemExport (the freeze). No export was started.
#
# v4 changes: the old batch loop blocked inside `continue`, so Ctrl+C just became
# another stop and the loop resumed the target again - nearly impossible to stop,
# and a hard kill could orphan the JLink server (port 2331) or leave the Deluge
# halted. This version drops you into a live gdb session: every stop is logged
# automatically (hook-stop) to the report AND shown live; you stay in control.
#
# How to use:
#   1. Power-cycle the Deluge, load your song, start it.
#   2. Run:  ./scripts/watch_scribble.sh
#      It attaches, arms watchpoints, resumes the target, and leaves you at a
#      (gdb) prompt.
#   3. Reproduce on the device: visual menu -> FILTERS > LPF -> hold KIT ->
#      turn SELECT; also audition a kit row with the menu open, then edit a slot.
#      Every watchpoint hit stops the CPU, logs pc/instruction/backtrace/
#      watchpoint old/new values, and shows them live.
#   5. To finish: type  q  then Enter (detaches and RESUMES the Deluge), or
#      Ctrl+C then q if mid-stop. The report is at scripts/watch_report.txt.
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

# Kill any stale server from a previous hard exit so port 2331 is free.
if pkill -f JLinkGDBServer 2>/dev/null; then
	echo "Killed a stale JLinkGDBServer from a previous run."
	sleep 1
fi

cleanup() {
	[ -n "${SERVER_PID:-}" ] && kill "$SERVER_PID" 2>/dev/null
}
trap cleanup EXIT INT TERM

# Resolve addresses from the on-disk elf up front. A missing symbol resolves to
# "SKIP" and every use of it is omitted from the generated gdb script.
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
	echo "set print thread-events off"
	echo "set logging file $REPORT"
	echo "set logging overwrite off"
	echo "set logging on"
	echo "target remote :2331"
	echo 'echo \n\n=== ARMING WATCHPOINTS ===\n'
	[ "$ADDR_PS" != SKIP ] && echo "watch *(unsigned char *)$ADDR_PS"
	[ "$ADDR_NUM" != SKIP ] && echo "watch *(unsigned int *)$ADDR_NUM"
	echo 'echo \nWatchpoints armed. Resuming target - reproduce on the device now.\n'
	# Log everything at every stop, live and into the report file.
	echo "define hook-stop"
	echo '  echo \n\n=== STOP ===\n'
	echo "  p/x \$pc"
	echo "  x/i \$pc"
	echo "  info breakpoints"
	echo "  bt 12"
	[ "$ADDR_PS" != SKIP ] && echo "  x/bx $ADDR_PS"
	[ "$ADDR_NUM" != SKIP ] && echo "  x/wx $ADDR_NUM"
	[ "$ADDR_TOTAL" != SKIP ] && echo "  x/wx $ADDR_TOTAL"
	[ "$ADDR_CANARY" != SKIP ] && echo "  x/wx $ADDR_CANARY"
	[ "$ADDR_SONG" != SKIP ] && echo "  x/wx $ADDR_SONG"
	echo "end"
	echo "continue"
	echo 'echo \n\ngdb script finished (target was stopped or interrupted).\nType  c  + Enter to resume the Deluge and keep watching, or  q  + Enter to detach and resume.\n'
} >"$GDBCMD"

"$GDBSERVER" -device "$DEVICE" -if "$IFACE" -speed "$SPEED" -port 2331 -nogui >/dev/null 2>&1 &
SERVER_PID=$!
sleep 2

cat <<'BANNER'

  Live watch session starting. On the device, reproduce now:
    visual menu -> FILTERS > LPF -> hold KIT -> turn SELECT
    (or audition a kit row with the menu open, then edit a slot)

  Every stop logs pc / instruction / watchpoint old+new / backtrace live
  and to scripts/watch_report.txt.
  - c + Enter  : resume the Deluge after a stop (keep watching)
  - Ctrl+C, then q + Enter : detach and resume the Deluge, finish up

BANNER

"$GDB" -q -x "$GDBCMD" "$ELF"
STATUS=$?

echo
echo "Done. Report: $REPORT"
exit $STATUS
