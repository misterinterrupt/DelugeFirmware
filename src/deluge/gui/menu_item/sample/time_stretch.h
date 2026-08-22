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
#include "gui/menu_item/formatted_title.h"
#include "gui/menu_item/integer.h"
#include "gui/ui/sound_editor.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "model/clip/audio_clip.h"
#include "model/clip/clip.h"
#include "model/drum/drum.h"
#include "model/instrument/kit.h"
#include "model/sample/sample.h"
#include "model/song/song.h"
#include "processing/sound/sound_drum.h"
#include "storage/multi_range/multisample_range.h"
#include "util/d_string.h"
#include <cmath>

namespace deluge::gui::menu_item::sample {
class TimeStretch final : public Integer, public FormattedTitle {
public:
	TimeStretch(l10n::String name, l10n::String title_format_str) : Integer(name), FormattedTitle(title_format_str) {}

	[[nodiscard]] std::string_view getTitle() const override {
		if (activePage_ == 1) {
			return "TEMPO (BPM)";
		}
		if (activePage_ == 2) {
			return "SAMPLE LENGTH";
		}
		if (activePage_ == 3) {
			return "AUTO DETECT BPM";
		}
		return FormattedTitle::title();
	}

	bool usesAffectEntire() override { return true; }

	void readCurrentValue() override {
		if (soundEditor.currentSource) {
			this->setValue(soundEditor.currentSource->timeStretchAmount);
			origBpmVal_ = soundEditor.currentSource->originalBPM;
			timeStretchPercentVal_ = soundEditor.currentSource->timeStretchPercent;
		}
	}

	void writeCurrentValue() override {
		if (currentUIMode == UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR && soundEditor.editingKit()) {
			Kit* kit = getCurrentKit();
			for (Drum* thisDrum = kit->firstDrum; thisDrum != nullptr; thisDrum = thisDrum->next) {
				if (thisDrum->type == DrumType::SOUND) {
					auto* soundDrum = static_cast<SoundDrum*>(thisDrum);
					Source* source = &soundDrum->sources[soundEditor.currentSourceIndex];
					source->timeStretchAmount = this->getValue();
				}
			}
		}
		else if (soundEditor.currentSource) {
			soundEditor.currentSource->timeStretchAmount = this->getValue();
		}
	}

	void togglePage() {
		activePage_ = (activePage_ + 1) % 4;
		selectedParam_ = 0;
		if (display->haveOLED()) {
			renderUIsForOled();
		}
		drawValue();
	}

	int32_t getActivePage() const { return activePage_; }

	void beginSession(MenuItem* navigatedBackwardFrom = nullptr) override {
		Integer::beginSession(navigatedBackwardFrom);
		activePage_ = 0;
		selectedParam_ = 0;
		detectStatus_ = 0;
		if (soundEditor.currentSource) {
			origBpmVal_ = soundEditor.currentSource->originalBPM;
			timeStretchPercentVal_ = soundEditor.currentSource->timeStretchPercent;
		}
		if (currentSong) {
			newBpmVal_ = (int32_t)(currentSong->calculateBPM() * 10.0f);
		}
		else {
			newBpmVal_ = 1200;
		}
	}

	MenuItem* selectButtonPress() override {
		if (activePage_ == 1) {
			selectedParam_ = (selectedParam_ + 1) % 2;
			if (display->haveOLED()) {
				renderUIsForOled();
			}
			drawValue();
			return NO_NAVIGATION;
		}
		else if (activePage_ == 2) {
			selectedParam_ = (selectedParam_ + 1) % 3;
			if (display->haveOLED()) {
				renderUIsForOled();
			}
			drawValue();
			return NO_NAVIGATION;
		}
		else if (activePage_ == 3) {
			if (selectedParam_ == 0) { // ACTION: [DETECT]
				runAutoDetect();
			}
			else {
				selectedParam_ = (selectedParam_ + 1) % 2;
			}
			if (display->haveOLED()) {
				renderUIsForOled();
			}
			drawValue();
			return NO_NAVIGATION;
		}
		return Integer::selectButtonPress();
	}

	void selectEncoderAction(int32_t offset) override {
		if (activePage_ == 0) {
			Integer::selectEncoderAction(offset);
		}
		else if (activePage_ == 1) {
			if (selectedParam_ == 0) { // ORIG BPM
				int32_t bpm = origBpmVal_ + offset * 10;
				if (bpm < 0) bpm = 0;
				if (bpm > 3000) bpm = 3000;
				origBpmVal_ = bpm;
				if (soundEditor.currentSource) {
					soundEditor.currentSource->originalBPM = bpm;
					if (bpm > 0 && newBpmVal_ > 0) {
						int32_t pct = (bpm * 1000) / newBpmVal_;
						if (pct < 100) pct = 100;
						if (pct > 5000) pct = 5000;
						soundEditor.currentSource->timeStretchPercent = pct;
					}
				}
			}
			else if (selectedParam_ == 1) { // NEW BPM
				int32_t bpm = newBpmVal_ + offset * 10;
				if (bpm < 300) bpm = 300;
				if (bpm > 3000) bpm = 3000;
				newBpmVal_ = bpm;
				if (origBpmVal_ > 0 && soundEditor.currentSource) {
					int32_t pct = (origBpmVal_ * 1000) / newBpmVal_;
					if (pct < 100) pct = 100;
					if (pct > 5000) pct = 5000;
					soundEditor.currentSource->timeStretchPercent = pct;
				}
			}

			if (display->haveOLED()) {
				renderUIsForOled();
			}
			drawValue();
		}
		else if (activePage_ == 2) {
			if (selectedParam_ == 0) { // BARS
				placeholderBars_ += offset;
				if (placeholderBars_ < 1) placeholderBars_ = 1;
				if (placeholderBars_ > 128) placeholderBars_ = 128;
			}
			else if (selectedParam_ == 1) { // BEATS
				placeholderBeats_ += offset;
				if (placeholderBeats_ < 0) placeholderBeats_ = 0;
				if (placeholderBeats_ > 31) placeholderBeats_ = 31;
			}
			else if (selectedParam_ == 2) { // MEAS
				placeholderMeasures_ += offset;
				if (placeholderMeasures_ < 0) placeholderMeasures_ = 0;
				if (placeholderMeasures_ > 128) placeholderMeasures_ = 128;
			}

			if (display->haveOLED()) {
				renderUIsForOled();
			}
			drawValue();
		}
		else if (activePage_ == 3) {
			if (selectedParam_ == 0) { // ACTION: [DETECT]
				runAutoDetect();
			}

			if (display->haveOLED()) {
				renderUIsForOled();
			}
			drawValue();
		}
	}

	void runAutoDetect() {
		SampleHolder* holder = nullptr;
		AudioClip* currentClip = getCurrentAudioClip();
		if (currentClip != nullptr) {
			holder = &currentClip->sampleHolder;
		}
		else if (soundEditor.currentMultiRange != nullptr) {
			holder = &static_cast<MultisampleRange*>(soundEditor.currentMultiRange)->sampleHolder;
		}

		if (!holder || !holder->audioFile) {
			detectStatus_ = 1; // NO SAMPLE
			display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_NO_SAMPLE));
			return;
		}

		auto* sample = static_cast<Sample*>(holder->audioFile);
		if (!sample || sample->sampleRate == 0 || sample->lengthInSamples == 0) {
			detectStatus_ = 1; // NO SAMPLE
			display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_NO_SAMPLE));
			return;
		}

		double durationSec = (double)sample->lengthInSamples / (double)sample->sampleRate;
		constexpr double maxLimitSec = 30.0;

		if (durationSec > maxLimitSec) {
			detectStatus_ = 2; // TOO LONG (>30s)
			display->displayPopup("TOO LONG (>30s)");
			return;
		}

		// Estimate BPM and Bar Count
		static const int32_t barOptions[] = {1, 2, 4, 8, 16, 32};
		int32_t bestBars = 4;
		float bestBPM = 120.0f;
		float bestDiff = 99999.0f;

		float targetBPM = 120.0f;
		if (currentSong) {
			targetBPM = currentSong->calculateBPM();
		}

		for (int32_t b : barOptions) {
			float bpm = (float)((b * 4 * 60) / durationSec);
			float diff = std::abs(bpm - targetBPM);
			if (bpm >= 60.0f && bpm <= 200.0f && diff < bestDiff) {
				bestDiff = diff;
				bestBars = b;
				bestBPM = bpm;
			}
		}

		detectedBpmVal_ = (int32_t)(bestBPM * 10.0f);
		detectedBars_ = bestBars;
		detectStatus_ = 3; // OK

		// Apply detected original BPM
		origBpmVal_ = detectedBpmVal_;
		if (soundEditor.currentSource) {
			soundEditor.currentSource->originalBPM = detectedBpmVal_;
			if (targetBPM > 0) {
				int32_t targetBpmVal = (int32_t)(targetBPM * 10.0f);
				int32_t pct = (detectedBpmVal_ * 1000) / targetBpmVal;
				if (pct < 100) pct = 100;
				if (pct > 5000) pct = 5000;
				soundEditor.currentSource->timeStretchPercent = pct;
			}
		}

		DEF_STACK_STRING_BUF(popMsg, 30);
		popMsg.append("BPM SET: ");
		popMsg.appendFloat(bestBPM, 1, 1);
		display->displayPopup(popMsg.c_str());
	}

	void drawPixelsForOled() override {
		if (activePage_ == 0) {
			Integer::drawPixelsForOled();
		}
		else if (activePage_ == 1) {
			deluge::hid::display::oled_canvas::Canvas& canvas = deluge::hid::display::OLED::main;
			const int32_t topY = OLED_MAIN_TOPMOST_PIXEL + 1;

			// Page 2: TEMPO (BPM)
			// Line 1: ORIG BPM
			const int32_t l1Y = topY + 16;
			DEF_STACK_STRING_BUF(b1, 28);
			b1.append(selectedParam_ == 0 ? ">ORIG BPM: " : " ORIG BPM: ");
			if (origBpmVal_ == 0) {
				b1.append("---");
			}
			else {
				b1.appendFloat((float)origBpmVal_ / 10.0f, 1, 1);
			}
			canvas.drawString(b1.c_str(), 0, l1Y, kTextSpacingX, kTextSpacingY);

			// Line 2: NEW BPM
			const int32_t l2Y = topY + 32;
			DEF_STACK_STRING_BUF(b2, 28);
			b2.append(selectedParam_ == 1 ? ">NEW BPM:  " : " NEW BPM:  ");
			if (newBpmVal_ == 0) {
				b2.append("---");
			}
			else {
				b2.appendFloat((float)newBpmVal_ / 10.0f, 1, 1);
			}
			canvas.drawString(b2.c_str(), 0, l2Y, kTextSpacingX, kTextSpacingY);
		}
		else if (activePage_ == 2) {
			deluge::hid::display::oled_canvas::Canvas& canvas = deluge::hid::display::OLED::main;
			const int32_t topY = OLED_MAIN_TOPMOST_PIXEL + 1;

			// Page 3: SAMPLE LENGTH
			// Line 1: BARS
			const int32_t l1Y = topY + 14;
			DEF_STACK_STRING_BUF(b1, 28);
			b1.append(selectedParam_ == 0 ? ">BARS:  " : " BARS:  ");
			b1.appendInt(placeholderBars_);
			canvas.drawString(b1.c_str(), 0, l1Y, kTextSpacingX, kTextSpacingY);

			// Line 2: BEATS
			const int32_t l2Y = topY + 28;
			DEF_STACK_STRING_BUF(b2, 28);
			b2.append(selectedParam_ == 1 ? ">BEATS: " : " BEATS: ");
			b2.appendInt(placeholderBeats_);
			canvas.drawString(b2.c_str(), 0, l2Y, kTextSpacingX, kTextSpacingY);

			// Line 3: MEAS
			const int32_t l3Y = topY + 42;
			DEF_STACK_STRING_BUF(b3, 28);
			b3.append(selectedParam_ == 2 ? ">MEAS:  " : " MEAS:  ");
			b3.appendInt(placeholderMeasures_);
			canvas.drawString(b3.c_str(), 0, l3Y, kTextSpacingX, kTextSpacingY);
		}
		else if (activePage_ == 3) {
			deluge::hid::display::oled_canvas::Canvas& canvas = deluge::hid::display::OLED::main;
			const int32_t topY = OLED_MAIN_TOPMOST_PIXEL + 1;

			// Page 4: AUTO DETECT BPM
			// Line 1: ACTION
			const int32_t l1Y = topY + 18;
			DEF_STACK_STRING_BUF(b1, 28);
			b1.append(selectedParam_ == 0 ? ">ACTION: [DETECT]" : " ACTION: [DETECT]");
			canvas.drawString(b1.c_str(), 0, l1Y, kTextSpacingX, kTextSpacingY);

			// Line 2: RESULT
			const int32_t l2Y = topY + 36;
			DEF_STACK_STRING_BUF(b2, 28);
			b2.append(selectedParam_ == 1 ? ">RESULT: " : " RESULT: ");
			if (detectStatus_ == 1) {
				b2.append("NO SAMPLE");
			}
			else if (detectStatus_ == 2) {
				b2.append("TOO LONG");
			}
			else if (detectStatus_ == 3) {
				b2.appendFloat((float)detectedBpmVal_ / 10.0f, 1, 1);
				b2.append(" (");
				b2.appendInt(detectedBars_);
				b2.append("B)");
			}
			else {
				b2.append("READY");
			}
			canvas.drawString(b2.c_str(), 0, l2Y, kTextSpacingX, kTextSpacingY);
		}
	}

	void drawValue() override {
		if (activePage_ == 0) {
			Integer::drawValue();
		}
		else if (activePage_ == 1) {
			if (display->have7SEG()) {
				DEF_STACK_STRING_BUF(buf, 16);
				if (selectedParam_ == 0) {
					if (origBpmVal_ == 0) display->setText("OBPM");
					else {
						buf.appendFloat((float)origBpmVal_ / 10.0f, 1, 1);
						display->setText(buf.c_str());
					}
				}
				else {
					buf.appendFloat((float)newBpmVal_ / 10.0f, 1, 1);
					display->setText(buf.c_str());
				}
			}
			else if (display->haveOLED()) {
				renderUIsForOled();
			}
		}
		else if (activePage_ == 2) {
			if (display->have7SEG()) {
				DEF_STACK_STRING_BUF(buf, 16);
				switch (selectedParam_) {
				case 0:
					buf.append("BAR ");
					buf.appendInt(placeholderBars_);
					display->setText(buf.c_str());
					break;
				case 1:
					buf.append("BEAT ");
					buf.appendInt(placeholderBeats_);
					display->setText(buf.c_str());
					break;
				case 2:
					buf.append("MEAS ");
					buf.appendInt(placeholderMeasures_);
					display->setText(buf.c_str());
					break;
				}
			}
			else if (display->haveOLED()) {
				renderUIsForOled();
			}
		}
		else if (activePage_ == 3) {
			if (display->have7SEG()) {
				DEF_STACK_STRING_BUF(buf, 16);
				if (selectedParam_ == 0) {
					display->setText("DETC");
				}
				else {
					if (detectStatus_ == 2) {
						display->setText("TLNG");
					}
					else if (detectStatus_ == 3) {
						buf.appendFloat((float)detectedBpmVal_ / 10.0f, 1, 1);
						display->setText(buf.c_str());
					}
					else {
						display->setText("READY");
					}
				}
			}
			else if (display->haveOLED()) {
				renderUIsForOled();
			}
		}
	}

	[[nodiscard]] int32_t getMinValue() const override { return -48; }
	[[nodiscard]] int32_t getMaxValue() const override { return 48; }
	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) final {
		return getCurrentAudioClip() || isSampleModeSample(modControllable, whichThing);
	}

private:
	int32_t activePage_ = 0; // 0 = SPEED, 1 = TEMPO (BPM), 2 = SAMPLE LENGTH, 3 = AUTO DETECT BPM
	int32_t selectedParam_ = 0;
	int32_t origBpmVal_ = 0;
	int32_t newBpmVal_ = 1200;
	int32_t timeStretchPercentVal_ = 1000;
	int32_t placeholderBars_ = 4;
	int32_t placeholderBeats_ = 0;
	int32_t placeholderMeasures_ = 0;

	int32_t detectStatus_ = 0;   // 0 = READY, 1 = NO SAMPLE, 2 = TOO LONG, 3 = OK
	int32_t detectedBpmVal_ = 1200;
	int32_t detectedBars_ = 4;
};
} // namespace deluge::gui::menu_item::sample
