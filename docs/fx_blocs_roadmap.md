# FX Blocs — roadmap & status

Living plan for the modular FX system. Engine coverage vs. the KDFX manual is in
`docs/kdfx_coverage.md`; this file is the push order and the rules.

## Where we are (2026-10-03, compressor adapter landed, build 11:50)

Working on device (user-confirmed): chain renders, blocs audible, routing, per-slot
selection via full-width pages, dynamic per-bloc params.

**User directive (2026-10-03): the KDFX engines are the most important work.**
Stock wrapping is a convenience; the manual's algorithms are the product.

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
| be80a78a6 | loop-gain runaways fixed (delay tape drive ×11.9 in-loop; reverb bass ×1.74) |
| d4fdc1e42 | param accuracy audit: wet read 50% low (unity pan 2^27), dead zones, dead params |
| (this push) | COMPRESSOR bloc live: stock RMSFeedbackCompressor wrapped insert-style (rule 0 — wrap only), THRSH/RATIO/ATK/REL/SIDE/BLEND with faithful curves |
| 785fd9096 | **param flush off-by-one fixed** — every edit had pushed the NEXT param id's cached value (the "edits do nothing, then snap" report); song-load apply had the same bug |

**Not yet hardware-verified:** everything from ecd715ad9 onward. Flash
`build/Release/deluge.bin` (11:57 or later).

## Push order (KDFX first)

### 0. Verification gate (blocks everything below)
Flash the latest build. In order:
- **Params must respond per encoder detent** (the off-by-one made them deferred
  until a neighbour edit); re-check the song reload too — loads applied staged
  params shifted by one id before the fix.
- LEVEL now reads ~2× louder at 100% — check overall gain staging still sane.
- Delay: FDBK high + DRV high → tail must always decay (kill = FDBK).
- Reverb: DECAY high + BASS high → no bass accumulation.
- Delay page 1 shows TIM L / TIM R / FDBK; chorus VOICE 3 and 5 now do things.
- COMPRESSOR bloc: audible pumping; THRSH/RATIO read in DB, ATK/REL in MS.
- Save → reboot → reload: chain + params + routing restored.

### 1. KDFX-native engines (top priority — user directive)
- **Distortion family 724–732**: Mono/Stereo Distortion, cabinet, TubeAmp chains —
  insert engines, no tanks; the tone engine's shaper modes are the seed.
- **Combination algorithms 700–723**: Chorus+Delay, Flange+Reverb, LaserVerb class —
  composed from existing engines inside one bloc.
- **KDFX filter bank 900–906**: Resonant, LFO Sweep, Env Follow, EQ Morpher as native
  engines (preferred over wrapping the stock FilterSet).
- Then: special FX 907–913 (Ring Mod first; Pitcher is the hard one), comp/gate
  refinements 950–957 on top of the adapter.

### 2. Track FX — milestone 2a: blocs on tracks
- FxBloc slots per track (kit rows are tracks; no special policy).
- PAU budget: global concurrent-tank cap (CPU is the real gate), optional per-track
  fairness cap, OOM → INSUFFICIENT_RAM popup + revert, EMPTY default = zero cost.
- UI: reuse the FX BLOCS pages, items target the context track instead of master.

### 3. Track FX — milestone 2b: track XML
- Per-output `<slot>` blocks in the song (same idiom as master).
- Load degrades silently to EMPTY when the budget can't fit; shown as EMPTY in menu.

### 4. Grid-shortcut removal
Only meaningful at track integration: static gold-knob/grid mappings for stock FX
params break once slots are re-selectable. Master-chain bloc params have no grid
registrations today, so nothing to remove yet.

### Parked
- FILTER as a stock FilterSet wrap — superseded by KDFX-native filter bank (1.3);
  the FILTER model stays hidden until then.
- Upstream `main` merge (141 conflicts) — deferred until green-lit.
- `docs/kdfx manual.pdf` — untracked; commit on request.
- KDFX Wet/Dry semantics for send blocs; fine encoder stepping in the visual menu.
- The old bloc-select wedge — presumed fixed by the OOB spray removal (432073ac1);
  `scripts/watch_scribble.sh` stands by if it recurs.

## Rules for every new engine (hard-won)

0. **Stock code is never edited — freeverb, mutable, the compressor, FilterSet.**
   They integrate only through their existing public API (`reverb::Base` setters,
   or adapter wrappers); exposure is menu-side only (ParamDef tables).
1. **Loop gain < 1 always**, and equal to the feedback control the user can see.
   No nonlinearity inside a feedback loop may raise small-signal gain above unity
   (delay tape drive bug). Multiplied path gains count (reverb bass bug).
2. **Wet level** flows through `setPanLevels` (unity = `kdfx::unityWetPan()` = 2^27,
   since mixWet computes wet × pan / 2^27).
3. **Never list a param the engine doesn't implement**, and derive every range so
   full travel reaches the engine's actual limit — no dead zones, no phantom columns.
4. **Never draw outside [0, OLED_MAIN_HEIGHT_PIXELS)** — canvas primitives don't clip.
5. Params write the UI-side cache only; engines are touched solely at the render
   boundary (`applyPending`).
6. New C++ objects stay out of `.sdram_bss` unless trivially constructible (see
   5a36d3f63 boot-fix rationale).
