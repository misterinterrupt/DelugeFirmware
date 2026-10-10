#pragma once
#include <cstdio>

#include "gui/menu_item/integer.h"
#include "gui/ui/sound_editor.h"
#include "processing/sound/sound.h"

namespace deluge::gui::menu_item::fx {
/// When this track's FX chain stops processing, in dBFS measured on the chain's
/// output. Above 0 dB: processing ends with the voices. -96..0 dB: an RMS gate
/// holds processing until the output falls below the threshold. -96: the chain
/// is always on (INF tails ring until the panic kill).
class FxCutoff final : public Integer {
public:
	using Integer::Integer;
	void readCurrentValue() override {
		this->setValue(static_cast<int32_t>(std::lround(soundEditor.currentSound->fxChain.fxRmsCutoffDb)));
	}
	void writeCurrentValue() override {
		soundEditor.currentSound->fxChain.fxRmsCutoffDb = static_cast<float>(this->getValue());
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
