#pragma once

#include "dsp/reverb/reverb.hpp"
#include "gui/l10n/strings.h"
#include "gui/menu_item/integer.h"
#include "gui/menu_item/selection.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "processing/engines/audio_engine.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string_view>

namespace deluge::gui::menu_item::reverb {

class ChorusDelay final : public Integer {
public:
	using Integer::Integer;

	[[nodiscard]] int32_t getMinValue() const override { return 1; }
	[[nodiscard]] int32_t getMaxValue() const override { return 400; } // 0.1 to 40.0 ms in tenths

	const char* getUnit() override { return " MS"; }

	void readCurrentValue() override {
		float norm = AudioEngine::reverb.getBaseDelay();
		this->setValue(1 + static_cast<int32_t>(std::round(norm * 399.0f)));
	}

	void writeCurrentValue() override {
		float norm = static_cast<float>(this->getValue() - 1) / 399.0f;
		AudioEngine::reverb.setBaseDelay(std::clamp(norm, 0.0f, 1.0f));
	}

	void selectEncoderAction(int32_t offset) override {
		bool shift = Buttons::isShiftButtonPressed();
		int32_t step = shift ? 1 : 10; // 0.1 ms vs 1.0 ms
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
		return l10n::getView(STRING_FOR_BASE_DELAY);
	}
	[[nodiscard]] std::string_view getTitle() const override { return getName(); }

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		return AudioEngine::reverb.getModel() == dsp::Reverb::Model::KDFX_CHORUS;
	}
};

class ChorusRate final : public Integer {
public:
	using Integer::Integer;

	[[nodiscard]] int32_t getMinValue() const override { return 5; }
	[[nodiscard]] int32_t getMaxValue() const override { return 1500; } // 0.05 to 15.00 Hz in hundredths

	const char* getUnit() override { return " HZ"; }

	void readCurrentValue() override {
		float norm = AudioEngine::reverb.getRoomSize();
		this->setValue(5 + static_cast<int32_t>(std::round(norm * 1495.0f)));
	}

	void writeCurrentValue() override {
		float norm = static_cast<float>(this->getValue() - 5) / 1495.0f;
		AudioEngine::reverb.setRoomSize(std::clamp(norm, 0.0f, 1.0f));
	}

	void selectEncoderAction(int32_t offset) override {
		bool shift = Buttons::isShiftButtonPressed();
		int32_t step = shift ? 1 : 20; // 0.01 Hz vs 0.20 Hz
		int32_t val = std::clamp(this->getValue() + offset * step, getMinValue(), getMaxValue());
		this->setValue(val);
		Number::selectEncoderAction(offset);
	}

	void drawPixelsForOled() override {
		char buffer[16];
		int32_t v = this->getValue();
		snprintf(buffer, sizeof(buffer), "%d.%02d HZ", v / 100, std::abs(v % 100));
		deluge::hid::display::OLED::main.drawStringCentred(buffer, 18 + OLED_MAIN_TOPMOST_PIXEL, kTextHugeSpacingX,
		                                                   kTextHugeSizeY);
	}

	void drawValue() override {
		char buffer[8];
		int32_t v = this->getValue();
		snprintf(buffer, sizeof(buffer), "%d.%02d", v / 100, std::abs(v % 100));
		display->setText(buffer, true);
	}

	[[nodiscard]] std::string_view getName() const override {
		using enum l10n::String;
		return l10n::getView(STRING_FOR_RATE);
	}
	[[nodiscard]] std::string_view getTitle() const override { return getName(); }

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		return AudioEngine::reverb.getModel() == dsp::Reverb::Model::KDFX_CHORUS;
	}
};

class ChorusDepth final : public Integer {
public:
	using Integer::Integer;

	[[nodiscard]] int32_t getMinValue() const override { return 0; }
	[[nodiscard]] int32_t getMaxValue() const override { return 100; } // 0.0 to 10.0 ms in tenths

	const char* getUnit() override { return " MS"; }

	void readCurrentValue() override {
		float norm = AudioEngine::reverb.getDecay();
		this->setValue(static_cast<int32_t>(std::round(norm * 100.0f)));
	}

	void writeCurrentValue() override {
		float norm = static_cast<float>(this->getValue()) / 100.0f;
		AudioEngine::reverb.setDecay(std::clamp(norm, 0.0f, 1.0f));
	}

	void selectEncoderAction(int32_t offset) override {
		bool shift = Buttons::isShiftButtonPressed();
		int32_t step = shift ? 1 : 10; // 0.1 ms vs 1.0 ms
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
		return l10n::getView(STRING_FOR_DEPTH);
	}
	[[nodiscard]] std::string_view getTitle() const override { return getName(); }

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		return AudioEngine::reverb.getModel() == dsp::Reverb::Model::KDFX_CHORUS;
	}
};

class ChorusFeedback final : public Integer {
public:
	using Integer::Integer;

	[[nodiscard]] int32_t getMinValue() const override { return -95; }
	[[nodiscard]] int32_t getMaxValue() const override { return 95; }
	const char* getUnit() override { return " %"; }

	void readCurrentValue() override {
		float f = AudioEngine::reverb.getChorusFeedback();
		this->setValue(static_cast<int32_t>(std::round(f * 100.0f)));
	}

	void writeCurrentValue() override {
		float f = static_cast<float>(this->getValue()) / 100.0f;
		AudioEngine::reverb.setChorusFeedback(std::clamp(f, -0.95f, 0.95f));
	}

	void selectEncoderAction(int32_t offset) override {
		bool shift = Buttons::isShiftButtonPressed();
		int32_t step = shift ? 1 : 5;
		int32_t val = std::clamp(this->getValue() + offset * step, getMinValue(), getMaxValue());
		this->setValue(val);
		Number::selectEncoderAction(offset);
	}

	[[nodiscard]] std::string_view getName() const override {
		using enum l10n::String;
		return l10n::getView(STRING_FOR_FEEDBACK);
	}
	[[nodiscard]] std::string_view getTitle() const override { return getName(); }

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		return AudioEngine::reverb.getModel() == dsp::Reverb::Model::KDFX_CHORUS;
	}
};

class ChorusVoices final : public Selection {
public:
	using Selection::Selection;

	void readCurrentValue() override {
		int32_t v = AudioEngine::reverb.getChorusVoices();
		this->setValue((v <= 2) ? 0 : (v <= 4) ? 1 : 2);
	}

	void writeCurrentValue() override {
		int32_t v = (this->getValue() == 0) ? 2 : (this->getValue() == 1) ? 4 : 6;
		AudioEngine::reverb.setChorusVoices(v);
	}

	deluge::vector<std::string_view> getOptions() override {
		return {
		    "2",
		    "4",
		    "6",
		};
	}

	[[nodiscard]] std::string_view getName() const override {
		using enum l10n::String;
		return l10n::getView(STRING_FOR_VOICES);
	}
	[[nodiscard]] std::string_view getTitle() const override { return getName(); }

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		return AudioEngine::reverb.getModel() == dsp::Reverb::Model::KDFX_CHORUS;
	}
};

class ChorusShape final : public Selection {
public:
	using Selection::Selection;

	void readCurrentValue() override {
		this->setValue(AudioEngine::reverb.getChorusWaveform());
	}

	void writeCurrentValue() override {
		AudioEngine::reverb.setChorusWaveform(this->getValue());
	}

	deluge::vector<std::string_view> getOptions() override {
		using enum l10n::String;
		return {
		    l10n::getView(STRING_FOR_SINE),
		    l10n::getView(STRING_FOR_TRIANGLE),
		    l10n::getView(STRING_FOR_SAMPLE_AND_HOLD),
		};
	}

	[[nodiscard]] std::string_view getName() const override {
		using enum l10n::String;
		return l10n::getView(STRING_FOR_SHAPE);
	}
	[[nodiscard]] std::string_view getTitle() const override { return getName(); }

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		return AudioEngine::reverb.getModel() == dsp::Reverb::Model::KDFX_CHORUS;
	}
};

} // namespace deluge::gui::menu_item::reverb
