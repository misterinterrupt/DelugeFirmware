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

// The per-column slot button labels (SYNTH/KIT/MIDI/CV) from the original
// 64-row draft never fit the 48-row panel — the row lived at y=52+ on every
// build, so it was always invisible — and no 7px row remains below the value
// and caret. Dropped rather than drawn out of bounds.

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

	// Every row below is derived from the panel macros and stays inside
	// [top, OLED_MAIN_HEIGHT_PIXELS - 1]. Canvas primitives do NOT clamp Y —
	// image_ is only OLED_MAIN_HEIGHT_PIXELS/8 pages, so any row past the
	// bottom writes over adjacent .bss (needsSending, saveSongOrInstrument,
	// stemExport, dxBrowser — the spray that wedged bloc-select).
	int32_t top = OLED_MAIN_TOPMOST_PIXEL;
	int32_t bottom = OLED_MAIN_HEIGHT_PIXELS - 1;

	// Two-line header so the page counter never collides with long titles:
	//   line 1: category name
	//   line 2: page title (left) + [n/m] (right-aligned)
	std::string_view catName = category.getName();
	canvas.drawString(catName, 2, top + 0, kTextSpacingX, kTextSizeYUpdated, 0, 95);

	std::string_view pageTitle = page.getTitle();
	if (!pageTitle.empty()) {
		canvas.drawString(pageTitle, 2, top + 8, kTextSpacingX, kTextSizeYUpdated, 0, 90);
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
	int32_t counterWidth = static_cast<int32_t>(strlen(pageCounterBuf)) * kTextSpacingX;
	canvas.drawString(pageCounterBuf, 126 - counterWidth, top + 8, kTextSpacingX, kTextSizeYUpdated);

	// Header separator
	canvas.drawHorizontalLine(top + 15, 0, 127);

	// 2. Render 4 Columns (32 pixels each). A slot may span several columns
	// (VisualSlot::span) so long names stay readable — FX bloc names need more
	// than 32px; spans on a page must sum to kSlotsPerPage.
	int32_t colLeft = 0;
	for (int32_t s = 0; s < kSlotsPerPage; ++s) {
		const auto& slot = page.slots[s];
		if (slot.span == 0) {
			continue; // covered by a previous slot's span
		}
		if (colLeft >= OLED_MAIN_WIDTH_PIXELS) {
			break; // page's spans overflow the width — never draw past the canvas
		}
		int32_t colRight = colLeft + slot.span * 32 - 1;
		int32_t colCenter = colLeft + (slot.span * 32 >> 1);

		// Vertical separator line
		if (colLeft > 0) {
			canvas.drawVerticalLine(colLeft, top + 16, bottom);
		}

		if (slot.item != nullptr) {
			// Row 1: Parameter Short Name
			std::string_view name = slot.getDisplayName();
			int32_t nameWidth = static_cast<int32_t>(name.length()) * kTextSpacingX;
			int32_t nameX = colCenter - (nameWidth / 2);
			if (nameX < colLeft + 1) nameX = colLeft + 1;
			canvas.drawString(name, nameX, top + 17, kTextSpacingX, kTextSizeYUpdated, 0, colRight);

			// Row 2: Live Value
			char valBuf[16];
			formatSlotValue(slot.item, slot.sourceIndexOverride, valBuf, sizeof(valBuf));
			int32_t valLen = static_cast<int32_t>(strlen(valBuf));
			int32_t valWidth = valLen * kTextSpacingX;
			int32_t valX = colCenter - (valWidth / 2);
			if (valX < colLeft + 1) valX = colLeft + 1;
			canvas.drawString(valBuf, valX, top + 27, kTextSpacingX, kTextSizeYUpdated, 0, colRight);

			// Decimal cursor if active
			if (s == decimalCursorSlot && decimalEditPos >= 0 && slot.item->numDecimalPositions() > 1) {
				const char* dot = strchr(valBuf, '.');
				bool kNotation = false; // "3.7K": the dot is a thousands separator, not a decimal point
				if (dot != nullptr) {
					size_t dotIdx = static_cast<size_t>(dot - valBuf);
					kNotation = valBuf[dotIdx + 1] != '\0' && (valBuf[dotIdx + 1] == 'K' || valBuf[dotIdx + 2] == 'K');
				}
				int32_t targetCharIdx = -1;
				if (dot != nullptr && !kNotation) {
					int32_t dotIdx = static_cast<int32_t>(dot - valBuf);
					if (decimalEditPos == 0) {
						targetCharIdx = dotIdx - 1;
					}
					else if (decimalEditPos == 1) {
						targetCharIdx = dotIdx + 1;
					}
					else if (decimalEditPos == 2) {
						targetCharIdx = dotIdx + 2;
					}
				}
				else {
					int32_t lastDigit = -1;
					for (int32_t i = 0; i < valLen; i++) {
						if (valBuf[i] >= '0' && valBuf[i] <= '9') {
							lastDigit = i;
						}
					}
					if (lastDigit >= 0) {
						targetCharIdx = lastDigit - decimalEditPos;
						if (targetCharIdx < 0) {
							targetCharIdx = 0;
						}
					}
				}
				int32_t caretX = (targetCharIdx >= 0 && targetCharIdx < valLen)
				                     ? (valX + targetCharIdx * kTextSpacingX)
				                     : (colCenter - 3);
				canvas.drawString("^", caretX, top + 34, kTextSpacingX, kTextSizeYUpdated);
			}

			// Active highlight: invertArea()'s byte walk (image_[rowY][xMin] + width bytes)
				// runs past the end of the canvas image for these coordinates and XORs
				// display masks over adjacent .bss (stemExport, masterFxChain, ...),
				// which is what crashed the audio task. Use bounded line primitives instead.
				if (s == activeHeldSlot) {
					canvas.drawVerticalLine(colLeft + 1, top + 16, bottom);
					canvas.drawVerticalLine(colLeft + 30, top + 16, bottom);
					canvas.drawHorizontalLine(top + 16, colLeft + 1, colLeft + 30);
					canvas.drawHorizontalLine(bottom, colLeft + 1, colLeft + 30);
				}
		}
		else {
			// Empty slot indicator
			canvas.drawString("--", colCenter - 6, top + 27, kTextSpacingX, kTextSizeYUpdated);
		}

		colLeft += slot.span * 32;
	}
}

} // namespace deluge::gui::ui::visual_menu
