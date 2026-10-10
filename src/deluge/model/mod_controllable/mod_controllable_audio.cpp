/*
 * Copyright © 2016-2023 Synthstrom Audible Limited
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

#include "model/mod_controllable/mod_controllable_audio.h"
#include "definitions_cxx.hpp"
#include "deluge/model/settings/runtime_feature_settings.h"
#include "dsp/fx_bloc/master_fx_chain.hpp"
#include "dsp/stereo_sample.h"
#include "gui/l10n/l10n.h"
#include "gui/views/automation_view.h"
#include "gui/views/performance_session_view.h"
#include "gui/views/session_view.h"
#include "gui/views/view.h"
#include "io/debug/log.h"
#include "io/midi/midi_device.h"
#include "io/midi/midi_engine.h"
#include "io/midi/midi_takeover.h"
#include "mem_functions.h"
#include "model/clip/audio_clip.h"
#include "model/clip/instrument_clip.h"
#include "model/note/note_row.h"
#include "model/song/song.h"
#include "modulation/params/param_set.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound.h"
#include "storage/storage_manager.h"

namespace params = deluge::modulation::params;

extern int32_t spareRenderingBuffer[][SSI_TX_BUFFER_NUM_SAMPLES];

ModControllableAudio::ModControllableAudio() {
	// EQ
	// Filters
	lpfMode = FilterMode::TRANSISTOR_24DB_LEGACY;
	hpfMode = FilterMode::HPLADDER_LEGACY;
	filterRoute = FilterRoute::HIGH_TO_LOW;

	// Sample rate reduction

	// Saturation
	clippingAmount = 0;

	SyncLevel syncLevel;
	Song* song = preLoadedSong;
	if (!song) {
		song = currentSong;
	}
	if (song) {
		syncLevel = (SyncLevel)(8 - (song->insideWorldTickMagnitude + song->insideWorldTickMagnitudeOffsetFromBPM));
	}
	else {
		syncLevel = (SyncLevel)(8 - FlashStorage::defaultMagnitude);
	}
}

ModControllableAudio::~ModControllableAudio() {
}

void ModControllableAudio::cloneFrom(ModControllableAudio* other) {
	lpfMode = other->lpfMode;
	hpfMode = other->hpfMode;
	clippingAmount = other->clippingAmount;
	filterRoute = other->filterRoute;
	midiKnobArray.cloneFrom(&other->midiKnobArray); // Could fail if no RAM... not too big a concern
	if (other->getFxChain() != nullptr && getFxChain() != nullptr) {
		getFxChain()->cloneFrom(*other->getFxChain());
	}
}

void ModControllableAudio::initParams(ParamManager* paramManager) {

	UnpatchedParamSet* unpatchedParams = paramManager->getUnpatchedParamSet();

	Stutterer::initParams(paramManager);

}

void ModControllableAudio::processOutputVolume(StereoSample* buffer, int32_t numSamples, int32_t postFXVolume,
                                               int32_t postReverbVolume, int32_t pan, bool doAmplitudeIncrement) {

	StereoSample* bufferEnd = buffer + numSamples;

	int32_t postFXAndReverbVolumeL, postFXAndReverbVolumeR, amplitudeIncrementL, amplitudeIncrementR;
	postFXAndReverbVolumeL = postFXAndReverbVolumeR = (multiply_32x32_rshift32(postReverbVolume, postFXVolume) << 5);

	// The amplitude increment applies to the post-FX volume. We want to have it just so that we can respond better to
	// sidechain volume ducking, which is done through post-FX volume.
	if (doAmplitudeIncrement) {
		auto postReverbSendVolumeIncrement =
		    (int32_t)((double)(postReverbVolume - postReverbVolumeLastTime) / (double)numSamples);
		amplitudeIncrementL = amplitudeIncrementR =
		    (multiply_32x32_rshift32(postFXVolume, postReverbSendVolumeIncrement) << 5);
	}

	if (pan != 0 && AudioEngine::renderInStereo) {
		// Set up panning
		int32_t amplitudeL;
		int32_t amplitudeR;
		shouldDoPanning(pan, &amplitudeL, &amplitudeR);

		postFXAndReverbVolumeL = multiply_32x32_rshift32(postFXAndReverbVolumeL, amplitudeL) << 2;
		postFXAndReverbVolumeR = multiply_32x32_rshift32(postFXAndReverbVolumeR, amplitudeR) << 2;

		amplitudeIncrementL = multiply_32x32_rshift32(amplitudeIncrementL, amplitudeL) << 2;
		amplitudeIncrementR = multiply_32x32_rshift32(amplitudeIncrementR, amplitudeR) << 2;
	}

	StereoSample* inputSample = buffer;

	do {
		StereoSample processingSample = *inputSample;

		if (doAmplitudeIncrement) {
			postFXAndReverbVolumeL += amplitudeIncrementL;
			postFXAndReverbVolumeR += amplitudeIncrementR;
		}

		// Apply post-fx and post-reverb-send volume (the tap the sidechain ducks)
		inputSample->l = multiply_32x32_rshift32(processingSample.l, postFXAndReverbVolumeL) << 5;
		inputSample->r = multiply_32x32_rshift32(processingSample.r, postFXAndReverbVolumeR) << 5;

	} while (++inputSample != bufferEnd);

	postReverbVolumeLastTime = postReverbVolume;
}

void ModControllableAudio::writeAttributesToFile(Serializer& writer) {
	writer.writeAttribute("lpfMode", (char*)lpfTypeToString(lpfMode));
	// Community Firmware parameters (always write them after the official ones, just before closing the parent tag)
}

void ModControllableAudio::writeParamAttributesToFile(Serializer& writer, ParamManager* paramManager,
                                                      bool writeAutomation, int32_t* valuesForOverride) {
	UnpatchedParamSet* unpatchedParams = paramManager->getUnpatchedParamSet();

	unpatchedParams->writeParamAsAttribute(writer, "stutterRate", params::UNPATCHED_STUTTER_RATE, writeAutomation,
	                                       false, valuesForOverride);
}

void ModControllableAudio::writeTagsToFile(Serializer& writer) {
	// MIDI knobs
	if (midiKnobArray.getNumElements()) {
		writer.writeOpeningTag("midiKnobs");
		for (int32_t k = 0; k < midiKnobArray.getNumElements(); k++) {
			MIDIKnob* knob = midiKnobArray.getElement(k);
			writer.writeOpeningTagBeginning("midiKnob");
			knob->midiInput.writeAttributesToFile(
			    writer,
			    MIDI_MESSAGE_CC); // Writes channel and CC, but not device - we do that below.
			writer.writeAttribute("relative", knob->relative);
			writer.writeAttribute("controlsParam", params::paramNameForFile(unpatchedParamKind_,
			                                                                knob->paramDescriptor.getJustTheParam()));
			if (!knob->paramDescriptor.isJustAParam()) { // TODO: this only applies to Sounds
				writer.writeAttribute("patchAmountFromSource",
				                      sourceToString(knob->paramDescriptor.getTopLevelSource()));

				if (knob->paramDescriptor.hasSecondSource()) {
					writer.writeAttribute("patchAmountFromSecondSource",
				                      sourceToString(knob->paramDescriptor.getSecondSourceFromTop()));
				}
			}

			// Because we manually called LearnedMIDI::writeAttributesToFile() above, we have to give the MIDIDevice its
			// own tag, cos that can't be written as just an attribute.
			if (knob->midiInput.device) {
				writer.writeOpeningTagEnd();
				knob->midiInput.device->writeReferenceToFile(writer);
				writer.writeClosingTag("midiKnob");
			}
			else {
				writer.closeTag();
			}
		}
		writer.writeClosingTag("midiKnobs");
	}

	// Modular track FX chain (Milestone 2b)
	auto* chain = getFxChain();
	if (chain != nullptr) {
		bool anySlot = false;
		for (size_t s = 0; s < dsp::FxChain::kNumSlots; s++) {
			if (!chain->slot(s).isEmpty()) {
				anySlot = true;
				break;
			}
		}
		if (anySlot || chain->getRouting() != dsp::FxChain::Routing::SERIES) {
			writer.writeOpeningTagBeginning("fxChain");
			writer.writeAttribute("routing", util::to_underlying(chain->getRouting()));
			writer.writeOpeningTagEnd();
			for (size_t s = 0; s < dsp::FxChain::kNumSlots; s++) {
				auto& bloc = chain->slot(s);
				if (bloc.isEmpty()) {
					continue;
				}
				writer.writeOpeningTagBeginning("slot");
				writer.writeAttribute("index", (int32_t)s);
				writer.writeAttribute("model", util::to_underlying(bloc.getModel()));
				for (const auto& def : dsp::FxBloc::paramDefs(bloc.getModel())) {
					if (def.param == dsp::FxBloc::Param::None) {
						continue;
					}
					const char* name = dsp::FxBloc::paramName(def.param);
					if (name == nullptr) {
						continue;
					}
					if (def.isInt) {
						writer.writeAttribute(name, (int32_t)std::lround(bloc.getParam(def.param)));
					}
					else {
						uint32_t v = bloc.getParam(def.param) * (uint32_t)2147483648u;
						v = std::min(v, (uint32_t)2147483647);
						writer.writeAttribute(name, v);
					}
				}
				writer.closeTag();
			}
			writer.writeClosingTag("fxChain");
		}
	}
}

void ModControllableAudio::writeParamTagsToFile(Serializer& writer, ParamManager* paramManager, bool writeAutomation,
                                                int32_t* valuesForOverride) {
	UnpatchedParamSet* unpatchedParams = paramManager->getUnpatchedParamSet();

	(void)unpatchedParams;
}

bool ModControllableAudio::readParamTagFromFile(Deserializer& reader, char const* tagName,
                                                ParamManagerForTimeline* paramManager, int32_t readAutomationUpToPos) {
	ParamCollectionSummary* unpatchedParamsSummary = paramManager->getUnpatchedParamSetSummary();
	UnpatchedParamSet* unpatchedParams = (UnpatchedParamSet*)unpatchedParamsSummary->paramCollection;

	if (!strcmp(tagName, "stutterRate")) {
		unpatchedParams->readParam(reader, unpatchedParamsSummary, params::UNPATCHED_STUTTER_RATE,
		                           readAutomationUpToPos);
		reader.exitTag("stutterRate");
	}

	else {
		return false;
	}

	return true;
}

// paramManager is optional
Error ModControllableAudio::readTagFromFile(Deserializer& reader, char const* tagName,
                                            ParamManagerForTimeline* paramManager, int32_t readAutomationUpToPos,
                                            Song* song) {

	int32_t p;

	if (!strcmp(tagName, "lpfMode")) {
		lpfMode = stringToLPFType(reader.readTagOrAttributeValue());
		reader.exitTag("lpfMode");
	}
	else if (!strcmp(tagName, "hpfMode")) {
		hpfMode = stringToLPFType(reader.readTagOrAttributeValue());
		reader.exitTag("hpfMode");
	}
	else if (!strcmp(tagName, "filterRoute")) {
		filterRoute = stringToFilterRoute(reader.readTagOrAttributeValue());
		reader.exitTag("filterRoute");
	}

	else if (!strcmp(tagName, "clippingAmount")) {
		clippingAmount = reader.readTagOrAttributeValueInt();
		reader.exitTag("clippingAmount");
	}

	// this is actually the sidechain but pre c1.1 songs save it as compressor
	else if (!strcmp(tagName, "midiKnobs")) {

		while (*(tagName = reader.readNextTagOrAttributeName())) {
			if (!strcmp(tagName, "midiKnob")) {

				MIDIDevice* device = nullptr;
				uint8_t channel;
				uint8_t ccNumber;
				bool relative;
				uint8_t p = params::GLOBAL_NONE;
				PatchSource s = PatchSource::NOT_AVAILABLE;
				PatchSource s2 = PatchSource::NOT_AVAILABLE;

				while (*(tagName = reader.readNextTagOrAttributeName())) {
					if (!strcmp(tagName, "device")) {
						device = MIDIDeviceManager::readDeviceReferenceFromFile(reader);
					}
					else if (!strcmp(tagName, "channel")) {
						channel = reader.readTagOrAttributeValueInt();
					}
					else if (!strcmp(tagName, "ccNumber")) {
						ccNumber = reader.readTagOrAttributeValueInt();
					}
					else if (!strcmp(tagName, "relative")) {
						relative = reader.readTagOrAttributeValueInt();
					}
					else if (!strcmp(tagName, "controlsParam")) {
						// if the unpatched kind for the current mod controllable is sound then we also want to check
						// against patched params. Otherwise skip them to avoid a bug from patched volume params having
						// the same name in files as unpatched global volumes
						p = params::fileStringToParam(unpatchedParamKind_, reader.readTagOrAttributeValue(),
						                              unpatchedParamKind_
						                                  == deluge::modulation::params::Kind::UNPATCHED_SOUND);
					}
					else if (!strcmp(tagName, "patchAmountFromSource")) {
						s = stringToSource(reader.readTagOrAttributeValue());
					}
					else if (!strcmp(tagName, "patchAmountFromSecondSource")) {
						s2 = stringToSource(reader.readTagOrAttributeValue());
					}
					reader.exitTag();
				}

				if (p != params::GLOBAL_NONE && p != params::PLACEHOLDER_RANGE) {
					MIDIKnob* newKnob = midiKnobArray.insertKnobAtEnd();
					if (newKnob) {
						newKnob->midiInput.device = device;
						newKnob->midiInput.channelOrZone = channel;
						newKnob->midiInput.noteOrCC = ccNumber;
						newKnob->relative = relative;

						if (s == PatchSource::NOT_AVAILABLE) {
							newKnob->paramDescriptor.setToHaveParamOnly(p);
						}
						else if (s2 == PatchSource::NOT_AVAILABLE) {
							newKnob->paramDescriptor.setToHaveParamAndSource(p, s);
						}
						else {
							newKnob->paramDescriptor.setToHaveParamAndTwoSources(p, s, s2);
						}
					}
				}
			}
			reader.exitTag();
		}
		reader.exitTag("midiKnobs");
	}

	else if (!strcmp(tagName, "fxChain")) {
		auto* chain = getFxChain();
		while (*(tagName = reader.readNextTagOrAttributeName())) {
			if (!strcmp(tagName, "routing")) {
				int32_t r = reader.readTagOrAttributeValueInt();
				if (chain != nullptr) {
					chain->setRouting(static_cast<dsp::FxChain::Routing>(
					    std::clamp<int32_t>(r, 0, util::to_underlying(dsp::FxChain::Routing::PARALLEL_SERIES))));
				}
				reader.exitTag("routing");
			}
			else if (!strcmp(tagName, "slot")) {
				int32_t slotIndex = -1;
				int32_t modelVal = 0;
				float params[dsp::FxBloc::kMaxSlotParams + 1] = {};
				uint32_t seen = 0;
				while (*(tagName = reader.readNextTagOrAttributeName())) {
					if (!strcmp(tagName, "index")) {
						slotIndex = reader.readTagOrAttributeValueInt();
						reader.exitTag("index");
					}
					else if (!strcmp(tagName, "model")) {
						modelVal = reader.readTagOrAttributeValueInt();
						reader.exitTag("model");
					}
					else {
						bool matched = false;
						for (uint32_t pid = 1; pid <= dsp::FxBloc::kMaxSlotParams && !matched; pid++) {
							const char* nm = dsp::FxBloc::paramName(static_cast<dsp::FxBloc::Param>(pid));
							if (nm != nullptr && !strcmp(tagName, nm)) {
								int32_t raw = reader.readTagOrAttributeValueInt();
								auto p = static_cast<dsp::FxBloc::Param>(pid);
								params[pid] = dsp::FxBloc::paramIsInt(p) ? (float)raw : (float)raw / 2147483648u;
								seen |= (1u << pid);
								matched = true;
							}
						}
						reader.exitTag(tagName);
					}
				}
				if (chain != nullptr && slotIndex >= 0 && slotIndex < (int32_t)dsp::FxChain::kNumSlots) {
					auto model = static_cast<dsp::FxBloc::Model>(
					    std::clamp<int32_t>(modelVal, 0, util::to_underlying(dsp::FxBloc::Model::COMPRESSOR)));
					bool ok = chain->slot(slotIndex).setModel(model);
					if (!ok) {
						// Budget/RAM limit: gracefully degrade to EMPTY
						chain->slot(slotIndex).setModel(dsp::FxBloc::Model::EMPTY);
					}
					else {
						for (const auto& def : dsp::FxBloc::paramDefs(model)) {
							if (def.param == dsp::FxBloc::Param::None) {
								continue;
							}
							uint32_t pid = static_cast<uint32_t>(def.param);
							if (seen & (1u << pid)) {
								chain->slot(slotIndex).setParam(def.param, params[pid]);
							}
						}
					}
				}
				reader.exitTag("slot");
			}
			else {
				reader.exitTag(tagName);
			}
		}
		reader.exitTag("fxChain");
	}

	else {
		return Error::RESULT_TAG_UNUSED;
	}

	return Error::NONE;
}

ModelStackWithAutoParam* ModControllableAudio::getParamFromMIDIKnob(MIDIKnob* knob,
                                                                    ModelStackWithThreeMainThings* modelStack) {

	ParamCollectionSummary* summary = modelStack->paramManager->getUnpatchedParamSetSummary();
	ParamCollection* paramCollection = summary->paramCollection;

	int32_t paramId = knob->paramDescriptor.getJustTheParam() - params::UNPATCHED_START;

	ModelStackWithParamId* modelStackWithParamId =
	    modelStack->addParamCollectionAndId(paramCollection, summary, paramId);

	ModelStackWithAutoParam* modelStackWithAutoParam = paramCollection->getAutoParamFromId(modelStackWithParamId);

	return modelStackWithAutoParam;
}

ModelStackWithThreeMainThings* ModControllableAudio::addNoteRowIndexAndStuff(ModelStackWithTimelineCounter* modelStack,
                                                                             int32_t noteRowIndex) {
	NoteRow* noteRow = nullptr;
	int32_t noteRowId = 0;
	ParamManager* paramManager;

	if (noteRowIndex != -1) {
		InstrumentClip* clip = (InstrumentClip*)modelStack->getTimelineCounter();
#if ALPHA_OR_BETA_VERSION
		if (noteRowIndex >= clip->noteRows.getNumElements()) {
			FREEZE_WITH_ERROR("E406");
		}
#endif
		noteRow = clip->noteRows.getElement(noteRowIndex);
		noteRowId = clip->getNoteRowId(noteRow, noteRowIndex);
		paramManager = &noteRow->paramManager;
	}
	else {
		if (modelStack->timelineCounterIsSet()) {
			paramManager = &modelStack->getTimelineCounter()->paramManager;
		}
		else {
			paramManager = modelStack->song->getBackedUpParamManagerPreferablyWithClip(
			    this,
			    nullptr); // Could be NULL if a NonAudioInstrument - those don't back up any paramManagers (when they
			              // even have them).
		}
	}

	ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
	    modelStack->addNoteRow(noteRowId, noteRow)->addOtherTwoThings(this, paramManager);
	return modelStackWithThreeMainThings;
}

bool ModControllableAudio::offerReceivedCCToLearnedParamsForClip(MIDIDevice* fromDevice, uint8_t channel,
                                                                 uint8_t ccNumber, uint8_t value,
                                                                 ModelStackWithTimelineCounter* modelStack,
                                                                 int32_t noteRowIndex) {
	bool messageUsed = false;

	// For each MIDI knob...
	for (int32_t k = 0; k < midiKnobArray.getNumElements(); k++) {
		MIDIKnob* knob = midiKnobArray.getElement(k);

		// If this is the knob...
		if (knob->midiInput.equalsNoteOrCC(fromDevice, channel, ccNumber)) {

			messageUsed = true;

			// See if this message is evidence that the knob is not "relative"
			if (value >= 16 && value < 112) {
				knob->relative = false;
			}

			int32_t modPos = 0;
			int32_t modLength = 0;
			bool isStepEditing = false;

			if (modelStack->timelineCounterIsSet()) {
				TimelineCounter* timelineCounter = modelStack->getTimelineCounter();

				// Only if this exact TimelineCounter is having automation step-edited, we can set the value for just a
				// region.
				if (view.modLength
				    && timelineCounter == view.activeModControllableModelStack.getTimelineCounterAllowNull()) {
					modPos = view.modPos;
					modLength = view.modLength;
					isStepEditing = true;
				}

				timelineCounter->possiblyCloneForArrangementRecording(modelStack);
			}

			// Ok, that above might have just changed modelStack->timelineCounter. So we're basically starting from
			// scratch now from that.
			ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
			    addNoteRowIndexAndStuff(modelStack, noteRowIndex);

			ModelStackWithAutoParam* modelStackWithParam = getParamFromMIDIKnob(knob, modelStackWithThreeMainThings);

			if (modelStackWithParam && modelStackWithParam->autoParam) {
				int32_t newKnobPos;
				int32_t currentValue;

				// get current value
				if (isStepEditing) {
					currentValue = modelStackWithParam->autoParam->getValuePossiblyAtPos(modPos, modelStackWithParam);
				}
				else {
					currentValue = modelStackWithParam->autoParam->getCurrentValue();
				}

				// convert current value to knobPos to compare to cc value being received
				int32_t knobPos =
				    modelStackWithParam->paramCollection->paramValueToKnobPos(currentValue, modelStackWithParam);

				// calculate new knob position based on value received and deluge current value
				newKnobPos = MidiTakeover::calculateKnobPos(knobPos, value, knob, false, CC_NUMBER_NONE, isStepEditing);

				// is the cc being received for the same value as the current knob pos? If so, do nothing
				if (newKnobPos == knobPos) {
					continue;
				}

				// Convert the New Knob Position to a Parameter Value
				int32_t newValue =
				    modelStackWithParam->paramCollection->knobPosToParamValue(newKnobPos, modelStackWithParam);

				// Set the new Parameter Value for the MIDI Learned Parameter
				modelStackWithParam->autoParam->setValuePossiblyForRegion(newValue, modelStackWithParam, modPos,
				                                                          modLength);

				// if you're in automation view and editing the same parameter that was just updated
				// by a learned midi knob, then re-render the pads on the automation editor grid
				if (getRootUI() == &automationView && !automationView.onArrangerView) {
					Clip* clip = (Clip*)modelStack->getTimelineCounter();
					// check that the clip that the param is being edited for is the same as the
					// current clip as the current clip is what's actively displayed in automation view
					if (clip == getCurrentClip()) {
						// pass the current clip because you want to check that you're editing the param
						// for the same clip active in automation view
						int32_t id = modelStackWithParam->paramId;
						params::Kind kind = modelStackWithParam->paramCollection->getParamKind();
						automationView.possiblyRefreshAutomationEditorGrid(clip, kind, id);
					}
				}
			}
		}
	}
	return messageUsed;
}

bool ModControllableAudio::offerReceivedCCToLearnedParamsForSong(
    MIDIDevice* fromDevice, uint8_t channel, uint8_t ccNumber, uint8_t value,
    ModelStackWithThreeMainThings* modelStackWithThreeMainThings) {
	bool messageUsed = false;

	// For each MIDI knob...
	for (int32_t k = 0; k < midiKnobArray.getNumElements(); k++) {
		MIDIKnob* knob = midiKnobArray.getElement(k);

		// If this is the knob...
		if (knob->midiInput.equalsNoteOrCC(fromDevice, channel, ccNumber)) {

			messageUsed = true;

			// See if this message is evidence that the knob is not "relative"
			if (value >= 16 && value < 112) {
				knob->relative = false;
			}

			int32_t modPos = 0;
			int32_t modLength = 0;
			bool isStepEditing = false;

			if (modelStackWithThreeMainThings->timelineCounterIsSet()) {
				TimelineCounter* timelineCounter = modelStackWithThreeMainThings->getTimelineCounter();

				// Only if this exact TimelineCounter is having automation step-edited, we can set the value for just a
				// region.
				if (view.modLength
				    && timelineCounter == view.activeModControllableModelStack.getTimelineCounterAllowNull()) {
					modPos = view.modPos;
					modLength = view.modLength;
					isStepEditing = true;
				}
			}

			ModelStackWithAutoParam* modelStackWithParam = getParamFromMIDIKnob(knob, modelStackWithThreeMainThings);

			if (modelStackWithParam && modelStackWithParam->autoParam) {
				int32_t newKnobPos;
				int32_t currentValue;

				// get current value
				if (isStepEditing) {
					currentValue = modelStackWithParam->autoParam->getValuePossiblyAtPos(modPos, modelStackWithParam);
				}
				else {
					currentValue = modelStackWithParam->autoParam->getCurrentValue();
				}

				// convert current value to knobPos to compare to cc value being received
				int32_t knobPos =
				    modelStackWithParam->paramCollection->paramValueToKnobPos(currentValue, modelStackWithParam);

				// calculate new knob position based on value received and deluge current value
				newKnobPos = MidiTakeover::calculateKnobPos(knobPos, value, knob, false, CC_NUMBER_NONE, isStepEditing);

				// is the cc being received for the same value as the current knob pos? If so, do nothing
				if (newKnobPos == knobPos) {
					continue;
				}

				// Convert the New Knob Position to a Parameter Value
				int32_t newValue =
				    modelStackWithParam->paramCollection->knobPosToParamValue(newKnobPos, modelStackWithParam);

				// Set the new Parameter Value for the MIDI Learned Parameter
				modelStackWithParam->autoParam->setValuePossiblyForRegion(newValue, modelStackWithParam, modPos,
				                                                          modLength);

				// check if you're currently editing the same learned param in automation view or
				// performance view if so, you will need to refresh the automation editor grid or the
				// performance view
				RootUI* rootUI = getRootUI();
				if (rootUI == &automationView || rootUI == &performanceSessionView) {
					int32_t id = modelStackWithParam->paramId;
					params::Kind kind = modelStackWithParam->paramCollection->getParamKind();

					if (rootUI == &automationView) {
						automationView.possiblyRefreshAutomationEditorGrid(nullptr, kind, id);
					}
					else {
						performanceSessionView.possiblyRefreshPerformanceViewDisplay(kind, id, newKnobPos);
					}
				}
			}
		}
	}
	return messageUsed;
}

// Returns true if the message was used by something
bool ModControllableAudio::offerReceivedPitchBendToLearnedParams(MIDIDevice* fromDevice, uint8_t channel, uint8_t data1,
                                                                 uint8_t data2,
                                                                 ModelStackWithTimelineCounter* modelStack,
                                                                 int32_t noteRowIndex) {

	bool messageUsed = false;

	// For each MIDI knob...
	for (int32_t k = 0; k < midiKnobArray.getNumElements(); k++) {
		MIDIKnob* knob = midiKnobArray.getElement(k);

		// If this is the knob...
		if (knob->midiInput.equalsNoteOrCC(fromDevice, channel,
		                                   128)) { // I've got 128 representing pitch bend here... why again?

			messageUsed = true;

			// Only if this exact TimelineCounter is having automation step-edited, we can set the value for just a
			// region.
			int32_t modPos = 0;
			int32_t modLength = 0;

			if (modelStack->timelineCounterIsSet()) {
				TimelineCounter* timelineCounter = modelStack->getTimelineCounter();
				if (view.modLength
				    && timelineCounter == view.activeModControllableModelStack.getTimelineCounterAllowNull()) {
					modPos = view.modPos;
					modLength = view.modLength;
				}

				timelineCounter->possiblyCloneForArrangementRecording(modelStack);
			}

			// Ok, that above might have just changed modelStack->timelineCounter. So we're basically starting from
			// scratch now from that.
			ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
			    addNoteRowIndexAndStuff(modelStack, noteRowIndex);

			ModelStackWithAutoParam* modelStackWithParam = getParamFromMIDIKnob(knob, modelStackWithThreeMainThings);

			if (modelStackWithParam->autoParam) {

				uint32_t value14 = (uint32_t)data1 | ((uint32_t)data2 << 7);

				int32_t newValue = (value14 << 18) - 2147483648;

				modelStackWithParam->autoParam->setValuePossiblyForRegion(newValue, modelStackWithParam, modPos,
				                                                          modLength);
				return true;
			}
		}
	}
	return messageUsed;
}

void ModControllableAudio::beginStutter(ParamManagerForTimeline* paramManager) {
	if (currentUIMode != UI_MODE_NONE && currentUIMode != UI_MODE_CLIP_PRESSED_IN_SONG_VIEW
	    && currentUIMode != UI_MODE_HOLDING_ARRANGEMENT_ROW
	    && currentUIMode != UI_MODE_HOLDING_ARRANGEMENT_ROW_AUDITION) {
		return;
	}
	if (Error::NONE
	    == stutterer.beginStutter(
	        this, paramManager, runtimeFeatureSettings.isOn(RuntimeFeatureSettingType::QuantizedStutterRate),
	        currentSong->getInputTickMagnitude(), playbackHandler.getTimePerInternalTickInverse())) {
		// Redraw the LEDs. Really only for quantized stutter, but doing it for unquantized won't hurt.
		view.notifyParamAutomationOccurred(paramManager);
		enterUIMode(UI_MODE_STUTTERING);
	}
}

void ModControllableAudio::processStutter(StereoSample* buffer, int32_t numSamples, ParamManager* paramManager) {
	if (stutterer.isStuttering(this)) {
		stutterer.processStutter(buffer, numSamples, paramManager, currentSong->getInputTickMagnitude(),
		                         playbackHandler.getTimePerInternalTickInverse());
	}
}

// paramManager is optional - if you don't send it, it won't restore the stutter rate and we won't redraw the LEDs
void ModControllableAudio::endStutter(ParamManagerForTimeline* paramManager) {
	stutterer.endStutter(paramManager);
	if (paramManager) {
		// Redraw the LEDs.
		view.notifyParamAutomationOccurred(paramManager);
	}
	exitUIMode(UI_MODE_STUTTERING);
}

char const* ModControllableAudio::getFilterTypeDisplayName(FilterType currentFilterType) {
	using enum deluge::l10n::String;
	switch (currentFilterType) {
	case FilterType::LPF:
		return l10n::get(STRING_FOR_LPF);
	case FilterType::HPF:
		return l10n::get(STRING_FOR_HPF);
	case FilterType::EQ:
		return l10n::get(STRING_FOR_EQ);
	default:
		return l10n::get(STRING_FOR_NONE);
	}
}

void ModControllableAudio::switchLPFMode() {
	lpfMode = static_cast<FilterMode>((util::to_underlying(lpfMode) + 1) % kNumLPFModes);
}

// for future use with FM
void ModControllableAudio::switchLPFModeWithOff() {
	lpfMode = static_cast<FilterMode>((util::to_underlying(lpfMode) + 1) % kNumLPFModes);
	switch (lpfMode) {
	case FilterMode::OFF:
		lpfMode = FilterMode::TRANSISTOR_12DB;
		break;
	case lastLpfMode:
		lpfMode = FilterMode::OFF;
		break;
	default:
		lpfMode = static_cast<FilterMode>((util::to_underlying(lpfMode) + 1));
	}
}

char const* ModControllableAudio::getFilterModeDisplayName(FilterType currentFilterType) {
	switch (currentFilterType) {
	case FilterType::LPF:
		return getLPFModeDisplayName();
	case FilterType::HPF:
		return getHPFModeDisplayName();
	default:
		return l10n::get(deluge::l10n::String::STRING_FOR_NONE);
	}
}

char const* ModControllableAudio::getLPFModeDisplayName() {

	using enum deluge::l10n::String;
	switch (lpfMode) {
	case FilterMode::TRANSISTOR_12DB_LEGACY:
		return l10n::get(STRING_FOR_12DB_LADDER_LEGACY);
	case FilterMode::TRANSISTOR_24DB_LEGACY:
		return l10n::get(STRING_FOR_24DB_LADDER_LEGACY);
	case FilterMode::TRANSISTOR_12DB:
		return l10n::get(STRING_FOR_12DB_LADDER);
	case FilterMode::TRANSISTOR_24DB:
		return l10n::get(STRING_FOR_24DB_LADDER);
	case FilterMode::TRANSISTOR_24DB_DRIVE:
		return l10n::get(STRING_FOR_DRIVE);
	case FilterMode::SVF_BAND:
		return l10n::get(STRING_FOR_SVF_BAND);
	case FilterMode::SVF_NOTCH:
		return l10n::get(STRING_FOR_SVF_NOTCH);
	default:
		return l10n::get(STRING_FOR_NONE);
	}
}

void ModControllableAudio::switchHPFMode() {
	// this works fine, the offset to the first hpf doesn't matter with the modulus
	hpfMode = static_cast<FilterMode>((util::to_underlying(hpfMode) + 1) % kNumHPFModes + kFirstHPFMode);
}

// for future use with FM
void ModControllableAudio::switchHPFModeWithOff() {
	switch (hpfMode) {
	case FilterMode::OFF:
		hpfMode = firstHPFMode;
		break;
	default:
		hpfMode = static_cast<FilterMode>((util::to_underlying(hpfMode) + 1));
	}
}

char const* ModControllableAudio::getHPFModeDisplayName() {
	using enum deluge::l10n::String;
	switch (hpfMode) {
	case FilterMode::HPLADDER:
		return l10n::get(STRING_FOR_HPLADDER);
	case FilterMode::HPLADDER_LEGACY:
		return l10n::get(STRING_FOR_HP_LADDER_LEGACY);
	case FilterMode::SVF_BAND:
		return l10n::get(STRING_FOR_SVF_BAND);
	case FilterMode::SVF_NOTCH:
		return l10n::get(STRING_FOR_SVF_NOTCH);
	default:
		return l10n::get(STRING_FOR_NONE);
	}
}

// This can get called either for hibernation, or because drum now has no active noteRow
void ModControllableAudio::wontBeRenderedForAWhile() {
	endStutter(nullptr);
}



// whichKnob is either which physical mod knob, or which MIDI CC code.
// For mod knobs, supply midiChannel as 255
// Returns false if fail due to insufficient RAM.
bool ModControllableAudio::learnKnob(MIDIDevice* fromDevice, ParamDescriptor paramDescriptor, uint8_t whichKnob,
                                     uint8_t modKnobMode, uint8_t midiChannel, Song* song) {

	bool overwroteExistingKnob = false;

	// If a mod knob
	if (midiChannel >= 16) {
		return false;

		// TODO: make function not virtual after this changed

		/*
		// If that knob was patched to something else...
		overwroteExistingKnob = (modKnobs[modKnobMode][whichKnob].s != s || modKnobs[modKnobMode][whichKnob].p != p);

		modKnobs[modKnobMode][whichKnob].s = s;
		modKnobs[modKnobMode][whichKnob].p = p;
		*/
	}

	// If a MIDI knob
	else {

		MIDIKnob* knob;

		// Was this MIDI knob already set to control this thing?
		for (int32_t k = 0; k < midiKnobArray.getNumElements(); k++) {
			knob = midiKnobArray.getElement(k);
			if (knob->midiInput.equalsNoteOrCC(fromDevice, midiChannel, whichKnob)
			    && paramDescriptor == knob->paramDescriptor) {
				// overwroteExistingKnob = (midiKnobs[k].s != s || midiKnobs[k].p != p);
				goto midiKnobFound;
			}
		}

		// Or if we're here, it doesn't already exist, so find an unused MIDIKnob
		knob = midiKnobArray.insertKnobAtEnd();
		if (!knob) {
			return false;
		}

midiKnobFound:
		knob->midiInput.noteOrCC = whichKnob;
		knob->midiInput.channelOrZone = midiChannel;
		knob->midiInput.device = fromDevice;
		knob->paramDescriptor = paramDescriptor;
		knob->relative = (whichKnob != 128); // Guess that it's relative, unless this is a pitch-bend "knob"
	}

	if (overwroteExistingKnob) {
		ensureInaccessibleParamPresetValuesWithoutKnobsAreZero(song);
	}

	return true;
}

// Returns whether anything was found to unlearn
bool ModControllableAudio::unlearnKnobs(ParamDescriptor paramDescriptor, Song* song) {
	bool anythingFound = false;

	// I've deactivated the unlearning of mod knobs, mainly because, if you want to unlearn a MIDI knob, you might not
	// want to also deactivate a mod knob to the same param at the same time

	for (int32_t k = 0; k < midiKnobArray.getNumElements();) {
		MIDIKnob* knob = midiKnobArray.getElement(k);
		if (knob->paramDescriptor == paramDescriptor) {
			anythingFound = true;
			midiKnobArray.deleteAtIndex(k);
		}
		else {
			k++;
		}
	}

	if (anythingFound) {
		ensureInaccessibleParamPresetValuesWithoutKnobsAreZero(song);
	}

	return anythingFound;
}

void ModControllableAudio::displayFilterSettings(bool on, FilterType currentFilterType) {
	if (display->haveOLED()) {
		if (on) {
			DEF_STACK_STRING_BUF(popupMsg, 40);
			popupMsg.append(getFilterTypeDisplayName(currentFilterType));
			if (currentFilterType != FilterType::EQ) {
				popupMsg.append("\n");
				popupMsg.append(getFilterModeDisplayName(currentFilterType));
			}
			display->popupText(popupMsg.c_str());
		}
		else {
			display->cancelPopup();
		}
	}
	else {
		if (on) {
			display->displayPopup(getFilterTypeDisplayName(currentFilterType));
		}
		else {
			display->displayPopup(getFilterModeDisplayName(currentFilterType));
		}
	}
}



/// displays names of parameters assigned to gold knobs
void ModControllableAudio::displayOtherModKnobSettings(uint8_t whichModButton, bool on) {
	// the code below handles displaying parameter names on OLED and 7SEG

	/* logic for OLED:
	- it will display parameter names when you are holding down the mod button
	- it will display the parameter name assigned to the top and bottom gold knob
	- e.g. Volume
	       Pan
	*/

	/* logic for 7SEG:
	- while holding down mod button: display parameter assigned to top gold knob
	- after mod button is released: display parameter assigned to bottom gold knob
	*/

	DEF_STACK_STRING_BUF(popupMsg, 100);
	// if we have an OLED display
	// or a 7SEG display and mod button is pressed
	// then we will display the top gold knob parameter
	if (display->haveOLED() || on) {
		char parameterName[30];
		view.getParameterNameFromModEncoder(1, parameterName);
		popupMsg.append(parameterName);
	}
	// in the song context,
	// the bottom knob for modButton 6 (stutter) doesn't have a parameter
	if (!((whichModButton == 6) && (!view.isClipContext()))) {
		// if we have an OLED display, we want to add a new line so we can
		// display the bottom gold knob param below the top gold knob param
		if (display->haveOLED()) {
			popupMsg.append("\n");
		}
		// if we have an OLED display
		// or a 7SEG display and mod button is released
		// then we will display the bottom gold knob parameter
		// on OLED bottom gold knob param is rendered below the top gold knob param
		if (display->haveOLED() || !on) {

			char parameterName[30];
			view.getParameterNameFromModEncoder(0, parameterName);
			popupMsg.append(parameterName);
		}
	}
	// if we have OLED, a pop up is shown while we are holding down mod button
	// pop-up is removed after we release the mod button
	if (display->haveOLED()) {
		if (on) {
			display->popupText(popupMsg.c_str());
		}
		else {
			display->cancelPopup();
		}
	}
	// if we have 7SEG, we display a temporary popup whenever mod button is pressed
	// and when it is released
	else {
		display->displayPopup(popupMsg.c_str());
	}
}
