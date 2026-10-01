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

#include "dsp/reverb/reverb.hpp"
#include "gui/menu_item/integer.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "processing/engines/audio_engine.h"
#include <cmath>
#include <cstdio>

namespace deluge::gui::menu_item::reverb {
class RoomSize final : public Integer {
public:
	using Integer::Integer;

	[[nodiscard]] int32_t getMinValue() const override {
		switch (AudioEngine::reverb.getModel()) {
		case dsp::Reverb::Model::KDFX_REVERB:
			return 1;
		case dsp::Reverb::Model::KDFX_DELAY:
			return 1;
		case dsp::Reverb::Model::KDFX_CHORUS:
			return 1;
		default:
			return 0;
		}
	}

	[[nodiscard]] int32_t getMaxValue() const override {
		switch (AudioEngine::reverb.getModel()) {
		case dsp::Reverb::Model::KDFX_REVERB:
			return 16;
		case dsp::Reverb::Model::KDFX_DELAY:
			return 740;
		case dsp::Reverb::Model::KDFX_CHORUS:
			return 100;
		case dsp::Reverb::Model::KDFX_TONE:
			return 24;
		default:
			return kMaxMenuValue;
		}
	}

	const char* getUnit() override {
		switch (AudioEngine::reverb.getModel()) {
		case dsp::Reverb::Model::KDFX_REVERB:
			return " M";
		case dsp::Reverb::Model::KDFX_DELAY:
			return " MS";
		case dsp::Reverb::Model::KDFX_CHORUS:
			return " HZ";
		case dsp::Reverb::Model::KDFX_TONE:
			return " DB";
		default:
			return "";
		}
	}

	void readCurrentValue() override {
		float norm = AudioEngine::reverb.getRoomSize();
		int32_t minV = getMinValue();
		int32_t maxV = getMaxValue();
		this->setValue(minV + static_cast<int32_t>(std::round(norm * (maxV - minV))));
	}

	void writeCurrentValue() override {
		int32_t minV = getMinValue();
		int32_t maxV = getMaxValue();
		float norm = (maxV > minV) ? (float)(this->getValue() - minV) / (float)(maxV - minV) : 0.0f;
		AudioEngine::reverb.setRoomSize(std::clamp(norm, 0.0f, 1.0f));
	}

	void selectEncoderAction(int32_t offset) override {
		if (AudioEngine::reverb.getModel() == dsp::Reverb::Model::KDFX_DELAY) {
			int32_t step = (this->getValue() > 50) ? 10 : 1;
			this->setValue(this->getValue() + offset * step);
			this->setValue(std::clamp(this->getValue(), getMinValue(), getMaxValue()));
			Number::selectEncoderAction(offset);
		}
		else {
			Integer::selectEncoderAction(offset);
		}
	}

	void drawPixelsForOled() override {
		if (AudioEngine::reverb.getModel() == dsp::Reverb::Model::KDFX_CHORUS) {
			char buffer[16];
			int32_t v = this->getValue();
			snprintf(buffer, sizeof(buffer), "%d.%d HZ", v / 10, v % 10);
			deluge::hid::display::OLED::main.drawStringCentred(buffer, 18 + OLED_MAIN_TOPMOST_PIXEL, kTextHugeSpacingX,
			                                                   kTextHugeSizeY);
		}
		else {
			Integer::drawPixelsForOled();
		}
	}

	void drawValue() override {
		if (AudioEngine::reverb.getModel() == dsp::Reverb::Model::KDFX_CHORUS) {
			char buffer[8];
			int32_t v = this->getValue();
			snprintf(buffer, sizeof(buffer), "%d.%d", v / 10, v % 10);
			display->setText(buffer, true);
		}
		else {
			Integer::drawValue();
		}
	}

	[[nodiscard]] std::string_view getName() const override {
		using enum l10n::String;
		switch (AudioEngine::reverb.getModel()) {
		case dsp::Reverb::Model::MUTABLE:
		case dsp::Reverb::Model::KDFX_DELAY:
			return l10n::getView(STRING_FOR_TIME);
		case dsp::Reverb::Model::KDFX_CHORUS:
			return l10n::getView(STRING_FOR_RATE);
		case dsp::Reverb::Model::KDFX_TONE:
			return l10n::getView(STRING_FOR_DRIVE);
		default:
			return l10n::getView(this->name);
		}
	}
	[[nodiscard]] std::string_view getTitle() const override { return getName(); }
};
} // namespace deluge::gui::menu_item::reverb
