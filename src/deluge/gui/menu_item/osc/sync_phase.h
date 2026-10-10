/*
 * Copyright (c) 2014-2023 Synthstrom Audible Limited
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
#include "gui/menu_item/patched_param/integer.h"
#include "gui/ui/sound_editor.h"
#include "processing/sound/sound.h"

namespace deluge::gui::menu_item::osc {
class SyncPhase final : public menu_item::patched_param::Integer {
public:
	using Integer::Integer;
	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		Sound* sound = static_cast<Sound*>(modControllable);
		return (whichThing == 1 && sound->oscSyncMode != OscSyncMode::OFF && sound->synthMode != SynthMode::FM
		        && sound->sources[0].oscType != OscType::SAMPLE && sound->sources[1].oscType != OscType::SAMPLE);
	}
};
} // namespace deluge::gui::menu_item::osc
