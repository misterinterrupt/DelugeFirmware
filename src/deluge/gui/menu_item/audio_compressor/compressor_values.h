#pragma once
#include "definitions_cxx.hpp"
#include "gui/menu_item/integer.h"
#include "gui/menu_item/unpatched_param.h"
#include "gui/menu_item/value_scaling.h"
#include "gui/ui/sound_editor.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "model/mod_controllable/mod_controllable_audio.h"
#include "modulation/params/param.h"
#include "modulation/params/param_set.h"
#include "util/cfunctions.h"
#include "util/fixedpoint.h"

namespace deluge::gui::menu_item::audio_compressor {

class Threshold final : public UnpatchedParam {
public:
	Threshold(l10n::String newName, l10n::String title)
	    : UnpatchedParam(newName, title, deluge::modulation::params::UNPATCHED_COMPRESSOR_THRESHOLD) {}
	Threshold(l10n::String newName)
	    : UnpatchedParam(newName, deluge::modulation::params::UNPATCHED_COMPRESSOR_THRESHOLD) {}

	void readCurrentValue() override {
		int32_t raw = soundEditor.currentParamManager->getUnpatchedParamSet()->getValue(getP());
		this->setValue(computeCurrentValueForHalfPrecisionMenuItem(raw));
	}

	int32_t getFinalValue() override {
		return computeFinalValueForHalfPrecisionMenuItem(this->getValue());
	}

	int32_t getDisplayValue() override {
		float pos = (float)this->getValue() / (float)kMaxMenuValue;
		return (int32_t)std::round(-pos * 48.0f);
	}

	const char* getUnit() override { return " dB"; }
	[[nodiscard]] int32_t getMaxValue() const override { return kMaxMenuValue; }
	[[nodiscard]] int32_t getMinValue() const override { return 0; }
};

class Attack final : public UnpatchedParam {
public:
	Attack(l10n::String newName, l10n::String title)
	    : UnpatchedParam(newName, title, deluge::modulation::params::UNPATCHED_COMPRESSOR_ATTACK) {}
	Attack(l10n::String newName)
	    : UnpatchedParam(newName, deluge::modulation::params::UNPATCHED_COMPRESSOR_ATTACK) {}

	void readCurrentValue() override {
		int32_t raw = soundEditor.currentParamManager->getUnpatchedParamSet()->getValue(getP());
		this->setValue(computeCurrentValueForHalfPrecisionMenuItem(raw));
	}

	int32_t getFinalValue() override {
		return computeFinalValueForHalfPrecisionMenuItem(this->getValue());
	}

	void formatAttack(char* buffer, size_t maxLen) {
		if (maxLen < 10) return;
		float normA = (float)this->getValue() / (float)kMaxMenuValue;
		float attackMS = 0.5f + normA * normA * 99.5f;
		if (attackMS < 9.95f) {
			int32_t aTenths = (int32_t)std::round(attackMS * 10.0f);
			intToString(aTenths / 10, buffer, 1);
			size_t len = strlen(buffer);
			buffer[len] = '.';
			intToString(aTenths % 10, buffer + len + 1, 1);
		}
		else {
			int32_t aInt = (int32_t)std::round(attackMS);
			intToString(aInt, buffer, 1);
		}
		size_t len = strlen(buffer);
		buffer[len] = ' ';
		buffer[len + 1] = 'M';
		buffer[len + 2] = 'S';
		buffer[len + 3] = '\0';
	}

	void drawPixelsForOled() override {
		char buffer[16];
		formatAttack(buffer, sizeof(buffer));
		deluge::hid::display::OLED::main.drawStringCentred(buffer, 18 + OLED_MAIN_TOPMOST_PIXEL, kTextHugeSpacingX,
		                                                   kTextHugeSizeY);
	}

	void drawValue() override {
		char buffer[16];
		formatAttack(buffer, sizeof(buffer));
		display->setText(buffer, true);
	}

	int32_t getDisplayValue() override { return this->getValue(); }
	const char* getUnit() override { return ""; }
	[[nodiscard]] int32_t getMaxValue() const override { return kMaxMenuValue; }
	[[nodiscard]] int32_t getMinValue() const override { return 0; }
};

class Release final : public UnpatchedParam {
public:
	Release(l10n::String newName, l10n::String title)
	    : UnpatchedParam(newName, title, deluge::modulation::params::UNPATCHED_COMPRESSOR_RELEASE) {}
	Release(l10n::String newName)
	    : UnpatchedParam(newName, deluge::modulation::params::UNPATCHED_COMPRESSOR_RELEASE) {}

	void readCurrentValue() override {
		int32_t raw = soundEditor.currentParamManager->getUnpatchedParamSet()->getValue(getP());
		this->setValue(computeCurrentValueForHalfPrecisionMenuItem(raw));
	}

	int32_t getFinalValue() override {
		return computeFinalValueForHalfPrecisionMenuItem(this->getValue());
	}

	void formatRelease(char* buffer, size_t maxLen) {
		if (maxLen < 10) return;
		float normR = (float)this->getValue() / (float)kMaxMenuValue;
		float releaseMS = 10.0f + normR * normR * 990.0f;
		if (releaseMS >= 999.5f) {
			int32_t rTenths = (int32_t)std::round(releaseMS / 100.0f);
			intToString(rTenths / 10, buffer, 1);
			size_t len = strlen(buffer);
			buffer[len] = '.';
			intToString(rTenths % 10, buffer + len + 1, 1);
			len = strlen(buffer);
			buffer[len] = ' ';
			buffer[len + 1] = 'S';
			buffer[len + 2] = '\0';
		}
		else {
			int32_t rInt = (int32_t)std::round(releaseMS);
			intToString(rInt, buffer, 1);
			size_t len = strlen(buffer);
			buffer[len] = ' ';
			buffer[len + 1] = 'M';
			buffer[len + 2] = 'S';
			buffer[len + 3] = '\0';
		}
	}

	void drawPixelsForOled() override {
		char buffer[16];
		formatRelease(buffer, sizeof(buffer));
		deluge::hid::display::OLED::main.drawStringCentred(buffer, 18 + OLED_MAIN_TOPMOST_PIXEL, kTextHugeSpacingX,
		                                                   kTextHugeSizeY);
	}

	void drawValue() override {
		char buffer[16];
		formatRelease(buffer, sizeof(buffer));
		display->setText(buffer, true);
	}

	int32_t getDisplayValue() override { return this->getValue(); }
	const char* getUnit() override { return ""; }
	[[nodiscard]] int32_t getMaxValue() const override { return kMaxMenuValue; }
	[[nodiscard]] int32_t getMinValue() const override { return 0; }
};

class Ratio final : public UnpatchedParam {
public:
	Ratio(l10n::String newName, l10n::String title)
	    : UnpatchedParam(newName, title, deluge::modulation::params::UNPATCHED_COMPRESSOR_RATIO) {}
	Ratio(l10n::String newName)
	    : UnpatchedParam(newName, deluge::modulation::params::UNPATCHED_COMPRESSOR_RATIO) {}

	void readCurrentValue() override {
		int32_t raw = soundEditor.currentParamManager->getUnpatchedParamSet()->getValue(getP());
		this->setValue(computeCurrentValueForHalfPrecisionMenuItem(raw));
	}

	int32_t getFinalValue() override {
		return computeFinalValueForHalfPrecisionMenuItem(this->getValue());
	}

	void formatRatio(char* buffer, size_t maxLen) {
		if (maxLen < 8) return;
		float pos = (float)this->getValue() / (float)kMaxMenuValue;
		float r;
		if (pos >= 0.5f) {
			float t = (pos - 0.5f) / 0.5f;
			r = std::exp(4.85203026f * t);
		}
		else {
			float t = (0.5f - pos) / 0.5f;
			r = 1.0f / (1.0f + 9.0f * t);
		}
		buffer[0] = '1';
		buffer[1] = ':';
		if (r >= 9.95f) {
			int32_t rInt = (int32_t)(r + 0.5f);
			intToString(rInt, buffer + 2, 1);
		}
		else if (r >= 0.995f) {
			int32_t rTenths = (int32_t)std::round(r * 10.0f);
			intToString(rTenths / 10, buffer + 2, 1);
			size_t len = strlen(buffer);
			buffer[len] = '.';
			intToString(rTenths % 10, buffer + len + 1, 1);
		}
		else {
			int32_t rHundredths = (int32_t)std::round(r * 100.0f);
			if (rHundredths < 0) rHundredths = 0;
			if (rHundredths > 99) rHundredths = 99;
			buffer[2] = '0';
			buffer[3] = '.';
			buffer[4] = (char)('0' + (rHundredths / 10));
			buffer[5] = (char)('0' + (rHundredths % 10));
			buffer[6] = '\0';
		}
	}

	void drawPixelsForOled() override {
		char buffer[16];
		formatRatio(buffer, sizeof(buffer));
		deluge::hid::display::OLED::main.drawStringCentred(buffer, 18 + OLED_MAIN_TOPMOST_PIXEL, kTextHugeSpacingX,
		                                                   kTextHugeSizeY);
	}

	void drawValue() override {
		char buffer[16];
		formatRatio(buffer, sizeof(buffer));
		display->setText(buffer, true);
	}

	int32_t getDisplayValue() override { return this->getValue(); }
	const char* getUnit() override { return ""; }
	[[nodiscard]] int32_t getMaxValue() const override { return kMaxMenuValue; }
	[[nodiscard]] int32_t getMinValue() const override { return 0; }
};

class InputGain final : public UnpatchedParam {
public:
	InputGain(l10n::String newName, l10n::String title)
	    : UnpatchedParam(newName, title, deluge::modulation::params::UNPATCHED_COMPRESSOR_INPUT_GAIN) {}
	InputGain(l10n::String newName)
	    : UnpatchedParam(newName, deluge::modulation::params::UNPATCHED_COMPRESSOR_INPUT_GAIN) {}

	void readCurrentValue() override {
		int32_t raw = soundEditor.currentParamManager->getUnpatchedParamSet()->getValue(getP());
		this->setValue(computeCurrentValueForHalfPrecisionMenuItem(raw));
	}

	int32_t getFinalValue() override {
		return computeFinalValueForHalfPrecisionMenuItem(this->getValue());
	}

	int32_t getDisplayValue() override {
		float pos = (float)this->getValue() / (float)kMaxMenuValue;
		return (int32_t)std::round(pos * 12.0f);
	}

	const char* getUnit() override { return " dB"; }
	[[nodiscard]] int32_t getMaxValue() const override { return kMaxMenuValue; }
	[[nodiscard]] int32_t getMinValue() const override { return 0; }
};

class MakeupGain final : public UnpatchedParam {
public:
	MakeupGain(l10n::String newName, l10n::String title)
	    : UnpatchedParam(newName, title, deluge::modulation::params::UNPATCHED_COMPRESSOR_MAKEUP_GAIN) {}
	MakeupGain(l10n::String newName)
	    : UnpatchedParam(newName, deluge::modulation::params::UNPATCHED_COMPRESSOR_MAKEUP_GAIN) {}

	void readCurrentValue() override {
		int32_t raw = soundEditor.currentParamManager->getUnpatchedParamSet()->getValue(getP());
		this->setValue(computeCurrentValueForHalfPrecisionMenuItem(raw));
	}

	int32_t getFinalValue() override {
		return computeFinalValueForHalfPrecisionMenuItem(this->getValue());
	}

	int32_t getDisplayValue() override {
		float pos = (float)this->getValue() / (float)kMaxMenuValue;
		return (int32_t)std::round((pos - 0.5f) * 24.0f);
	}

	const char* getUnit() override { return " dB"; }
	[[nodiscard]] int32_t getMaxValue() const override { return kMaxMenuValue; }
	[[nodiscard]] int32_t getMinValue() const override { return 0; }
};

class SideHPF final : public Integer {
public:
	using Integer::Integer;
	void readCurrentValue() override {
		auto value = (uint64_t)soundEditor.currentModControllable->compressor.getSidechain();
		this->setValue(value >> 24);
	}
	void writeCurrentValue() override {
		auto value = this->getValue();
		if (value < kMaxKnobPos) {
			q31_t knobPos = lshiftAndSaturate<24>(value);
			soundEditor.currentModControllable->compressor.setSidechain(knobPos);
		}
		else if (value == kMaxKnobPos) {
			soundEditor.currentModControllable->compressor.setSidechain(ONE_Q31);
		}
	}
	int32_t getDisplayValue() override {
		return soundEditor.currentModControllable->compressor.getSidechainForDisplay();
	}
	const char* getUnit() override { return " HZ"; }
	[[nodiscard]] int32_t getMaxValue() const override { return kMaxKnobPos; }
	[[nodiscard]] int32_t getMinValue() const override { return 0; }
};

class Blend final : public UnpatchedParam {
public:
	Blend(l10n::String newName, l10n::String title)
	    : UnpatchedParam(newName, title, deluge::modulation::params::UNPATCHED_COMPRESSOR_BLEND) {}
	Blend(l10n::String newName)
	    : UnpatchedParam(newName, deluge::modulation::params::UNPATCHED_COMPRESSOR_BLEND) {}

	void readCurrentValue() override {
		int32_t raw = soundEditor.currentParamManager->getUnpatchedParamSet()->getValue(getP());
		this->setValue(computeCurrentValueForHalfPrecisionMenuItem(raw));
	}

	int32_t getFinalValue() override {
		return computeFinalValueForHalfPrecisionMenuItem(this->getValue());
	}

	int32_t getDisplayValue() override {
		return (this->getValue() * 100) / kMaxMenuValue;
	}

	const char* getUnit() override { return " %"; }
	[[nodiscard]] int32_t getMaxValue() const override { return kMaxMenuValue; }
	[[nodiscard]] int32_t getMinValue() const override { return 0; }
};

} // namespace deluge::gui::menu_item::audio_compressor
