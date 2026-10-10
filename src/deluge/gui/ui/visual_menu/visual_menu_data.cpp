/*
 * Copyright © 2024 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

#include "visual_menu_data.h"
#include "gui/l10n/strings.h"
#include "gui/menu_item/menu_item.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

using namespace deluge;
using namespace deluge::gui::ui::visual_menu;
using enum deluge::l10n::String;

#include "gui/menu_item/audio_compressor/compressor_params.h"
#include "gui/menu_item/audio_compressor/compressor_values.h"
#include "gui/menu_item/envelope/segment.h"
#include "gui/menu_item/filter/hpf_freq.h"
#include "gui/menu_item/filter/hpf_mode.h"
#include "gui/menu_item/filter/lpf_freq.h"
#include "gui/menu_item/filter/lpf_mode.h"
#include "gui/menu_item/filter/morph.h"
#include "gui/menu_item/filter_route.h"
#include "gui/menu_item/modulator/transpose.h"
#include "gui/menu_item/osc/pulse_width.h"
#include "gui/menu_item/osc/retrigger_phase.h"
#include "gui/menu_item/osc/source/feedback.h"
#include "gui/menu_item/osc/source/volume.h"
#include "gui/menu_item/osc/source/wave_index.h"
#include "gui/menu_item/arpeggiator/mode.h"
#include "gui/menu_item/arpeggiator/note_mode.h"
#include "gui/menu_item/arpeggiator/octave_mode.h"
#include "gui/menu_item/arpeggiator/octaves.h"
#include "gui/menu_item/arpeggiator/preset_mode.h"
#include "gui/menu_item/arpeggiator/rate.h"
#include "gui/menu_item/arpeggiator/rhythm.h"
#include "gui/menu_item/arpeggiator/sync.h"
#include "gui/menu_item/unpatched_param/sound_unpatched_param.h"
#include "gui/menu_item/osc/sync.h"
#include "gui/menu_item/osc/type.h"
#include "gui/menu_item/patched_param/integer.h"
#include "gui/menu_item/patched_param/integer_non_fm.h"
#include "gui/menu_item/sample/transpose.h"
#include "gui/menu_item/source/patched_param/fm.h"
#include "gui/menu_item/unison/count.h"
#include "gui/menu_item/unison/detune.h"
#include "gui/menu_item/unpatched_param.h"
#include "gui/menu_item/lfo/rate.h"
#include "gui/menu_item/lfo/sync.h"
#include "gui/menu_item/lfo/type.h"
#include "gui/menu_item/modulator/destination.h"
#include "gui/menu_item/voice/polyphony.h"
#include "gui/menu_item/voice/priority.h"
#include "gui/menu_item/fx/clipping.h"
#include "gui/menu_item/midi/bank.h"
#include "gui/menu_item/midi/pgm.h"
#include "gui/menu_item/midi/sub.h"
#include "gui/menu_item/midi/y_axis_to_cc1.h"
#include "gui/menu_item/sidechain/attack.h"
#include "gui/menu_item/sidechain/release.h"
#include "gui/menu_item/sidechain/send.h"
#include "gui/menu_item/sidechain/sync.h"
#include "gui/menu_item/sidechain/volume.h"
#include "gui/menu_item/unpatched_param/updating_reverb_params.h"
#include "gui/menu_item/audio_clip/attack.h"
#include "gui/menu_item/audio_clip/reverse.h"
#include "gui/menu_item/audio_clip/transpose.h"
#include "gui/menu_item/audio_interpolation.h"
#include "gui/menu_item/unpatched_param/pan.h"
#include "gui/menu_item/patched_param/pan.h"
#include "gui/menu_item/synth_mode.h"
#include "gui/menu_item/patch_cable_strength/fixed.h"
#include "gui/menu_item/master_transpose.h"
#include "gui/menu_item/fx/within_env.h"
#include "gui/menu_item/fx_bloc/master_fx_items.h"
#include "gui/ui/sound_editor.h"
#include "model/global_effectable/global_effectable_for_clip.h"
#include "processing/sound/sound.h"

// Defined at global scope in menus.cpp
extern deluge::gui::menu_item::fx::FxCutoff fxCutoffMenu;

// -------------------------------------------------------------------------------------------------
// External MenuItem declarations
// -------------------------------------------------------------------------------------------------
// Filters & Envelopes
extern deluge::gui::menu_item::filter::LPFFreq lpfFreqMenu;
extern deluge::gui::menu_item::patched_param::Integer lpfResMenu;
extern deluge::gui::menu_item::filter::LPFMode lpfModeMenu;
extern deluge::gui::menu_item::filter::FilterMorph lpfMorphMenu;
extern deluge::gui::menu_item::filter::HPFFreq hpfFreqMenu;
extern deluge::gui::menu_item::patched_param::Integer hpfResMenu;
extern deluge::gui::menu_item::filter::HPFMode hpfModeMenu;
extern deluge::gui::menu_item::filter::FilterMorph hpfMorphMenu;
extern deluge::gui::menu_item::FilterRouting filterRoutingMenu;

extern deluge::gui::menu_item::envelope::Segment envAttackMenu;
extern deluge::gui::menu_item::envelope::Segment envDecayMenu;
extern deluge::gui::menu_item::envelope::Segment envSustainMenu;
extern deluge::gui::menu_item::envelope::Segment envReleaseMenu;

// Master & Voice
extern deluge::gui::menu_item::patched_param::Integer volumeMenu;
extern deluge::gui::menu_item::patched_param::Pan panMenu;
extern deluge::gui::menu_item::MasterTranspose masterTransposeMenu;
extern deluge::gui::menu_item::patch_cable_strength::Fixed vibratoMenu;
extern deluge::gui::menu_item::SynthMode synthModeMenu;
extern deluge::gui::menu_item::UnpatchedParam portaMenu;
extern deluge::gui::menu_item::voice::PolyphonyType polyphonyMenu;
extern deluge::gui::menu_item::voice::Priority priorityMenu;
extern deluge::gui::menu_item::unison::Count numUnisonMenu;
extern deluge::gui::menu_item::unison::Detune unisonDetuneMenu;
namespace deluge::gui::menu_item::unison {
extern StereoSpread stereoSpreadMenu;
}

// Oscillators
extern deluge::gui::menu_item::osc::Type oscTypeMenu;
extern deluge::gui::menu_item::osc::source::Volume sourceVolumeMenu;
extern deluge::gui::menu_item::sample::Transpose sourceTransposeMenu;
extern deluge::gui::menu_item::osc::source::WaveIndex sourceWaveIndexMenu;
extern deluge::gui::menu_item::osc::PulseWidth pulseWidthMenu;
extern deluge::gui::menu_item::osc::source::Feedback sourceFeedbackMenu;
extern deluge::gui::menu_item::osc::Sync oscSyncMenu;
extern deluge::gui::menu_item::osc::RetriggerPhase oscPhaseMenu;
extern deluge::gui::menu_item::patched_param::IntegerNonFM noiseMenu;

// Modulators
extern deluge::gui::menu_item::source::patched_param::FM modulatorVolume;
extern deluge::gui::menu_item::modulator::Transpose modulatorTransposeMenu;
extern deluge::gui::menu_item::osc::RetriggerPhase modulatorPhaseMenu;
extern deluge::gui::menu_item::source::patched_param::FM modulatorFeedbackMenu;
extern deluge::gui::menu_item::modulator::Destination modulatorDestMenu;

// LFOs
extern deluge::gui::menu_item::lfo::Rate lfo1RateMenu;
extern deluge::gui::menu_item::lfo::Sync lfo1SyncMenu;
extern deluge::gui::menu_item::lfo::Type lfo1TypeMenu;
extern deluge::gui::menu_item::lfo::Rate lfo2RateMenu;
extern deluge::gui::menu_item::lfo::Sync lfo2SyncMenu;
extern deluge::gui::menu_item::lfo::Type lfo2TypeMenu;


extern deluge::gui::menu_item::fx::Clipping clippingMenu;
extern deluge::gui::menu_item::patched_param::Integer foldMenu;

extern deluge::gui::menu_item::audio_compressor::Threshold compThreshold;
extern deluge::gui::menu_item::audio_compressor::Ratio compRatio;
extern deluge::gui::menu_item::audio_compressor::Attack compAttack;
extern deluge::gui::menu_item::audio_compressor::Release compRelease;
extern deluge::gui::menu_item::audio_compressor::InputGain compInputGain;
extern deluge::gui::menu_item::audio_compressor::MakeupGain compMakeupGain;
extern deluge::gui::menu_item::audio_compressor::SideHPF compHPF;
extern deluge::gui::menu_item::audio_compressor::Blend compBlend;

// Sidechain
extern deluge::gui::menu_item::sidechain::VolumeShortcut sidechainVolumeShortcutMenu;
extern deluge::gui::menu_item::sidechain::Attack sidechainAttackMenu;
extern deluge::gui::menu_item::sidechain::Release sidechainReleaseMenu;
extern deluge::gui::menu_item::sidechain::Sync sidechainSyncMenu;
extern deluge::gui::menu_item::sidechain::Send sidechainSendMenu;
extern deluge::gui::menu_item::unpatched_param::UpdatingReverbParams sidechainShapeMenu;

// Arpeggiator
extern deluge::gui::menu_item::arpeggiator::PresetMode arpPresetModeMenu;
extern deluge::gui::menu_item::arpeggiator::Mode arpModeMenu;
extern deluge::gui::menu_item::arpeggiator::Sync arpSyncMenu;
extern deluge::gui::menu_item::arpeggiator::Octaves arpOctavesMenu;
extern deluge::gui::menu_item::arpeggiator::OctaveMode arpOctaveModeMenu;
extern deluge::gui::menu_item::arpeggiator::NoteMode arpNoteModeMenu;
extern deluge::gui::menu_item::arpeggiator::OnlyForSoundUnpatchedParam arpGateMenu;
extern deluge::gui::menu_item::arpeggiator::Rate arpRateMenu;
extern deluge::gui::menu_item::arpeggiator::Rhythm arpRhythmMenu;
extern deluge::gui::menu_item::arpeggiator::OnlyForSoundUnpatchedParam arpSequenceLengthMenu;
extern deluge::gui::menu_item::arpeggiator::OnlyForSoundUnpatchedParam arpRatchetAmountMenu;
extern deluge::gui::menu_item::arpeggiator::OnlyForSoundUnpatchedParam arpRatchetProbabilityMenu;

// Global Parameter Variants (for Audio Clips, Song View, Kits)
extern deluge::gui::menu_item::UnpatchedParam globalLevelMenu;
extern deluge::gui::menu_item::unpatched_param::Pan globalPanMenu;
extern deluge::gui::menu_item::UnpatchedParam globalPitchMenu;
extern deluge::gui::menu_item::UnpatchedParam globalLPFFreqMenu;
extern deluge::gui::menu_item::UnpatchedParam globalLPFResMenu;
extern deluge::gui::menu_item::UnpatchedParam globalLPFMorphMenu;
extern deluge::gui::menu_item::UnpatchedParam globalHPFFreqMenu;
extern deluge::gui::menu_item::UnpatchedParam globalHPFResMenu;
extern deluge::gui::menu_item::UnpatchedParam globalHPFMorphMenu;
extern deluge::gui::menu_item::unpatched_param::UpdatingReverbParams globalSidechainVolumeMenu;

// Audio Clip specifics
extern deluge::gui::menu_item::audio_clip::Reverse audioClipReverseMenu;
extern deluge::gui::menu_item::audio_clip::Transpose audioClipTransposeMenu;
extern deluge::gui::menu_item::audio_clip::Attack audioClipAttackMenu;
extern deluge::gui::menu_item::AudioInterpolation audioClipInterpolationMenu;

// MIDI specifics
extern deluge::gui::menu_item::midi::PGM midiPGMMenu;
extern deluge::gui::menu_item::midi::Bank midiBankMenu;
extern deluge::gui::menu_item::midi::Sub midiSubMenu;
extern deluge::gui::menu_item::midi::MPEYToModWheel mpeyToModWheelMenu;

namespace deluge::gui::ui::visual_menu {

// -------------------------------------------------------------------------------------------------
// 1. SYNTH SOUND CATEGORIES & PAGES
// -------------------------------------------------------------------------------------------------
static const VisualPage synthMasterPages[] = {
    {"Main Level", STRING_FOR_MASTER_LEVEL,
     {{{&volumeMenu, "LEVEL"}, {&panMenu, "PAN"}, {&masterTransposeMenu, "PITCH"}, {&vibratoMenu, "VIB"}}}},
    {"Voice Mode", STRING_FOR_VOICE,
     {{{&synthModeMenu, "MODE"}, {&portaMenu, "GLIDE"}, {&polyphonyMenu, "POLY"}, {&priorityMenu, "PRIOR"}}}},
    {"Unison", STRING_FOR_UNISON,
     {{{&numUnisonMenu, "COUNT"}, {&unisonDetuneMenu, "DETUNE"}, {&deluge::gui::menu_item::unison::stereoSpreadMenu, "SPREAD"}, {nullptr, ""}}}},
};

static const VisualPage synthFilterPages[] = {
    {"Low-Pass Filter", STRING_FOR_LPF,
     {{{&lpfFreqMenu, "CUTOFF"}, {&lpfResMenu, "RESO"}, {&lpfMorphMenu, "MORPH"}, {&lpfModeMenu, "MODE"}}}},
    {"High-Pass Filter", STRING_FOR_HPF,
     {{{&hpfFreqMenu, "CUTOFF"}, {&hpfResMenu, "RESO"}, {&hpfMorphMenu, "MORPH"}, {&hpfModeMenu, "MODE"}}}},
    {"Routing", STRING_FOR_FILTER_ROUTE,
     {{{&filterRoutingMenu, "ROUTE"}, {nullptr, ""}, {nullptr, ""}, {nullptr, ""}}}},
};

static const VisualPage synthEnvPages[] = {
    {"Envelope 1 (Amp)", STRING_FOR_ENVELOPE_1,
     {{{&envAttackMenu, "ATK", 0}, {&envDecayMenu, "DEC", 0}, {&envSustainMenu, "SUS", 0}, {&envReleaseMenu, "REL", 0}}}},
    {"Envelope 2 (Mod)", STRING_FOR_ENVELOPE_2,
     {{{&envAttackMenu, "ATK", 1}, {&envDecayMenu, "DEC", 1}, {&envSustainMenu, "SUS", 1}, {&envReleaseMenu, "REL", 1}}}},
};

static const VisualPage synthOsc1Pages[] = {
    {"Osc 1 Wave", STRING_FOR_OSCILLATOR_1,
     {{{&oscTypeMenu, "TYPE", 0}, {&sourceVolumeMenu, "LEVEL", 0}, {&sourceTransposeMenu, "PITCH", 0}, {&sourceWaveIndexMenu, "WAVE", 0}}}},
    {"Osc 1 Shape", STRING_FOR_OSCILLATOR_1,
     {{{&pulseWidthMenu, "WIDTH", 0}, {&sourceFeedbackMenu, "FDBK", 0}, {&oscSyncMenu, "SYNC", 0}, {&oscPhaseMenu, "PHASE", 0}}}},
    {"Noise", EMPTY_STRING,
     {{{&noiseMenu, "NOISE"}, {nullptr, ""}, {nullptr, ""}, {nullptr, ""}}}},
};

static const VisualPage synthOsc2Pages[] = {
    {"Osc 2 Wave", STRING_FOR_OSCILLATOR_2,
     {{{&oscTypeMenu, "TYPE", 1}, {&sourceVolumeMenu, "LEVEL", 1}, {&sourceTransposeMenu, "PITCH", 1}, {&sourceWaveIndexMenu, "WAVE", 1}}}},
    {"Osc 2 Shape", STRING_FOR_OSCILLATOR_2,
     {{{&pulseWidthMenu, "WIDTH", 1}, {&sourceFeedbackMenu, "FDBK", 1}, {&oscSyncMenu, "SYNC", 1}, {&oscPhaseMenu, "PHASE", 1}}}},
};

static const VisualPage synthFmPages[] = {
    {"FM Modulator 1", STRING_FOR_MODULATOR_1,
     {{{&modulatorVolume, "LEVEL", 0}, {&modulatorTransposeMenu, "PITCH", 0}, {&modulatorPhaseMenu, "PHASE", 0}, {&modulatorFeedbackMenu, "FDBK", 0}}}},
    {"FM Modulator 2", STRING_FOR_MODULATOR_2,
     {{{&modulatorVolume, "LEVEL", 1}, {&modulatorTransposeMenu, "PITCH", 1}, {&modulatorPhaseMenu, "PHASE", 1}, {&modulatorDestMenu, "DEST", 1}}}},
};

static const VisualPage synthLfoPages[] = {
    {"LFO 1", STRING_FOR_LFO1,
     {{{&lfo1RateMenu, "RATE"}, {&lfo1SyncMenu, "SYNC"}, {&lfo1TypeMenu, "SHAPE"}, {nullptr, ""}}}},
    {"LFO 2", STRING_FOR_LFO2,
     {{{&lfo2RateMenu, "RATE"}, {&lfo2SyncMenu, "SYNC"}, {&lfo2TypeMenu, "SHAPE"}, {nullptr, ""}}}},
};

static const VisualPage synthFxPages[] = {
    {"Distortion", STRING_FOR_DISTORTION,
     {{{&clippingMenu, "CLIP"}, {nullptr, ""}, {nullptr, ""}, {&foldMenu, "FOLD"}}}},
    {"Compressor 1", STRING_FOR_COMMUNITY_FEATURE_MASTER_COMPRESSOR,
     {{{&compThreshold, "THRESH"}, {&compRatio, "RATIO"}, {&compAttack, "ATK"}, {&compRelease, "REL"}}}},
    {"Compressor 2", STRING_FOR_COMMUNITY_FEATURE_MASTER_COMPRESSOR,
     {{{&compInputGain, "IN-GN"}, {&compMakeupGain, "MAKEUP"}, {&compHPF, "HPF"}, {&compBlend, "BLEND"}}}},
    {"Sidechain", STRING_FOR_SIDECHAIN,
     {{{&sidechainVolumeShortcutMenu, "VOL"}, {&sidechainAttackMenu, "ATK"}, {&sidechainReleaseMenu, "REL"}, {&sidechainSyncMenu, "SYNC"}}}},
    {"Sidechain Send", STRING_FOR_SIDECHAIN,
     {{{&sidechainSendMenu, "SEND"}, {&sidechainShapeMenu, "SHAPE"}, {nullptr, ""}, {nullptr, ""}}}},
};

static const VisualPage synthArpPages[] = {
    {"Arp Main", STRING_FOR_ARPEGGIATOR,
     {{{&arpModeMenu, "MODE"}, {&arpRateMenu, "RATE"}, {&arpSyncMenu, "SYNC"}, {&arpGateMenu, "GATE"}}}},
    {"Arp Pattern", STRING_FOR_ARPEGGIATOR,
     {{{&arpOctavesMenu, "OCTS"}, {&arpOctaveModeMenu, "OCT-M"}, {&arpNoteModeMenu, "NOTE-M"}, {&arpRhythmMenu, "RHYTHM"}}}},
    {"Arp Ratchet", STRING_FOR_ARPEGGIATOR,
     {{{&arpSequenceLengthMenu, "LENGTH"}, {&arpRatchetAmountMenu, "RATCH"}, {&arpRatchetProbabilityMenu, "PROB"}, {&arpPresetModeMenu, "PRESET"}}}},
};

static const VisualCategory synthCategories[] = {
    {STRING_FOR_MASTER, nullptr, synthMasterPages},
    {STRING_FOR_FILTERS, nullptr, synthFilterPages},
    {EMPTY_STRING, "Envelopes", synthEnvPages},
    {STRING_FOR_OSCILLATOR_1, nullptr, synthOsc1Pages},
    {STRING_FOR_OSCILLATOR_2, nullptr, synthOsc2Pages},
    {STRING_FOR_FM, nullptr, synthFmPages},
    {EMPTY_STRING, "LFOs", synthLfoPages},
    {STRING_FOR_FX, nullptr, synthFxPages},
    {STRING_FOR_ARPEGGIATOR, nullptr, synthArpPages},
};

// -------------------------------------------------------------------------------------------------
// 2. AUDIO CLIP CATEGORIES & PAGES
// -------------------------------------------------------------------------------------------------
static const VisualPage audioClipMasterPages[] = {
    {"Main Level", STRING_FOR_MASTER_LEVEL,
     {{{&globalLevelMenu, "LEVEL"}, {&globalPanMenu, "PAN"}, {&audioClipTransposeMenu, "PITCH"}, {&audioClipAttackMenu, "ATTACK"}}}},
    {"Playback", STRING_FOR_SAMPLE,
     {{{&audioClipReverseMenu, "REVERSE"}, {&audioClipInterpolationMenu, "INTERP"}, {nullptr, ""}, {nullptr, ""}}}},
};

static const VisualPage audioClipFxPages[] = {
    {"Distortion", STRING_FOR_DISTORTION,
     {{{&clippingMenu, "CLIP"}, {nullptr, ""}, {nullptr, ""}, {nullptr, ""}}}},
    {"Sidechain", STRING_FOR_SIDECHAIN,
     {{{&globalSidechainVolumeMenu, "VOL"}, {&sidechainAttackMenu, "ATK"}, {&sidechainReleaseMenu, "REL"}, {&sidechainSyncMenu, "SYNC"}}}},
};

static const VisualCategory audioClipCategories[] = {
    {STRING_FOR_MASTER, nullptr, audioClipMasterPages},
    {STRING_FOR_FX, nullptr, audioClipFxPages},
};

// -------------------------------------------------------------------------------------------------
// 3. MIDI & CV CATEGORIES & PAGES
// -------------------------------------------------------------------------------------------------
static const VisualPage midiCvPages[] = {
    {"MIDI Out", STRING_FOR_MIDI,
     {{{&midiPGMMenu, "PGM"}, {&midiBankMenu, "BANK"}, {&midiSubMenu, "SUB"}, {&mpeyToModWheelMenu, "MPE-Y"}}}},
    {"Arp Main", STRING_FOR_ARPEGGIATOR,
     {{{&arpModeMenu, "MODE"}, {&arpRateMenu, "RATE"}, {&arpSyncMenu, "SYNC"}, {&arpGateMenu, "GATE"}}}},
    {"Arp Pattern", STRING_FOR_ARPEGGIATOR,
     {{{&arpOctavesMenu, "OCTS"}, {&arpOctaveModeMenu, "OCT-M"}, {&arpNoteModeMenu, "NOTE-M"}, {&arpRhythmMenu, "RHYTHM"}}}},
};

static const VisualCategory midiCvCategories[] = {
    {STRING_FOR_MIDI, nullptr, midiCvPages},
};

// -------------------------------------------------------------------------------------------------
// Modular master FX blocs (song mode). Slot models/routing write through the PAU
} // namespace deluge::gui::ui::visual_menu

namespace deluge::gui::menu_item::fx_bloc {
dsp::FxChain* getCurrentFxChain() {
	if (rootUIIsClipMinderScreen()) {
		if (soundEditor.currentSound != nullptr) {
			return &soundEditor.currentSound->fxChain;
		}
		if (soundEditor.currentModControllable != nullptr) {
			auto* chain = soundEditor.currentModControllable->getFxChain();
			if (chain != nullptr) {
				return chain;
			}
		}
	}
	return &AudioEngine::masterFxChain;
}
} // namespace deluge::gui::menu_item::fx_bloc

namespace deluge::gui::ui::visual_menu {
using namespace deluge::gui::menu_item::fx_bloc;

MasterSlotBloc masterSlotBlocMenus[3] = {{0}, {1}, {2}};
MasterRouting masterRoutingMenu{};
// Per-slot param item pool: defIndex is the absolute index into the bloc's
// paramDefs() table; the page layout below decides which are on screen.
MasterSlotParam masterParamMenus[3][dsp::FxBloc::kMaxSlotParams] = {
    {{0, 0}, {0, 1}, {0, 2}, {0, 3}, {0, 4}, {0, 5}, {0, 6}, {0, 7}, {0, 8}, {0, 9}, {0, 10}, {0, 11}, {0, 12},
     {0, 13}, {0, 14}, {0, 15}},
    {{1, 0}, {1, 1}, {1, 2}, {1, 3}, {1, 4}, {1, 5}, {1, 6}, {1, 7}, {1, 8}, {1, 9}, {1, 10}, {1, 11}, {1, 12},
     {1, 13}, {1, 14}, {1, 15}},
    {{2, 0}, {2, 1}, {2, 2}, {2, 3}, {2, 4}, {2, 5}, {2, 6}, {2, 7}, {2, 8}, {2, 9}, {2, 10}, {2, 11}, {2, 12},
     {2, 13}, {2, 14}, {2, 15}},
};

// FX BLOCS pages are built at runtime: Chain + (Bloc page + N param pages) per
// slot, where N follows the selected bloc's real param count (4 per page, the
// last page may be partial). Rebuilt whenever the song-mode menu is opened and
// whenever a slot's bloc changes — see rebuildFxBlocPages().
static VisualPage fxBlocRuntimePages[2 + 3 * (1 + dsp::FxBloc::kMaxSlotParams / 4)];
static size_t fxBlocRuntimePageCount = 0;
static char fxBlocRuntimeTitles[sizeof(fxBlocRuntimePages) / sizeof(VisualPage)][12];

static VisualCategory songCategories[3];
static constexpr size_t kFxBlocSongCategoryIndex = 2;

static VisualCategory synthCategoriesRuntime[10];
static constexpr size_t kFxBlocSynthCategoryIndex = 9;

static VisualCategory audioClipCategoriesRuntime[3];
static constexpr size_t kFxBlocAudioCategoryIndex = 2;

static void ensureCategoriesInit();

void rebuildFxBlocPages() {
	ensureCategoriesInit();
	size_t p = 0;
	auto nextTitle = [&p]() { return fxBlocRuntimeTitles[p]; };

	fxBlocRuntimePages[p++] = VisualPage{
	    .title = "FX Gate",
	    .titleString = EMPTY_STRING,
	    .slots = {VisualSlot{&::fxCutoffMenu, "CUTOFF", -1, 4}, VisualSlot{nullptr, "", -1, 0},
	              VisualSlot{nullptr, "", -1, 0}, VisualSlot{nullptr, "", -1, 0}}};

	fxBlocRuntimePages[p++] = VisualPage{
	    .title = "Chain",
	    .titleString = EMPTY_STRING,
	    .slots = {VisualSlot{&masterRoutingMenu, "ROUTE", -1, 4}, VisualSlot{nullptr, "", -1, 0},
	              VisualSlot{nullptr, "", -1, 0}, VisualSlot{nullptr, "", -1, 0}}};

	auto* chain = getCurrentFxChain();
	for (int32_t s = 0; s < 3; s++) {
		snprintf(nextTitle(), 12, "BLOC %d", s + 1);
		fxBlocRuntimePages[p++] = VisualPage{
		    .title = fxBlocRuntimeTitles[p - 1],
		    .titleString = EMPTY_STRING,
		    .slots = {VisualSlot{&masterSlotBlocMenus[s], "BLOC", -1, 4}, VisualSlot{nullptr, "", -1, 0},
		              VisualSlot{nullptr, "", -1, 0}, VisualSlot{nullptr, "", -1, 0}}};

		auto model = chain != nullptr ? chain->slot(s).getModel() : dsp::FxBloc::Model::EMPTY;
		size_t n = dsp::FxBloc::paramDefs(model).size();
		size_t numParamPages = std::max<size_t>(1, (n + 3) / 4);
		for (size_t pg = 0; pg < numParamPages; pg++) {
			char* t = nextTitle();
			snprintf(t, 12, "SLOT %d", s + 1);
			for (size_t plus = 0; plus < pg; plus++) {
				strncat(t, "+", 2);
			}
			VisualPage page{t, EMPTY_STRING, {}};
			for (size_t c = 0; c < 4; c++) {
				size_t idx = pg * 4 + c;
				if (idx < n) {
					page.slots[c] = VisualSlot{&masterParamMenus[s][idx], nullptr, -1, 1};
				}
				else {
					page.slots[c] = VisualSlot{nullptr, "", -1, 0};
				}
			}
			fxBlocRuntimePages[p++] = page;
		}
	}
	fxBlocRuntimePageCount = p;
	auto pagesSpan = std::span{fxBlocRuntimePages, fxBlocRuntimePageCount};
	songCategories[kFxBlocSongCategoryIndex].pages = pagesSpan;
	synthCategoriesRuntime[kFxBlocSynthCategoryIndex].pages = pagesSpan;
	audioClipCategoriesRuntime[kFxBlocAudioCategoryIndex].pages = pagesSpan;
}

// -------------------------------------------------------------------------------------------------
// Song-level (master FX) categories. Deliberately NOT the audio-clip tables: the audio-clip
// items (reverse / interpolation / transpose / attack) dereference getCurrentAudioClip(),
// which is null outside an actual audio clip — editing them from song mode is a hard fault.
// -------------------------------------------------------------------------------------------------
static const VisualPage songMasterPages[] = {
    {"Main Level", STRING_FOR_MASTER_LEVEL,
     {{{&globalLevelMenu, "LEVEL"}, {&globalPanMenu, "PAN"}, {nullptr, ""}, {nullptr, ""}}}},
};

static bool categoriesInitialized = false;
static void ensureCategoriesInit() {
	if (categoriesInitialized) {
		return;
	}
	categoriesInitialized = true;
	for (size_t i = 0; i < 9; i++) {
		synthCategoriesRuntime[i] = synthCategories[i];
	}
	synthCategoriesRuntime[kFxBlocSynthCategoryIndex] = {EMPTY_STRING, "FX BLOCS", {}};

	for (size_t i = 0; i < 2; i++) {
		audioClipCategoriesRuntime[i] = audioClipCategories[i];
	}
	audioClipCategoriesRuntime[kFxBlocAudioCategoryIndex] = {EMPTY_STRING, "FX BLOCS", {}};

	songCategories[0] = {STRING_FOR_MASTER, nullptr, songMasterPages};
	songCategories[1] = {STRING_FOR_FX, nullptr, audioClipFxPages};
	songCategories[2] = {EMPTY_STRING, "FX BLOCS", {}};
}

// -------------------------------------------------------------------------------------------------
// Getters
// -------------------------------------------------------------------------------------------------
std::span<const VisualCategory> getSynthCategories() {
	ensureCategoriesInit();
	rebuildFxBlocPages();
	return synthCategoriesRuntime;
}

std::span<const VisualCategory> getKitCategories() {
	ensureCategoriesInit();
	rebuildFxBlocPages();
	return synthCategoriesRuntime; // Kit sound drum rows use synth sound parameter tables
}

std::span<const VisualCategory> getAudioClipCategories() {
	ensureCategoriesInit();
	rebuildFxBlocPages();
	return audioClipCategoriesRuntime;
}

std::span<const VisualCategory> getMidiCvCategories() {
	return midiCvCategories;
}

std::span<const VisualCategory> getGlobalFxCategories() {
	ensureCategoriesInit();
	rebuildFxBlocPages(); // page structure follows the currently selected blocs
	return songCategories;
}

bool findMenuItemLocation(std::span<const VisualCategory> categories, const MenuItem* targetItem,
                          int32_t* outCategory, int32_t* outPage, int32_t* outSlot) {
	if (targetItem == nullptr) {
		return false;
	}

	for (size_t c = 0; c < categories.size(); ++c) {
		const auto& cat = categories[c];
		for (size_t p = 0; p < cat.pages.size(); ++p) {
			const auto& page = cat.pages[p];
			for (size_t s = 0; s < kSlotsPerPage; ++s) {
				if (page.slots[s].item == targetItem) {
					if (outCategory) *outCategory = static_cast<int32_t>(c);
					if (outPage) *outPage = static_cast<int32_t>(p);
					if (outSlot) *outSlot = static_cast<int32_t>(s);
					return true;
				}
			}
		}
	}
	return false;
}

} // namespace deluge::gui::ui::visual_menu
