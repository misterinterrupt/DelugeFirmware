/*
 * Copyright © 2024 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include "gui/ui/ui.h"
#include "gui/ui/visual_menu/visual_menu_data.h"
#include "model/clip/clip.h"
#include "processing/source.h"
#include <cstdint>
#include <span>

namespace deluge::gui::ui::visual_menu {

class VisualMenuUI final : public UI {
public:
	VisualMenuUI();

	bool opened() override;
	void focusRegained() override;
	bool exitUI() override;

	ActionResult buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) override;
	ActionResult padAction(int32_t x, int32_t y, int32_t velocity) override;
	void selectEncoderAction(int8_t offset) override;
	void renderOLED(deluge::hid::display::oled_canvas::Canvas& canvas) override;

	UIType getUIType() override { return UIType::VISUAL_MENU; }
	const char* getName() override { return "visual_menu"; }

	bool openForClip(Clip* clip, const MenuItem* targetItem = nullptr);
	void closeVisualMenu();

	int8_t getActiveHeldSlot() const;
	void updateButtonLEDs();

	/// Save/restore the soundEditor source context (index AND pointer) around per-slot access, so that slots
	/// targeting osc/env/modulator 2 don't silently read or write source 1's state.
	struct SlotSourceContext {
		int8_t index;
		Source* source;
		SampleControls* sampleControls;
	};
	static void pushSlotContext(const VisualSlot& slot, SlotSourceContext& ctx);
	static void popSlotContext(const SlotSourceContext& ctx);

private:
	std::span<const VisualCategory> categories_;
	int32_t currentCategoryIndex_ = 0;
	int32_t currentPageIndex_ = 0;

	int8_t selectedSlot_ = -1;
	int8_t decimalCursorSlot_ = -1;
	int8_t decimalEditPos_ = 0;

	std::array<bool, 4> slotHeld_{false, false, false, false};
	std::array<bool, 4> turnedWhileHeld_{false, false, false, false};
	std::array<bool, 4> wasAlreadySelected_{false, false, false, false};

	Clip* currentClip_ = nullptr;
	bool contextIsClipMinder_ = false;

	void applySourceIndexForCurrentPage();
};

extern VisualMenuUI visualMenuUI;

} // namespace deluge::gui::ui::visual_menu
