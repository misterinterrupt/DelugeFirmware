#pragma once

#include "dsp/reverb/reverb.hpp"
#include "gui/l10n/strings.h"
#include "gui/menu_item/integer.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "processing/engines/audio_engine.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace deluge::gui::menu_item::reverb {

class PreDelay final : public Integer {
public:
	using Integer::Integer;

	[[nodiscard]] int32_t getMinValue() const override { return 0; }
	[[nodiscard]] int32_t getMaxValue() const override { return 3000; } // 0.0 to 300.0 ms in tenths

	const char* getUnit() override { return " MS"; }

	void readCurrentValue() override {
		float norm = AudioEngine::reverb.getPreDelay();
		this->setValue(static_cast<int32_t>(std::round(norm * 3000.0f)));
	}

	void writeCurrentValue() override {
		float norm = static_cast<float>(this->getValue()) / 3000.0f;
		AudioEngine::reverb.setPreDelay(std::clamp(norm, 0.0f, 1.0f));
	}

	void selectEncoderAction(int32_t offset) override {
		bool shift = Buttons::isShiftButtonPressed();
		int32_t step = shift ? 1 : 50; // 0.1 ms vs 5.0 ms
		int32_t val = std::clamp(this->getValue() + offset * step, getMinValue(), getMaxValue());
		this->setValue(val);
		Number::selectEncoderAction(offset);
	}

	void drawPixelsForOled() override {
		char buffer[16];
		int32_t v = this->getValue();
		snprintf(buffer, sizeof(buffer), "%d.%d MS", v / 10, std::abs(v % 10));
		deluge::hid::display::OLED::main.drawStringCentred(buffer, 18 + OLED_MAIN_TOPMOST_PIXEL, kTextHugeSpacingX,
		                                                   kTextHugeSizeY);
	}

	void drawValue() override {
		char buffer[8];
		int32_t v = this->getValue();
		snprintf(buffer, sizeof(buffer), "%d.%d", v / 10, std::abs(v % 10));
		display->setText(buffer, true);
	}

	[[nodiscard]] std::string_view getName() const override {
		using enum l10n::String;
		return l10n::getView(STRING_FOR_PREDELAY);
	}
	[[nodiscard]] std::string_view getTitle() const override { return getName(); }

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		return AudioEngine::reverb.getModel() == dsp::Reverb::Model::KDFX_REVERB;
	}
};

} // namespace deluge::gui::menu_item::reverb
