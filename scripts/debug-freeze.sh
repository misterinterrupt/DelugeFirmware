#!/bin/bash
#
# debug-freeze.sh — attach to a (frozen) Deluge via J-Link and turn the raw
# CPU state into symbols from your firmware ELF.
#
#   ./scripts/debug-freeze.sh                 # symbolize vs build/Release/deluge.elf
#   ./scripts/debug-freeze.sh debug           # vs build/Debug/deluge.elf
#   ./scripts/debug-freeze.sh relwithdebinfo  # vs build/RelWithDebInfo/deluge.elf
#   ./scripts/debug-freeze.sh path/to/x.elf   # vs an explicit ELF
#   Anywhere: --resume   resume the core after dumping, but ONLY if it was
#             halted in a normal context (SVC/SYS/USR). A core caught in
#             ABORT/UND/IRQ is left halted — resuming a faulted core just
#             re-triggers the fault loop.
#
# READ-ONLY: this script never writes to the target (no flash, no reset, no
# registers set). It halts the core to read state; unless --resume is given
# and safe, the core stays halted after the script exits.
#
# What it does, and why each step exists:
#
#   1. JLinkExe pass 1: connect (SWD) + halt + `regs`
#        -> full register dump, including the *banked* registers of every
#           CPU mode. On a crash you are usually halted in ABORT/UND mode,
#           and the interesting address is the banked LR (link register) of
#           the mode that faulted — not the current PC, which just points at
#           the fault handler spinning in a loop.
#
#   2. Decode the CPSR mode word
#        ABORT = data/prefetch abort (wild pointer, bad function pointer)
#        UND   = undefined instruction (jumped to garbage — classic corrupt
#                vtable / null-ish function pointer, e.g. 0xCA)
#        IRQ   = died inside an interrupt handler
#        SVC   = normal context — a hang/loop, not a CPU fault
#
#   3. Symbolize PC + every banked LR with addr2line -f -i -e <elf>
#        -> function name + file:line in this repo. -i follows inlined
#           frames, which this firmware uses heavily.
#
#   4. JLinkExe pass 2: mem32 a window below the USR stack pointer, keep any
#      words that land inside the ELF's .text section, symbolize those
#        -> a poor-man's backtrace: stale return addresses still sitting on
#           the stack tell you the call chain that led to the freeze.
#
# Requires: JLinkExe on PATH (see scripts/setup-jlink-sudo.sh) and an
# existing build (./dbt build release). Debug info is best in debug /
# relwithdebinfo builds, but release still symbolizes function names.
#
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

# Extract --resume anywhere in the args; everything else is positional.
RESUME=0
POSITIONAL=()
for arg in "$@"; do
    case "$arg" in
        --resume) RESUME=1 ;;
        *) POSITIONAL+=("$arg") ;;
    esac
done
set -- "${POSITIONAL[@]}"

BUILD="${1:-release}"
case "$BUILD" in
    release|debug|relwithdebinfo)
        ELF="build/$(tr '[:lower:][:upper:]' '[:upper:][:lower:]' <<<"${BUILD:0:1}")${BUILD:1}/deluge.elf"
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

# Locate the toolchain binaries (addr2line, readelf) next to the build
# toolchain shipped in ./toolchain, or on PATH if the toolchain was installed
# system-wide.
TCBIN=""
for candidate in "$(find toolchain -type f -name arm-none-eabi-addr2line 2>/dev/null | head -n1)" \
                 "$(command -v arm-none-eabi-addr2line || true)"; do
    if [ -n "$candidate" ] && [ -x "$candidate" ]; then
        TCBIN="$(dirname "$candidate")"
        break
    fi
done
if [ -z "$TCBIN" ]; then
    echo "ERROR: arm-none-eabi-addr2line not found (looked in ./toolchain and PATH)."
    exit 1
fi

# Where the raw dumps get saved so you can re-examine them later.
STAMP="$(date +%Y%m%d-%H%M%S)"
OUTDIR="build/debug-freeze"
mkdir -p "$OUTDIR"
REGS_FILE="$OUTDIR/regs-$STAMP.txt"
STACK_FILE="$OUTDIR/stack-$STAMP.txt"

# ---------------------------------------------------------------------------
# Pass 1: connect, halt, dump registers.
#   h       = halt the core (safe on a frozen device; halts a running one)
#   regs    = all registers, every banked mode
#   exit    = disconnect. Target stays halted; we do NOT reset or continue.
# ---------------------------------------------------------------------------
CMDFILE="$(mktemp)"
trap 'rm -f "$CMDFILE"' EXIT
cat > "$CMDFILE" <<EOF
device R7S721020
if SWD
speed ${JLINK_SPEED:-4000}
connect
h
regs
exit
EOF

echo "==> Attaching (read-only) and halting core..."
JLinkExe -autoconnect 1 -exitonerror 1 -CommandFile "$CMDFILE" < /dev/null \
    | tee "$REGS_FILE" >/dev/null

echo "==> Raw register dump saved to $REGS_FILE"

# ---------------------------------------------------------------------------
# Pass 2: read a window of the USR stack (address learned from pass 1),
# then hand both dumps to the python below for parsing + symbolication.
#   mem32 <addr>, <numBytes>   — displayed as 32-bit words
# ---------------------------------------------------------------------------
USR_SP=$(python3 - "$REGS_FILE" <<'PYEOF'
import re, sys
text = open(sys.argv[1]).read()
cur = None
for line in text.splitlines():
    if line.startswith("USR:"):
        cur = "USR"
        continue
    if cur == "USR":
        m = re.search(r"R13=([0-9A-Fa-f]+)", line)
        if m:
            print(m.group(1))
            break
PYEOF
)

if [ -n "${USR_SP:-}" ]; then
    # 1KB below SP: return addresses of current + recent callers live here.
    CMDFILE2="$(mktemp)"
    cat > "$CMDFILE2" <<EOF
device R7S721020
if SWD
speed ${JLINK_SPEED:-4000}
connect
h
mem32 0x${USR_SP}, 0x400
exit
EOF
    echo "==> Dumping 1KB of USR stack below SP (0x${USR_SP})..."
    JLinkExe -autoconnect 1 -exitonerror 1 -CommandFile "$CMDFILE2" < /dev/null \
        | tee "$STACK_FILE" >/dev/null
    rm -f "$CMDFILE2"
else
    STACK_FILE=""
    echo "==> (could not find USR SP in register dump; skipping stack scan)"
fi

# ---------------------------------------------------------------------------
# Parse + symbolize. Everything below is plain tooling on the host side.
# ---------------------------------------------------------------------------
python3 - "$ELF" "$TCBIN" "$REGS_FILE" "$STACK_FILE" <<'PYEOF'
import re, subprocess, sys

elf, tcbin, regs_path, stack_path = sys.argv[1:5]
addr2line = tcbin + "/arm-none-eabi-addr2line"
readelf = tcbin + "/arm-none-eabi-readelf"

def sym(addr):
    """addr -> 'function (file:line)' using the ELF's debug info."""
    a = int(addr, 16) & ~1          # strip Thumb bit that LR return addrs carry
    if a == 0:
        return None
    try:
        out = subprocess.run([addr2line, "-f", "-i", "-e", elf, hex(a)],
                             capture_output=True, text=True, timeout=10).stdout
    except Exception:
        return None
    lines = [l for l in out.splitlines() if l.strip()]
    # addr2line prints pairs: function, then file:line (repeated per inline frame)
    pairs = []
    for i in range(0, len(lines) - 1, 2):
        fn, loc = lines[i], lines[i + 1]
        if "??" in loc and fn in ("?", "??"):
            return None
        pairs.append((fn, loc))
    return " <- ".join(f"{fn} ({loc})" for fn, loc in pairs) if pairs else None

MODE_HINTS = {
    "ABORT": "data or prefetch abort — wild pointer / bad memory access",
    "ABT":   "data or prefetch abort — wild pointer / bad memory access",
    "UND":   "undefined instruction — jumped to garbage (corrupt vtable / null-ish fn ptr)",
    "IRQ":   "died inside an interrupt handler",
    "FIQ":   "died inside an FIQ handler",
    "SVC":   "normal context — a hang or loop, not a CPU fault",
    "SYS":   "normal context — a hang or loop, not a CPU fault",
    "USR":   "normal user context — a hang or loop, not a CPU fault",
}

regs = open(regs_path).read()
m = re.search(r"PC: \(R15\) = ([0-9A-Fa-f]+), CPSR = ([0-9A-Fa-f]+) \(([^)]*)\)", regs)
if not m:
    print("!! Could not parse register dump — see the raw file.")
    sys.exit(0)

pc, cpsr, modestr = m.group(1), m.group(2), m.group(3)
mode_word = modestr.split()[0].rstrip(",").upper()
print()
print("=" * 74)
print(f"CPU MODE : {modestr}")
hint = MODE_HINTS.get(mode_word) or MODE_HINTS.get(mode_word.rstrip("T"))
if not hint and mode_word.startswith("SYSTEM"):
    hint = MODE_HINTS["SYS"]
if hint:
    print(f"          {hint}")
print("=" * 74)

# Collect the banked R13/R14 pairs. The register dump prints blocks like
#   ABT: R13=2002FFD0, R14=000000CB, SPSR=280E019B
banked = {}
cur = "CUR"
for line in regs.splitlines():
    lm = re.match(r"\s*(USR|FIQ|IRQ|SVC|ABT|UND|SYS):", line)
    if lm:
        cur = lm.group(1)
        continue
    rm = re.search(r"R13=([0-9A-Fa-f]+), R14=([0-9A-Fa-f]+)", line)
    if rm:
        banked.setdefault(cur, []).append((rm.group(1), rm.group(2)))

print(f"\nPC  = 0x{pc}")
s = sym(pc)
print(f"      {s}" if s else "      (no symbol)")

# Symbolize every banked LR. On a fault caught by the handler, the faulting
# context's LR is the single most valuable address in the dump.
print("\nBanked link registers (LR = where this mode would return to):")
for mode in ["CUR", "USR", "ABT", "UND", "IRQ", "FIQ", "SVC", "SYS"]:
    for sp, lr in banked.get(mode, []):
        s = sym(lr)
        tag = f"{mode:>3} LR = 0x{lr}"
        if s:
            print(f"  {tag}  {s}")

# Optional: stack scan for return addresses (poor-man's backtrace).
if stack_path and len(stack_path) > 0:
    try:
        elf_sections = subprocess.run([readelf, "-SW", elf],
                                      capture_output=True, text=True, timeout=10).stdout
    except Exception:
        elf_sections = ""
    tm = re.search(r"\.text\s+PROGBITS\s+([0-9a-f]+)\s+[0-9a-f]+\s+([0-9a-f]+)", elf_sections)
    if tm:
        text_lo = int(tm.group(1), 16)
        text_hi = text_lo + int(tm.group(2), 16)
        words = []
        for line in open(stack_path):
            mm = re.match(r"\s*([0-9A-Fa-f]+) = (.*)", line)
            if mm:
                for w in mm.group(2).split():
                    if re.fullmatch(r"[0-9A-Fa-f]{8}", w):
                        words.append(int(w, 16))
        seen, frames = set(), []
        for w in words:
            if text_lo <= w < text_hi and w | 1 not in seen:
                s = sym(hex(w))
                if s:
                    seen.add(w | 1)
                    frames.append((w, s))
        if frames:
            print("\nReturn addresses found on the USR stack (innermost first):")
            for w, s in frames[:20]:
                print(f"  0x{w:08X}  {s}")
        else:
            print("\n(No symbolizable return addresses found in the stack window)")
print()
print(f"Raw dumps kept in: {regs_path}" + (f" and {stack_path}" if stack_path else ""))
resume_safe = mode_word.startswith("SYSTEM") or mode_word in ("SYS", "SVC", "USR")
with open("build/debug-freeze/last-status.txt", "w") as f:
    f.write("RESUME_SAFE: yes\n" if resume_safe else "RESUME_SAFE: no\n")
if resume_safe:
    print("RESUME_SAFE: yes   (normal context — safe to continue)")
else:
    print("RESUME_SAFE: no    (fault context — core must stay halted)")
print("Core left halted. Power-cycle the Deluge to recover." if not resume_safe else "Core left halted (resume available via --resume).")
PYEOF

# ---------------------------------------------------------------------------
# Optional resume pass: only when --resume was given AND the python analysis
# said the halted context was a normal (non-fault) one.
# ---------------------------------------------------------------------------
if [ "$RESUME" -eq 1 ]; then
    if grep -q "RESUME_SAFE: yes" build/debug-freeze/last-status.txt 2>/dev/null; then
        echo "==> --resume: resuming core (normal context detected)..."
        CMDFILE3="$(mktemp)"
        cat > "$CMDFILE3" <<EOF
device R7S721020
if SWD
speed ${JLINK_SPEED:-4000}
connect
h
g
exit
EOF
        JLinkExe -autoconnect 1 -exitonerror 1 -CommandFile "$CMDFILE3" < /dev/null >/dev/null
        rm -f "$CMDFILE3"
        echo "==> Core resumed. Deluge should carry on where it left off."
    else
        echo "==> --resume: NOT resuming — core is in a fault context (or status unknown)."
        echo "    Power-cycle the Deluge to recover."
    fi
fi
