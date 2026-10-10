# Sync Windows & Pre-Effect Synthesis Expansion — Report

*Date: 2026-10-10 · Branch: `fx-blocs` · Status: outline only — nothing implemented, does not preempt the PTCHR/buglist queue.*

## Scope

Tutorial-video research on oscillator sync techniques that go beyond the classic
"reset-a-saw-and-sweep-it" lead sound, mapped against what the Deluge engine
actually has today, with concrete expansion proposals for the pre-effect
synthesis system (the oscillator/source section — not the FX-blocs chain).

## What the engine has today (grounding audit)

- Hard sync exists **one way only**: source A resets source B
  (`voice.cpp:1184`–`1200`, `doingOscSync` + `oscSyncPos`).
- The reset always snaps the slave back to `retriggerPhase`
  (`Voice::renderOsc`, `voice.cpp:2815`).
- PW on non-square oscillators is internally implemented as *self*-sync —
  a hint at how general the reset machinery already is.
- WAVETABLE sources already render through the sync path
  (`waveTable->render(..., doOscSync, ...)`).
- SAMPLE sources get a separate tempo "stretch-sync" instead (no phase windows).
- Osc types: SINE, TRIANGLE, SQUARE, ANALOG_SQUARE, SAW, ANALOG_SAW_2,
  WAVETABLE, PHYSICAL_MODEL, SAMPLE, DX7, INPUT_L/R/STEREO
  (`definitions_cxx.hpp:363`).

## The techniques beyond the basic sync sound

### 1. Sync as a formant filter (spectral design)

Hard sync's spectrum is the slave's harmonics multiplied by a comb of formant
peaks anchored at the reset rate. Sweeping the *slave* pitch moves those peaks
while the note stays put — a second, filter-independent spectral axis. This is
where the "sync vowels," formant basses, and vocal-pad patches come from.
Deluge can already do this today (osc A as master, sweep osc B pitch), but
nothing in the UI frames it as a formant control.

### 2. Sync windows — the core tutorial content

When the slave is reading a *long* waveform (wavetable or sample) and gets
reset before it finishes, each sync cycle cuts a "window" out of the underlying
wave. At reset ratio N you hear window N; modulating the reset rate (or the
reset phase) smoothly scrolls through the waveform. One idea, three named
manifestations:

- Vital/Serum wavetable **window modes**,
- Casio CZ **phase distortion** (cycle split into windows with different read
  rates — literally window sync),
- window-granular sample playback.

Highest-value technique on the list, because Deluge's wavetable renderer
already accepts `doOscSync`.

### 3. Audio-rate sync = hard waveshaping

Feed arbitrary audio as the resetter and the oscillator stops being an
oscillator — every input zero-crossing resets the phase, making the whole thing
a nonlinear shaper of the input. Sync'd oscs "frying" drums, input-synced fuzz,
metallic cross-mod clangs — all this family. On Deluge the sync master is
computed from source A's phase counter, so this needs input sources to become
legitimate masters.

### 4. Softer reset variants

Hard reset has a discontinuity; that's its sound, but it's not the only reset
rule:

- **Reverse sync** — flip phase direction instead of jumping to zero:
  pitch-stable timbre morphing with much less aliasing.
- **Soft sync** — only reset when phase is already near zero: subtler
  harmonic shifts.

Both are cheap variants of the same reset event.

### 5. Sync as a timbre-tracking clock

Because the reset event is phase-addressable, timbre (pulse width, formant
position) can be locked to keyboard pitch so a patch stays consistent across
the range — the "why does my patch sound different up the octave" fix.

## What we could add (priority order)

> **Status update (2026-10-10):** items 1 and 2 are implemented (see
> `docs/sync_expansion_spec.md`). Items 3–5 remain roadmap.

1. **Per-source sync mode (HARD / SOFT / REVERSE).** One new param on the sync
   path in `renderOsc`: reverse sync negates the phase increment at the reset
   event instead of loading `retriggerPhase`; soft sync gates the reset on phase
   proximity. No new buffers, no new engine — a branch inside the existing
   `doOscSync` setup.
2. **Reset-phase as a modulation target.** The reset value is already a variable
   (`retriggerPhase`); exposing it as a patch-cable destination turns any synced
   oscillator into a window-scroller — point an LFO at it and a static
   wavetable becomes a morphing one. Smallest change, biggest tonal payoff,
   and it composes with the existing complete FX-mod routing.
3. **Any-source sync matrix + dividers.** Let B or C be the master (currently
   hardcoded `s == 0`), allow divided masters (÷2, ÷4) for sub-reset formant
   placement. Making INPUT_L/R valid masters unlocks the audio-rate waveshaping
   family (§3) — drums into a synced osc is a whole genre of patches.
4. **Windowed sample sync.** Route the SAMPLE source through the same
   phase-window logic as wavetables (currently tempo stretch-sync only) for
   grain-window / formant playback over arbitrary samples — a poor-man's
   granular without a new engine.
5. **A proper phase-distortion oscillator type.** CZ-style two-window read of
   the existing wavetable infrastructure with a "window" morph param —
   technique §2 promoted to a first-class osc type, fitting the same `OscType`
   slot pattern the engine already extends through.

## Suggested sequencing

Items 1–2 touch almost nothing and unlock techniques §1, §2, and §4 immediately.
The sync matrix and windowed samples (3–4) are bigger digs. The PD osc type (5)
is a spec-first project the way PTCHR was.
