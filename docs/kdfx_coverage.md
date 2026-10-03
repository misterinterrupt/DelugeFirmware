# KDFX algorithm coverage

Full algorithm list from `docs/kdfx manual.pdf` (Appendix A, "KDFX Algorithms"), mapped
against the blocs implemented in `dsp/fx_bloc` + `dsp/reverb/kdfx`. Size = PAUs as printed
in the manual. The PAU budget model (4 insert PAUs + 3 aux PAUs on the K2500) is the
reference for our master/track chain resource accounting.

## Implemented as blocs

| Bloc | Manual algorithms covered | Notes |
|---|---|---|
| KDFX REV | Reverb family 1–15 (Classic Verb, TQ Verb, Diffuse Verb, OmniVerb, Stereo Hall, Grand Plate, …) | One parametric engine stands in for the reverb family |
| KDFX DLY | Delay family 130–136 (Complex Echo, 4/8-Tap, Spectral taps) | L/R times, feedback, crossfeed, drive |
| KDFX CHO | Chorus family 150–153 + vibrato sides of 159/160 | 2/4/6 voices, waveforms, quadrature spread |
| KDFX TONE | 909 Super Shaper, 910 3 Band Shaper, enhancers 959/960, EQ tools | Drive, 4 shaper modes, 3-band EQ, level |
| KDFX FLANGER | 154 Flanger 1, 155 Flanger 2 | Single stereo tap, bipolar feedback |
| KDFX PHASER | 156–160 (LFO Phaser family) | 4× one-pole allpass cascade, swept coeff, feedback |
| KDFX TREMOLO | 961 Tremolo, 962 Tremolo BPM | Insert-style; BPM sync later |
| KDFX PAN | 963 AutoPanner, 964 Dual AutoPanner | Insert-style; linear crossfade v1 |

Send-style blocs wet-add (reverb-class); tremolo/panner are insert engines via
`Base::isInsert()/processInsert()` — the same path the COMPRESSOR/FILTER adapters will use.

## Not yet implemented (manual backlog, by category)

- **Reverb variants** 1–15 as distinct voicings (MiniVerb, gated, plate/room/hall presets)
- **Combination chains** 700–723 (Chorus+Delay, Flange<>Reverb, Pitcher+Chor+Dly, LaserVerb …)
- **Distortion** 724–732 (Mono/Poly/Stereo Distort, TubeAmp chains, cabinet)
- **Tone wheel organ** 733–737 (VibChor+Rotor, KB3)
- **Filters** 900–906 (Env Follow, Trig Envelope, LFO Sweep, Resonant, EQ Morpher)
- **Special FX** 907–913 (Ring Modulator, Pitcher, shapers, LaserVerb family)
- **Comp/gate** 950–957 (Hard/Soft Knee, Expander, sidechain EQ, 3-band comp, Gate) — the
  planned COMPRESSOR bloc adapter lands here
- **Studio/mixdown** 958–967 (Super Gate, enhancers, SRS)
- **Tools** 966–970, 998–999 (Stereo Image, Mono→Stereo, Graphic EQs, 5 Band EQ)
