# Buglist

User-reported issues on the FX-blocs branch.

## Spectral 4-Tap (KDFX 135)

1. **Several P params appear to do nothing.** The SCALE params have audible effect, but the other params don't seem to do anything obvious (if anything at all). Needs an audit of which params are actually wired into the DSP vs. dead knobs.

2. **Frequency param numeric-place selection looks reversed.** When editing the frequency params, the digit place selected on screen doesn't match the digits that actually change — the selected place and the changing numbers are mismatched, possibly reversed.
