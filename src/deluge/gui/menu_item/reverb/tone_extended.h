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

class ToneDrive final : public Integer {
public:
	using Integer::Integer;

	[[nodiscard]] int32_t getMinValue() const override { return 0; }
	[[nodiscard]] int32_t getMaxValue() const override { return 240; } // 0.0 to 24.0 dB in tenths

	const char* getUnit() override { return " DB"; }

	void readCurrentValue() override {
		float norm = AudioEngine::reverb.getDrive();
		this->setValue(static_cast<int32_t>(std::round(norm * 240.0f)));
	}

	void writeCurrentValue() override {
		float norm = static_cast<float>(this->getValue()) / 240.0f;
		AudioEngine::reverb.setDrive(std::clamp(norm, 0.0f, 1.0f));
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
		return l10n::getView(STRING_FOR_DRIVE);
	}
	[[nodiscard]] std::string_view getTitle() const override { return getName(); }

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		return AudioEngine::reverb.getModel() == dsp::Reverb::Model::KDFX_TONE;
	}
};

class ToneShaperMode final : public Selection {
public:
	using Selection::Selection;

	void readCurrentValue() override {
		this->setValue(AudioEngine::reverb.getShaperMode());
	}

	void writeCurrentValue() override {
		AudioEngine::reverb.setShaperMode(this->getValue());
	}

	deluge::vector<std::string_view> getOptions() override {
		using enum l10n::String;
		return {
		    l10n::getView(STRING_FOR_SOFT_TUBE),
		    l10n::getView(STRING_FOR_TAPE_WARMTH),
		    l10n::getView(STRING_FOR_HARD_CLIP),
		    l10n::getView(STRING_FOR_WAVEFOLD),
		};
	}

	[[nodiscard]] std::string_view getName() const override {
		using enum l10n::String;
		return l10n::getView(STRING_FOR_SHAPER_MODE);
	}
	[[nodiscard]] std::string_view getTitle() const override { return getName(); }

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		return AudioEngine::reverb.getModel() == dsp::Reverb::Model::KDFX_TONE;
	}
};

class ToneBassGain final : public Integer {
public:
	using Integer::Integer;

	[[nodiscard]] int32_t getMinValue() const override { return -150; }
	[[nodiscard]] int32_t getMaxValue() const override { return 150; } // -15.0 to +15.0 dB in tenths

	const char* getUnit() override { return " DB"; }

	void readCurrentValue() override {
		float norm = AudioEngine::reverb.getBassGain();
		this->setValue(-150 + static_cast<int32_t>(std::round(norm * 300.0f)));
	}

	void writeCurrentValue() override {
		float norm = static_cast<float>(this->getValue() + 150) / 300.0f;
		AudioEngine::reverb.setBassGain(std::clamp(norm, 0.0f, 1.0f));
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
		if (v < 0) {
			snprintf(buffer, sizeof(buffer), "-%d.%d DB", std::abs(v) / 10, std::abs(v) % 10);
		}
		else {
			snprintf(buffer, sizeof(buffer), "+%d.%d DB", v / 10, v % 10);
		}
		deluge::hid::display::OLED::main.drawStringCentred(buffer, 18 + OLED_MAIN_TOPMOST_PIXEL, kTextHugeSpacingX,
		                                                   kTextHugeSizeY);
	}

	void drawValue() override {
		char buffer[8];
		int32_t v = this->getValue();
		if (v < 0) {
			snprintf(buffer, sizeof(buffer), "-%d.%d", std::abs(v) / 10, std::abs(v) % 10);
		}
		else {
			snprintf(buffer, sizeof(buffer), "%d.%d", v / 10, v % 10);
		}
		display->setText(buffer, true);
	}

	[[nodiscard]] std::string_view getName() const override {
		using enum l10n::String;
		return l10n::getView(STRING_FOR_BASS);
	}
	[[nodiscard]] std::string_view getTitle() const override { return getName(); }

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		return AudioEngine::reverb.getModel() == dsp::Reverb::Model::KDFX_TONE;
	}
};

class ToneBassFreq final : public Integer {
public:
	using Integer::Integer;

	[[nodiscard]] int32_t getMinValue() const override { return 30; }
	[[nodiscard]] int32_t getMaxValue() const override { return 400; }
	const char* getUnit() override { return " HZ"; }

	void readCurrentValue() override {
		float norm = AudioEngine::reverb.getBassFreq();
		this->setValue(30 + static_cast<int32_t>(std::round(norm * 370.0f)));
	}

	void writeCurrentValue() override {
		float norm = static_cast<float>(this->getValue() - 30) / 370.0f;
		AudioEngine::reverb.setBassFreq(std::clamp(norm, 0.0f, 1.0f));
	}

	void selectEncoderAction(int32_t offset) override {
		bool shift = Buttons::isShiftButtonPressed();
		int32_t step = shift ? 1 : 10;
		int32_t val = std::clamp(this->getValue() + offset * step, getMinValue(), getMaxValue());
		this->setValue(val);
		Number::selectEncoderAction(offset);
	}

	[[nodiscard]] std::string_view getName() const override {
		using enum l10n::String;
		return l10n::getView(STRING_FOR_BASS_FREQUENCY);
	}
	[[nodiscard]] std::string_view getTitle() const override { return getName(); }

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		return AudioEngine::reverb.getModel() == dsp::Reverb::Model::KDFX_TONE;
	}
};

class ToneMidGain final : public Integer {
public:
	using Integer::Integer;

	[[nodiscard]] int32_t getMinValue() const override { return -150; }
	[[nodiscard]] int32_t getMaxValue() const override { return 150; }

	const char* getUnit() override { return " DB"; }

	void readCurrentValue() override {
		float norm = AudioEngine::reverb.getMidGain();
		this->setValue(-150 + static_cast<int32_t>(std::round(norm * 300.0f)));
	}

	void writeCurrentValue() override {
		float norm = static_cast<float>(this->getValue() + 150) / 300.0f;
		AudioEngine::reverb.setMidGain(std::clamp(norm, 0.0f, 1.0f));
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
		if (v < 0) {
			snprintf(buffer, sizeof(buffer), "-%d.%d DB", std::abs(v) / 10, std::abs(v) % 10);
		}
		else {
			snprintf(buffer, sizeof(buffer), "+%d.%d DB", v / 10, v % 10);
		}
		deluge::hid::display::OLED::main.drawStringCentred(buffer, 18 + OLED_MAIN_TOPMOST_PIXEL, kTextHugeSpacingX,
		                                                   kTextHugeSizeY);
	}

	void drawValue() override {
		char buffer[8];
		int32_t v = this->getValue();
		if (v < 0) {
			snprintf(buffer, sizeof(buffer), "-%d.%d", std::abs(v) / 10, std::abs(v) % 10);
		}
		else {
			snprintf(buffer, sizeof(buffer), "%d.%d", v / 10, v % 10);
		}
		display->setText(buffer, true);
	}

	[[nodiscard]] std::string_view getName() const override {
		using enum l10n::String;
		return l10n::getView(STRING_FOR_MID_GAIN);
	}
	[[nodiscard]] std::string_view getTitle() const override { return getName(); }

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		return AudioEngine::reverb.getModel() == dsp::Reverb::Model::KDFX_TONE;
	}
};

class ToneMidFreq final : public Integer {
public:
	using Integer::Integer;

	[[nodiscard]] int32_t getMinValue() const override { return 200; }
	[[nodiscard]] int32_t getMaxValue() const override { return 8000; }
	const char* getUnit() override { return " HZ"; }

	void readCurrentValue() override {
		float norm = AudioEngine::reverb.getMidFreq();
		this->setValue(200 + static_cast<int32_t>(std::round(norm * 7800.0f)));
	}

	void writeCurrentValue() override {
		float norm = static_cast<float>(this->getValue() - 200) / 7800.0f;
		AudioEngine::reverb.setMidFreq(std::clamp(norm, 0.0f, 1.0f));
	}

	void selectEncoderAction(int32_t offset) override {
		bool shift = Buttons::isShiftButtonPressed();
		int32_t step = shift ? 10 : 100;
		int32_t val = std::clamp(this->getValue() + offset * step, getMinValue(), getMaxValue());
		this->setValue(val);
		Number::selectEncoderAction(offset);
	}

	[[nodiscard]] std::string_view getName() const override {
		using enum l10n::String;
		return l10n::getView(STRING_FOR_MID_FREQUENCY);
	}
	[[nodiscard]] std::string_view getTitle() const override { return getName(); }

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		return AudioEngine::reverb.getModel() == dsp::Reverb::Model::KDFX_TONE;
	}
};

class ToneTrebleGain final : public Integer {
public:
	using Integer::Integer;

	[[nodiscard]] int32_t getMinValue() const override { return -150; }
	[[nodiscard]] int32_t getMaxValue() const override { return 150; }

	const char* getUnit() override { return " DB"; }

	void readCurrentValue() override {
		float norm = AudioEngine::reverb.getTrebleGain();
		this->setValue(-150 + static_cast<int32_t>(std::round(norm * 300.0f)));
	}

	void writeCurrentValue() override {
		float norm = static_cast<float>(this->getValue() + 150) / 300.0f;
		AudioEngine::reverb.setTrebleGain(std::clamp(norm, 0.0f, 1.0f));
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
		if (v < 0) {
			snprintf(buffer, sizeof(buffer), "-%d.%d DB", std::abs(v) / 10, std::abs(v) % 10);
		}
		else {
			snprintf(buffer, sizeof(buffer), "+%d.%d DB", v / 10, v % 10);
		}
		deluge::hid::display::OLED::main.drawStringCentred(buffer, 18 + OLED_MAIN_TOPMOST_PIXEL, kTextHugeSpacingX,
		                                                   kTextHugeSizeY);
	}

	void drawValue() override {
		char buffer[8];
		int32_t v = this->getValue();
		if (v < 0) {
			snprintf(buffer, sizeof(buffer), "-%d.%d", std::abs(v) / 10, std::abs(v) % 10);
		}
		else {
			snprintf(buffer, sizeof(buffer), "%d.%d", v / 10, v % 10);
		}
		display->setText(buffer, true);
	}

	[[nodiscard]] std::string_view getName() const override {
		using enum l10n::String;
		return l10n::getView(STRING_FOR_TREBLE);
	}
	[[nodiscard]] std::string_view getTitle() const override { return getName(); }

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		return AudioEngine::reverb.getModel() == dsp::Reverb::Model::KDFX_TONE;
	}
};

class ToneTrebleFreq final : public Integer {
public:
	using Integer::Integer;

	[[nodiscard]] int32_t getMinValue() const override { return 1000; }
	[[nodiscard]] int32_t getMaxValue() const override { return 16000; }
	const char* getUnit() override { return " HZ"; }

	void readCurrentValue() override {
		float norm = AudioEngine::reverb.getTrebleFreq();
		this->setValue(1000 + static_cast<int32_t>(std::round(norm * 15000.0f)));
	}

	void writeCurrentValue() override {
		float norm = static_cast<float>(this->getValue() - 1000) / 15000.0f;
		AudioEngine::reverb.setTrebleFreq(std::clamp(norm, 0.0f, 1.0f));
	}

	void selectEncoderAction(int32_t offset) override {
		bool shift = Buttons::isShiftButtonPressed();
		int32_t step = shift ? 50 : 500;
		int32_t val = std::clamp(this->getValue() + offset * step, getMinValue(), getMaxValue());
		this->setValue(val);
		Number::selectEncoderAction(offset);
	}

	[[nodiscard]] std::string_view getName() const override {
		using enum l10n::String;
		return l10n::getView(STRING_FOR_TREBLE_FREQUENCY);
	}
	[[nodiscard]] std::string_view getTitle() const override { return getName(); }

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		return AudioEngine::reverb.getModel() == dsp::Reverb::Model::KDFX_TONE;
	}
};

class ToneLevel final : public Integer {
public:
	using Integer::Integer;

	[[nodiscard]] int32_t getMinValue() const override { return -240; }
	[[nodiscard]] int32_t getMaxValue() const override { return 120; } // -24.0 to +12.0 dB in tenths

	const char* getUnit() override { return " DB"; }

	void readCurrentValue() override {
		float norm = AudioEngine::reverb.getOutLevel();
		this->setValue(-240 + static_cast<int32_t>(std::round(norm * 360.0f)));
	}

	void writeCurrentValue() override {
		float norm = static_cast<float>(this->getValue() + 240) / 360.0f;
		AudioEngine::reverb.setOutLevel(std::clamp(norm, 0.0f, 1.0f));
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
		if (v < 0) {
			snprintf(buffer, sizeof(buffer), "-%d.%d DB", std::abs(v) / 10, std::abs(v) % 10);
		}
		else {
			snprintf(buffer, sizeof(buffer), "+%d.%d DB", v / 10, v % 10);
		}
		deluge::hid::display::OLED::main.drawStringCentred(buffer, 18 + OLED_MAIN_TOPMOST_PIXEL, kTextHugeSpacingX,
		                                                   kTextHugeSizeY);
	}

	void drawValue() override {
		char buffer[8];
		int32_t v = this->getValue();
		if (v < 0) {
			snprintf(buffer, sizeof(buffer), "-%d.%d", std::abs(v) / 10, std::abs(v) % 10);
		}
		else {
			snprintf(buffer, sizeof(buffer), "%d.%d", v / 10, v % 10);
		}
		display->setText(buffer, true);
	}

	[[nodiscard]] std::string_view getName() const override {
		using enum l10n::String;
		return l10n::getView(STRING_FOR_VOLUME_LEVEL);
	}
	[[nodiscard]] std::string_view getTitle() const override { return getName(); }

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		return AudioEngine::reverb.getModel() == dsp::Reverb::Model::KDFX_TONE;
	}
};

} // namespace deluge::gui::menu_item::reverb
