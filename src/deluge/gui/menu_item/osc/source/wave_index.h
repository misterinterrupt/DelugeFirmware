/*
 * Copyright (c) 2014-2023 Synthstrom Audible Limited
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
#include "gui/menu_item/formatted_title.h"
#include "gui/menu_item/source/patched_param.h"
#include "hid/display/display.h"
#include "processing/sound/sound.h"

namespace deluge::gui::menu_item::osc::source {
class WaveIndex final : public menu_item::source::PatchedParam, public FormattedTitle {
public:
	WaveIndex(l10n::String name, l10n::String title_format_str, int32_t newP)
	    : PatchedParam(name, newP), FormattedTitle(title_format_str) {}

	[[nodiscard]] std::string_view getTitle() const override {
		if (soundEditor.currentSource && soundEditor.currentSource->oscType == OscType::PHYSICAL_MODEL) {
			return "MODEL TYPE";
		}
		return FormattedTitle::title();
	}

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		Sound* sound = static_cast<Sound*>(modControllable);
		Source* source = &sound->sources[whichThing];
		return (source->oscType == OscType::WAVETABLE || source->oscType == OscType::PHYSICAL_MODEL);
	}

	void drawValue() override {
		if (soundEditor.currentSource && soundEditor.currentSource->oscType == OscType::PHYSICAL_MODEL) {
			int32_t rawVal = soundEditor.currentParamManager->getPatchedParamSet()->getValue(getP());
			uint32_t unsignedIndex = (uint32_t)rawVal + 2147483648u;
			int32_t idx = std::clamp<int32_t>((int32_t)(unsignedIndex / (4294967295u / 6)), 0, 5);
			static const char* segNames[6] = {
			    "MEMB", "WOOD", "BOWD", "CYMB", "DIGI", "STRG"};

			if (display->have7SEG()) {
				display->setText(segNames[idx]);
			}
			return;
		}
		menu_item::source::PatchedParam::drawValue();
	}

	void drawPixelsForOled() override {
		if (soundEditor.currentSource && soundEditor.currentSource->oscType == OscType::PHYSICAL_MODEL) {
			int32_t rawVal = soundEditor.currentParamManager->getPatchedParamSet()->getValue(getP());
			uint32_t unsignedIndex = (uint32_t)rawVal + 2147483648u;
			int32_t idx = std::clamp<int32_t>((int32_t)(unsignedIndex / (4294967295u / 6)), 0, 5);
			
			static const char* oledNames[6] = {
			    "MEMBRANE", "WOOD BLOCK", "BOWED METAL", "REAL CYMBAL", "DIGITAL CYMBAL", "STRING"};
			
			deluge::hid::display::OLED::main.drawStringCentred(oledNames[idx], 18 + OLED_MAIN_TOPMOST_PIXEL, kTextBigSpacingX, kTextBigSizeY);
			return;
		}
		menu_item::source::PatchedParam::drawPixelsForOled();
	}

	void selectEncoderAction(int32_t offset) override {
		if (soundEditor.currentSource && soundEditor.currentSource->oscType == OscType::PHYSICAL_MODEL) {
			int32_t rawVal = soundEditor.currentParamManager->getPatchedParamSet()->getValue(getP());
			uint32_t unsignedIndex = (uint32_t)rawVal + 2147483648u;
			int32_t idx = std::clamp<int32_t>((int32_t)(unsignedIndex / (4294967295u / 6)), 0, 5);
			
			idx += offset;
			idx = std::clamp<int32_t>(idx, 0, 5);
			
			// Map to center of segment
			uint32_t newUnsigned = (idx * (4294967295u / 6)) + ((4294967295u / 6) / 2);
			int32_t newRaw = (int32_t)(newUnsigned - 2147483648u);
			
			char modelStackMemory[MODEL_STACK_MAX_SIZE];
			ModelStackWithAutoParam* modelStack = getModelStack(modelStackMemory);
			modelStack->autoParam->setCurrentValueInResponseToUserInput(newRaw, modelStack);
			
			readCurrentValue();
			return;
		}
		menu_item::source::PatchedParam::selectEncoderAction(offset);
	}
};
} // namespace deluge::gui::menu_item::osc::source
