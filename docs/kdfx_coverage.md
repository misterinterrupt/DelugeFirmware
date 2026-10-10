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
| KDFX DIST | Distortion 724–728 (Mono/Stereo Distort, +Cab, +EQ) | Insert-style: LOCUT/DRIVE/4 shaper modes/CAB/HCUT; "+EQ" = chain KDFX TONE after it |
| KDFX TUBEAMP | TubeAmp/PolyAmp<>MD>Chor/Flan 729–732 | Insert-style combination: amp (distortion engine) + modulated delay; CFG swaps order (AMP>MOD / MOD>AMP) |
| KDFX LASER | LaserVerb 911–913 (Mono/Lite/full) | Widening-spaces impulse train: TIME 0–2 S, SPAC 0–40 SMP, CONT (0=delay, 34%=reverse), FDBK to 100% |
| CHO+DLY / CHO+REV | 700 / 703–704 class combinations | Generic pair engine; Mod* trio for the chorus half; CFG where the manual's <> allows |
| KDFX RESFILT / ENVFILT / LFOSWP | 903 / 900 / 902 | TPT SVF bank: TYPE LP/HP/BP/NOTCH, RES 0-50 DB (self-ring only at top); ENVFILT adds MINF + bipolar SWEEP + ATK/REL 0-300 DB/S; LFOSWP adds BPM+beats LFO (FIX/SYS clock via per-block tempo push), MIN/MAX limits (inverting allowed), 5 shapes + width + smoothing |
| KDFX RINGMOD | 907 | MODE L*R (only LEFT passes dry) / OSC: Osc1 shape+width+smooth + 4 sines, each Lvl + Freq 16-25088 HZ; one generic setRmOsc(osc, which, v) virtual |
| KDFX QUANT / QNT+FLG | (714 partner / 714) | DYN 0-144 DB (31-bit cap = transparent top), HEAD 0-144 DB, DC Off/-79-0 DB; QNT+FLG fixed-order pair |
| SPEC 4-TAP | 135 Spectral Multitap (4-tap) | Loop + FDBK + Fdbk Image + feedback HPF/TONE; per-tap comb (P1-P4, Chamberlin resonator, unity peak), PtAmt (A1-A4), Shapr (S1-S4), tap fractions (T1-T4) |
| FLG+DLY / FLG+REV / FLG+LASER | 706 / 710 / 711 | Pair engine, fixed order (manual "+") |
| SHPR+REV | 718 Shaper<>Reverb | Pair engine; tone drive/EQ + full reverb suite; CFG. The EQ/shaper routes to the primary half exist now (the SHPR/BASS/MID/TRBL columns used to be dead) |
| REV<>COMP | 719 Reverb<>Compress | Pair engine (reverb + stock compressor half via the Mod* trio: THRSH/RATIO/ATK); CFG RVB>CMP / CMP>RVB |
| TUBAMP<>FLG | 730 TubeAmp<>MD>Flan (our pair: TubeAmp<>Flan) | Pair engine; tubeamp drive/mode/LOCUT/CAB/HCUT + flanger Mod* trio; CFG TUB>FLG / FLG>TUB |
| PTCHR | 908 Pitcher | Tuned harmonic resonator bank per docs/pitcher_bloc_spec.md: PITCH C-1..G9 (MIDI note), PTCH-OFF ±12 ST (FXMod target), ODD/PAIR/QTR/HALF drawbar weights; 16 Chamberlin resonators, unity peak, Q≈25, per-block tuning |
| FLG<>PTCHR | 712 Flange<>Pitcher | Pair engine; pitcher leads, flanger rides the Mod* trio; CFG PT>FLG / FLG>PT |
| MNPTCHR+CHO / MNPTCHR+FLN | 720 / 721 MonoPitcher+Chor / +Flan | Pair engine, fixed order (our pitcher runs stereo in the pair — the manual's "Mono" is a PAU saver we don't need) |
| PTCHR+CHO+DLY / PTCHR+FLN+DLY | 722 / 723 Pitcher+Chor+Dly / Pitcher+Flan+Dly | KdfxPitcherTriple: pitcher → modulated half → delay, fixed order; TIME/FDBK/TONE/XFEED/HPF/PHASE on the delay half |
| COMPRESSOR | Comp/gate family 950–957 stand-in | Stock engine wrapped insert-style; THRSH/RATIO/ATK/REL/SIDE/BLEND |

Send-style blocs wet-add (reverb-class); tremolo/panner are insert engines via
`Base::isInsert()/processInsert()` — the same path the COMPRESSOR/FILTER adapters will use.

## Track FX (milestone 2, banked + pending device verification)

Each ModControllable owns a full FxChain (3 slots, same engines/bloc UI, same XML
idiom — `<fxChain>` inside the track's tags). The menu resolves the chain by
context (`getCurrentFxChain()`: sound/kit/audio clip in clip view, master at song
level). Send architecture: a track chain slot can hold a **GLOBAL_SEND** bloc
(BUS 0-2 / PAN / IN-GN / LEVEL) which accumulates the track's post-chain audio
into the master chain's per-slot send buses; the master chain mixes bus audio
into each slot's input at render time (parallel into one slot = send semantics).
Buses clear per block before track rendering. The stock reverb send is
superseded: legacy songs regain reverb by placing a reverb bloc in a master
slot. NOT YET DEVICE-VERIFIED.

## Not yet implemented (manual backlog, by category)

- **Reverb variants** 1–15 as distinct voicings (MiniVerb, gated, plate/room/hall presets)
- **Combination chains** 700–723 remainder: tone-wheel organ family 733–737; 730 is covered by the TUBAMP<>FLG pair stand-in
- **Tone wheel organ** 733–737 (VibChor+Rotor, KB3)
- **Filters 904/905/906** (Dual Res, EQ Morpher 4-PAU, Mono EQ Morpher) + 901 Trig Envelope Filt
- **Comp/gate refinements** on the adapter (manual attack range 0-228 MS, release 0-3000 MS, makeup gain)
- **Studio/mixdown** 958–967 (Super Gate, enhancers, SRS)
- **Tools** 966–970, 998–999 (Stereo Image, Mono→Stereo, Graphic EQs, 5 Band EQ)
