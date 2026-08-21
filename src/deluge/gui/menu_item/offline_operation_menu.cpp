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
	if (currentSample && !normalizerJob().isActive()) {
		normalizerJob().setTargetCeilingDb((float)targetCeilingVal_ / 100.0f);
		normalizerJob().start(currentSample, gOfflineJobOverwrite);
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
}

void GainActionMenuItem::beginSession(MenuItem* navigatedBackwardFrom) {
	MenuItem::beginSession(navigatedBackwardFrom);
	Sample* currentSample = getCurrentSample();
	if (currentSample && !gainerJob().isActive()) {
		gainerJob().setGainDb((float)gainVal_ / 100.0f);
		gainerJob().start(currentSample, gOfflineJobOverwrite);
	}
}

void GainActionMenuItem::readValueAgain() {
	display->setText("GAIN");
}

void GainActionMenuItem::selectEncoderAction(int32_t offset) {
	if (gainerJob().isWaitingForGain()) {
		gainVal_ += offset * 50;
		if (gainVal_ > 2400) {
			gainVal_ = 2400;
		}
		else if (gainVal_ < -2400) {
			gainVal_ = -2400;
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
}

void GainActionMenuItem::drawPixelsForOled() {
	deluge::hid::display::oled_canvas::Canvas& canvas = deluge::hid::display::OLED::main;
	const int32_t topY = OLED_MAIN_TOPMOST_PIXEL + 1;
	const int32_t yBase = OLED_MAIN_TOPMOST_PIXEL + 22;

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

MenuItem* GainActionMenuItem::selectButtonPress() {
	Sample* currentSample = getCurrentSample();

	if (!currentSample) {
		display->displayPopup("NO SAMPLE");
		return nullptr;
	}

	if (gainerJob().isWaitingForGain()) {
		float gainDb = (float)gainVal_ / 100.0f;
		gainerJob().setGainDb(gainDb);
		gainerJob().confirmGainAndRun();
		return nullptr;
	}
	else if (!gainerJob().isActive()) {
		gainerJob().setGainDb((float)gainVal_ / 100.0f);
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
	if (currentSample && !reverserJob().isActive()) {
		reverserJob().setModeParam(modeVal_);
		reverserJob().start(currentSample, gOfflineJobOverwrite);
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
