#pragma once
#include "gui/menu_item/toggle.h"
#include "gui/ui/sound_editor.h"
#include "processing/sound/sound.h"

namespace deluge::gui::menu_item::fx {
/// Whether the track's FX chain renders inside the sound's amp envelope (wet
/// tails end with the release) or outlives it (tails drain past the last voice).
class WithinEnv final : public Toggle {
public:
	using Toggle::Toggle;
	void readCurrentValue() override { this->setValue(soundEditor.currentSound->fxWithinAmpEnv); }
	void writeCurrentValue() override { soundEditor.currentSound->fxWithinAmpEnv = this->getValue(); }
};
} // namespace deluge::gui::menu_item::fx
