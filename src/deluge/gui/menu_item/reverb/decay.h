#pragma once

#include "dsp/reverb/reverb.hpp"
#include "gui/l10n/strings.h"
#include "gui/menu_item/integer.h"
#include "processing/engines/audio_engine.h"
#include <algorithm>
#include <cmath>

namespace deluge::gui::menu_item::reverb {
class Decay final : public Integer {
public:
	using Integer::Integer;

	[[nodiscard]] int32_t getMinValue() const override {
		if (AudioEngine::reverb.getModel() == dsp::Reverb::Model::KDFX_TONE) {
			return -15;
		}
		return 0;
	}

	[[nodiscard]] int32_t getMaxValue() const override {
		if (AudioEngine::reverb.getModel() == dsp::Reverb::Model::KDFX_TONE) {
			return 15;
		}
		return 100;
	}

	const char* getUnit() override {
		if (AudioEngine::reverb.getModel() == dsp::Reverb::Model::KDFX_TONE) {
			return " DB";
		}
		return " %";
	}

	void readCurrentValue() override {
		float norm = AudioEngine::reverb.getDecay();
		int32_t minV = getMinValue();
		int32_t maxV = getMaxValue();
		this->setValue(minV + static_cast<int32_t>(std::round(norm * (maxV - minV))));
	}

	void writeCurrentValue() override {
		int32_t minV = getMinValue();
		int32_t maxV = getMaxValue();
		float norm = (maxV > minV) ? (float)(this->getValue() - minV) / (float)(maxV - minV) : 0.0f;
		AudioEngine::reverb.setDecay(std::clamp(norm, 0.0f, 1.0f));
	}

	[[nodiscard]] std::string_view getName() const override {
		using enum l10n::String;
		switch (AudioEngine::reverb.getModel()) {
		case dsp::Reverb::Model::KDFX_DELAY:
			return l10n::getView(STRING_FOR_FEEDBACK);
		case dsp::Reverb::Model::KDFX_CHORUS:
			return l10n::getView(STRING_FOR_DEPTH);
		case dsp::Reverb::Model::KDFX_TONE:
			return l10n::getView(STRING_FOR_TREBLE);
		default:
			return l10n::getView(STRING_FOR_DECAY);
		}
	}
	[[nodiscard]] std::string_view getTitle() const override { return getName(); }

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		auto model = AudioEngine::reverb.getModel();
		return model == dsp::Reverb::Model::KDFX_REVERB || model == dsp::Reverb::Model::KDFX_DELAY
		       || model == dsp::Reverb::Model::KDFX_CHORUS || model == dsp::Reverb::Model::KDFX_TONE;
	}
};
} // namespace deluge::gui::menu_item::reverb
