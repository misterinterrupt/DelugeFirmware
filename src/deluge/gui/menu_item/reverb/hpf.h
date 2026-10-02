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
#include "definitions_cxx.hpp"
#include "dsp/reverb/mutable/reverb.hpp"
#include "dsp/reverb/reverb.hpp"
#include "gui/l10n/strings.h"
#include "gui/menu_item/integer.h"
#include "hid/buttons.h"
#include "processing/engines/audio_engine.h"
#include <algorithm>
#include <cmath>

namespace deluge::gui::menu_item::reverb {
class HPF final : public Integer {
public:
	using Integer::Integer;

	[[nodiscard]] int32_t getMinValue() const override {
		switch (AudioEngine::reverb.getModel()) {
		case dsp::Reverb::Model::KDFX_REVERB:
		case dsp::Reverb::Model::KDFX_DELAY:
		case dsp::Reverb::Model::KDFX_CHORUS:
			return 20;
		default:
			return 0;
		}
	}

	[[nodiscard]] int32_t getMaxValue() const override {
		switch (AudioEngine::reverb.getModel()) {
		case dsp::Reverb::Model::KDFX_REVERB:
		case dsp::Reverb::Model::KDFX_DELAY:
		case dsp::Reverb::Model::KDFX_CHORUS:
			return 1000;
		default:
			return kMaxMenuValue;
		}
	}

	const char* getUnit() override {
		switch (AudioEngine::reverb.getModel()) {
		case dsp::Reverb::Model::KDFX_REVERB:
		case dsp::Reverb::Model::KDFX_DELAY:
		case dsp::Reverb::Model::KDFX_CHORUS:
			return " HZ";
		default:
			return "";
		}
	}

	void readCurrentValue() override {
		float norm = AudioEngine::reverb.getHPF();
		int32_t minV = getMinValue();
		int32_t maxV = getMaxValue();
		this->setValue(minV + static_cast<int32_t>(std::round(norm * (maxV - minV))));
	}

	void writeCurrentValue() override {
		int32_t minV = getMinValue();
		int32_t maxV = getMaxValue();
		float norm = (maxV > minV) ? (float)(this->getValue() - minV) / (float)(maxV - minV) : 0.0f;
		AudioEngine::reverb.setHPF(std::clamp(norm, 0.0f, 1.0f));
	}

	void selectEncoderAction(int32_t offset) override {
		if (AudioEngine::reverb.getModel() != dsp::Reverb::Model::MUTABLE) {
			bool shiftPressed = Buttons::isShiftButtonPressed();
			int32_t step = shiftPressed ? 1 : ((this->getValue() > 100) ? 10 : 5);
			this->setValue(this->getValue() + offset * step);
			this->setValue(std::clamp(this->getValue(), getMinValue(), getMaxValue()));
			Number::selectEncoderAction(offset);
		}
		else {
			Integer::selectEncoderAction(offset);
		}
	}

	[[nodiscard]] std::string_view getName() const override { return l10n::getView(this->name); }
	[[nodiscard]] std::string_view getTitle() const override { return getName(); }

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		auto model = AudioEngine::reverb.getModel();
		return model == dsp::Reverb::Model::MUTABLE || model == dsp::Reverb::Model::KDFX_REVERB
		       || model == dsp::Reverb::Model::KDFX_DELAY || model == dsp::Reverb::Model::KDFX_CHORUS;
	}
};
} // namespace deluge::gui::menu_item::reverb
