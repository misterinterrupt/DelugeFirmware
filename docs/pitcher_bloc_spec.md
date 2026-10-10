# PTCHR — KDFX Pitcher (algorithm 908) bloc specification

Implementation spec for the Deluge fx-blocs firmware. Read together with
`docs/fx_blocs_roadmap.md` (the house rules) and `src/deluge/dsp/fx_bloc/fx_bloc.hpp`
(the bloc framework). Manual source: `docs/kdfx manual.pdf`, "Pitcher", chapter 5-14.

## What the Pitcher is (and is not)

**The Pitcher is NOT a pitch shifter.** From the manual:

> "Pitcher applies a filter to the input signal which has a series of peaks in
> the frequency response. These peaks are normally adjusted so that their
> frequencies are all multiples of a specific, selectable frequency, which
> imposes a strong sense of pitch at the selected fundamental frequency."

It is a **tuned harmonic resonator bank**: a stack of narrow resonant peaks at
f0, 2f0, 3f0, ... that makes any input hum with an imposed pitch. PitcherA
(preset 914) and PitcherB (preset 915) are factory presets of the same
algorithm, not separate engines. There is also a MonoPitcher variant used in
combos.

This is dramatically simpler than a granular/phase-vocoder pitch shifter and
reuses resonator building blocks that already exist in the firmware.

## Engine

File: `src/deluge/dsp/reverb/kdfx/pitcher.hpp`, class `KdfxPitcher : public
dsp::reverb::Base` (insert engine: `isInsert() == true`, audio path in
`processInsert`).

- **Topology**: a bank of one-pole resonant bandpasses (the same resonator
  voice as the Spectral 4-Tap's combs — see `KdfxSpectral4Tap::runComb` in
  `spectral4.hpp`: LP then subtract HP-of-LP, coefficient from a cutoff
  frequency). One bank per stereo channel sharing a single f0 and weights.
- **Harmonics**: N = 1..16 peaks at N × f0. Harmonics whose frequency exceeds
  Nyquist (or a practical ceiling ~18 kHz) are dropped from the sum; as PITCH
  rises, the upper harmonics fall out automatically. The engine must handle the
  harmonic count changing without clicks (weights go through the same
  coefficient-update path as frequency, once per render block).
- **Wet path**: wet = sum over harmonics of (resonator(harmonic N) × weight N).
  Insert blend is the framework's LEVEL (see "Framework integration"). No
  feedback loop exists (each resonator is a one-pole pair — the loop-gain rule
  holds trivially).
- **Coefficient updates**: per render block, on the audio thread, at the top of
  `processInsert` — the same pattern as `KdfxFilter::processInsert` (env/LFO
  filters) after the FilterSet wrap: compute all coefficients from current
  params, then process. The setters on the UI thread only store normalized
  values; all expensive math (Hz, exp, Q31 conversion) happens per block.

## Parameters (manual-faithful)

| Param   | Range                | Param id routing | Notes |
|---------|----------------------|------------------|-------|
| PITCH   | C-1..G9 (MIDI 0..127)| RoomSize         | the imposed fundamental, note numbers |
| PTCH-OFF| ±12.0 semitones      | Decay            | offset from PITCH; **the FXMod target** — manual: "useful to assign pitch bend, a ribbon, or another continuous controller to this parameter" |
| ODD-WT  | 0..100%              | TrebleGain       | odd-harmonic group weight |
| PAIR-WT | 0..100%              | TrebleFreq       | pair (2nd-group) harmonic weight |
| QTR-WT  | 0..100%              | AirShelf        | quarter-group weight |
| HALF-WT | 0..100%              | DelayTimeR       | half-group weight |
| IN-GN   | -96..+3 dB           | INGAIN_DEF()     | house style — first table entry |
| LEVEL   | 0..100%              | LEVEL_DEF()      | house style |

Manual on the four weights: "An exact description of what each one does is,
unfortunately, impossible, since there is a great deal of interaction between
them. For more information and examples, see the KDFX Algorithm Reference."
(The Algorithm Reference is not in our manual copy — group mapping is our
design, see below.)

### Weight grouping (our design, tunable by ear)

Harmonic H's weight combines its group memberships, drawbar-style:

- `odd(H)`  = H % 2 == 1
- `pair(H)`  = H % 2 == 0
- `half(H)`  = H % 2 == 0 (the "half-series" 2f0, 4f0, ... subgroup bending)
- `quarter(H)` = H % 4 == 0

Starting point (tune against presets 914/915 by ear):

```
weight(H) = odd(H)  ? ODD_WT
          : PAIR_WT
then:  if (H % 4 == 0) weight(H) *= QTR_WT    // quarter subgroup
       if (H % 2 == 0) weight(H) *= HALF_WT    // half subgroup bend
```

The resonators' narrowness (Q) is fixed high enough to "impose a strong sense
of pitch" — start around Q 20-30 per peak. No manual control exists for it;
do not add one.

## f0 control path

- `setRoomSize(float v)` (PITCH): store norm 0..1; f0 = 8.175 Hz × 2^((v×127)/12)
  (MIDI note → Hz, C-1 = 8.175 Hz). Per block: for each harmonic N, fN = N × f0;
  if fN >= min(18000, Nyquist) the harmonic drops; else coefficient from fN via
  the existing one-pole cutoff helper (see `KdfxSpectral4Tap::setTapPitch` for
  the exact Q31 coefficient formula — reuse the same math for consistency).
- `setDecay(float v)` (PTCH-OFF): store; per block, effective semitone offset =
  (v × 2 − 1) × 12.0; f0_effective = f0 × 2^(offset/12).
- This parameter pair is the showpiece for the FX modulation gesture
  (shift+grid): an LFO or pitch-bend source on PTCH-OFF sweeps the imposed
  pitch. Verify modulated PTCH-OFF through the render-boundary fold
  (`FxModConnection`, see `fx_bloc.hpp`).

## Framework integration

Follow the existing pattern exactly (reference: `spectral4.hpp` + its wiring
in `fx_bloc.hpp`):

1. **Model enum**: add `KDFX_PITCHER` (and combo models, below) to
   `FxBloc::Model` in `fx_bloc.hpp`.
2. **Construct/size**: cases in `constructEngine()` —
   `sizeof(reverb::KdfxPitcher)`, `return new (memory) reverb::KdfxPitcher();`.
3. **Param table**: `kDefsKdfxPitcher[]` with IN-GN first (house directive),
   the 8 entries above; add `static_assert(sizeof(kDefsKdfxPitcher)/... <=
   kMaxSlotParams, ...)` alongside the other 28 table locks; case in
   `paramDefs()` switch.
4. **Param names**: cases in `FxBloc::paramName()` and in
   `applyParamToEngine()`/`readEngineParam()` — PITCH on RoomSize, PTCH-OFF on
   Decay, weights on TrebleGain/TrebleFreq/AirShelf/DelayTimeR (float setters).

   WEIGHT IDS: the float param ids listed above are the ones unused by other
   engines of this table; if a future table needs them, the Pitcher's weights
   can move — ids are per-bloc, not global.
5. **Selector**: add to `MasterSlotBloc::kOptions` + `kNames` (in
   `master_fx_items.h`) — the lists are static_assert-locked, both arrays must
   gain the entry together.
6. **tailSamples()**: the resonators ring after input stops — estimate
  RT60-ish from Q (Q≈25 → several hundred ms). Return a modest fixed estimate
  (e.g. 44100/2) rather than 0 so the TAILS gate doesn't cut the ring early.
7. **The kill/panic path** (`killTail`) needs no engine work — insert engines
   fade via LEVEL automatically.

## Combinations (after the solo engine works)

| Model | Manual | Form |
|-------|-------|------|
| `KDFX_FLG_PITCHER` | 712 Flange<>Pitcher | standard pair: A = Pitcher (primary ids + CFG order swap), B = Flanger (Mod* trio rides the flanger), table shape `kDefsKdfxShprRev`, HasCfg = true |
| `KDFX_PITCH_CHO_DLY` | 910 Pitcher+Chor+Dly | triple, "+" = fixed order. Implement as nested pair: outer pair A = Pitcher, B = pre-built Chor+Dly (existing `KDFX_CHO_DLY` pair engine as the B half). PairEngine variant would need to accept a pair — simplest is a small dedicated wrapper class holding a Pitcher + the CHO_DLY pair. 2 PAU. |
| `KDFX_PITCH_FLG_DLY` | 911 Pitcher+Flan+Dly | same as 910 with the FLG_DLY pair. 2 PAU. |
| (optional) MonoPitcher pairs | MonoPitcher+Chor / +Flan | same pair form; MonoPitcher = the solo engine fed a mono downmix (send-style run). Only if cheap; they are minor presets. |

Pair-engine kinds: the variant in `pair.hpp` gains `PITCHER` (emplace
`KdfxPitcher`), mirroring how TUBEAMP/COMPRESSOR were added.

## Testing checklist

1. **Solo engine**: noise in → strong pitch at the selected fundamental;
   PITCH sweep is click-free (per-block coefficient updates); PTCH-OFF at ±12
   tracks; weights at 0 mute their groups; LEVEL 0 = dry through.
2. **Modulation**: LFO1 on PTCH-OFF vibrates the imposed pitch; envelope on
   LEVEL gates the bank. Both through the `<mod>` XML round-trip.
3. **Loop gain**: all ODD/PAIR/HALF/QTR at 100% with sustained input — the
   output must not build up run-away (no feedback loops; verify by inspection
   and long-tail listening).
4. **Tail**: TAILS gate waits for the resonator ring (~500 ms estimate).
5. **Persistence**: save/reload a song with a PTCHR slot and mod routings.
6. **Combos**: 712 CFG order swap audible; 910/911 chains in the manual's
   fixed order (Pitcher first).
7. **Device**: no CPU surprises — 16 resonators × 2 channels is the budget
   concern; measure with PTCHR + reverb + Spectral on one track.

## House rules (from fx_blocs_roadmap.md, apply to all of the above)

- IN-GN leads every param table; new blocs start at LEVEL 0% (silent until
  raised).
- Loop-gain rule: bound every recirculating path at fully-correlated content.
- XML params are name-keyed via `paramName()`; unknown tags skip on load.
- Every param table must be covered by a `static_assert` against
  `kMaxSlotParams`.
- The firmware printf is `snprintf_` (printf.h) — raw `snprintf` breaks the
  link (`_sbrk` undefined).
