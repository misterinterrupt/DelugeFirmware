# SYNC — oscillator sync expansion, phase 1 (mode + phase target)

Implementation spec for the fx-blocs firmware. Read together with
`docs/sync_synthesis_report.md` (the technique survey and roadmap) and
`docs/fx_blocs_roadmap.md` (house rules). This covers **report items 1 and 2**;
items 3–5 (sync matrix, windowed sample sync, phase-distortion osc type) stay
spec'd only at report level until this lands and soaks.

**Status: IMPLEMENTED** (pending on-device test pass — see test plan).

## What exists today (grounding audit)

- Sync is a single bool `Sound::oscillatorSync` (`sound.h:143`), menu item
  `osc::Sync` (a `Toggle`, `gui/menu_item/osc/sync.h`) living on osc B
  (`whichThing == 1`), XML attribute `oscillatorSync` (`sound.cpp:3031/3361`).
- Only A→B, one direction: `renderBasicSource` passes
  `(s == 1) && doingOscSync` (`voice.cpp:1204`), master phase from
  `unisonParts[u].sources[0].oscPos` (`voice.cpp:1104`).
- The reset target is the **slave's own retrigger phase**
  (`sound->oscRetriggerPhase[s]` → `retriggerPhase` in `Voice::renderOsc`),
  with sub-sample-accurate placement:
  `phase = (masterSubPhase × slaveInc × resetterDivideByPhaseIncrement) + 1 + retriggerPhase`.
- PW on non-square oscs is *self*-sync through the same machinery
  (`voice.cpp:2873`–`2918`): `renderOsc` sets `doOscSync = true` internally and
  fakes a resetter at its own phase/increment. **This path must keep exact
  HARD semantics** — pulse width depends on the reset.
- Reset sites (every place the master-wrap condition
  `resetterPhaseNow < resetterPhaseIncrement` is acted on):
  1. triangle low-rate loop — `voice.cpp:2987`
  2. crude saw loop — `voice.cpp:3108`
  3. crude square loop — `voice.cpp:3165`
  4. `RENDER_OSC_SYNC` macro crossover block — `render_wave.h:59`–`84`,
     used by four call sites: general table + pulse-table (`voice.cpp`),
     wavetable multi-cycle + single-cycle (`wave_table.cpp:1126/1154`)
- Wavetable sync runs the same macro inside `WaveTable::render`
  (`wave_table.h:46`), so macro-level mode handling reaches wavetables for free.
- Patched params flow generically: `patcher.cpp` buckets by enum section
  (volume / linear / hybrid / exp), so a new linear param needs no patcher edit.
  XML names round-trip through `paramNameForFileConst` / `fileStringToParam`,
  guarded by `static_assert(validateParams())` (`param.cpp:485`).

## Design

### 1. Sync mode — `OscSyncMode` replaces the bool

`definitions_cxx.hpp` (next to `OscType`):

```cpp
enum class OscSyncMode : uint8_t {
	OFF     = 0,
	HARD    = 1, // current behavior, sample-accurate reset + crossfade
	SOFT    = 2, // reset only if the slave is already near the reset target
	REVERSE = 3, // flip slave direction at the master wrap; never reset phase
};
```

- `Sound::oscillatorSync` (bool) → `Sound::oscSyncMode` (`OscSyncMode`).
  `renderingOscillatorSyncCurrently/Ever` become `oscSyncMode != OscSyncMode::OFF`.
- **Persistence, zero legacy code**: keep the XML attribute name
  `oscillatorSync`, write the enum value as an int. Old files wrote `1` for on —
  which *is* `HARD`. Load: read the int, clamp 0–3. Save: write when
  `s == 1 && mode != OFF`, matching the current conditional write.
- Menu `osc::Sync` becomes a `Selection` with options OFF / HARD / SOFT / REV,
  same `isRelevant` (osc B, not FM, neither source a SAMPLE).

**SOFT** — at a master wrap, apply the HARD reset only if the slave phase is
within **1/4 cycle (0x40000000)** *ahead* of the reset target:
`(uint32_t)(phaseNow - resetTarget) < 0x40000000`. Otherwise the reset is
skipped entirely: no discontinuity, no crossfade, slave free-runs. Result:
frequency locking that comes and goes with phase drift, far fewer upper
harmonics than HARD.

**REVERSE** — at a master wrap, negate the slave phase increment; phase itself
is continuous (no reset, no crossfade, no crossover-sample redo). The direction
persists across render blocks as `VoiceUnisonPartSource::oscSyncDirection`
(int8_t, +1/−1, reset to +1 on note-on), threaded into `renderOsc` as a
pointer arg (`nullptr` from the phase-advance-only call site at
`voice.cpp:1333`). Pitch stays stable; the timbre is the alternation itself.
`getTableNumber` / band selection read magnitude only, so a reversed increment
keeps the same band — correct, since reversal adds no harmonics.

**PW self-sync forces HARD**: `renderOsc` captures
`bool genuineSync = doOscSync` on entry (before the PW block can flip
`doOscSync`); the effective mode is
`genuineSync ? sound->oscSyncMode : OscSyncMode::HARD`, and the direction
pointer is only honored when `genuineSync`. Same rule at the
`WaveTable::render` call, which also serves PW-on-wavetable.

### 2. Sync phase — a patched (modulatable) reset-target offset

New param `LOCAL_SYNC_PHASE`, in the **linear** section of the local enum
(after `LOCAL_ENV_1_RELEASE`, before the hybrid anchor):

- Range: default (±1073741824), neutral 0, bipolar in effect.
- Reset target becomes `retriggerPhase + (uint32_t)(finalValue << 2)` —
  full param swing = one full cycle of the slave wave. `<< 2` so an LFO over
  the param scrolls through the *entire* window (the report's technique §2).
- Applied **only to genuine A→B sync** (guarded by `genuineSync`), at the top
  of `renderOsc`, before PW setup — so PW self-sync is untouched.
- Being a patched param, it is automatically a patch-cable destination
  (LFO/env/velocity/…) and per-step automatable once given an automation-grid
  slot (below). XML name `syncPhase` via `paramNameForFileConst`; save/load of
  the value itself is the stock patched-param machinery.

### Parameter naming (no KDFX constraint — this is synth-side, not an FX bloc)

| Thing | Value |
|---|---|
| Param enum | `LOCAL_SYNC_PHASE` |
| XML attribute | `syncPhase` |
| Display name | "Sync Phase" |
| Mod-matrix short name | "Sync phas" |
| Menu item | `osc::SyncPhase : source::PatchedParam`, osc B only |

## Touch points

| File | Change |
|---|---|
| `definitions_cxx.hpp` | `enum class OscSyncMode` |
| `modulation/params/param.h` | `LOCAL_SYNC_PHASE` (linear section) |
| `modulation/params/param.cpp` | short name, display name, `syncPhase` file name |
| `gui/l10n/strings.h`, `g_english.cpp`, `english.json` | `STRING_FOR_PARAM_LOCAL_SYNC_PHASE`, `STRING_FOR_SYNC_PHASE`, option strings HARD/SOFT/REV |
| `util/functions.cpp` | explicit range/neutral cases (defaults already correct) |
| `processing/sound/sound.h/.cpp` | member, render predicates, init, save/load |
| `model/voice/voice_unison_part_source.h/.cpp` | `oscSyncDirection` member + init |
| `model/voice/voice.h/.cpp` | `renderOsc` mode arg plumbing; mode logic at reset sites 1–3; `genuineSync` capture; sync-phase offset; both call sites |
| `processing/render_wave.h` | mode logic in `RENDER_OSC_SYNC` crossover block |
| `storage/wave_table/wave_table.h/.cpp` | `render()` gains mode/direction args |
| `gui/menu_item/osc/sync.h` | `Toggle` → `Selection` (4 options) |
| `gui/menu_item/osc/sync_phase.h` | new `SyncPhase` menu item |
| `gui/ui/menus.cpp` | instantiate `oscSyncPhaseMenu`; free OSC-B slot in `patchedParamShortcuts` automation grid |
| `gui/menu_item/generate/dmenus/oscillator/__init__.py` + regenerate `g_menus.inc` | sync entry becomes selection; add sync-phase entry |
| `gui/ui/visual_menu/visual_menu_data.cpp` | osc 2 pages: SYNC slot shows mode; SYNPH slot for the new item |

## Visual menu layout (osc 2)

Keep the Shape page's four slots meaningful; sync's two controls sit together:

- **Osc 2 Wave** — TYPE / LEVEL / PITCH / WAVE (unchanged)
- **Osc 2 Shape** — WIDTH / FDBK / **SYNC** (now 4-way) / **SYNPH**
- **Osc 2 Phase** — PHASE (retrig phase moved here — it is the reset base
  target, thematically sync's neighbor) / empty / empty / empty

`oscPhaseMenu` moving pages keeps it reachable and frees the Shape slot for
SYNPH. Osc 1's Shape page drops its dead SYNC slot (sync is B-only — dead-link
sweep rule) and keeps WIDTH / FDBK / PHASE.

## Implementation notes (discovered during the build)

- **REVERSE direction re-application**: `phaseIncrement` re-enters `renderOsc`
  positive every block (the caller recomputes it from pitch), so the persisted
  direction is re-applied in the `doOscSyncSetup` block, after band/table
  selection (which happened before, on magnitude).
- **Magnitude normalization**: band and kernel selection must never see the
  flipped (negative) increment — `getTableNumber` keeps using the pre-flip
  value, `WaveTable::render` selects band/kernel by
  `phaseIncrementMagnitude`, and the triangle low-rate gate uses
  `std::min(phaseIncrement, 0u - phaseIncrement)`. Reversal adds no harmonics,
  so the band is unchanged — this is correctness, not just cosmetics.
- **Crude loops** derive their own `phaseIncrementNow` from the (already
  re-applied) local, flip it at REVERSE wraps, and write the direction flag
  back after the loop; the hard-reset sub-sample math uses the same local
  (equal to the positive increment in HARD/SOFT, which never flip).
- **The sync crossfade** in `RENDER_OSC_SYNC` now keys off a new
  `lastCrossoverDidReset` flag instead of `renderedASyncFromItsStartYet`
  (which also serves resetter-phase bookkeeping and must stay stock). Skipped
  SOFT resets and REVERSE crossovers produce no discontinuity, so they skip
  both the reset and the crossover-sample blend.
- **`syncPhase` persistence is explicit**, like every other patched param:
  `writeParamAsAttribute(writer, "syncPhase", LOCAL_SYNC_PHASE, ...)` +
  matching read tag in `sound.cpp` (the `paramNameForFileConst` entry keeps
  the compile-time uniqueness assert and mod-matrix naming happy).

## Persistence audit checklist

- [x] `oscillatorSync` attribute: writes int 1–3, old `1` loads as HARD, `0`/absent = OFF.
- [x] `syncPhase` patched param round-trips (explicit save/load lines + assert-validated file name).
- [x] `oscSyncDirection` is runtime-only state (never persisted — like `oscPos`; reset on note-on).
- [ ] Default patch (no sync): renders bit-identical to pre-change firmware — verify by ear + A/B on device.

## Test plan

1. **Regression**: osc B saw + SYNC HARD, sweep B pitch — the classic sync
   scream must be unchanged vs. current firmware.
2. **PW guard**: PW on saw/square/wavetable with sync off — pulse width must
   behave exactly as before (self-sync path untouched).
3. **SOFT**: slow-drift slave (B detuned a few cents below A) — timbre
   thickens/locks intermittently instead of screaming; no zipper/clicks at
   skip points (no crossfade is applied on skipped resets — verify silence of
   transition by ear + scope).
4. **REVERSE**: A=saw master, B=saw slave — pitch tracks A, timbre is
   hollow/square-ish; flip B between wave types (sine/tri/saw/table) — no
   discontinuity glitches at mode change; sustained note across many seconds —
   direction flag stable (no drift to DC).
5. **Sync phase**: WAVETABLE slave + SYNC HARD + LFO→Sync Phase at slow rate:
   audible window scroll through the wavetable frame; at SYNC off the param
   must do nothing (and the menu item hides).
6. **Unison**: 4-voice unison + REVERSE — each part's direction flag
   independent, no phasing artifacts beyond expected.
7. **Save/load**: patch with SOFT + Sync Phase 25% + LFO cable → save →
   reload → identical render.
