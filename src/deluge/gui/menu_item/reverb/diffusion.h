#pragma once

#include "dsp/reverb/reverb.hpp"
#include "gui/l10n/strings.h"
#include "gui/menu_item/integer.h"
#include "processing/engines/audio_engine.h"
#include <algorithm>
#include <cmath>

namespace deluge::gui::menu_item::reverb {
class Diffusion final : public Integer {
public:
	using Integer::Integer;

	[[nodiscard]] int32_t getMinValue() const override { return 0; }
	[[nodiscard]] int32_t getMaxValue() const override { return 100; }
	const char* getUnit() override { return " %"; }

	void readCurrentValue() override {
		float norm = AudioEngine::reverb.getDiffusion();
		int32_t minV = getMinValue();
		int32_t maxV = getMaxValue();
		this->setValue(minV + static_cast<int32_t>(std::round(norm * (maxV - minV))));
	}

	void writeCurrentValue() override {
		int32_t minV = getMinValue();
		int32_t maxV = getMaxValue();
		float norm = (maxV > minV) ? (float)(this->getValue() - minV) / (float)(maxV - minV) : 0.0f;
		AudioEngine::reverb.setDiffusion(std::clamp(norm, 0.0f, 1.0f));
	}

	[[nodiscard]] std::string_view getName() const override {
		using enum l10n::String;
		return l10n::getView(STRING_FOR_DIFFUSION);
	}
	[[nodiscard]] std::string_view getTitle() const override { return getName(); }

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		auto model = AudioEngine::reverb.getModel();
		return model == dsp::Reverb::Model::MUTABLE || model == dsp::Reverb::Model::KDFX_REVERB;
	}
};
} // namespace deluge::gui::menu_item::reverb
