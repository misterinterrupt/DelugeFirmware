/*
 * Copyright (c) 2014-2023 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the
 * implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */
#pragma once
#include "gui/menu_item/decimal.h"
#include "gui/menu_item/formatted_title.h"
#include "gui/ui/sound_editor.h"
#include "model/drum/drum.h"
#include "model/instrument/kit.h"
#include "model/song/song.h"
#include "processing/sound/sound_drum.h"

namespace deluge::gui::menu_item::sample {
class OriginalBPM final : public Decimal, public FormattedTitle {
public:
	OriginalBPM(l10n::String name, l10n::String title_format_str)
	    : Decimal(name), FormattedTitle(title_format_str) {}

	[[nodiscard]] std::string_view getTitle() const override { return FormattedTitle::title(); }

	bool usesAffectEntire() override { return true; }

	// Stored as BPM*10 (e.g. 1200 = 120.0). 0 means unset.
	void readCurrentValue() override { this->setValue(soundEditor.currentSource->originalBPM); }

	void writeCurrentValue() override {
		int32_t value = this->getValue();

		if (currentUIMode == UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR && soundEditor.editingKit()) {
			Kit* kit = getCurrentKit();
			for (Drum* thisDrum = kit->firstDrum; thisDrum != nullptr; thisDrum = thisDrum->next) {
				if (thisDrum->type == DrumType::SOUND) {
					auto* soundDrum = static_cast<SoundDrum*>(thisDrum);
					Source* source = &soundDrum->sources[soundEditor.currentSourceIndex];
					source->originalBPM = value;
				}
			}
		}
		else {
			soundEditor.currentSource->originalBPM = value;
		}
	}

	[[nodiscard]] int32_t getMinValue() const override { return 0; }    // 0 = unset
	[[nodiscard]] int32_t getMaxValue() const override { return 3000; } // 300.0 BPM max
	[[nodiscard]] int32_t getNumDecimalPlaces() const override { return 1; }

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) final {
		return isSampleModeSample(modControllable, whichThing);
	}
};
} // namespace deluge::gui::menu_item::sample
