# Buglist

User-reported issues on the FX-blocs branch.

## Spectral 4-Tap (KDFX 135)

1. **Several P params appear to do nothing.** ✅ FIXED — three stacked causes:
   - **S2–S4 (tap 2–4 shapers) were dead.** They rode the int-param ids
     (`ChorusVoices`/`ShaperMode`/`ComboCfg`) whose FxBloc flush casts the
     0..1 cache value to `int32_t`, truncating it to 0 — the shapers never
     left their 0.10x floor. They now ride the float `ModRate`/`ModDepth`/
     `ModFdbk` ids.
   - **The P (pitch) and A (PtAmt) params were real but near-inaudible**: the
     "comb" was a one-pole LP-minus-HP pseudo-bandpass (Q≈1, tiny output), so
     sweeping pitch or amount barely changed anything. Replaced with a
     Chamberlin two-pole resonator (unity peak gain, Q≈8) — a genuinely tuned
     peak, same voice as the Pitcher engine.
   - **Defaults never reached the engine**: the coefficient fields were only
     computed in setters, so a freshly picked bloc rendered with zero
     coefficients while the menu showed the defaults. The constructor now
     tunes from the declared defaults.

2. **Frequency param numeric-place selection looks reversed.** ✅ FIXED —
   values ≥ 995 (TONE at default ~3.7K, HPF at the top) were compacted to
   K-notation ("3.7K"), and the decimal cursor treated the K-notation dot as
   a decimal point — so the caret sat on a digit the encoder wasn't stepping.
   The value formatter now drops the unit before resorting to K-notation
   ("7019" instead of "3.7K"), keeping every digit on screen and the selected
   place honest; the renderer also guards the K-dot case directly.

## Found while fixing (also fixed)

3. **`<fxChain>` load clamped every saved bloc model to COMPRESSOR (=1).**
   The reader clamped the persisted enum ordinal to `0..COMPRESSOR` instead of
   the last valid model, so any saved FREEVERB/DLY/etc. slot loaded as
   COMPRESSOR. Fixed; new models are appended after GLOBAL_SEND so old saves
   keep their numbering.

4. **REV<>COMP and TUBAMP<>FLG were built but unreachable** (absent from the
   bloc chooser) and both pointed at the TONE+REV param table — wrong labels
   and dead columns (SHPR/BASS/MID/TRBL did nothing). Now selectable, with
   their own tables and per-combo CFG labels; the pair engine also routes the
   EQ/shaper ids to the primary half, which fixes the same dead columns on
   TONE+REV.
