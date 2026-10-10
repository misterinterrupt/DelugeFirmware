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

#include "model/global_effectable/global_effectable.h"
#include "definitions_cxx.hpp"
#include "gui/l10n/l10n.h"
#include "gui/views/performance_session_view.h"
#include "gui/views/view.h"
#include "hid/buttons.h"
#include "hid/led/indicator_leds.h"
#include "memory/general_memory_allocator.h"
#include "model/action/action_logger.h"
#include "model/settings/runtime_feature_settings.h"
#include "model/song/song.h"
#include "modulation/params/param_collection.h"
#include "modulation/params/param_set.h"
#include "playback/playback_handler.h"
#include "storage/storage_manager.h"
#include "util/firmware_version.h"

using namespace deluge;
namespace params = deluge::modulation::params;

GlobalEffectable::GlobalEffectable() {
	unpatchedParamKind_ = params::Kind::UNPATCHED_GLOBAL;
	lpfMode = FilterMode::TRANSISTOR_24DB_LEGACY;
	editingComp = false;
	currentCompParam = CompParam::RATIO;
}

void GlobalEffectable::cloneFrom(ModControllableAudio* other) {
	ModControllableAudio::cloneFrom(other);

}

void GlobalEffectable::initParams(ParamManager* paramManager) {
	ModControllableAudio::initParams(paramManager);

	UnpatchedParamSet* unpatchedParams = paramManager->getUnpatchedParamSet();
	unpatchedParams->kind = deluge::modulation::params::Kind::UNPATCHED_GLOBAL;

	unpatchedParams->params[params::UNPATCHED_PAN].setCurrentValueBasicForSetup(0);

	unpatchedParams->params[params::UNPATCHED_VOLUME].setCurrentValueBasicForSetup(889516852); // 3/4 of the way up
	unpatchedParams->params[params::UNPATCHED_PITCH_ADJUST].setCurrentValueBasicForSetup(0);



}

void GlobalEffectable::initParamsForAudioClip(ParamManagerForTimeline* paramManager) {
	initParams(paramManager);
	paramManager->getUnpatchedParamSet()->params[params::UNPATCHED_VOLUME].setCurrentValueBasicForSetup(-536870912);
}

void GlobalEffectable::modButtonAction(uint8_t whichModButton, bool on, ParamManagerForTimeline* paramManager) {

	// leave stutter running in perfomance session view
	if (getRootUI() != &performanceSessionView) {
		// If we're leaving this mod function or anything else is happening, we want to be sure that stutter has stopped
		endStutter(paramManager);
	}

	// Compressor
	else if (whichModButton == 4) {
		displayCompressorSettings(on);
	}
	// Stutter / Custom 1
	else if (whichModButton == 6) {
		if (on) {
			custom1Shifted = Buttons::isShiftButtonPressed();
			if (display->haveOLED()) {
				if (custom1Shifted) {
					display->popupText("Compressor:\nAttack / Release");
				}
				else {
					display->popupText("Compressor:\nStutter / Blend");
				}
			}
			else {
				if (custom1Shifted) {
					display->displayPopup("AT.RL");
				}
				else {
					display->displayPopup("ST.BL");
				}
			}
		}
	}
	// Envelope Attack / Release (Sound only)
	else if (whichModButton == 2) {
		if (on) {
			if (display->haveOLED()) {
				display->popupText("Attack / Release:\n(Sound only)");
			}
			else {
				display->displayPopup("AT.RL");
			}
		}
	}
	// Custom 2 / Custom 3
	else if (whichModButton == 7) {
		if (on) {
			custom3Shifted = Buttons::isShiftButtonPressed();
			if (display->haveOLED()) {
				if (custom3Shifted) {
					display->popupText("Compressor:\nInput / Makeup");
				}
				else {
					display->popupText("Compressor:\nRatio / Threshold");
				}
			}
			else {
				if (custom3Shifted) {
					display->displayPopup("IG.MG");
				}
				else {
					display->displayPopup("RA.TH");
				}
			}
		}
	}
	// Other Mod Buttons
	else {
		displayOtherModKnobSettings(whichModButton, on);
	}
}

void GlobalEffectable::displayCompressorSettings(bool on) {
	if (display->haveOLED()) {
		if (on) {
			DEF_STACK_STRING_BUF(popupMsg, 100);
			popupMsg.append("Comp Mode: ");
			popupMsg.append(getCompressorModeDisplayName());
			popupMsg.append("\n");

			popupMsg.append("Comp Param: ");
			popupMsg.append(getCompressorParamDisplayName());

			display->popupText(popupMsg.c_str());
		}
		else {
			display->cancelPopup();
		}
	}
	else {
		if (on) {
			display->displayPopup(getCompressorModeDisplayName());
		}
		else {
			display->displayPopup(getCompressorParamDisplayName());
		}
	}
}

char const* GlobalEffectable::getCompressorModeDisplayName() {
	return editingComp ? "FULL" : "ONE";
}

char const* GlobalEffectable::getCompressorParamDisplayName() {
	currentCompParam = static_cast<CompParam>(util::to_underlying(currentCompParam) % maxCompParam);
	const char* params[util::to_underlying(CompParam::LAST)] = {"ratio", "attack", "release", "hpf", "blend"};
	return params[int(currentCompParam)];
}

// Returns whether Instrument changed
bool GlobalEffectable::modEncoderButtonAction(uint8_t whichModEncoder, bool on,
                                              ModelStackWithThreeMainThings* modelStack) {
	using enum l10n::String;
	int32_t modKnobMode = *getModKnobMode();

	// Stutter section
	if (modKnobMode == 6 && !custom1Shifted && whichModEncoder == 1) {
		if (on) {
			beginStutter((ParamManagerForTimeline*)modelStack->paramManager);
		}
		else {
			endStutter((ParamManagerForTimeline*)modelStack->paramManager);
		}
		return false;
	}

	// Compressor section
	else if (modKnobMode == 4) {
		if (whichModEncoder == 0) { // Comp param select
			if (on) {
				currentCompParam =
				    static_cast<CompParam>((util::to_underlying(currentCompParam) + 1) % maxCompParam);

				// if mod button is pressed, update mod button pop up
				if (Buttons::isButtonPressed(
				        deluge::hid::button::fromXY(modButtonX[modKnobMode], modButtonY[modKnobMode]))) {
					displayCompressorSettings(on);
				}
				else {
					display->displayPopup(getCompressorParamDisplayName());
				}
			}
		}
		else {
			if (on) {
				editingComp = !editingComp;

				// if mod button is pressed, update mod button pop up
				if (Buttons::isButtonPressed(
				        deluge::hid::button::fromXY(modButtonX[modKnobMode], modButtonY[modKnobMode]))) {
					displayCompressorSettings(on);
				}
				else {
					display->displayPopup(getCompressorModeDisplayName());
				}
			}
		}

		return false;
	}

	return false; // Some cases could lead here
}

int32_t GlobalEffectable::getKnobPosForNonExistentParam(int32_t whichModEncoder, ModelStackWithAutoParam* modelStack) {
	int current = 0;
	if (*getModKnobMode() == 4) {

		// this is only reachable in comp editing mode, otherwise it's an existent param
		if (whichModEncoder == 1) { // sidechain (threshold)
			current = (compressor.getThreshold() >> 24);
		}
		else if (whichModEncoder == 0) {
			switch (currentCompParam) {

			case CompParam::RATIO:
				current = (compressor.getRatio() >> 24);

				break;

			case CompParam::ATTACK:
				current = compressor.getAttack() >> 24;

				break;

			case CompParam::RELEASE:
				current = compressor.getRelease() >> 24;

				break;

			case CompParam::SIDECHAIN:
				current = compressor.getSidechain() >> 24;
				break;

			case CompParam::BLEND:
				current = compressor.getBlend() >> 24;
				break;
			}
		}
	}
	return current - 64;
}

ActionResult GlobalEffectable::modEncoderActionForNonExistentParam(int32_t offset, int32_t whichModEncoder,
                                                                   ModelStackWithAutoParam* modelStack) {
	if (*getModKnobMode() == 4) {
		DEF_STACK_STRING_BUF(popupMsg, 40);
		int current;
		int displayLevel;
		int ledLevel;
		const char* unit;
		// this is only reachable in comp editing mode, otherwise it's an existent param
		if (whichModEncoder == 1) { // sidechain (threshold)
			if (display->haveOLED()) {
				popupMsg.append(deluge::l10n::get(deluge::l10n::String::STRING_FOR_THRESHOLD));
			}
			current = (compressor.getThreshold() >> 24) - 64;
			current += offset;
			current = std::clamp(current, -64, 64);
			ledLevel = (64 + current);
			q31_t level = current == 64 ? ONE_Q31 : lshiftAndSaturate<24>(current + 64);
			compressor.setThreshold(level);
			displayLevel = compressor.getThresholdForDisplay();
			indicator_leds::setKnobIndicatorLevel(1, ledLevel);
			unit = " dB";
		}
		else if (whichModEncoder == 0) {
			switch (currentCompParam) {

			case CompParam::RATIO:
				if (display->haveOLED()) {
					popupMsg.append(deluge::l10n::get(deluge::l10n::String::STRING_FOR_RATIO));
				}
				current = (compressor.getRatio() >> 24) - 64;
				current += offset;
				current = std::clamp(current, -64, 64);
				ledLevel = (64 + current);
				{
					q31_t ratLevel = current == 64 ? ONE_Q31 : lshiftAndSaturate<24>(current + 64);
					compressor.setRatio(ratLevel);
				}
				displayLevel = compressor.getRatioForDisplay();
				unit = "";
				break;

			case CompParam::ATTACK:
				if (display->haveOLED()) {
					popupMsg.append(deluge::l10n::get(deluge::l10n::String::STRING_FOR_ATTACK));
				}
				current = (compressor.getAttack() >> 24) - 64;
				current += offset;
				current = std::clamp(current, -64, 64);
				ledLevel = (64 + current);

				displayLevel = compressor.setAttack(lshiftAndSaturate<24>(current + 64));
				unit = " MS";
				break;

			case CompParam::RELEASE:
				if (display->haveOLED()) {
					popupMsg.append(deluge::l10n::get(deluge::l10n::String::STRING_FOR_RELEASE));
				}
				current = (compressor.getRelease() >> 24) - 64;
				current += offset;
				current = std::clamp(current, -64, 64);
				ledLevel = (64 + current);

				displayLevel = compressor.setRelease(lshiftAndSaturate<24>(current + 64));
				unit = " MS";
				break;

			case CompParam::SIDECHAIN:
				if (display->haveOLED()) {
					popupMsg.append(deluge::l10n::get(deluge::l10n::String::STRING_FOR_HPF));
				}
				current = (compressor.getSidechain() >> 24) - 64;
				current += offset;
				current = std::clamp(current, -64, 64);
				ledLevel = (64 + current);

				displayLevel = compressor.setSidechain(lshiftAndSaturate<24>(current + 64));
				unit = " HZ";
				break;

			case CompParam::BLEND:
				if (display->haveOLED()) {
					popupMsg.append(deluge::l10n::get(deluge::l10n::String::STRING_FOR_BLEND));
				}
				current = (compressor.getBlend() >> 24) - 64;
				current += offset;
				current = std::clamp(current, -64, 64);
				ledLevel = (64 + current);
				q31_t level = current == 64 ? ONE_Q31 : lshiftAndSaturate<24>(current + 64);
				displayLevel = compressor.setBlend(level);
				unit = " %";
				break;
			}
			indicator_leds::setKnobIndicatorLevel(0, ledLevel);
		}

		if (display->haveOLED()) {
			popupMsg.append(": ");
			if (whichModEncoder == 0 && currentCompParam == CompParam::RATIO) {
				char ratioBuffer[16];
				compressor.getRatioString(ratioBuffer, sizeof(ratioBuffer));
				popupMsg.append(ratioBuffer);
			}
			else {
				popupMsg.appendInt(displayLevel);
				popupMsg.append(unit);
			}
		}
		else {
			if (whichModEncoder == 0 && currentCompParam == CompParam::RATIO) {
				char ratioBuffer[16];
				compressor.getRatioString(ratioBuffer, sizeof(ratioBuffer));
				popupMsg.append(ratioBuffer);
			}
			else {
				popupMsg.appendInt(displayLevel);
			}
		}
		display->displayPopup(popupMsg.c_str());

		return ActionResult::DEALT_WITH;
	}
	else if (*getModKnobMode() == 2) {
		if (display->haveOLED()) {
			display->popupText("Attack / Release:\n(Sound only)");
		}
		else {
			display->displayPopup("AT.RL");
		}
		return ActionResult::DEALT_WITH;
	}
	return ActionResult::NOT_DEALT_WITH;
}

// Always check this doesn't return NULL!
int32_t GlobalEffectable::getParameterFromKnob(int32_t whichModEncoder) {

	int32_t modKnobMode = *getModKnobMode();

	if (modKnobMode == 0) {
		if (whichModEncoder != 0) {
			return params::UNPATCHED_VOLUME;
		}
		else {
			return params::UNPATCHED_PAN;
		}
	}
	else if (modKnobMode == 4) {
		// Encoder 0 here selects comp params via clicks; no continuous param while
		// not editing the compressor.
	}
	else if (modKnobMode == 6) { // Stutter / Custom 1
		if (custom1Shifted) {
			if (whichModEncoder == 1) {
				return params::UNPATCHED_COMPRESSOR_ATTACK;
			}
			else {
				return params::UNPATCHED_COMPRESSOR_RELEASE;
			}
		}
		else {
			if (whichModEncoder == 1) {
				return params::UNPATCHED_STUTTER_RATE;
			}
			else {
				return params::UNPATCHED_COMPRESSOR_BLEND;
			}
		}
	}
	else if (modKnobMode == 7) { // Custom 2 / Custom 3
		if (custom3Shifted) {
			if (whichModEncoder == 1) {
				return params::UNPATCHED_COMPRESSOR_INPUT_GAIN;
			}
			else {
				return params::UNPATCHED_COMPRESSOR_MAKEUP_GAIN;
			}
		}
		else {
			if (whichModEncoder == 1) {
				return params::UNPATCHED_COMPRESSOR_RATIO;
			}
			else {
				return params::UNPATCHED_COMPRESSOR_THRESHOLD;
			}
		}
	}
	return 255;
}

ModelStackWithAutoParam* GlobalEffectable::getParamFromModEncoder(int32_t whichModEncoder,
                                                                  ModelStackWithThreeMainThings* modelStack,
                                                                  bool allowCreation) {
	ParamCollectionSummary* summary = modelStack->paramManager->getUnpatchedParamSetSummary();
	ParamCollection* paramCollection = summary->paramCollection;
	int32_t paramId;

	paramId = getParameterFromKnob(whichModEncoder);

	ModelStackWithParamId* newModelStack1 = modelStack->addParamCollectionAndId(paramCollection, summary, paramId);

	if (paramId == 255) {
		return newModelStack1->addAutoParam(NULL); // Communicate there's no param, back to caller
	}
	else {
		return newModelStack1->paramCollection->getAutoParamFromId(newModelStack1, allowCreation);
	}
}



void GlobalEffectable::writeAttributesToFile(Serializer& writer, bool writeAutomation) {
	ModControllableAudio::writeAttributesToFile(writer);
	// Community Firmware parameters (always write them after the official ones, just before closing the parent tag)
	// <--
}

void GlobalEffectable::writeTagsToFile(Serializer& writer, ParamManager* paramManager, bool writeAutomation) {
	if (paramManager) {
		writer.writeOpeningTagBeginning("defaultParams");
		GlobalEffectable::writeParamAttributesToFile(writer, paramManager, writeAutomation);
		writer.writeOpeningTagEnd();
		GlobalEffectable::writeParamTagsToFile(writer, paramManager, writeAutomation);
		writer.writeClosingTag("defaultParams");
	}

	ModControllableAudio::writeTagsToFile(writer);
}

void GlobalEffectable::writeParamAttributesToFile(Serializer& writer, ParamManager* paramManager, bool writeAutomation,
                                                  int32_t* valuesForOverride) {

	UnpatchedParamSet* unpatchedParams = paramManager->getUnpatchedParamSet();

	unpatchedParams->writeParamAsAttribute(writer, "volume", params::UNPATCHED_VOLUME, writeAutomation, false,
	                                       valuesForOverride);
	unpatchedParams->writeParamAsAttribute(writer, "pan", params::UNPATCHED_PAN, writeAutomation, false,
	                                       valuesForOverride);

	if (unpatchedParams->params[params::UNPATCHED_PITCH_ADJUST].containsSomething(0)) {
		unpatchedParams->writeParamAsAttribute(writer, "pitchAdjust", params::UNPATCHED_PITCH_ADJUST, writeAutomation,
		                                       false, valuesForOverride);
	}

	ModControllableAudio::writeParamAttributesToFile(writer, paramManager, writeAutomation, valuesForOverride);

	// Community Firmware parameters (always write them after the official ones, just before closing the parent tag)
	unpatchedParams->writeParamAsAttribute(writer, "tempo", params::UNPATCHED_TEMPO, writeAutomation, false,
	                                       valuesForOverride);
}

void GlobalEffectable::writeParamTagsToFile(Serializer& writer, ParamManager* paramManager, bool writeAutomation,
                                            int32_t* valuesForOverride) {

	UnpatchedParamSet* unpatchedParams = paramManager->getUnpatchedParamSet();

	ModControllableAudio::writeParamTagsToFile(writer, paramManager, writeAutomation, valuesForOverride);
}

void GlobalEffectable::readParamsFromFile(Deserializer& reader, ParamManagerForTimeline* paramManager,
                                          int32_t readAutomationUpToPos) {
	char const* tagName;

	while (*(tagName = reader.readNextTagOrAttributeName())) {
		if (readParamTagFromFile(reader, tagName, paramManager, readAutomationUpToPos)) {}
		else {
			reader.exitTag(tagName);
		}
	}
}

bool GlobalEffectable::readParamTagFromFile(Deserializer& reader, char const* tagName,
                                            ParamManagerForTimeline* paramManager, int32_t readAutomationUpToPos) {

	ParamCollectionSummary* unpatchedParamsSummary = paramManager->getUnpatchedParamSetSummary();
	UnpatchedParamSet* unpatchedParams = (UnpatchedParamSet*)unpatchedParamsSummary->paramCollection;

	if (!strcmp(tagName, "volume")) {
		unpatchedParams->readParam(reader, unpatchedParamsSummary, params::UNPATCHED_VOLUME, readAutomationUpToPos);
		reader.exitTag("volume");
	}

	else if (!strcmp(tagName, "pan")) {
		unpatchedParams->readParam(reader, unpatchedParamsSummary, params::UNPATCHED_PAN, readAutomationUpToPos);
		reader.exitTag("pan");
	}

	else if (!strcmp(tagName, "pitchAdjust")) {
		unpatchedParams->readParam(reader, unpatchedParamsSummary, params::UNPATCHED_PITCH_ADJUST,
		                           readAutomationUpToPos);
		reader.exitTag("pitchAdjust");
	}

	else if (ModControllableAudio::readParamTagFromFile(reader, tagName, paramManager, readAutomationUpToPos)) {}

	else {
		return false;
	}

	return true;
}

// paramManager is optional
Error GlobalEffectable::readTagFromFile(Deserializer& reader, char const* tagName,
                                        ParamManagerForTimeline* paramManager, int32_t readAutomationUpToPos,
                                        Song* song) {

	// This is here for compatibility only for people (Lou and Ian) who saved songs with firmware in September 2016
	// if (paramManager && strcmp(tagName, "delay") && GlobalEffectable::readParamTagFromFile(tagName, paramManager,
	// readAutomation)) {}

	// else
	if (paramManager && !strcmp(tagName, "defaultParams")) {

		if (!paramManager->containsAnyMainParamCollections()) {
			Error error = paramManager->setupUnpatched();
			if (error != Error::NONE) {
				return error;
			}
			initParams(paramManager);
		}

		GlobalEffectable::readParamsFromFile(reader, paramManager, readAutomationUpToPos);
		reader.exitTag("defaultParams");
	}

	else {
		return ModControllableAudio::readTagFromFile(reader, tagName, NULL, readAutomationUpToPos, song);
	}

	return Error::NONE;
}

// Before calling this, check that (reader.firmwareVersionOfFileBeingRead < FIRMWARE_1P2P0 &&
// !paramManager->resonanceBackwardsCompatibilityProcessed)
