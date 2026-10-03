# FX Blocs — roadmap & status

Living plan for the modular FX system. Engine coverage vs. the KDFX manual is in
`docs/kdfx_coverage.md`; this file is the push order and the rules.

## Where we are (2026-10-03, HEAD d4fdc1e42, build 11:44)

Working on device (user-confirmed): chain renders, blocs audible, routing, per-slot
selection via full-width pages, dynamic per-bloc params.

Landed tonight, in order:

| Commit | What |
|---|---|
| 432073ac1 | visual menu rows fitted to the 48-row panel (was drawing to row 68 + OOB .bss spray) |
| 79b666f26 | `VisualSlot::span`, full-width Bloc pages |
| e500e2d56 | params adapt to selected bloc; Chain page = ROUTE only |
| fa4c27a11 | flanger / phaser / tremolo / auto-pan engines; insert-engine path (`Base::isInsert`) |
| 59dfde219 | global reverb (~521KB) → SDRAM; internal RAM headroom restored |
| b67fd50dd | **master chain XML persistence** (done — not waiting on anything) |
| ecd715ad9 | dry-at-unity parallel summing; per-bloc LEVEL; extended suites on "Slot N+" pages |
| fdc942fee | faithful units/ranges (MS/HZ/DB/±%/×) from the engines' own formulas |
| be80a78a6 | loop-gain runaways fixed (delay tape drive ×11.9 in-loop; reverb bass ×1.74); delay page = TIM L/TIM R/FDBK |
| d4fdc1e42 | param accuracy audit: wet read 50% low (unity pan 2^27), reverb WIDTH dead zones, DECAY dead travel, chorus VOICE snap, freeverb phantom HPF |

**Not yet hardware-verified:** everything from ecd715ad9 onward (the 11:44 build
supersedes the 04:16 build that was flashed — flash `build/Release/deluge.bin` 11:44).

## Push order

### 0. Verification gate (blocks everything below)
Flash the 11:44 build. In order:
- LEVEL now reads ~2× louder at 100% — check overall gain staging still sane.
- Delay: FDBK high + DRV high → tail must always decay (kill = FDBK).
- Reverb: DECAY high + BASS high → no bass accumulation.
- Delay page 1 shows TIM L / TIM R / FDBK; chorus VOICE 3 and 5 now do things.
- Save → reboot → reload: chain + params + routing restored.
- Reverb WIDTH/DECAY sweep smoothly with no dead zones.

### 1. COMPRESSOR / FILTER adapters (master) — small, unblocks track parity
- Wrap stock compressor + filter as insert blocs on the proven `isInsert()` path.
- Un-hide them in the bloc list once real.
- Same push: remove gold-knob/grid shortcuts for modular FX slots (decided earlier:
  static param mappings break when slots are re-selectable).

### 2. Track FX — milestone 2a: blocs on tracks
- FxBloc slots per track (kit rows are tracks; no special policy).
- PAU budget: global concurrent-tank cap (CPU is the real gate), optional per-track
  fairness cap, OOM → INSUFFICIENT_RAM popup + revert, EMPTY default = zero cost.
- UI: reuse the FX BLOCS pages, items target the context track instead of master.

### 3. Track FX — milestone 2b: track XML
- Per-output `<slot>` blocks in the song (same idiom as master).
- Load degrades silently to EMPTY when the budget can't fit; shown as EMPTY in menu.

### 4. Manual backlog (see docs/kdfx_coverage.md for the full list)
Rough value order: distortion family 724–732 (insert path) → combination
algorithms 700–723 (Chorus+Delay class macros) → filter bank 900–906 → EQ tools.

## Rules for every new engine (hard-won)

0. **Freeverb and Mutable are stock code — never edit the engines themselves.**
   They integrate only through the `reverb::Base` setters they already implement
   (freeverb: SIZE/DAMP/WIDTH; mutable: SIZE/DAMP/WIDTH/HPF); exposure is menu-side
   only (ParamDef tables).

1. **Loop gain < 1 always**, and equal to the feedback control the user can see.
   No nonlinearity inside a feedback loop may raise small-signal gain above unity
   (delay tape drive bug). Multiplied path gains count (reverb bass bug).
2. **Wet level** flows through `setPanLevels` (unity = `kdfx::unityWetPan()` = 2^27,
   since mixWet computes wet × pan / 2^27).
3. **Never list a param the engine doesn't implement**, and derive display ranges so
   full travel hits the engine's real limit — no dead zones, no phantom columns.
4. **Never draw outside [0, OLED_MAIN_HEIGHT_PIXELS)** — canvas primitives don't clip.
5. Params write the UI-side cache only; engines are touched solely at the render
   boundary (`applyPending`).
6. New C++ objects stay out of `.sdram_bss` unless trivially constructible (see
   5a36d3f63 boot-fix rationale).

## Parked / open decisions

- Upstream `main` merge (141 conflicts) — deferred until green-lit.
- `docs/kdfx manual.pdf` — untracked; commit on request.
- KDFX Wet/Dry semantics for send blocs (LEVEL is wet-only today; manual separates
  Wet/Dry + In/Out gains) — revisit when a user need shows up.
- Fine encoder stepping (shift+select for decimal adjust) in the visual menu.
- The old bloc-select wedge / currentSong watchpoint — presumed fixed by the OOB
  spray removal (432073ac1); `scripts/watch_scribble.sh` stands by if it recurs.
