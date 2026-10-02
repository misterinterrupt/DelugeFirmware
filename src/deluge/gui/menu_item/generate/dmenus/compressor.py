from dmui.dsl import *

threshold = Menu(
    "audio_compressor::Threshold",
    "compThreshold",
    ["{name}", "{title}"],
    "compressor/threshold.md",
    name="STRING_FOR_THRESHOLD",
)

attack = Menu(
    "audio_compressor::Attack",
    "compAttack",
    ["{name}", "{title}"],
    "compressor/attack.md",
    name="STRING_FOR_ATTACK",
)

release = Menu(
    "audio_compressor::Release",
    "compRelease",
    ["{name}", "{title}"],
    "compressor/release.md",
    name="STRING_FOR_RELEASE",
)

ratio = Menu(
    "audio_compressor::Ratio",
    "compRatio",
    ["{name}", "{title}"],
    "compressor/ratio.md",
    name="STRING_FOR_RATIO",
)

input_gain = Menu(
    "audio_compressor::InputGain",
    "compInputGain",
    ["{name}", "{title}"],
    "compressor/input_gain.md",
    name="STRING_FOR_INPUT_GAIN",
)

makeup_gain = Menu(
    "audio_compressor::MakeupGain",
    "compMakeupGain",
    ["{name}", "{title}"],
    "compressor/makeup_gain.md",
    name="STRING_FOR_MAKEUP_GAIN",
)

hpf = Menu(
    "audio_compressor::SideHPF",
    "compHPF",
    ["{name}", "{title}"],
    "compressor/hpf.md",
    name="STRING_FOR_HPF",
)

blend = Menu(
    "audio_compressor::Blend",
    "compBlend",
    ["{name}", "{title}"],
    "compressor/blend.md",
    name="STRING_FOR_BLEND",
)

menu = Submenu(
    "Submenu",
    "audioCompMenu",
    ["{name}", "%%CHILDREN%%"],
    "compressor/index.md",
    [threshold, ratio, attack, release, input_gain, makeup_gain, hpf, blend],
    name="STRING_FOR_COMMUNITY_FEATURE_MASTER_COMPRESSOR",
)
