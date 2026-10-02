#pragma once

#include "dsp/reverb/reverb.hpp"
#include "gui/l10n/strings.h"
#include "gui/menu_item/integer.h"
#include "hid/buttons.h"
#include "processing/engines/audio_engine.h"
#include <algorithm>
#include <cmath>

namespace deluge::gui::menu_item::reverb {

class EarlyReflections final : public Integer {
public:
	using Integer::Integer;

	[[nodiscard]] int32_t getMinValue() const override { return 0; }
	[[nodiscard]] int32_t getMaxValue() const override { return 100; }
	const char* getUnit() override { return " %"; }

	void readCurrentValue() override {
		float norm = AudioEngine::reverb.getEarlyReflections();
		this->setValue(static_cast<int32_t>(std::round(norm * 100.0f)));
	}

	void writeCurrentValue() override {
		float norm = static_cast<float>(this->getValue()) / 100.0f;
		AudioEngine::reverb.setEarlyReflections(std::clamp(norm, 0.0f, 1.0f));
	}

	[[nodiscard]] std::string_view getName() const override {
		using enum l10n::String;
		return l10n::getView(STRING_FOR_EARLY_REFLECTIONS);
	}
	[[nodiscard]] std::string_view getTitle() const override { return getName(); }

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		return AudioEngine::reverb.getModel() == dsp::Reverb::Model::KDFX_REVERB;
	}
};

class BassDecay final : public Integer {
public:
	using Integer::Integer;

	[[nodiscard]] int32_t getMinValue() const override { return 20; }
	[[nodiscard]] int32_t getMaxValue() const override { return 250; } // 0.2x to 2.5x (20% to 250%)
	const char* getUnit() override { return " %"; }

	void readCurrentValue() override {
		float norm = AudioEngine::reverb.getBassDecay();
		this->setValue(20 + static_cast<int32_t>(std::round(norm * 230.0f)));
	}

	void writeCurrentValue() override {
		float norm = static_cast<float>(this->getValue() - 20) / 230.0f;
		AudioEngine::reverb.setBassDecay(std::clamp(norm, 0.0f, 1.0f));
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
		return l10n::getView(STRING_FOR_BASS_DECAY);
	}
	[[nodiscard]] std::string_view getTitle() const override { return getName(); }

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		return AudioEngine::reverb.getModel() == dsp::Reverb::Model::KDFX_REVERB;
	}
};

class AirShelf final : public Integer {
public:
	using Integer::Integer;

	[[nodiscard]] int32_t getMinValue() const override { return 0; }
	[[nodiscard]] int32_t getMaxValue() const override { return 100; }
	const char* getUnit() override { return " %"; }

	void readCurrentValue() override {
		float norm = AudioEngine::reverb.getAirShelf();
		this->setValue(static_cast<int32_t>(std::round(norm * 100.0f)));
	}

	void writeCurrentValue() override {
		float norm = static_cast<float>(this->getValue()) / 100.0f;
		AudioEngine::reverb.setAirShelf(std::clamp(norm, 0.0f, 1.0f));
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
		return l10n::getView(STRING_FOR_AIR);
	}
	[[nodiscard]] std::string_view getTitle() const override { return getName(); }

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		return AudioEngine::reverb.getModel() == dsp::Reverb::Model::KDFX_REVERB;
	}
};

} // namespace deluge::gui::menu_item::reverb
