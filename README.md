# Deluge AntiFirmware

Firmware for the [Synthstrom Deluge](https://synthstrom.com/product/deluge/) —
the same hardware and sequencer you know, with the effect engine gutted and
rebuilt around a modular KDFX bloc system, a visual-menu UI, and a set of new
synthesis capabilities. This is a hard fork of the upstream firmware:
**no legacy effect support** — old songs load, but their stock-effect settings
are ignored (the full removed/kept list is in the manual).

**Read the manual: [docs/manual.md](docs/manual.md).** It covers everything —
the bloc reference, chain architecture, FX modulation, sync, time stretching,
the visual menu, and what was deleted. Summary below.

## What's different

- **FX blocs** — every stock effect engine (reverb, delay, chorus, flanger,
  phaser, grain, EQ, filters, crush, sidechain, compressor) is deleted and
  replaced by a modular system of KDFX-modeled blocs: reverbs (FREEVERB,
  MUTABLE, KDFX REV), delays (KDFX DLY, Spectral 4-Tap with its comb bank),
  modulation (chorus, flanger, phaser, tremolo, auto-pan), distortion
  (TONE, DIST, TUBEAMP), the synth's own filter cores as FX (RESFILT, ENVFILT,
  LFOFILT), LaserVerb, the Pitcher resonator bank, ring mod, quantizer,
  compressor, and ~20 combination blocs. Three slots each on the master chain,
  every synth, every kit drum row, and every audio clip; four routing
  topologies; MST SEND taps any track into any master slot bus.
- **FX modulation** — with a bloc param selected in the menu, shift + a grid
  pad connects an LFO, envelope, velocity, note, aftertouch, MPE or random
  source to it; repeated presses cycle depth 25/50/100/OFF. Eight connections
  per bloc, saved with the song.
- **TAILS + panic kill** — each chain gates its own render tail (voice /
  RMS dB threshold / always-on), and holding play/stop for a third of a second
  fades every wet tail in the song to silence at once.
- **Oscillator sync expansion** — sync is now OFF / HARD / SOFT / REV, plus
  SYNPH, a patch-cable-ready reset-position offset: sync windows on wavetables,
  soft phase-locking, direction-flip timbres. Full technique notes in the
  manual.
- **BPM time stretching** — per-sample Original BPM + Stretch % relative to
  song tempo, a one-press auto-BPM detector, and an Akai-style granular stretch
  mode alongside the stock WSOLA.
- **The visual menu** — the primary editing UI: category/page/4-slot layouts
  on the OLED, slot buttons under your fingers, live grid underneath. Enable
  under SETTINGS → Visual Menu Style → "Visual 4-Param".
- Kept on purpose: the per-voice synth filters (now with MORPH), the
  wavefolder, clipping, the stutterer — synthesis-side, not FX.

## Working on it

We build with **`./dbt`** (`./dbt build release` → `build/Release/deluge.bin`)
and flash over **J-Link** with **`scripts/flash-jlink.sh`** (flashes the
Release ELF to RAM via JLinkExe; `debug` or an explicit ELF path also accepted,
see the script header).
