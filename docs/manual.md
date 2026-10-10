# Deluge AntiFirmware — User Manual

*Firmware branch `fx-blocs`. This manual covers everything this firmware adds and
changes on top of the stock Deluge Community Firmware: the modular FX bloc system,
chain architecture, FX modulation, oscillator sync expansion, BPM-based time
stretching, and the visual menu that ties it all together. A "what was removed"
section at the end lists the stock features that were deleted and what replaced
them.*

---

## 1. The Visual Menu

The visual menu is this firmware's primary editing interface. Enable it once under
SETTINGS → COMMUNITY FEATURES → **Visual Menu Style → "Visual 4-Param"**. Then, on
an OLED Deluge, a **press of the select encoder** (the silver knob) opens it —
from a synth or kit clip, an audio clip, or song/arranger view (in song view it
opens the master-FX editor).

### Layout

The screen shows **one page at a time**:

- The **header** names the page ("Osc 2 Shape", "BLOC 2", "SLOT 1+").
- Below it are **four slots** — named parameter cells like `LEVEL`, `CUTOFF`,
  `FDBK`. Each slot shows its label and current value.
- Pages are grouped into **categories** — OSCILLATOR 1, FILTERS, FX BLOCS,
  ARPPEGGIATOR and so on. The category count appears in the header.

Which categories you get depends on what you're editing:

| Context | Categories |
|---|---|
| Synth clip (or selected kit drum row) | MASTER, FILTERS, ENVELOPES, OSCILLATOR 1, OSCILLATOR 2, FM, LFOs, FX, ARPPEGGIATOR, FX BLOCS |
| Kit clip, affect-entire | same table, editing the kit's global chain context |
| Audio clip | MASTER (level/pan/pitch/attack), PLAYBACK (reverse/interpolation), FX (clipping), FX BLOCS |
| Song view | MASTER (level/pan), FX, FX BLOCS — the master chain |
| MIDI/CV clip | MIDI OUT, ARP |

### Controls

- **Select encoder, nothing held** — scrolls pages. Scrolling past the last page
  of a category wraps into the next category.
- **Shift + select encoder** — jumps category by category.
- **The four slot buttons** (the `SYNTH` / `KIT` / `MIDI` / `CV` row buttons) are
  the four slots on the current page. Tap one to select its slot; **hold it and
  turn the select encoder** to edit that value.
- **Fine editing**: for bloc and numeric params, a select-button press while the
  slot is held moves the edit cursor by decimal position (ones → tenths →
  hundredths); shift while turning makes steps ×10 coarser on fine items.
- **Exit**: the SESSION VIEW, CLIP VIEW, or KEYBOARD buttons close the menu. The
  underlying grid stays live the whole time the menu is open — notes, kit rows and
  auditioning keep working, and the menu deliberately forwards pad presses it
  doesn't use.

The classic list-based menu still exists (shift + select encoder press) but the
visual menu is the intended interface — everything new in this firmware lives
there first.

---

## 2. FX Chains

Every mixing context has an **FX chain of three slots**, and each slot holds one
**bloc**:

- **Master chain** (song): three slots, fed post-fader from every track.
- **Per-sound chains**: every synth, every kit drum row, and every audio clip has
  its own three-slot chain. Chains render inside the sound when the sound is
  playing.
- **Send buses**: each of the three *master* slots doubles as a **bus** that
  tracks can tap into via the MST SEND bloc (below).

### The Chain page

Every FX BLOCS category starts with a **Chain** page:

- **ROUTE** — the routing topology of the three slots:
  - `SERIES` — 1 → 2 → 3 (default)
  - `PARALLEL` — (1 + 2 + 3) summed against dry
  - `SERIES_PARALLEL` — 1 → (2 + 3)
  - `PARALLEL_SERIES` — (1 + 2) → 3
- **TAILS** — the render-hold policy: how long a chain keeps rendering after its
  voices stop:
  - `VOICE` — voices rule; the chain stops when the amp envelope ends (default).
  - `-1 … -96 DB` — an RMS gate on the chain's own output. The chain keeps
    rendering while its wet output is above the threshold; after 200 ms below it,
    the tail is cut. Great for delays/reverbs with finite tails.
  - `ALWAYS` — the chain renders every block, forever. Infinite-feedback
    drones ring until you kill them (see panic kill below).

### Rules of the chain

- **EMPTY** slots are transparent — zero CPU, zero memory. That's the default.
- Engines allocate lazily the moment you pick a bloc (right-sized from SDRAM). If
  the pool is exhausted you get an `INSUFFICIENT_RAM` popup and the slot keeps its
  previous model.
- **LEVEL starts at 0%** on every fresh bloc: a send bloc is silent, an insert
  bloc passes dry, until you raise it. Raise LEVEL to hear the effect.
- **IN-GN** (−96…+3 dB) is the input trim into the bloc, before the engine.
- **Hold PLAY/STOP for a third of a second** = **panic kill**: every chain's wet
  output (master + all tracks) fades out over ~100 ms, tanks latch silent, and
  the bloc revives the next time you touch any of its params. Tail drones,
  delay walls, runaway feedback — one gesture ends them all.

### Send vs insert blocs

- **Send blocs** (reverbs, delays, chorus, flanger, phaser, laser, spectral
  4-tap) take a mono downmix of their input and wet-add the result; **LEVEL is
  the wet level** into the chain.
- **Insert blocs** (filters, distortion, ringmod, quantize, pitcher, tremolo,
  auto-pan, compressor, and all combination blocs) process the stereo signal in
  place; **LEVEL is a wet/dry blend** (turn it down for parallel distortion).

---

## 3. The Blocs

All bloc names follow the KDFX manual. Params are shown 4 per page in the order
listed; ranges are the exact display ranges. IN-GN (−96…+3 dB) leads every bloc
and is not repeated below. LEVEL is 0–100% everywhere.

### Spaces

**FREEVERB** — send. Classic Schroeder/Freeverb.
`SIZE` 0–100% · `DAMP` 0–100% · `WIDTH` 0–100% · `LEVEL`.

**MUTABLE** — send. Mutable-style reverb.
`SIZE` · `DAMP` · `WIDTH` · `LEVEL` · `HPF` 20–1000 Hz.

**KDFX REV** — send. KDFX reverb: 4-line prime FDN, input diffusers, early
reflections, dithered tank.
`SIZE` 3–48 m · `DCAY` 0.6–999 s (RT60; the top renders `INF` = unity loop gain,
sustains forever) · `DAMP` 351–7019 Hz · `WIDTH` 0–100% · `PREDL` 0–300 ms ·
`ER` 0–100% (early reflections) · `BASS` 0.20–2.50× (bass decay multiplier) ·
`AIR` 0–100%. Decay is clamped below unity even at BASS 2.5×, so it never runs
away; the RT60 tail length feeds the TAILS gate automatically.

### Delay

**KDFX DLY** — send. Dual-line delay with tape saturation.
`TIM L` / `TIM R` 1–1480 ms · `FDBK` 0–100% (100% = unity loop, sustains) ·
`TONE` 351–7019 Hz (feedback damping) · `XFEED` 0–100% (ping-pong) · `DRV` 0–18 dB
(tape drive — normalized so loop gain is exactly FDBK) · `HPF` 20–1000 Hz ·
`WIDTH` 0–100% · `PHASE` `OFF`/`ON` (feedback polarity inversion).

**SPEC 4-TAP** — send. The KDFX Spectral 4-Tap: one shared feedback loop, four
output taps each with its own tuned comb, shaper and position.
`LOOP` 1–1480 ms · `FDBK` 0–100% · `IMAGE` ±100% (stereo image rotation per
feedback pass) · `HPF` 20–1000 Hz + `TONE` 351–7019 Hz (feedback path) · then per
tap n = 1…4: **Pn** 0–108 (comb pitch as note numbers C-1…C8, ~8.2 Hz…4.2 kHz) ·
**An** 0–100% (comb intensity) · **Sn** 0.10–6.00× (shaper drive) · **Tn** 2–100%
(tap position as a fraction of the loop). 23 params, six pages.

### Modulation

**KDFX CHO** — send. 2–6 voice chorus.
`RATE` 0.05–15.00 Hz · `DEPTH` 0.0–10.0 ms · `FDBK` ±95% · `BASE` 0.1–40.0 ms ·
`VOICE` 2–6 · `WAVE` `SINE`/`TRI`/`S&H` · `SPRD` 0–180° (stereo) · `HPF`.

**KDFX FLANGER** — send.
`RATE` 0.05–8.00 Hz · `DEPTH` 0.00–3.00 ms · `FDBK` ±95% (negative = flange
character flips) · `BASE` 0.1–7.0 ms · `SPRD` 0–180° · `HPF`.

**KDFX PHASER** — send. Four cascaded allpass sections per side; loop gain is
exactly |FDBK| at every setting.
`RATE` 0.05–8.00 Hz · `DEPTH` 0–100% · `FDBK` ±95% · `SPRD` 0–180° · `HPF`.

**KDFX TREMOLO** — insert. `RATE` 0.10–16.00 Hz · `DEPTH` 0–100% · `LEVEL`
(blend).

**KDFX PAN** — insert. Auto-panner. `RATE` 0.05–10.00 Hz · `WIDTH` 0–100% ·
`LEVEL` (blend).

### Distortion & tone

**KDFX TONE** — send. Shaper + 3-band EQ; the output is normalized by the drive
gain (1/drive) so DRIVE changes timbre, not loudness.
`DRIVE` 0–24 dB · `SHPR` `SOFT`/`TAPE`/`HARD`/`FOLD` · `BASS` ±15 dB @
`BFREQ` 30–400 Hz · `MID` ±15 dB @ `MFREQ` 200–8000 Hz · `TRBL` ±15 dB @
`TFREQ` 1000–16000 Hz · `TLVL` 0–100% (output level) · `LEVEL`.

**KDFX DIST** — insert. Parallel-friendly distortion: pre-filter, shaper, cabinet
sim, post-filter.
`DRIVE` 0–24 dB · `MODE` `SOFT`/`TAPE`/`HARD`/`FOLD` · `LOCUT` 20–300 Hz ·
`CAB` 0–100% (speaker-cab lowpass crossfade) · `HCUT` 1000–16000 Hz · `LEVEL`
(blend — turn down for parallel distortion).

**KDFX TUBEAMP** — insert. The DIST stack plus a built-in modulated delay
(~20 ms base — the "MD" character).
Amp half: `DRIVE` · `MODE` · `LOCUT` · `CAB` · `HCUT`. Mod half: `MRATE`
0.05–5.00 Hz · `MFDBK` ±95% · `MDEP` 0.0–10.0 ms. `CFG` `AMP>MOD`/`MOD>AMP`
swaps the order — in `MOD>AMP` the amp shapes the modulated wet too.

### Filters

All three filter blocs are insert engines built on the synth's per-voice TPT
state-variable filter — unconditionally stable. `TYPE` is the synth's LPF mode
menu: `12L-L` / `24L-L` / `12L` / `24L` / `24L-DR` / `SVF-B` / `SVF-N`.
`FREQ` 16–8372 Hz (log) · `RES` 0–50 dB (the top detent self-oscillates — a
feature, like the reverb's INF) · `LEVEL` = blend.

- **RESFILT** — static resonant filter: `IN-GN`, `TYPE`, `FREQ`, `RES`, `LEVEL`.
- **ENVFILT** — envelope-followed filter: `MINF` (at-rest frequency), `SWP`
  ±100% (sweep direction), `ATK`/`REL` 0–300 dB/s.
- **LFOFILT** — LFO-swept filter: `MRATE` 0.01–10.00 Hz, `MDEP` 0.0–6.0 oct.

### Spectral manglers

**KDFX LASER** — send. LaserVerb: 12 taps with geometrically widening offsets
recirculating in a loop.
`TIME` 0–2000 ms · `SPAC` 0.0–40.0 samples (impulse-gap growth) · `CONT` 0–100%
(contour: 0% = plain delay; ~34% = reverse-reverb build-up; high = starts loud,
slowly decays) · `FDBK` 0–100% (100% sustains) · `LEVEL`. The tap sum is
normalized by tap count, so loop gain is exactly FDBK no matter the contour.

**PTCHR** — insert. **Not a pitch shifter** — a tuned harmonic resonator bank:
16 narrow peaks at 1×…16× f0 that make any input hum at the selected pitch.
`PITCH` C-1…G9 (note numbers) · `PTOFF` ±12.0 st (the modulation target — patch
pitch bend or an LFO here) · drawbar weights `ODD` / `PAIR` / `QTR` / `HALF`
±100% each (negative = phase-inverted group) · `LEVEL` (blend). Coefficients
recompute per render block, so PITCH sweeps are click-free; harmonics above
~18.5 kHz drop out automatically.

**RINGMOD** — insert. Two modes: `L*R` (the channels modulate each other) or
`OSC` (input × a 5-oscillator bank: Osc1 with any shape/width/smoothing + four
sines). In L*R only the left channel passes dry.
`MODE` `L*R`/`OSC` · `O1LVL` 0–100% · `O1FRQ` 16–25088 Hz · `O1SHP`
`SINE`/`SAW+`/`SAW-`/`PULSE`/`TRI`/`EXPN` · `O1PLS` 0–100% (pulse width) ·
`O1SMH` 0–100% (smoothing) · `S2`–`S5` each `LVL` 0–100% + `FRQ` 16–25088 Hz ·
`LEVEL` (blend/wet).

**QUANT** — insert. Bitcrusher/quantizer.
`DYN` 0–144 dB of dynamic range (0 dB = 2-level square wave; every 6 dB doubles
resolution; 144 dB = transparent) · `HEAD` 0–144 dB of headroom above the signal
· `DC` −79…0 dB (DC offset injection — "at low DYN settings this can cause the
output to sputter", says the KDFX manual) · `LEVEL` (blend).

**COMPRESSOR** — insert. RMS-feedback compressor with sidechain HPF.
`THRSH` 0…−48 dB (threshold, dBFS) · `RATIO` 1:0.1…1:128 (exponential around
1:1) · `ATK` 0.5–100.0 ms · `REL` 10–1000 ms · `SIDE` 0–100 Hz (sidechain HPF) ·
`BLEND` 0–100% (wet/dry of the compression — parallel compression) · `LEVEL`.

### MST SEND (track slots only)

A send tap from a track into one of the master chain's three slot buses.
`BUS` `BUS 1`/`BUS 2`/`BUS 3` · `PAN` ±100% · `IN-GN` · `LEVEL` (send level).
Not selectable in the master chain itself (a dead tap there — and it
self-feeds under parallel routing).

### Combination blocs

Two-engine blocs from the KDFX manual's combination tables. Convention: the
**primary** half keeps its natural params; the modulated half rides the
**MRATE / MDEP / MFDBK** trio; where the manual allows, **CFG** swaps the
processing order. All are insert engines.

- **CHO+DLY** / **FLG+DLY** — chorus or flanger + delay:
  `XFEED`, `TIME`, `FDBK`, `MRATE`, `MDEP`, `MFDBK` (+`CFG` on CHO+DLY).
- **CHO+REV** / **FLG+REV** — chorus or flanger + reverb:
  `SIZE`, `DCAY`, `DAMP`, `MRATE`, `MDEP`, `MFDBK`, `CFG`.
- **FLG+LASER** — flanger + LaserVerb: `TIME`, `FDBK`, `SPAC`, `MRATE`, `MDEP`,
  `MFDBK`, `CFG`.
- **CHO+4TAP** / **CHO<>4TAP** / **FLG+4TAP** / **FLG<>4TAP** — chorus or
  flanger + the 4-tap multitap delay family.
- **TONE+REV** — shaper/EQ + reverb: `DRIVE`, `SHPR`, `BASS`, `MID`, `TRBL`,
  `SIZE`, `DCAY`, `CFG`.
- **QNT+FLG** — quantizer + flanger (fixed order): `DYN`, `HEAD`, `DC`, `MRATE`,
  `MDEP`, `MFDBK`.
- **REV<>COMP** — reverb + compressor: `SIZE`, `DCAY`, `DAMP`, `THRSH`, `RATIO`,
  `ATK`, `CFG` (`RVB>CMP` / `CMP>RVB`).
- **TUBAMP<>FLG** — TubeAmp + flanger: full TubeAmp stack + `MRATE`/`MDEP`/
  `MFDBK` + `CFG` (`TUB>FLG` / `FLG>TUB`).
- **FLG<>PTCHR** — flanger + Pitcher: `PITCH`, `PTOFF`, `ODD`, `PAIR`, `QTR`,
  `HALF`, `MRATE`, `MDEP`, `MFDBK`, `CFG` (`PT>FLG` / `FLG>PT`).
- **MNPTCHR+CHO** / **MNPTCHR+FLN** — mono Pitcher + chorus/flanger (fixed
  order): Pitcher params + the Mod trio.
- **PTCHR+CHO+DLY** / **PTCHR+FLN+DLY** — triple stacks: Pitcher params, the
  Mod trio, then the delay half (`TIME`, `FDBK`, `TONE`, `XFEED`, `HPF`,
  `PHASE`).

---

## 4. FX Modulation

Every bloc param (except int-selector params like MODE/CFG/BUS) accepts up to
**8 modulation connections**, each = {source, depth}. The connection is folded
into the param's cached value at the render boundary:
`final = clamp(base + depth × source)`.

### The gesture

With an FX BLOCS **slot param selected** (slot held), **shift + a grid pad** in
the two rightmost grid columns connects that pad's modulation source to the
param:

| Pad | Source |
|---|---|
| Rightmost column, rows 1–4 | AFTERTOUCH, VELOCITY, RANDOM, NOTE |
| Rightmost column, row 6 | LFO 2 |
| Rightmost column, rows 7–8 | ENV 2, Y |
| Second-from-right column, rows 6–8 | LFO 1, ENV 1, X |

(All other pads in those two columns have no source; in some synth modes the X/Y
pads are disabled.)

Pressing the same pad again **cycles depth: 25% → 50% → 100% → OFF**, with a
popup readout (`MOD 50%` … `MOD OFF`). Depth is bipolar — it adds and subtracts
around the base value as the source swings.

### What the sources are

- **On synth tracks**: LFO 1 (global), LFO 2 (per-sound), ENV 1, ENV 2, VELOCITY,
  NOTE, AFTERTOUCH, X / Y (MPE), RANDOM. The envelopes and LFO 2 render on
  demand — connecting them is what makes them run.
- **On the master chain, kits and audio clips**: a dedicated global LFO on the
  LFO 1 pads plus RANDOM; per-note sources are honestly zero there.

Connections save with the song (`<mod>` entries inside each slot), live and die
with the bloc, and never touch the patch-cable param system — which is what keeps
them cheap.

**Classic patch cables still work** for all the synth's own patched params
(osc pitch, PW, filter cutoff, etc.) exactly as in stock firmware — the FX
modulation system is separate and lives only on bloc params.

---

## 5. Oscillator Sync (new)

Oscillator sync (osc A resets osc B) is now a four-mode per-patch setting with a
modulatable reset position — it turns the sync pair into a formant/window
instrument, not just the classic scream.

Both controls live on the **Osc 2 Shape** page (osc 1 has no sync — it's always
the master):

- **SYNC** — `OFF` / `HARD` / `SOFT` / `REV`
  - `HARD` — the classic: B's phase snaps to the reset position each time A
    wraps. Sample-accurate, with a crossfade at the reset. Sweep B's pitch for
    the screaming lead sound.
  - `SOFT` — the reset only happens if B is already within a quarter cycle of
    the reset position; otherwise it slips this cycle. Result: B's frequency
    locks to A in and out as its phase drifts near the target — thick,
    "breathing" timbres without HARD's upper-harmonic shriek.
  - `REV` — no reset at all: B's *direction* flips each time A wraps, phase
    continuous. Pitch tracks A and stays stable while the timbre morphs —
    hollow, vocal-ish, aliasing-light.
- **SYNPH** — *Sync Phase*: an offset added to B's reset position, and it's a
  full **patched param** — LFO, envelope, velocity, pitch bend, anything can
  drive it. One full parameter swing covers a whole cycle of B's wave. On a
  wavetable osc B, sweeping SYNPH **scrolls the window** — each sync cycle cuts
  a different slice out of the wavetable frame. This is the "sync window"
  technique: point a slow LFO at SYNPH and a static wavetable becomes a morphing
  one.

Notes:

- The reset base position is the osc's own **retrig phase** (Osc 2 Phase page);
  SYNPH is the offset *on top of it*.
- Pulse-width modulation still uses HARD-style self-sync internally — PW
  behavior is unchanged, and PW + sync coexist on square waves.
- SOFT's threshold is fixed at 1/4 cycle; REV's direction persists per unison
  voice, so wide unison spreads stay diffuse.
- Old songs: `oscillatorSync=1` loads as `HARD`, i.e. exactly what it was.

Techniques worth trying: A = saw an octave or two below B = wavetable, sync
`HARD`, slow LFO → SYNPH → evolving formant windows; B detuned a few cents with
`SOFT` for analog-ish phase locking; `REV` on a triangle for pitch-stable
harmonic shimmer.

---

## 6. Time Stretching (new)

Sample oscillators (and audio-clip-like sample playback) get **BPM-based time
stretching** and a new granular stretch algorithm. Per sample oscillator:

- **BPM (Original BPM)** — the tempo the sample was recorded at. `0` = unset.
- **STRETCH** — duration multiplier relative to *song tempo*:
  - `100%` — plays in sync with the song
  - `50%` — twice as fast (half duration)
  - `200%` — half speed (double duration)

When BPM is set, playback speed is computed as
`(song BPM / original BPM) × (100 / STRETCH)` and fed to the time-stretch DSP —
pitch stays put while tempo follows the song. Changing song BPM re-stretches
automatically. When BPM is `0`, the classic semitone-**SPEED** control applies
as before (old songs keep working).

### The TimeStretch menu (4 pages)

Opening TIME STRETCH on a sample oscillator gives a four-page editor (select
press cycles the page):

1. **TEMPO (BPM)** — the sample's original BPM, editable to 0.01.
2. **SAMPLE LENGTH** — readout of the sample's duration and bar count implied by
   the current BPM.
3. **AUTO DETECT BPM** — one press analyzes the sample: it tries bar counts
   1/2/4/8/16/32, computes the BPM each implies, and snaps to the one closest to
   the current song tempo (within 60–200 BPM; samples longer than 30 s are
   refused). Works on audio clips and multisample ranges too.
4. **SPEED** — the classic ±48-semitone speed control, for when you want pitch
   and tempo to move together.

### Stretch algorithms (TIME STRETCH MODE)

- **Standard** — the stock WSOLA phase-matching stretcher (best for general
  material).
- **Akai** — vintage cycle-repeat granular stretching: ~20 ms grains
  re-read/crossfaded at the stretch ratio. The classic MPC "beat-stretch"
  character — chunkier, but drums stay punchy and it takes big ratios well.

---

## 7. What Was Removed

This firmware deliberately deletes the stock per-context effect engines and
replaces them with the bloc system. If you're coming from stock firmware or
community firmware, this is what's gone and where it went:

| Stock feature | Status | Replacement |
|---|---|---|
| Global reverb + per-track reverb send | **deleted** | KDFX REV / FREEVERB / MUTABLE blocs; MST SEND feeds them |
| Delay (ping-pong, analog, sync) + stutter-style delay | **deleted** | KDFX DLY bloc (XFEED = ping-pong, DRV = analog, PHASE, TONE) |
| Mod FX: chorus, flanger, phaser, stereo chorus, grain | **deleted** | KDFX CHO / FLANGER / PHASER blocs (+ every combination bloc) |
| EQ (bass/treble) and global LPF/HPF filters | **deleted** | KDFX TONE (EQ+shape) and RESFILT/ENVFILT/LFOFILT blocs |
| Crush (sample-rate reduction + bitcrushing) | **deleted** | QUANT bloc (DYN = bit depth, HEAD = headroom) |
| Sidechain (ducking) | **deleted** | COMPRESSOR bloc + FX modulation (e.g. audio/env source → another bloc's LEVEL) |
| Per-sound compressor + gold-knob comp modes | **deleted** | COMPRESSOR bloc on any track or master slot |
| Stock mod-FX / reverb / delay / comp gold-knob modes and pad shortcuts | **removed with their engines** | the visual menu's FX BLOCS pages are the interface |

**Kept, by design** (all synthesis-side):

- The per-voice synth filters (LPF/HPF with the full mode menu + **MORPH**) —
  these are synthesis, not mixing FX.
- Sound-level clipping and the wavefolder (`FOLD`).
- The stutterer.
- All oscillators, FM, ring-mod synth mode, patch cables, envelopes, LFOs,
  arpeggiator, sequencer, sampling and SDRAM management.

Other consequences worth knowing:

- **Old songs load** — unknown effect tags are skipped — but their stock-effect
  settings (reverb sends, delay times, mod-FX, EQ, comp, crush) are ignored.
- Automation and performance-view row counts shrank with the deleted params.
- The classic list menu still exists but is not maintained as a primary
  interface; new features appear in the visual menu, and dead links to deleted
  engines have been swept from it.
- House rule throughout the bloc engines: every feedback loop's gain is bounded
  at its FDBK setting even with fully-correlated (bass/mono) content — delay tape
  drive, chorus voice sums, laser tap sums and reverb decay are all normalized,
  so "feedback causes runaway" at moderate settings is a bug, not a feature.

---

*Source references: bloc param tables in
`src/deluge/dsp/fx_bloc/fx_bloc.hpp` and `src/deluge/dsp/reverb/kdfx/`, chain
and routing in `src/deluge/dsp/fx_bloc/master_fx_chain.hpp`, visual menu layout
in `src/deluge/gui/ui/visual_menu/visual_menu_data.cpp`, modulation gesture in
`src/deluge/gui/ui/visual_menu/visual_menu_ui.cpp`, sync in
`src/deluge/model/voice/voice.cpp` + `src/deluge/processing/render_wave.h`
(spec: `docs/sync_expansion_spec.md`), time stretching in
`src/deluge/dsp/timestretch/` and
`src/deluge/gui/menu_item/sample/time_stretch.h`.*
