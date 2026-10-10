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
	void readCurrentValue() override {
		auto* chain = fx_bloc::getCurrentFxChain();
		if (chain == nullptr) {
			this->setValue(6);
			return;
		}
		this->setValue(static_cast<int32_t>(std::lround(chain->fxRmsCutoffDb)));
	}
	void writeCurrentValue() override {
		auto* chain = fx_bloc::getCurrentFxChain();
		if (chain != nullptr) {
			chain->fxRmsCutoffDb = static_cast<float>(this->getValue());
		}
	}
	[[nodiscard]] int32_t getMinValue() const override { return -96; }
	[[nodiscard]] int32_t getMaxValue() const override { return 6; }
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
