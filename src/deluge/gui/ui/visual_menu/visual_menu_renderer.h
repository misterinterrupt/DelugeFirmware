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

#include "gui/ui/visual_menu/visual_menu_data.h"
#include "hid/display/oled_canvas/canvas.h"
#include <cstdint>

namespace deluge::gui::ui::visual_menu {

class VisualMenuRenderer {
public:
	static void renderPage(deluge::hid::display::oled_canvas::Canvas& canvas,
	                       const VisualCategory& category, int32_t categoryIndex, int32_t totalCategories,
	                       const VisualPage& page, int32_t pageIndex, int32_t totalPages,
	                       int8_t activeHeldSlot, int8_t decimalCursorSlot, int8_t decimalEditPos);

	static void formatSlotValue(MenuItem* item, int8_t sourceIndexOverride, char* outBuffer, size_t bufferSize);
};

} // namespace deluge::gui::ui::visual_menu
