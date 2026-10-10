#pragma once
#include <cstdio>
#include <cmath>

#include "gui/menu_item/fx_bloc/master_fx_items.h"
#include "gui/menu_item/integer.h"
#include "processing/engines/audio_engine.h"

namespace deluge::gui::menu_item::fx {
/// When the edited FX chain stops processing, in dBFS measured on the chain's
/// output. Above 0 dB: processing ends with the voices. -96..0 dB: an RMS gate
/// holds processing until the output falls below the threshold. -96: the chain
/// is always on (INF tails ring until the panic kill). Resolves the track or
/// master chain from the editing context.
class FxCutoff final : public Integer {
public:
	using Integer::Integer;
	// Item value 0 means VOICE (stored as +6 dB — anything above 0 behaves the
	// same, and storing 0.0f would accidentally arm the 0 dB gate); -1..-96 map
	// straight through. One detent down from VOICE is visibly -1DB.
	void readCurrentValue() override {
		auto* chain = fx_bloc::getCurrentFxChain();
		float db = (chain != nullptr) ? chain->fxRmsCutoffDb : 6.0f;
		this->setValue(db > 0.0f ? 0 : static_cast<int32_t>(std::lround(db)));
	}
	void writeCurrentValue() override {
		auto* chain = fx_bloc::getCurrentFxChain();
		if (chain != nullptr) {
			chain->fxRmsCutoffDb = (this->getValue() > 0) ? 6.0f : static_cast<float>(this->getValue());
		}
	}
	[[nodiscard]] int32_t getMinValue() const override { return -96; }
	[[nodiscard]] int32_t getMaxValue() const override { return 0; }
	void formatValueForVisualMenu(char* outBuffer, size_t bufferSize) override {
		int32_t v = this->getValue();
		if (v >= 0) {
			snprintf(outBuffer, bufferSize, "VOICE");
		}
		else if (v <= -96) {
			snprintf(outBuffer, bufferSize, "ALWAYS");
		}
		else {
			snprintf(outBuffer, bufferSize, "%dDB", v);
		}
	}
};
} // namespace deluge::gui::menu_item::fx
