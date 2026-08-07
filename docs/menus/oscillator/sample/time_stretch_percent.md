# Stretch

A duration multiplier for BPM-based time stretching, expressed as a percentage relative to the song tempo.

- `100.0%` — the sample plays in sync with the song tempo
- `50.0%` — the sample plays at 2x speed (half duration)
- `200.0%` — the sample plays at 0.5x speed (double duration)

Only has an effect when Original BPM is set to a non-zero value. A value of `0` means unset (treated as `100%`).
