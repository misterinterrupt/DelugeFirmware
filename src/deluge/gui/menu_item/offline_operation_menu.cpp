#include "gui/menu_item/offline_operation_menu.h"
#include "gui/ui/sound_editor.h"
#include "gui/ui/ui.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "model/sample/sample.h"
#include "processing/offline/gain_job.h"
#include "processing/offline/gainer_job.h"
#include "processing/offline/leveler_job.h"
#include "processing/offline/normalize_job.h"
#include "processing/offline/normalizer_job.h"
#include "processing/offline/reverse_job.h"
#include "processing/offline/reverser_job.h"
#include "processing/sound/sound.h"
#include "storage/multi_range/multisample_range.h"

namespace deluge::gui::menu_item {

bool gOfflineJobOverwrite = false;

static SaveModeMenuItem saveModeMenuItem;
static NormalizeActionMenuItem normalizeActionMenuItem;
static ReverseActionMenuItem reverseActionMenuItem;
static GainActionMenuItem gainActionMenuItem;
static LevelerActionMenuItem levelerActionMenuItem;

SaveModeMenuItem::SaveModeMenuItem()
    : ToggleBool(deluge::l10n::String::EMPTY_STRING, deluge::l10n::String::EMPTY_STRING, gOfflineJobOverwrite) {
}

static void drawSafeHorizontalLine(deluge::hid::display::oled_canvas::Canvas& canvas, int32_t y, int32_t startX, int32_t endX) {
	if (y >= 0 && y < OLED_MAIN_HEIGHT_PIXELS) {
		canvas.drawHorizontalLine(y, startX, endX);
	}
}

static void drawSafeVerticalLine(deluge::hid::display::oled_canvas::Canvas& canvas, int32_t x, int32_t startY, int32_t endY) {
	if (x < 0 || x >= OLED_MAIN_WIDTH_PIXELS) return;
	if (startY < 0) startY = 0;
	if (startY >= OLED_MAIN_HEIGHT_PIXELS) startY = OLED_MAIN_HEIGHT_PIXELS - 1;
	if (endY < 0) endY = 0;
	if (endY >= OLED_MAIN_HEIGHT_PIXELS) endY = OLED_MAIN_HEIGHT_PIXELS - 1;
	if (startY > endY) std::swap(startY, endY);
	canvas.drawVerticalLine(x, startY, endY);
}

static Sample* getCurrentSample() {
	if (soundEditor.currentMultiRange == nullptr && soundEditor.currentSound != nullptr) {
		Source* source = &soundEditor.currentSound->sources[soundEditor.currentSourceIndex];
		if (source != nullptr) {
			soundEditor.currentMultiRange = source->getOrCreateFirstRange();
		}
	}
	AudioFileHolder* holder = soundEditor.getCurrentAudioFileHolder();
	if (holder && holder->audioFile && holder->audioFileType == AudioFileType::SAMPLE) {
		return (Sample*)holder->audioFile;
	}
	return nullptr;
}

NormalizeActionMenuItem::NormalizeActionMenuItem() : MenuItem(deluge::l10n::String::EMPTY_STRING) {
	targetCeilingVal_ = 0;
}

void NormalizeActionMenuItem::beginSession(MenuItem* navigatedBackwardFrom) {
	MenuItem::beginSession(navigatedBackwardFrom);
	Sample* currentSample = getCurrentSample();
	if (currentSample) {
		if (normalizerJob().isWaitingForCeiling() || !normalizerJob().isActive()) {
			normalizerJob().reset();
			normalizerJob().setTargetCeilingDb((float)targetCeilingVal_ / 100.0f);
			normalizerJob().start(currentSample, gOfflineJobOverwrite);
		}
	}
}

void NormalizeActionMenuItem::readValueAgain() {
	display->setText("NORMALIZE");
}

void NormalizeActionMenuItem::selectEncoderAction(int32_t offset) {
	if (normalizerJob().isWaitingForCeiling()) {
		targetCeilingVal_ += offset * 50;
		if (targetCeilingVal_ > 600) {
			targetCeilingVal_ = 600;
		}
		else if (targetCeilingVal_ < -2400) {
			targetCeilingVal_ = -2400;
		}
		float ceilingDb = (float)targetCeilingVal_ / 100.0f;
		normalizerJob().setTargetCeilingDb(ceilingDb);

		DEF_STACK_STRING_BUF(buffer, 16);
		if (ceilingDb >= 0.0f) {
			buffer.append("+");
		}
		buffer.appendFloat(ceilingDb, 1, 1);
		buffer.append("DB");
		display->setText(buffer.c_str());

		if (display->haveOLED()) {
			renderUIsForOled();
		}
	}
}

void NormalizeActionMenuItem::drawPixelsForOled() {
	deluge::hid::display::oled_canvas::Canvas& canvas = deluge::hid::display::OLED::main;
	const int32_t topY = OLED_MAIN_TOPMOST_PIXEL + 1;
	const int32_t yBase = OLED_MAIN_TOPMOST_PIXEL + 22;

	if (normalizerJob().isWaitingForCeiling()) {
		canvas.drawString("NORM CEIL", 0, topY, kTextSpacingX, kTextSpacingY);

		float db = (float)targetCeilingVal_ / 100.0f;
		DEF_STACK_STRING_BUF(buffer, 16);
		if (db >= 0.0f) {
			buffer.append("+");
		}
		buffer.appendFloat(db, 1, 1);
		buffer.append(" dB");
		canvas.drawStringAlignRight(buffer.c_str(), topY, kTextSpacingX, kTextSpacingY);

		uint8_t const* overview = normalizerJob().getWaveformOverview();
		for (int32_t x = 0; x < 128; x++) {
			uint8_t h = overview[x];
			int32_t halfH = h / 2;
			if (halfH < 1) {
				halfH = 1;
			}
			drawSafeVerticalLine(canvas, x, yBase - halfH, yBase + halfH);
		}

		float ratio = powf(10.0f, db / 20.0f);
		int32_t threshOffset = (int32_t)(ratio * 14.0f);
		if (threshOffset < 1) {
			threshOffset = 1;
		}

		int32_t yThreshTop = yBase - threshOffset;
		int32_t yThreshBot = yBase + threshOffset;

		drawSafeHorizontalLine(canvas, yThreshTop, 0, 127);
		drawSafeHorizontalLine(canvas, yThreshBot, 0, 127);
	}
	else if (normalizerJob().isActive()) {
		canvas.drawString("SCANNING...", 0, topY, kTextSpacingX, kTextSpacingY);
	}
	else {
		canvas.drawString("NORMALIZE", 0, topY, kTextSpacingX, kTextSpacingY);
	}
}


MenuItem* NormalizeActionMenuItem::selectButtonPress() {
	Sample* currentSample = getCurrentSample();

	if (!currentSample) {
		display->displayPopup("NO SAMPLE");
		return nullptr;
	}

	if (normalizerJob().isWaitingForCeiling()) {
		float ceilingDb = (float)targetCeilingVal_ / 100.0f;
		normalizerJob().setTargetCeilingDb(ceilingDb);
		normalizerJob().confirmCeilingAndRun();
		return nullptr;
	}
	else if (!normalizerJob().isActive()) {
		normalizerJob().setTargetCeilingDb((float)targetCeilingVal_ / 100.0f);
		if (normalizerJob().start(currentSample, gOfflineJobOverwrite)) {
			return NO_NAVIGATION;
		}
	}
	return nullptr;
}

GainActionMenuItem::GainActionMenuItem() : MenuItem(deluge::l10n::String::EMPTY_STRING) {
	gainVal_ = 0;
	activePage_ = 0;
	distType_ = 0;
	distSelectedParam_ = 0;
}

MenuItem* getGainActionMenuItem() {
	return &gainActionMenuItem;
}

void GainActionMenuItem::updateDistortionConfig() {
	DistortionConfig cfg;
	cfg.type = static_cast<DistortionType>(distType_);
	cfg.outputGainDb = (float)distOutGainVal_ / 100.0f;
	if (distType_ == 1) {
		cfg.param1 = (float)satDrive_ / 10.0f;
		cfg.param2 = (float)satWarmth_ / 100.0f;
	}
	else if (distType_ == 2) {
		cfg.param1 = (float)foldFolds_;
		cfg.param2 = (float)foldBias_ / 100.0f;
	}
	else if (distType_ == 3) {
		cfg.param1 = (float)crushBits_;
		cfg.param2 = (float)crushDownsample_;
	}
	else if (distType_ == 4) {
		cfg.param1 = (float)rectMode_;
		cfg.param2 = (float)rectCrossover_ / 100.0f;
	}
	else if (distType_ == 5) {
		cfg.param1 = (float)fmDepth_ / 10.0f;
		cfg.param2 = (float)fmFeedback_ / 100.0f;
	}
	else if (distType_ == 6) {
		cfg.param1 = (float)ringFreq_;
		cfg.param2 = (float)ringMix_ / 100.0f;
	}
	gainerJob().setDistortionConfig(cfg);
}

void GainActionMenuItem::beginSession(MenuItem* navigatedBackwardFrom) {
	MenuItem::beginSession(navigatedBackwardFrom);
	activePage_ = 0;
	distSelectedParam_ = 0;
	updateDistortionConfig();
	Sample* currentSample = getCurrentSample();
	if (currentSample) {
		if (gainerJob().isWaitingForGain() || !gainerJob().isActive()) {
			gainerJob().reset();
			gainerJob().setGainDb((float)gainVal_ / 100.0f);
			gainerJob().start(currentSample, gOfflineJobOverwrite);
		}
	}
}

void GainActionMenuItem::readValueAgain() {
	display->setText("GAIN");
}

ActionResult GainActionMenuItem::buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) {
	if (on) {
		if (b == deluge::hid::button::KIT) {
			activePage_ = 1;
			distSelectedParam_ = 0;
			if (display->haveOLED()) {
				renderUIsForOled();
			}
			display->setText("DISTORT");
			return ActionResult::DEALT_WITH;
		}
		else if (b == deluge::hid::button::SYNTH) {
			activePage_ = 0;
			if (display->haveOLED()) {
				renderUIsForOled();
			}
			float gainDb = (float)gainVal_ / 100.0f;
			DEF_STACK_STRING_BUF(buffer, 16);
			if (gainDb >= 0.0f) {
				buffer.append("+");
			}
			buffer.appendFloat(gainDb, 1, 1);
			buffer.append("DB");
			display->setText(buffer.c_str());
			return ActionResult::DEALT_WITH;
		}
	}
	return ActionResult::NOT_DEALT_WITH;
}

void GainActionMenuItem::selectEncoderAction(int32_t offset) {
	if (!gainerJob().isWaitingForGain()) {
		return;
	}

	if (activePage_ == 0) {
		gainVal_ += offset * 50;
		if (gainVal_ > 4800) {
			gainVal_ = 4800;
		}
		else if (gainVal_ < -4800) {
			gainVal_ = -4800;
		}
		float gainDb = (float)gainVal_ / 100.0f;
		gainerJob().setGainDb(gainDb);

		DEF_STACK_STRING_BUF(buffer, 16);
		if (gainDb >= 0.0f) {
			buffer.append("+");
		}
		buffer.appendFloat(gainDb, 1, 1);
		buffer.append("DB");
		display->setText(buffer.c_str());

		if (display->haveOLED()) {
			renderUIsForOled();
		}
	}
	else {
		if (distSelectedParam_ == 0) {
			int32_t newType = (int32_t)distType_ + offset;
			if (newType < 0) newType = 0;
			if (newType > 6) newType = 6;
			distType_ = (uint8_t)newType;
		}
		else if (distSelectedParam_ == 3) {
			distOutGainVal_ += offset * 50;
			if (distOutGainVal_ > 4800) distOutGainVal_ = 4800;
			if (distOutGainVal_ < -4800) distOutGainVal_ = -4800;

			DEF_STACK_STRING_BUF(buffer, 16);
			float db = (float)distOutGainVal_ / 100.0f;
			if (db >= 0.0f) buffer.append("+");
			buffer.appendFloat(db, 1, 1);
			buffer.append("DB");
			display->setText(buffer.c_str());
		}
		else if (distType_ == 1) {
			if (distSelectedParam_ == 1) {
				satDrive_ += offset * 5;
				if (satDrive_ < 10) satDrive_ = 10;
				if (satDrive_ > 100) satDrive_ = 100;
			}
			else if (distSelectedParam_ == 2) {
				satWarmth_ += offset * 5;
				if (satWarmth_ < 0) satWarmth_ = 0;
				if (satWarmth_ > 100) satWarmth_ = 100;
			}
		}
		else if (distType_ == 2) {
			if (distSelectedParam_ == 1) {
				foldFolds_ += offset;
				if (foldFolds_ < 1) foldFolds_ = 1;
				if (foldFolds_ > 8) foldFolds_ = 8;
			}
			else if (distSelectedParam_ == 2) {
				foldBias_ += offset * 5;
				if (foldBias_ < -50) foldBias_ = -50;
				if (foldBias_ > 50) foldBias_ = 50;
			}
		}
		else if (distType_ == 3) {
			if (distSelectedParam_ == 1) {
				crushBits_ += offset;
				if (crushBits_ < 1) crushBits_ = 1;
				if (crushBits_ > 16) crushBits_ = 16;
			}
			else if (distSelectedParam_ == 2) {
				crushDownsample_ += offset;
				if (crushDownsample_ < 1) crushDownsample_ = 1;
				if (crushDownsample_ > 32) crushDownsample_ = 32;
			}
		}
		else if (distType_ == 4) {
			if (distSelectedParam_ == 1) {
				rectMode_ = (rectMode_ == 0) ? 1 : 0;
			}
			else if (distSelectedParam_ == 2) {
				rectCrossover_ += offset * 5;
				if (rectCrossover_ < 0) rectCrossover_ = 0;
				if (rectCrossover_ > 100) rectCrossover_ = 100;
			}
		}
		else if (distType_ == 5) {
			if (distSelectedParam_ == 1) {
				fmDepth_ += offset * 5;
				if (fmDepth_ < 10) fmDepth_ = 10;
				if (fmDepth_ > 100) fmDepth_ = 100;
			}
			else if (distSelectedParam_ == 2) {
				fmFeedback_ += offset * 5;
				if (fmFeedback_ < 0) fmFeedback_ = 0;
				if (fmFeedback_ > 90) fmFeedback_ = 90;
			}
		}
		else if (distType_ == 6) {
			if (distSelectedParam_ == 1) {
				ringFreq_ += offset * 20;
				if (ringFreq_ < 20) ringFreq_ = 20;
				if (ringFreq_ > 2000) ringFreq_ = 2000;
			}
			else if (distSelectedParam_ == 2) {
				ringMix_ += offset * 5;
				if (ringMix_ < 10) ringMix_ = 10;
				if (ringMix_ > 100) ringMix_ = 100;
			}
		}

		updateDistortionConfig();

		if (display->haveOLED()) {
			renderUIsForOled();
		}
	}
}

void GainActionMenuItem::drawPixelsForOled() {
	deluge::hid::display::oled_canvas::Canvas& canvas = deluge::hid::display::OLED::main;
	const int32_t topY = OLED_MAIN_TOPMOST_PIXEL + 1;
	const int32_t yBase = OLED_MAIN_TOPMOST_PIXEL + 22;

	if (activePage_ == 0) {
		if (gainerJob().isWaitingForGain()) {
			canvas.drawString("GAIN ADJ", 0, topY, kTextSpacingX, kTextSpacingY);

			float db = (float)gainVal_ / 100.0f;
			DEF_STACK_STRING_BUF(buffer, 16);
			if (db >= 0.0f) {
				buffer.append("+");
			}
			buffer.appendFloat(db, 1, 1);
			buffer.append(" dB");
			canvas.drawStringAlignRight(buffer.c_str(), topY, kTextSpacingX, kTextSpacingY);

			uint8_t const* overview = gainerJob().getWaveformOverview();
			for (int32_t x = 0; x < 128; x++) {
				uint8_t h = overview[x];
				int32_t halfH = h / 2;
				if (halfH < 1) {
					halfH = 1;
				}
				drawSafeVerticalLine(canvas, x, yBase - halfH, yBase + halfH);
			}

			float ratio = powf(10.0f, db / 20.0f);
			int32_t threshOffset = (int32_t)(ratio * 14.0f);
			if (threshOffset < 1) {
				threshOffset = 1;
			}

			int32_t yThreshTop = yBase - threshOffset;
			int32_t yThreshBot = yBase + threshOffset;

			drawSafeHorizontalLine(canvas, yThreshTop, 0, 127);
			drawSafeHorizontalLine(canvas, yThreshBot, 0, 127);
		}
		else if (gainerJob().isActive()) {
			canvas.drawString("SCANNING...", 0, topY, kTextSpacingX, kTextSpacingY);
		}
		else {
			canvas.drawString("GAIN", 0, topY, kTextSpacingX, kTextSpacingY);
		}
	}
	else {
		canvas.drawString("DISTORT", 0, topY, kTextSpacingX, kTextSpacingY);
		canvas.drawStringAlignRight("[KIT]", topY, kTextSpacingX, kTextSpacingY);

		const int32_t line1Y = topY + 9;
		DEF_STACK_STRING_BUF(tBuf, 24);
		if (distSelectedParam_ == 0) {
			tBuf.append(">TYPE: ");
		}
		else {
			tBuf.append(" TYPE: ");
		}

		switch (distType_) {
		case 0: tBuf.append("OFF"); break;
		case 1: tBuf.append("SATURATE"); break;
		case 2: tBuf.append("WAVEFOLD"); break;
		case 3: tBuf.append("BITCRUSH"); break;
		case 4: tBuf.append("RECTIFY"); break;
		case 5: tBuf.append("FM FEEDBK"); break;
		case 6: tBuf.append("RING MOD"); break;
		}
		canvas.drawString(tBuf.c_str(), 0, line1Y, kTextSpacingX, kTextSpacingY);

		const int32_t line2Y = topY + 18;
		const int32_t line3Y = topY + 27;

		if (distType_ == 1) {
			DEF_STACK_STRING_BUF(p1Buf, 24);
			p1Buf.append(distSelectedParam_ == 1 ? ">DRIVE: " : " DRIVE: ");
			p1Buf.appendFloat((float)satDrive_ / 10.0f, 1, 1);
			p1Buf.append("x");
			canvas.drawString(p1Buf.c_str(), 0, line2Y, kTextSpacingX, kTextSpacingY);

			DEF_STACK_STRING_BUF(p2Buf, 24);
			p2Buf.append(distSelectedParam_ == 2 ? ">WARMTH: " : " WARMTH: ");
			p2Buf.appendInt(satWarmth_);
			p2Buf.append("%");
			canvas.drawString(p2Buf.c_str(), 0, line3Y, kTextSpacingX, kTextSpacingY);
		}
		else if (distType_ == 2) {
			DEF_STACK_STRING_BUF(p1Buf, 24);
			p1Buf.append(distSelectedParam_ == 1 ? ">FOLDS: " : " FOLDS: ");
			p1Buf.appendInt(foldFolds_);
			canvas.drawString(p1Buf.c_str(), 0, line2Y, kTextSpacingX, kTextSpacingY);

			DEF_STACK_STRING_BUF(p2Buf, 24);
			p2Buf.append(distSelectedParam_ == 2 ? ">BIAS: " : " BIAS: ");
			if (foldBias_ >= 0) p2Buf.append("+");
			p2Buf.appendInt(foldBias_);
			p2Buf.append("%");
			canvas.drawString(p2Buf.c_str(), 0, line3Y, kTextSpacingX, kTextSpacingY);
		}
		else if (distType_ == 3) {
			DEF_STACK_STRING_BUF(p1Buf, 24);
			p1Buf.append(distSelectedParam_ == 1 ? ">BITS: " : " BITS: ");
			p1Buf.appendInt(crushBits_);
			p1Buf.append(" bit");
			canvas.drawString(p1Buf.c_str(), 0, line2Y, kTextSpacingX, kTextSpacingY);

			DEF_STACK_STRING_BUF(p2Buf, 24);
			p2Buf.append(distSelectedParam_ == 2 ? ">DECIMAT: " : " DECIMAT: ");
			p2Buf.appendInt(crushDownsample_);
			p2Buf.append("x");
			canvas.drawString(p2Buf.c_str(), 0, line3Y, kTextSpacingX, kTextSpacingY);
		}
		else if (distType_ == 4) {
			DEF_STACK_STRING_BUF(p1Buf, 24);
			p1Buf.append(distSelectedParam_ == 1 ? ">MODE: " : " MODE: ");
			p1Buf.append(rectMode_ == 0 ? "HALF" : "FULL");
			canvas.drawString(p1Buf.c_str(), 0, line2Y, kTextSpacingX, kTextSpacingY);

			DEF_STACK_STRING_BUF(p2Buf, 24);
			p2Buf.append(distSelectedParam_ == 2 ? ">CROSS: " : " CROSS: ");
			p2Buf.appendInt(rectCrossover_);
			p2Buf.append("%");
			canvas.drawString(p2Buf.c_str(), 0, line3Y, kTextSpacingX, kTextSpacingY);
		}
		else if (distType_ == 5) {
			DEF_STACK_STRING_BUF(p1Buf, 24);
			p1Buf.append(distSelectedParam_ == 1 ? ">DEPTH: " : " DEPTH: ");
			p1Buf.appendFloat((float)fmDepth_ / 10.0f, 1, 1);
			p1Buf.append("x");
			canvas.drawString(p1Buf.c_str(), 0, line2Y, kTextSpacingX, kTextSpacingY);

			DEF_STACK_STRING_BUF(p2Buf, 24);
			p2Buf.append(distSelectedParam_ == 2 ? ">FEEDBK: " : " FEEDBK: ");
			p2Buf.appendInt(fmFeedback_);
			p2Buf.append("%");
			canvas.drawString(p2Buf.c_str(), 0, line3Y, kTextSpacingX, kTextSpacingY);
		}
		else if (distType_ == 6) {
			DEF_STACK_STRING_BUF(p1Buf, 24);
			p1Buf.append(distSelectedParam_ == 1 ? ">FREQ: " : " FREQ: ");
			p1Buf.appendInt(ringFreq_);
			p1Buf.append(" Hz");
			canvas.drawString(p1Buf.c_str(), 0, line2Y, kTextSpacingX, kTextSpacingY);

			DEF_STACK_STRING_BUF(p2Buf, 24);
			p2Buf.append(distSelectedParam_ == 2 ? ">PEAKMIX: " : " PEAKMIX: ");
			p2Buf.appendInt(ringMix_);
			p2Buf.append("%");
			canvas.drawString(p2Buf.c_str(), 0, line3Y, kTextSpacingX, kTextSpacingY);
		}

		if (distType_ > 0) {
			const int32_t line4Y = topY + 36;
			DEF_STACK_STRING_BUF(ogBuf, 24);
			ogBuf.append(distSelectedParam_ == 3 ? ">OUTGAIN: " : " OUTGAIN: ");
			float ogDb = (float)distOutGainVal_ / 100.0f;
			if (ogDb >= 0.0f) ogBuf.append("+");
			ogBuf.appendFloat(ogDb, 1, 1);
			ogBuf.append("dB");
			canvas.drawString(ogBuf.c_str(), 0, line4Y, kTextSpacingX, kTextSpacingY);
		}
	}
}

MenuItem* GainActionMenuItem::selectButtonPress() {
	Sample* currentSample = getCurrentSample();

	if (!currentSample) {
		display->displayPopup("NO SAMPLE");
		return nullptr;
	}

	if (activePage_ == 1) {
		if (distType_ == 0) {
			distSelectedParam_ = 0;
		}
		else {
			distSelectedParam_ = (distSelectedParam_ + 1) % 4;
		}

		if (display->haveOLED()) {
			renderUIsForOled();
		}
		return NO_NAVIGATION;
	}

	if (gainerJob().isWaitingForGain()) {
		float gainDb = (float)gainVal_ / 100.0f;
		gainerJob().setGainDb(gainDb);

		updateDistortionConfig();
		gainerJob().confirmGainAndRun();
		return nullptr;
	}
	else if (!gainerJob().isActive()) {
		gainerJob().setGainDb((float)gainVal_ / 100.0f);
		updateDistortionConfig();
		if (gainerJob().start(currentSample, gOfflineJobOverwrite)) {
			return NO_NAVIGATION;
		}
	}
	return nullptr;
}

ReverseActionMenuItem::ReverseActionMenuItem() : MenuItem(deluge::l10n::String::EMPTY_STRING) {
	modeVal_ = 0;
}

void ReverseActionMenuItem::beginSession(MenuItem* navigatedBackwardFrom) {
	MenuItem::beginSession(navigatedBackwardFrom);
	Sample* currentSample = getCurrentSample();
	if (currentSample) {
		if (reverserJob().isWaitingForMode() || !reverserJob().isActive()) {
			reverserJob().reset();
			reverserJob().setModeParam(modeVal_);
			reverserJob().start(currentSample, gOfflineJobOverwrite);
		}
	}
}

void ReverseActionMenuItem::readValueAgain() {
	display->setText("REVERSE");
}

void ReverseActionMenuItem::selectEncoderAction(int32_t offset) {
	if (reverserJob().isWaitingForMode()) {
		modeVal_ += offset;
		if (modeVal_ > 100) {
			modeVal_ = 100;
		}
		else if (modeVal_ < -1) {
			modeVal_ = -1;
		}
		reverserJob().setModeParam(modeVal_);

		DEF_STACK_STRING_BUF(buffer, 16);
		if (modeVal_ == -1) {
			buffer.append("DETECT");
		}
		else if (modeVal_ == 0) {
			buffer.append("FULL");
		}
		else {
			buffer.appendInt(modeVal_);
			buffer.append("%");
		}
		display->setText(buffer.c_str());

		if (display->haveOLED()) {
			renderUIsForOled();
		}
	}
}

void ReverseActionMenuItem::drawPixelsForOled() {
	deluge::hid::display::oled_canvas::Canvas& canvas = deluge::hid::display::OLED::main;
	const int32_t topY = OLED_MAIN_TOPMOST_PIXEL + 1;
	const int32_t yBase = OLED_MAIN_TOPMOST_PIXEL + 22;

	if (reverserJob().isWaitingForMode()) {
		canvas.drawString("REV MODE", 0, topY, kTextSpacingX, kTextSpacingY);

		DEF_STACK_STRING_BUF(buffer, 16);
		if (modeVal_ == -1) {
			buffer.append("DETECT");
		}
		else if (modeVal_ == 0) {
			buffer.append("FULL (0%)");
		}
		else {
			buffer.appendInt(modeVal_);
			buffer.append("%");
		}
		canvas.drawStringAlignRight(buffer.c_str(), topY, kTextSpacingX, kTextSpacingY);

		uint8_t const* overview = reverserJob().getWaveformOverview();
		for (int32_t x = 0; x < 128; x++) {
			uint8_t h = overview[x];
			int32_t halfH = h / 2;
			if (halfH < 1) {
				halfH = 1;
			}
			drawSafeVerticalLine(canvas, x, yBase - halfH, yBase + halfH);
		}

		if (modeVal_ == -1) {
			uint32_t tCount = reverserJob().getTransientCount();
			uint32_t const* tFrames = reverserJob().getTransientFrames();
			uint32_t totalFrames = reverserJob().getTotalFrames();
			if (totalFrames > 0) {
				for (uint32_t i = 0; i < tCount; i++) {
					int32_t xPos = (tFrames[i] * 128u) / totalFrames;
					if (xPos >= 0 && xPos < 128) {
						drawSafeVerticalLine(canvas, xPos, yBase - 14, yBase + 14);
					}
				}
			}
		}
		else if (modeVal_ > 0) {
			int32_t stepX = (128 * modeVal_) / 100;
			if (stepX >= 2) {
				for (int32_t x = stepX; x < 128; x += stepX) {
					drawSafeVerticalLine(canvas, x, yBase - 12, yBase + 12);
				}
			}
		}
	}
	else if (reverserJob().isActive()) {
		canvas.drawString("SCANNING...", 0, topY, kTextSpacingX, kTextSpacingY);
	}
	else {
		canvas.drawString("REVERSE", 0, topY, kTextSpacingX, kTextSpacingY);
	}
}

MenuItem* ReverseActionMenuItem::selectButtonPress() {
	Sample* currentSample = getCurrentSample();

	if (!currentSample) {
		display->displayPopup("NO SAMPLE");
		return nullptr;
	}

	if (reverserJob().isWaitingForMode()) {
		reverserJob().setModeParam(modeVal_);
		reverserJob().confirmModeAndRun();
		return nullptr;
	}
	else if (!reverserJob().isActive()) {
		reverserJob().setModeParam(modeVal_);
		if (reverserJob().start(currentSample, gOfflineJobOverwrite)) {
			return NO_NAVIGATION;
		}
	}
	return nullptr;
}

LevelerActionMenuItem::LevelerActionMenuItem() : MenuItem(deluge::l10n::String::EMPTY_STRING) {
	thresholdVal_ = -600;
}

void LevelerActionMenuItem::beginSession(MenuItem* navigatedBackwardFrom) {
	MenuItem::beginSession(navigatedBackwardFrom);
	Sample* currentSample = getCurrentSample();
	if (currentSample && !levelerJob().isActive()) {
		levelerJob().setThresholdDb((float)thresholdVal_ / 100.0f);
		levelerJob().start(currentSample, gOfflineJobOverwrite);
	}
}

void LevelerActionMenuItem::readValueAgain() {
	display->setText("LEVELER");
}

void LevelerActionMenuItem::selectEncoderAction(int32_t offset) {
	if (levelerJob().isWaitingForThreshold()) {
		thresholdVal_ += offset * 50;
		if (thresholdVal_ > 0) {
			thresholdVal_ = 0;
		}
		else if (thresholdVal_ < -2400) {
			thresholdVal_ = -2400;
		}
		float threshDb = (float)thresholdVal_ / 100.0f;
		levelerJob().setThresholdDb(threshDb);

		DEF_STACK_STRING_BUF(buffer, 16);
		buffer.appendFloat(threshDb, 1, 1);
		buffer.append("DB");
		display->setText(buffer.c_str());

		if (display->haveOLED()) {
			renderUIsForOled();
		}
	}
}

void LevelerActionMenuItem::drawPixelsForOled() {
	deluge::hid::display::oled_canvas::Canvas& canvas = deluge::hid::display::OLED::main;
	const int32_t topY = OLED_MAIN_TOPMOST_PIXEL + 1;
	const int32_t yBase = OLED_MAIN_TOPMOST_PIXEL + 22;

	if (levelerJob().isWaitingForThreshold()) {
		canvas.drawString("T THRESH", 0, topY, kTextSpacingX, kTextSpacingY);

		float db = (float)thresholdVal_ / 100.0f;
		DEF_STACK_STRING_BUF(buffer, 16);
		buffer.appendFloat(db, 1, 1);
		buffer.append(" dB");
		canvas.drawStringAlignRight(buffer.c_str(), topY, kTextSpacingX, kTextSpacingY);

		uint8_t const* overview = levelerJob().getWaveformOverview();
		for (int32_t x = 0; x < 128; x++) {
			uint8_t h = overview[x];
			int32_t halfH = h / 2;
			if (halfH < 1) {
				halfH = 1;
			}
			drawSafeVerticalLine(canvas, x, yBase - halfH, yBase + halfH);
		}

		float ratio = powf(10.0f, db / 20.0f);
		int32_t threshOffset = (int32_t)(ratio * 14.0f);
		if (threshOffset < 1) {
			threshOffset = 1;
		}

		int32_t yThreshTop = yBase - threshOffset;
		int32_t yThreshBot = yBase + threshOffset;

		drawSafeHorizontalLine(canvas, yThreshTop, 0, 127);
		drawSafeHorizontalLine(canvas, yThreshBot, 0, 127);
	}
	else if (levelerJob().isActive()) {
		canvas.drawString("SCANNING...", 0, topY, kTextSpacingX, kTextSpacingY);
	}
	else {
		canvas.drawString("LEVELER", 0, topY, kTextSpacingX, kTextSpacingY);
	}
}

MenuItem* LevelerActionMenuItem::selectButtonPress() {
	Sample* currentSample = getCurrentSample();

	if (!currentSample) {
		display->displayPopup("NO SAMPLE");
		return nullptr;
	}

	if (levelerJob().isWaitingForThreshold()) {
		float threshDb = (float)thresholdVal_ / 100.0f;
		levelerJob().setThresholdDb(threshDb);
		levelerJob().confirmThresholdAndRun();
		return nullptr;
	}
	else if (!levelerJob().isActive()) {
		levelerJob().setThresholdDb((float)thresholdVal_ / 100.0f);
		if (levelerJob().start(currentSample, gOfflineJobOverwrite)) {
			return NO_NAVIGATION;
		}
	}
	return nullptr;
}

OfflineOperationMenu offlineOperationMenu;

OfflineOperationMenu::OfflineOperationMenu()
    : Submenu(deluge::l10n::String::EMPTY_STRING,
              {&normalizeActionMenuItem, &gainActionMenuItem, &levelerActionMenuItem, &reverseActionMenuItem,
               &saveModeMenuItem}) {
}

bool OfflineOperationMenu::isRelevant(ModControllableAudio* modControllable, int32_t /*whichThing*/) {
	// Only makes sense for subtractive synths using samples as oscillator
	// sources (same restriction as the audio recorder that used to occupy
	// this shortcut slot).
	Sound* sound = static_cast<Sound*>(modControllable);
	return (sound->getSynthMode() == SynthMode::SUBTRACTIVE);
}

MenuPermission OfflineOperationMenu::checkPermissionToBeginSession(ModControllableAudio* modControllable,
                                                                   int32_t whichThing, ::MultiRange** currentRange) {
	if (!isRelevant(modControllable, whichThing)) {
		return MenuPermission::NO;
	}

	// Entering via a shortcut pad does not populate
	// soundEditor.currentMultiRange - following the AudioRecorder pattern,
	// we select the first range (C3) ourselves so that
	// getCurrentAudioFileHolder() has valid state to work with.
	Sound* sound = static_cast<Sound*>(modControllable);
	Source* source = &sound->sources[whichThing];
	::MultiRange* firstRange = source->getOrCreateFirstRange();
	if (firstRange == nullptr) {
		return MenuPermission::NO;
	}
	*currentRange = firstRange;
	return MenuPermission::YES;
}

} // namespace deluge::gui::menu_item
