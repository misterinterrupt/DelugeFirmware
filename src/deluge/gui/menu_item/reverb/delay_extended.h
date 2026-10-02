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

class DelayTimeL final : public Integer {
public:
	using Integer::Integer;

	[[nodiscard]] int32_t getMinValue() const override { return 10; }
	[[nodiscard]] int32_t getMaxValue() const override { return 14800; } // 1.0 to 1480.0 ms in tenths

	const char* getUnit() override { return " MS"; }

	void readCurrentValue() override {
		float norm = AudioEngine::reverb.getDelayTimeL();
		this->setValue(10 + static_cast<int32_t>(std::round(norm * 14790.0f)));
	}

	void writeCurrentValue() override {
		float norm = static_cast<float>(this->getValue() - 10) / 14790.0f;
		AudioEngine::reverb.setDelayTimeL(std::clamp(norm, 0.0f, 1.0f));
	}

	void selectEncoderAction(int32_t offset) override {
		bool shift = Buttons::isShiftButtonPressed();
		int32_t step = shift ? 1 : ((this->getValue() > 500) ? 100 : 10); // 0.1ms fine vs 10ms/1ms coarse
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
		return l10n::getView(STRING_FOR_DELAY_TIME_LEFT);
	}
	[[nodiscard]] std::string_view getTitle() const override { return getName(); }

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		return AudioEngine::reverb.getModel() == dsp::Reverb::Model::KDFX_DELAY;
	}
};

class DelayTimeR final : public Integer {
public:
	using Integer::Integer;

	[[nodiscard]] int32_t getMinValue() const override { return 10; }
	[[nodiscard]] int32_t getMaxValue() const override { return 14800; }

	const char* getUnit() override { return " MS"; }

	void readCurrentValue() override {
		float norm = AudioEngine::reverb.getDelayTimeR();
		this->setValue(10 + static_cast<int32_t>(std::round(norm * 14790.0f)));
	}

	void writeCurrentValue() override {
		float norm = static_cast<float>(this->getValue() - 10) / 14790.0f;
		AudioEngine::reverb.setDelayTimeR(std::clamp(norm, 0.0f, 1.0f));
	}

	void selectEncoderAction(int32_t offset) override {
		bool shift = Buttons::isShiftButtonPressed();
		int32_t step = shift ? 1 : ((this->getValue() > 500) ? 100 : 10);
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
		return l10n::getView(STRING_FOR_DELAY_TIME_RIGHT);
	}
	[[nodiscard]] std::string_view getTitle() const override { return getName(); }

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		return AudioEngine::reverb.getModel() == dsp::Reverb::Model::KDFX_DELAY;
	}
};

class DelayFeedback final : public Integer {
public:
	using Integer::Integer;

	[[nodiscard]] int32_t getMinValue() const override { return 0; }
	[[nodiscard]] int32_t getMaxValue() const override { return 100; }
	const char* getUnit() override { return " %"; }

	void readCurrentValue() override {
		float norm = AudioEngine::reverb.getDecay();
		this->setValue(static_cast<int32_t>(std::round(norm * 100.0f)));
	}

	void writeCurrentValue() override {
		float norm = static_cast<float>(this->getValue()) / 100.0f;
		AudioEngine::reverb.setDecay(std::clamp(norm, 0.0f, 1.0f));
	}

	[[nodiscard]] std::string_view getName() const override {
		using enum l10n::String;
		return l10n::getView(STRING_FOR_FEEDBACK);
	}
	[[nodiscard]] std::string_view getTitle() const override { return getName(); }

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		return AudioEngine::reverb.getModel() == dsp::Reverb::Model::KDFX_DELAY;
	}
};

class DelayDrive final : public Integer {
public:
	using Integer::Integer;

	[[nodiscard]] int32_t getMinValue() const override { return 0; }
	[[nodiscard]] int32_t getMaxValue() const override { return 180; } // 0.0 to 18.0 dB in tenths
	const char* getUnit() override { return " DB"; }

	void readCurrentValue() override {
		float norm = AudioEngine::reverb.getDelayDrive();
		this->setValue(static_cast<int32_t>(std::round(norm * 180.0f)));
	}

	void writeCurrentValue() override {
		float norm = static_cast<float>(this->getValue()) / 180.0f;
		AudioEngine::reverb.setDelayDrive(std::clamp(norm, 0.0f, 1.0f));
	}

	void selectEncoderAction(int32_t offset) override {
		bool shift = Buttons::isShiftButtonPressed();
		int32_t step = shift ? 1 : 10;
		int32_t val = std::clamp(this->getValue() + offset * step, getMinValue(), getMaxValue());
		this->setValue(val);
		Number::selectEncoderAction(offset);
	}

	void drawPixelsForOled() override {
		char buffer[16];
		int32_t v = this->getValue();
		snprintf(buffer, sizeof(buffer), "%d.%d DB", v / 10, std::abs(v % 10));
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
		return l10n::getView(STRING_FOR_TAPE_DRIVE);
	}
	[[nodiscard]] std::string_view getTitle() const override { return getName(); }

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		return AudioEngine::reverb.getModel() == dsp::Reverb::Model::KDFX_DELAY;
	}
};

class Crossfeed final : public Integer {
public:
	using Integer::Integer;

	[[nodiscard]] int32_t getMinValue() const override { return 0; }
	[[nodiscard]] int32_t getMaxValue() const override { return 100; }
	const char* getUnit() override { return " %"; }

	void readCurrentValue() override {
		float norm = AudioEngine::reverb.getDiffusion();
		this->setValue(static_cast<int32_t>(std::round(norm * 100.0f)));
	}

	void writeCurrentValue() override {
		float norm = static_cast<float>(this->getValue()) / 100.0f;
		AudioEngine::reverb.setDiffusion(std::clamp(norm, 0.0f, 1.0f));
	}

	[[nodiscard]] std::string_view getName() const override {
		using enum l10n::String;
		return l10n::getView(STRING_FOR_CROSSFEED);
	}
	[[nodiscard]] std::string_view getTitle() const override { return getName(); }

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		return AudioEngine::reverb.getModel() == dsp::Reverb::Model::KDFX_DELAY;
	}
};

class PhaseInvert final : public Selection {
public:
	using Selection::Selection;

	void readCurrentValue() override {
		this->setValue(AudioEngine::reverb.getPhaseInvert() ? 1 : 0);
	}

	void writeCurrentValue() override {
		AudioEngine::reverb.setPhaseInvert(this->getValue() != 0);
	}

	deluge::vector<std::string_view> getOptions() override {
		using enum l10n::String;
		return {
		    l10n::getView(STRING_FOR_OFF),
		    l10n::getView(STRING_FOR_ON),
		};
	}

	[[nodiscard]] std::string_view getName() const override {
		using enum l10n::String;
		return l10n::getView(STRING_FOR_PHASE_INVERT);
	}
	[[nodiscard]] std::string_view getTitle() const override { return getName(); }

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		return AudioEngine::reverb.getModel() == dsp::Reverb::Model::KDFX_DELAY;
	}
};

} // namespace deluge::gui::menu_item::reverb
