# FX Blocs — roadmap & status

Living plan for the modular FX system. Engine coverage vs. the KDFX manual is in
`docs/kdfx_coverage.md`; this file is the push order and the rules.

## Where we are (2026-10-03, compressor adapter landed, build 11:50)

Working on device (user-confirmed): chain renders, blocs audible, routing, per-slot
selection via full-width pages, dynamic per-bloc params.

**User directive (2026-10-03): the KDFX engines are the most important work.**
Stock wrapping is a convenience; the manual's algorithms are the product.

**User directive (2026-10-09): the end state is ALL effects as blocs.** The synth
engine keeps its osc/sampler sources and the signal path; every original effect
(reverb done, then delay, mod FX, EQ/filters, compressor, distortion) becomes a
module in the FX BLOCS chains and the stock implementations go away. **No legacy
support from here on** — old songs keep loading (unknown tags/params are skipped
by the readers) but stock-effect settings in them are ignored. Modulation
routing to FX params and some MIDI features will break mid-migration; that is
accepted. The low-level machinery (SDRAM management, lazy engine allocation,
render windows, sequencer) is what stays hard and stays.

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
| 380bd4268 | **reverb trio in real units** (SIZE metres, DECAY RT60 seconds, DAMP Hz) + **dynamic param pages**: tables variable-length (no 8 cap), pages = ceil(params/4) per bloc, rebuilt on selection; reverb gains AIR, delay gains WIDTH + PHASE, tone gains MFREQ/TFREQ, chorus gains HPF |
| 98a55adfe | **fine controls (FX items only, stock untouched)**: pads forwarded in song view (panel stays live), select-click cycles decimal position, value space at 1/100 display step; FxBloc::tFromReal inverse curves |
| afbac6d06 / e4dc480b7 | **KDFX DIST** (724-728) and **KDFX TUBEAMP** (729-732, first combination, CFG order swap; Mod* ids + uint64 dirty mask) |
| ebf8fd6ad | track-send FxChain groundwork banked (in flight, unverified) + manual-faithfulness: reverb DECAY = exponential RT60 0.2-20.0 S with INF only at the top detent (engine g = e^(-0.594/RT60)); delay FDBK reaches a true 100% (manual: sustains); KDFX manual PDF tracked |
| 28a5af24b / 19b290a77 | **KDFX LASER** (911-913: widening-spaces impulse train, TIME 0-2S/SPAC/CONT incl. reverse, FDBK 100%) + **six pair combinations** via one generic pair engine (CHO+DLY, CHO+REV, FLG+DLY, FLG+REV, FLG+LASER, SHPR+REV; CFG where the manual's <> allows) |
| ebf8fd6ad | reverb DECAY = exponential RT60 0.2-20.0 S with INF at top detent (engine g = e^(-0.594/RT60)); delay FDBK true 100% (manual: sustains); track-send FxChain groundwork banked (WIP, unverified); KDFX manual PDF tracked |
| 24582afb7 / 45365a05d | **filter bank whole**: RESFILT 903 + ENVFILT 900 + LFOSWP 902 complete (TPT SVF; tempo push via kSystemTempoBPM per render block; RES self-ring at 50 DB top only) + **RINGMOD 907** (L*R & 5-osc modes, generic setRmOsc) + **QUANT** (DYN/HEAD/DC) + **QNT+FLG 714** |
| 347f6bcb7 / b2f702ed5 | quantizer + QNT+FLG pair |
| ebf8fd6ad ( groundwork note) | **MILESTONE 2 STRUCTURALLY COMPLETE**: per-track FxChains (Sound + GlobalEffectable) render + persist XML; FX BLOCS pages resolve chain by context (getCurrentFxChain); GLOBAL_SEND track bloc (BUS/PAN/IN-GN/LEVEL) feeds master per-slot send buses; stock reverb send zeroed in favour of the new model — ALL pending device verification |
| 6d6f433f1 | bloc selector auto-deselect after one change fixed (rebuild no longer wipes held slots); RT60 DECAY capped at ~1 S and compressor RATIO at 1.0 fixed — ParamDef ranges are display units, engine conversions live in the curves |
| d404bfa66 | **regression fixes + coarse/fine swap**: percent params had collapsed to 0-1 (LEVEL read 0%/1%) — explicit 0-100 ranges; unshifted = coarse nice-number steps (span in ~100 detents), shift = fine; compact readouts (7.0K, 1480) never truncated; manual ch.5 parameter audit table in docs/kdfx_coverage.md |
| (this push) | **Track reverb shortcuts removed**; **Per-bloc IN-GN (input gain)** (-96dB/-INF to +3dB, default 0dB) on all blocs; **MST SEND bloc** (`GLOBAL_SEND`) to send track audio into master FX busses (BUS 1/2/3) across all routing topologies |
| a6942867e | **stock reverb UI retired** (same commit as the engine deletion below): `reverbMenu` (per-sound), `globalReverbMenu` (audio-clip / song view / kit global FX / performance view), the reverb sidechain submenu, and the reverb cells in the four pad shortcut tables deleted. Chain-side reverb (FREEVERB / MUTABLE / KDFX_REVERB) carries LEVEL + IN-GN as the amount |
| a6942867e | **stock reverb ENGINE deleted** (2026-10-09 directive: no legacy support): `AudioEngine::reverb` (~521KB SDRAM tank, bss −525KB), `renderReverb`, the `reverbBuffer` send buffer and its threading through Song/Clip/Output/Kit/Sound render signatures, `reverbAmountAdjust`, `GLOBAL_REVERB_AMOUNT` + `UNPATCHED_REVERB_SEND_AMOUNT` params (XML is name-keyed, old files skip cleanly), reverb sidechain globals + `updateReverbParams` + the `getThingWithMostReverb` machinery, reverb preset cycling (gold knobs), song `<reverb>` XML section, stock `dsp::Reverb` class + the old reverb menu_item headers. Kept: `GLOBAL_VOLUME_POST_REVERB_SEND` (sidechain duck tap), per-sound sidechain, `getMasterFxParamsFromSong` (renamed from getReverbParamsFromSong, now master-FX-only). |
| 4d050efd1 | **stock delay ENGINE deleted** — same playbook: `Delay` class (kept `DelayBuffer`, the stutterer's own buffer), `Delay::State` + `shouldLimitDelayFeedback` threading out of every render signature, `GLOBAL_DELAY_RATE`/`FEEDBACK` + `UNPATCHED_DELAY_RATE`/`AMOUNT` params, ping-pong/analog/sync + their gold-knob click actions, per-sound `delayMenu` + master `globalDelayMenu`, all table rows (automation 59→57 / 25→23, perf 19→17), delay XML. Delay = KDFX_DELAY / CHO_DLY / FLG_DLY blocs now. **Conversion gotcha: GCC C++ designated-init requires gap-free indices — a removed enum member that was an anchor (e.g. FIRST_GLOBAL_NON_VOLUME) leaves a hole; fill anchor slots with an explicit empty entry in param.cpp's name tables.** Next conversions: mod FX, EQ (filters), crush/tone (comp already has a bloc wrapper) |

**Not yet hardware-verified:** everything from ecd715ad9 onward. Flash
`build/Release/deluge.bin` (22:00 or later).

## Push order (KDFX first)

### 0. Verification gate (blocks everything below)
Flash the latest build. In order:
- **Params must respond per encoder detent** (the off-by-one made them deferred
  until a neighbour edit); re-check the song reload too — loads applied staged
  params shifted by one id before the fix.
- **Fine controls**: pads and the rest of the panel stay live with the FX menu
  open; unshifted turning = coarse (nice-number steps spanning the range in ~100
  detents), shift held = fine (base step + select-click decimals to 0.01); readouts
  compact instead of squashed (7.0K, -24DB, 1480); LEVEL and all percent params
  cover their full 0-100 range again.
- **Reverb reads metres / RT60 seconds / Hz**; every bloc's param pages match its
  full suite (reverb 3 pages, delay 3, tone 3; tremolo 1) and re-selecting a bloc
  reflows the pages live.
- LEVEL now reads ~2× louder at 100% — check overall gain staging still sane.
- Delay: FDBK high + DRV high → tail must always decay (kill = FDBK).
- Reverb: DECAY high + BASS high → no bass accumulation.
- Delay page 1 shows TIM L / TIM R / FDBK; chorus VOICE 3 and 5 now do things.
- COMPRESSOR bloc: audible pumping; THRSH/RATIO read in DB, ATK/REL in MS.
- Save → reboot → reload: chain + params + routing restored.

### 1. KDFX-native engines (top priority — user directive)
- **Distortion family 724–732**: Mono/Stereo Distortion, cabinet, TubeAmp chains —
  insert engines, no tanks; the tone engine's shaper modes are the seed.
- **KDFX filter bank 900–906**: Resonant, Envelope, Triggered, LFO Sweep filters as
  native insert engines (Freq 16-8372 HZ, Res 0-50 DB; Env adds sweep ±100% + rates
  0-300 DB/S per the manual).
- Then: special FX (Ring Modulator 907, quantizer for 714, Pitcher 908 — the hard one),
  comp/gate refinements 950-957, reverb voicings, studio tools.

### 2. Track FX — milestone 2a/2b: BANKED, awaiting device verification
Per-track FxChains (3 slots each, all engines), FX BLOCS pages resolving the chain
by context, per-track `<fxChain>` XML, and the GLOBAL_SEND track bloc feeding the
master chain's per-slot send buses. What remains: device verification of the whole
send path, then the PAU budget gate (OOM → popup when the tank pool is spent) —
the accounting was designed, not yet enforced at the track layer.

### 3. Verification-gate additions for track FX
- Track chain: synth/kit/audio track → FX BLOCS pages now edit THAT track; song
  level still edits master.
- GLOBAL_SEND bloc in a track slot + reverb bloc in a master slot → track audio
  reaches the master reverb; BUS/PAN/LEVEL behave.
- Save/load round-trips a track chain (per-track `<fxChain>`).

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
