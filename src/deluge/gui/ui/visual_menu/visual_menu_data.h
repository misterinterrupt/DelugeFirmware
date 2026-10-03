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

#include "definitions_cxx.hpp"
#include "gui/l10n/l10n.h"
#include "gui/l10n/strings.h"
#include "gui/menu_item/menu_item.h"
#include <array>
#include <cstdint>
#include <span>
#include <string_view>

namespace deluge::gui::ui::visual_menu {

constexpr uint8_t kSlotsPerPage = 4;

struct VisualSlot {
	MenuItem* item = nullptr;
	const char* shortName = nullptr;  // Optional custom short name (e.g., "CUT", "RES", "ATK")
	int8_t sourceIndexOverride = -1;  // -1 = use soundEditor current source, >= 0 = specific source index (0=OSC1, 1=OSC2)
	uint8_t span = 1;                 // 32px columns this slot occupies; 0 = slot skipped (covered by a previous span)

	[[nodiscard]] std::string_view getDisplayName() const {
		if (shortName != nullptr) {
			return shortName;
		}
		if (item != nullptr) {
			return item->getName();
		}
		return "--";
	}
};

struct VisualPage {
	const char* title = nullptr;
	l10n::String titleString = l10n::String::EMPTY_STRING;
	std::array<VisualSlot, kSlotsPerPage> slots{};

	[[nodiscard]] std::string_view getTitle() const {
		if (title != nullptr) {
			return title;
		}
		if (titleString != l10n::String::EMPTY_STRING) {
			return l10n::getView(titleString);
		}
		return "";
	}

	[[nodiscard]] bool hasAnyValidSlot() const {
		for (const auto& slot : slots) {
			if (slot.item != nullptr) {
				return true;
			}
		}
		return false;
	}

	[[nodiscard]] uint8_t validSlotCount() const {
		uint8_t count = 0;
		for (const auto& slot : slots) {
			if (slot.item != nullptr) {
				count++;
			}
		}
		return count;
	}
};

struct VisualCategory {
	l10n::String name = l10n::String::EMPTY_STRING;
	const char* customName = nullptr;
	std::span<const VisualPage> pages{};

	[[nodiscard]] std::string_view getName() const {
		if (customName != nullptr) {
			return customName;
		}
		if (name != l10n::String::EMPTY_STRING) {
			return l10n::getView(name);
		}
		return "";
	}
};

// Mode-specific category retrievers
std::span<const VisualCategory> getSynthCategories();
std::span<const VisualCategory> getKitCategories();
std::span<const VisualCategory> getAudioClipCategories();
std::span<const VisualCategory> getMidiCvCategories();
std::span<const VisualCategory> getGlobalFxCategories();

// Finds matching category, page, and slot index for a given MenuItem pointer
bool findMenuItemLocation(std::span<const VisualCategory> categories, const MenuItem* targetItem,
                          int32_t* outCategory, int32_t* outPage, int32_t* outSlot);

} // namespace deluge::gui::ui::visual_menu
