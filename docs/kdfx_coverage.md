# KDFX algorithm coverage

Full algorithm list from `docs/kdfx manual.pdf` (Appendix A, "KDFX Algorithms"), mapped
against the blocs implemented in `dsp/fx_bloc` + `dsp/reverb/kdfx`. Size = PAUs as printed
in the manual. The PAU budget model (4 insert PAUs + 3 aux PAUs on the K2500) is the
reference for our master/track chain resource accounting.

## Parameter audit — manual chapter 5 vs. our columns

The definitive completeness check for bloc param tables. "engine gap" = the manual
defines it but our engine does not implement it yet (never list those — rule 3).

| Family | Manual parameter | Our column | State |
|---|---|---|---|
| universal | Wet/Dry, In Gain, Out Gain, In/Out | LEVEL (wet scale / insert blend) | partial — dry-side gain not modeled |
| universal | HF Damping (pre-loop lowpass) | reverb DAMP / delay TONE | yes (in-loop damping) |
| universal | XCouple (crossfeed) | XFEED (delay) | delay only today |
| reverb | Rvrb Time (RT60) | DCAY (reads RT60 S) | yes |
| reverb | L/R Pre Dly | PREDL | yes |
| reverb | EarRef Lvl | ER | yes |
| reverb | LateRvbTim, Late Lvl, Density, Expanse, Build | — | engine gap (future voicings) |
| delay | L/R Dly Time | TIM L / TIM R | yes |
| delay | Fdbk Level | FDBK | yes |
| delay | Hold (freeze) | — | engine gap (kill-switch-adjacent, desirable) |
| delay | per-tap Level/Bal, diffusers, Fdbk Image | — | engine gap (4-tap structure is fixed) |
| chorus/flange/phaser | LFO Rate, LFO Dpth, L/R Phase | RATE, DEPTH, SPRD | yes |
| chorus/flange | StatDly (coarse+fine), Fdbk | BASE, FDBK | yes |
| chorus | voices/waveform (KDFX varients) | VOICE, WAVE | yes (ours extends the manual) |
| EQ/shaper | Drive, Xfer, band Gain/Freq | DRIVE, BASS/MID/TRBL + freqs | yes except Xfer (engine gap) |
| comp | Atk (0-228 MS), Rel (0-3000 MS), Ratio, Threshold | ATK, REL, RATIO, THRSH | yes — ranges narrower than manual (stock engine curves) |
| comp | MakeUpGain, gate/expander family 950-957 | — | engine gap |

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
