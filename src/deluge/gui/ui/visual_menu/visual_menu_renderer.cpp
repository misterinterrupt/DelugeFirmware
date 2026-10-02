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

#include "visual_menu_renderer.h"
#include "gui/ui/sound_editor.h"
#include "gui/ui/visual_menu/visual_menu_ui.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "util/cfunctions.h"
#include "util/functions.h"
#include <cstdio>
#include <cstring>

#include "gui/menu_item/integer.h"
#include "gui/menu_item/selection.h"
#include "gui/menu_item/toggle.h"
#include "gui/menu_item/value.h"

namespace deluge::gui::ui::visual_menu {

static const char* kSlotButtonLabels[4] = {"SYNTH", "KIT", "MIDI", "CV"};

void VisualMenuRenderer::formatSlotValue(MenuItem* item, int8_t sourceIndexOverride, char* outBuffer,
                                         size_t bufferSize) {
	if (item == nullptr || outBuffer == nullptr || bufferSize == 0) {
		if (outBuffer && bufferSize > 0) {
			outBuffer[0] = '\0';
		}
		return;
	}

	VisualSlot slotRef{};
	slotRef.item = item;
	slotRef.sourceIndexOverride = sourceIndexOverride;
	VisualMenuUI::SlotSourceContext ctx;
	VisualMenuUI::pushSlotContext(slotRef, ctx);

	item->formatValueForVisualMenu(outBuffer, bufferSize);

	VisualMenuUI::popSlotContext(ctx);
}

void VisualMenuRenderer::renderPage(deluge::hid::display::oled_canvas::Canvas& canvas,
                                    const VisualCategory& category, int32_t categoryIndex, int32_t totalCategories,
                                    const VisualPage& page, int32_t pageIndex, int32_t totalPages,
                                    int8_t activeHeldSlot, int8_t decimalCursorSlot, int8_t decimalEditPos) {
	canvas.clear();

	// 1. Top Status Header (Category Name & Page Counter)
	std::string_view catName = category.getName();
	std::string_view pageTitle = page.getTitle();

	canvas.drawString(catName, 2, 0, kTextSpacingX, kTextSizeYUpdated, 0, 95);
	if (!pageTitle.empty()) {
		int32_t sepX = 2 + (static_cast<int32_t>(catName.length()) + 1) * kTextSpacingX;
		canvas.drawString(">", sepX, 0, kTextSpacingX, kTextSizeYUpdated);
		canvas.drawString(pageTitle, sepX + kTextSpacingX * 2, 0, kTextSpacingX, kTextSizeYUpdated, 0, 95);
	}

	char pageCounterBuf[12];
	pageCounterBuf[0] = '[';
	char numBuf[8];
	intToString(pageIndex + 1, numBuf);
	strcpy(pageCounterBuf + 1, numBuf);
	strcat(pageCounterBuf, "/");
	intToString(totalPages, numBuf);
	strcat(pageCounterBuf, numBuf);
	strcat(pageCounterBuf, "]");
	canvas.drawString(pageCounterBuf, 96, 0, kTextSpacingX, kTextSizeYUpdated);

	// Header line
	canvas.drawHorizontalLine(10, 0, 127);

	// 2. Render 4 Columns (32 pixels each)
	for (int32_t s = 0; s < kSlotsPerPage; ++s) {
		const auto& slot = page.slots[s];
		int32_t colLeft = s * 32;
		int32_t colRight = colLeft + 31;
		int32_t colCenter = colLeft + 16;

		// Vertical separator line
		if (s > 0) {
			canvas.drawVerticalLine(colLeft, 11, 63);
		}

		if (slot.item != nullptr) {
			// Row 1: Parameter Short Name (y = 13)
			std::string_view name = slot.getDisplayName();
			int32_t nameWidth = static_cast<int32_t>(name.length()) * kTextSpacingX;
			int32_t nameX = colCenter - (nameWidth / 2);
			if (nameX < colLeft + 1) nameX = colLeft + 1;
			canvas.drawString(name, nameX, 13, kTextSpacingX, kTextSizeYUpdated, 0, colRight);

			// Row 2: Live Value (y = 26)
			char valBuf[16];
			formatSlotValue(slot.item, slot.sourceIndexOverride, valBuf, sizeof(valBuf));
			int32_t valLen = static_cast<int32_t>(strlen(valBuf));
			int32_t valWidth = valLen * kTextSpacingX;
			int32_t valX = colCenter - (valWidth / 2);
			if (valX < colLeft + 1) valX = colLeft + 1;
			canvas.drawString(valBuf, valX, 26, kTextSpacingX, kTextSizeYUpdated, 0, colRight);

			// Decimal cursor if active
			if (s == decimalCursorSlot && decimalEditPos >= 0) {
				canvas.drawString("^", colCenter - 3, 38, kTextSpacingX, kTextSizeYUpdated);
			}

			// Row 3: Button Label / Slot Indicator (y = 52)
			const char* btnLabel = kSlotButtonLabels[s];
			int32_t btnWidth = static_cast<int32_t>(strlen(btnLabel)) * kTextSpacingX;
			int32_t btnX = colCenter - (btnWidth / 2);
			canvas.drawString(btnLabel, btnX, 52, kTextSpacingX, kTextSizeYUpdated, 0, colRight);

			// Invert active held slot box for clear feedback
			if (s == activeHeldSlot) {
				canvas.invertArea(colLeft + 1, 30, 11, 63);
			}
		}
		else {
			// Empty slot indicator
			canvas.drawString("--", colCenter - 6, 26, kTextSpacingX, kTextSizeYUpdated);
		}
	}
}

} // namespace deluge::gui::ui::visual_menu
