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

#pragma once

#include "definitions_cxx.hpp"
#include "dsp/filter/filter_set.h"
#include "model/mod_controllable/mod_controllable_audio.h"
using namespace deluge;
class Serializer;
class GlobalEffectable : public ModControllableAudio {
public:
	GlobalEffectable();
	void cloneFrom(ModControllableAudio* other);

	static void initParams(ParamManager* paramManager);
	static void initParamsForAudioClip(ParamManagerForTimeline* paramManager);
	void modButtonAction(uint8_t whichModButton, bool on, ParamManagerForTimeline* paramManager);
	bool modEncoderButtonAction(uint8_t whichModEncoder, bool on, ModelStackWithThreeMainThings* modelStack);
	ModelStackWithAutoParam* getParamFromModEncoder(int32_t whichModEncoder, ModelStackWithThreeMainThings* modelStack,
	                                                bool allowCreation = true);

	void writeAttributesToFile(Serializer& writer, bool writeToFile);
	void writeTagsToFile(Serializer& writer, ParamManager* paramManager, bool writeToFile);
	Error readTagFromFile(Deserializer& reader, char const* tagName, ParamManagerForTimeline* paramManager,
	                      int32_t readAutomationUpToPos, Song* song);
	static void writeParamAttributesToFile(Serializer& writer, ParamManager* paramManager, bool writeAutomation,
	                                       int32_t* valuesForOverride = NULL);
	static void writeParamTagsToFile(Serializer& writer, ParamManager* paramManager, bool writeAutomation,
	                                 int32_t* valuesForOverride = NULL);
	static void readParamsFromFile(Deserializer& reader, ParamManagerForTimeline* paramManager,
	                               int32_t readAutomationUpToPos);
	static bool readParamTagFromFile(Deserializer& reader, char const* tagName, ParamManagerForTimeline* paramManager,
	                                 int32_t readAutomationUpToPos);
	int32_t getKnobPosForNonExistentParam(int32_t whichModEncoder, ModelStackWithAutoParam* modelStack) override;
	ActionResult modEncoderActionForNonExistentParam(int32_t offset, int32_t whichModEncoder,
	                                                 ModelStackWithAutoParam* modelStack) override;

protected:
	virtual int32_t getParameterFromKnob(int32_t whichModEncoder);

private:
};
