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
#include "hid/buttons.h"
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
		if (activePage_ == 0) {
			return "TEMPO (BPM)";
		}
		if (activePage_ == 1) {
			return "SAMPLE LENGTH";
		}
		if (activePage_ == 2) {
			return "AUTO DETECT BPM";
		}
		if (activePage_ == 3) {
			return FormattedTitle::title(); // SPEED
		}
		return FormattedTitle::title();
	}

	bool usesAffectEntire() override { return true; }

	void readCurrentValue() override {
		if (soundEditor.currentSource) {
			this->setValue(soundEditor.currentSource->timeStretchAmount);
			origBpmHundredths_ = soundEditor.currentSource->originalBPM * 10;
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
		activePage_ = 0; // Starts on Page 0: TEMPO (BPM)
		selectedParam_ = 0;
		detectStatus_ = 0;
		float songBpm = currentSong ? currentSong->calculateBPM() : 120.0f;
		int32_t songBpmHundredths = (int32_t)(songBpm * 100.0f + 0.5f);

		if (soundEditor.currentSource) {
			origBpmHundredths_ = soundEditor.currentSource->originalBPM * 10;
			timeStretchPercentVal_ = soundEditor.currentSource->timeStretchPercent;
			if (timeStretchPercentVal_ > 0) {
				newBpmHundredths_ = (int32_t)(((float)songBpm * 100.0f * 1000.0f) / (float)timeStretchPercentVal_ + 0.5f);
			}
			else {
				newBpmHundredths_ = songBpmHundredths;
			}
		}
		else {
			newBpmHundredths_ = songBpmHundredths;
		}

		// Derive initial length from sample duration and orig BPM if present
		SampleHolder* holder = nullptr;
		AudioClip* currentClip = getCurrentAudioClip();
		if (currentClip != nullptr) {
			holder = &currentClip->sampleHolder;
		}
		else if (soundEditor.currentMultiRange != nullptr) {
			holder = &static_cast<MultisampleRange*>(soundEditor.currentMultiRange)->sampleHolder;
		}

		if (holder && holder->audioFile) {
			auto* sample = static_cast<Sample*>(holder->audioFile);
			if (sample && sample->sampleRate > 0 && sample->lengthInSamples > 0) {
				double durationSec = (double)sample->lengthInSamples / (double)sample->sampleRate;
				if (durationSec > 0.0 && origBpmHundredths_ > 0) {
					double origBPM = (double)origBpmHundredths_ / 100.0;
					double totalBeats = (durationSec * origBPM) / 60.0;
					int32_t bars = (int32_t)(totalBeats / 4.0 + 0.5);
					if (bars >= 1 && bars <= 128) {
						placeholderBars_ = bars;
						placeholderBeats_ = 1;
						placeholder16ths_ = 1;
						placeholderTicks_ = 0;
					}
				}
			}
		}
	}

	MenuItem* selectButtonPress() override {
		if (activePage_ == 0) { // TEMPO (BPM)
			selectedParam_ = (selectedParam_ + 1) % 2;
			if (display->haveOLED()) {
				renderUIsForOled();
			}
			drawValue();
			return NO_NAVIGATION;
		}
		else if (activePage_ == 1) { // SAMPLE LENGTH
			selectedParam_ = (selectedParam_ + 1) % 3;
			if (display->haveOLED()) {
				renderUIsForOled();
			}
			drawValue();
			return NO_NAVIGATION;
		}
		else if (activePage_ == 2) { // AUTO DETECT BPM
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
		else if (activePage_ == 3) { // SPEED
			return Integer::selectButtonPress();
		}
		return Integer::selectButtonPress();
	}

	void recalculateBpmFromLength() {
		SampleHolder* holder = nullptr;
		AudioClip* currentClip = getCurrentAudioClip();
		if (currentClip != nullptr) {
			holder = &currentClip->sampleHolder;
		}
		else if (soundEditor.currentMultiRange != nullptr) {
			holder = &static_cast<MultisampleRange*>(soundEditor.currentMultiRange)->sampleHolder;
		}

		if (holder && holder->audioFile) {
			auto* sample = static_cast<Sample*>(holder->audioFile);
			if (sample && sample->sampleRate > 0 && sample->lengthInSamples > 0) {
				double durationSec = (double)sample->lengthInSamples / (double)sample->sampleRate;
				if (durationSec > 0.0) {
					double totalBeats = (placeholderBars_ * 4) + (placeholderBeats_ - 1) + ((placeholder16ths_ - 1) / 4.0) + (placeholderTicks_ / 192.0);
					double calculatedBpm = (totalBeats * 60.0) / durationSec;
					if (calculatedBpm >= 1.0 && calculatedBpm <= 6000.0) {
						origBpmHundredths_ = (int32_t)(calculatedBpm * 100.0 + 0.5);
						float songBpm = currentSong ? currentSong->calculateBPM() : 120.0f;
						newBpmHundredths_ = (int32_t)(songBpm * 100.0f + 0.5f);
						if (soundEditor.currentSource) {
							soundEditor.currentSource->originalBPM = (uint16_t)((origBpmHundredths_ + 5) / 10);
							soundEditor.currentSource->timeStretchPercent = 1000; // 100% = synced to target song BPM
							if (soundEditor.currentSound) {
								soundEditor.currentSound->unassignAllVoices();
							}
						}
					}
				}
			}
		}
	}

	void selectEncoderAction(int32_t offset) override {
		bool shiftPressed = Buttons::isShiftButtonPressed();
		int32_t dir = (offset > 0) ? 1 : (offset < 0) ? -1 : 0;

		if (activePage_ == 0) { // TEMPO (BPM)
			// 0.01 BPM step when Shift is held, 1.00 BPM step without Shift
			int32_t step = shiftPressed ? dir : (dir * 100);

			if (selectedParam_ == 0) { // ORIG BPM
				int32_t bpm = origBpmHundredths_ + step;
				if (bpm < 0) bpm = 0;
				if (bpm > 30000) bpm = 30000;
				origBpmHundredths_ = bpm;
				if (soundEditor.currentSource) {
					soundEditor.currentSource->originalBPM = (uint16_t)((origBpmHundredths_ + 5) / 10);
					if (soundEditor.currentSound) {
						soundEditor.currentSound->unassignAllVoices();
					}
				}
			}
			else if (selectedParam_ == 1) { // NEW BPM
				int32_t bpm = newBpmHundredths_ + step;
				if (bpm < 3000) bpm = 3000;
				if (bpm > 30000) bpm = 30000;
				newBpmHundredths_ = bpm;
				if (soundEditor.currentSource) {
					float songBpm = currentSong ? currentSong->calculateBPM() : 120.0f;
					float targetBpm = (float)newBpmHundredths_ / 100.0f;
					int32_t pct = (int32_t)((songBpm / targetBpm) * 1000.0f + 0.5f);
					if (pct < 100) pct = 100;
					if (pct > 5000) pct = 5000;
					soundEditor.currentSource->timeStretchPercent = pct;
					if (soundEditor.currentSound) {
						soundEditor.currentSound->unassignAllVoices();
					}
				}
			}

			if (display->haveOLED()) {
				renderUIsForOled();
			}
			drawValue();
		}
		else if (activePage_ == 1) { // SAMPLE LENGTH
			if (selectedParam_ == 0) { // BARS
				placeholderBars_ += dir;
				if (placeholderBars_ < 1) placeholderBars_ = 1;
				if (placeholderBars_ > 128) placeholderBars_ = 128;
			}
			else if (selectedParam_ == 1) { // BEATS (Beat . 16th)
				if (shiftPressed) {
					placeholder16ths_ += dir;
					if (placeholder16ths_ < 1) placeholder16ths_ = 1;
					if (placeholder16ths_ > 4) placeholder16ths_ = 4;
				}
				else {
					placeholderBeats_ += dir;
					if (placeholderBeats_ < 1) placeholderBeats_ = 1;
					if (placeholderBeats_ > 4) placeholderBeats_ = 4;
				}
			}
			else if (selectedParam_ == 2) { // TICKS (0..47)
				if (shiftPressed) {
					placeholderTicks_ += dir;
					if (placeholderTicks_ < 0) placeholderTicks_ = 0;
					if (placeholderTicks_ > 47) placeholderTicks_ = 47;
				}
				else {
					placeholder16ths_ += dir;
					if (placeholder16ths_ < 1) placeholder16ths_ = 1;
					if (placeholder16ths_ > 4) placeholder16ths_ = 4;
				}
			}

			// Automatically recalculate BPM & refresh timestretch playback on every length change
			recalculateBpmFromLength();

			if (display->haveOLED()) {
				renderUIsForOled();
			}
			drawValue();
		}
		else if (activePage_ == 2) { // AUTO DETECT BPM
			if (selectedParam_ == 0) { // ACTION: [DETECT]
				runAutoDetect();
			}

			if (display->haveOLED()) {
				renderUIsForOled();
			}
			drawValue();
		}
		else if (activePage_ == 3) { // SPEED
			Integer::selectEncoderAction(offset);
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

		detectedBpmHundredths_ = (int32_t)(bestBPM * 100.0f + 0.5f);
		detectedBars_ = bestBars;
		detectStatus_ = 3; // OK

		// Automatically update Page 1 sample length parameters from detection!
		placeholderBars_ = bestBars;
		placeholderBeats_ = 1;
		placeholder16ths_ = 1;
		placeholderTicks_ = 0;

		// Apply detected original BPM
		origBpmHundredths_ = detectedBpmHundredths_;
		if (soundEditor.currentSource) {
			soundEditor.currentSource->originalBPM = (uint16_t)((origBpmHundredths_ + 5) / 10);
			if (targetBPM > 0) {
				newBpmHundredths_ = (int32_t)(targetBPM * 100.0f + 0.5f);
				soundEditor.currentSource->timeStretchPercent = 1000; // 100% = synced to target song BPM
			}
			if (soundEditor.currentSound) {
				soundEditor.currentSound->unassignAllVoices();
			}
		}

		DEF_STACK_STRING_BUF(popMsg, 30);
		popMsg.append("BPM SET: ");
		popMsg.appendFloat(bestBPM, 2, 2);
		display->displayPopup(popMsg.c_str());
	}

	void drawPixelsForOled() override {
		if (activePage_ == 0) {
			deluge::hid::display::oled_canvas::Canvas& canvas = deluge::hid::display::OLED::main;
			const int32_t topY = OLED_MAIN_TOPMOST_PIXEL + 1;

			// Page 1: TEMPO (BPM)
			// Line 1: ORIG BPM
			const int32_t l1Y = topY + 16;
			DEF_STACK_STRING_BUF(b1, 28);
			b1.append(selectedParam_ == 0 ? ">ORIG BPM: " : " ORIG BPM: ");
			if (origBpmHundredths_ == 0) {
				b1.append("---");
			}
			else {
				b1.appendFloat((float)origBpmHundredths_ / 100.0f, 2, 2);
			}
			canvas.drawString(b1.c_str(), 0, l1Y, kTextSpacingX, kTextSpacingY);

			// Line 2: NEW BPM
			const int32_t l2Y = topY + 32;
			DEF_STACK_STRING_BUF(b2, 28);
			b2.append(selectedParam_ == 1 ? ">NEW BPM:  " : " NEW BPM:  ");
			if (newBpmHundredths_ == 0) {
				b2.append("---");
			}
			else {
				b2.appendFloat((float)newBpmHundredths_ / 100.0f, 2, 2);
			}
			canvas.drawString(b2.c_str(), 0, l2Y, kTextSpacingX, kTextSpacingY);
		}
		else if (activePage_ == 1) {
			deluge::hid::display::oled_canvas::Canvas& canvas = deluge::hid::display::OLED::main;
			const int32_t topY = OLED_MAIN_TOPMOST_PIXEL + 1;

			// Page 2: SAMPLE LENGTH (BARS : BEATS : 16THS : TICKS & TIME)
			// Line 1: BARS
			const int32_t l1Y = topY + 14;
			DEF_STACK_STRING_BUF(b1, 28);
			b1.append(selectedParam_ == 0 ? ">BARS:  " : " BARS:  ");
			b1.appendInt(placeholderBars_);
			canvas.drawString(b1.c_str(), 0, l1Y, kTextSpacingX, kTextSpacingY);

			// Line 2: BEATS (Beat . 16th . Tick)
			const int32_t l2Y = topY + 28;
			DEF_STACK_STRING_BUF(b2, 28);
			b2.append(selectedParam_ == 1 ? ">BEATS: " : " BEATS: ");
			b2.appendInt(placeholderBeats_);
			b2.append(".");
			b2.appendInt(placeholder16ths_);
			b2.append(".");
			if (placeholderTicks_ < 10) b2.append("0");
			b2.appendInt(placeholderTicks_);
			canvas.drawString(b2.c_str(), 0, l2Y, kTextSpacingX, kTextSpacingY);

			// Line 3: TIME (Sec / MS)
			const int32_t l3Y = topY + 42;
			DEF_STACK_STRING_BUF(b3, 28);
			b3.append(selectedParam_ == 2 ? ">TIME:  " : " TIME:  ");

			double totalBeats = (placeholderBars_ * 4) + (placeholderBeats_ - 1) + ((placeholder16ths_ - 1) / 4.0) + (placeholderTicks_ / 192.0);
			double songBpm = currentSong ? (double)currentSong->calculateBPM() : 120.0;
			double sec = totalBeats * (60.0 / songBpm);
			int32_t ms = (int32_t)(sec * 1000.0 + 0.5);

			b3.appendFloat((float)sec, 3, 3);
			b3.append("s (");
			b3.appendInt(ms);
			b3.append("ms)");
			canvas.drawString(b3.c_str(), 0, l3Y, kTextSpacingX, kTextSpacingY);
		}
		else if (activePage_ == 2) {
			deluge::hid::display::oled_canvas::Canvas& canvas = deluge::hid::display::OLED::main;
			const int32_t topY = OLED_MAIN_TOPMOST_PIXEL + 1;

			// Page 3: AUTO DETECT BPM
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
				b2.appendFloat((float)detectedBpmHundredths_ / 100.0f, 2, 2);
				b2.append(" (");
				b2.appendInt(detectedBars_);
				b2.append("B)");
			}
			else {
				b2.append("READY");
			}
			canvas.drawString(b2.c_str(), 0, l2Y, kTextSpacingX, kTextSpacingY);
		}
		else if (activePage_ == 3) {
			Integer::drawPixelsForOled(); // SPEED
		}
	}

	void drawValue() override {
		if (activePage_ == 0) {
			if (display->have7SEG()) {
				DEF_STACK_STRING_BUF(buf, 16);
				if (selectedParam_ == 0) {
					if (origBpmHundredths_ == 0) display->setText("OBPM");
					else {
						buf.appendFloat((float)origBpmHundredths_ / 100.0f, 2, 2);
						display->setText(buf.c_str());
					}
				}
				else {
					buf.appendFloat((float)newBpmHundredths_ / 100.0f, 2, 2);
					display->setText(buf.c_str());
				}
			}
			else if (display->haveOLED()) {
				renderUIsForOled();
			}
		}
		else if (activePage_ == 1) {
			if (display->have7SEG()) {
				DEF_STACK_STRING_BUF(buf, 16);
				switch (selectedParam_) {
				case 0:
					buf.append("BAR");
					buf.appendInt(placeholderBars_);
					display->setText(buf.c_str());
					break;
				case 1:
					buf.appendInt(placeholderBars_);
					buf.append(".");
					buf.appendInt(placeholderBeats_);
					buf.append(".");
					buf.appendInt(placeholder16ths_);
					display->setText(buf.c_str());
					break;
				case 2: {
					double totalBeats = (placeholderBars_ * 4) + (placeholderBeats_ - 1) + ((placeholder16ths_ - 1) / 4.0) + (placeholderTicks_ / 192.0);
					double songBpm = currentSong ? (double)currentSong->calculateBPM() : 120.0;
					double sec = totalBeats * (60.0 / songBpm);
					buf.appendFloat((float)sec, 3, 3);
					buf.append("S");
					display->setText(buf.c_str());
					break;
				}
				}
			}
			else if (display->haveOLED()) {
				renderUIsForOled();
			}
		}
		else if (activePage_ == 2) {
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
						buf.appendFloat((float)detectedBpmHundredths_ / 100.0f, 2, 2);
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
		else if (activePage_ == 3) {
			Integer::drawValue(); // SPEED
		}
	}

	[[nodiscard]] int32_t getMinValue() const override { return -48; }
	[[nodiscard]] int32_t getMaxValue() const override { return 48; }
	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) final {
		return getCurrentAudioClip() || isSampleModeSample(modControllable, whichThing);
	}

private:
	int32_t activePage_ = 0; // 0 = TEMPO (BPM), 1 = SAMPLE LENGTH, 2 = AUTO DETECT BPM, 3 = SPEED
	int32_t selectedParam_ = 0;
	int32_t origBpmHundredths_ = 0;
	int32_t newBpmHundredths_ = 12000;
	int32_t timeStretchPercentVal_ = 1000;

	int32_t placeholderBars_ = 4;
	int32_t placeholderBeats_ = 1;
	int32_t placeholder16ths_ = 1;
	int32_t placeholderTicks_ = 0;

	int32_t detectStatus_ = 0;   // 0 = READY, 1 = NO SAMPLE, 2 = TOO LONG, 3 = OK
	int32_t detectedBpmHundredths_ = 12000;
	int32_t detectedBars_ = 4;
};
} // namespace deluge::gui::menu_item::sample
