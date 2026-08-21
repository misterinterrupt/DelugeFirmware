#include "gui/menu_item/offline_operation_menu.h"
#include "fatfs/fatfs.hpp"
#include "gui/ui/sound_editor.h"
#include "gui/ui/ui.h"
#include "hid/button.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "model/sample/sample.h"
#include "processing/offline/gain_job.h"
#include "processing/offline/leveler_job.h"
#include "processing/offline/normalize_job.h"
#include "processing/offline/reverse_job.h"
#include "processing/sound/sound.h"
#include "storage/multi_range/multisample_range.h"
#include <algorithm>
#include <cmath>

namespace deluge::gui::menu_item {

bool gOfflineJobOverwrite = false;
OfflineWaveformCache gOfflineWaveformCache;

static SaveModeMenuItem saveModeMenuItem;
static NormalizeActionMenuItem normalizeActionMenuItem;
static ReverseActionMenuItem reverseActionMenuItem;
static GainActionMenuItem gainActionMenuItem;
static LevelerActionMenuItem levelerActionMenuItem;

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

bool OfflineWaveformCache::scanSample(Sample* sample) {
	if (!sample || !sample->audioDataLengthBytes || !sample->byteDepth || !sample->audioDataStartPosBytes) {
		return false;
	}

	auto opened = FatFS::File::open(sample->filePath.get(), FA_READ);
	if (!opened) {
		return false;
	}

	uint32_t dataStart = (uint32_t)sample->audioDataStartPosBytes;
	uint32_t totalDataBytes = (uint32_t)sample->audioDataLengthBytes;
	uint32_t byteDepth = sample->byteDepth;
	uint32_t numChannels = sample->numChannels > 0 ? sample->numChannels : 1;
	uint32_t frameSize = byteDepth * numChannels;

	opened->lseek(dataStart);

	uint32_t totalFrames = totalDataBytes / frameSize;
	if (totalFrames == 0) {
		return false;
	}

	uint32_t framesPerBin = totalFrames > kOverviewColumns ? (totalFrames / kOverviewColumns) : 1;

	uint8_t staticBuf[1024];
	uint32_t bytesReadTotal = 0;
	uint32_t binIdx = 0;
	int32_t maxInBin = -32768;
	int32_t minInBin = 32767;
	uint32_t binFrames = 0;

	std::memset(waveformOverview_, 0, sizeof(waveformOverview_));
	std::memset(waveformOverviewMin_, 0, sizeof(waveformOverviewMin_));
	std::memset(waveformOverviewMax_, 0, sizeof(waveformOverviewMax_));

	while (bytesReadTotal < totalDataBytes && binIdx < kOverviewColumns) {
		uint32_t toRead = std::min(totalDataBytes - bytesReadTotal, (uint32_t)sizeof(staticBuf));
		auto readRes = opened->read({(std::byte*)staticBuf, toRead});
		if (!readRes || readRes->empty()) {
			break;
		}
		uint32_t got = readRes->size();
		bytesReadTotal += got;

		uint32_t framesInChunk = got / frameSize;
		uint8_t* ptr = staticBuf;

		for (uint32_t f = 0; f < framesInChunk; f++) {
			int32_t val = 0;
			if (byteDepth == 2) {
				int16_t sampleVal = (int16_t)(ptr[0] | (ptr[1] << 8));
				val = (int32_t)sampleVal;
			}
			else if (byteDepth == 3) {
				int32_t sampleVal = (int32_t)((ptr[0] << 8) | (ptr[1] << 16) | (ptr[2] << 24)) >> 8;
				val = sampleVal >> 8;
			}
			ptr += frameSize;

			if (val > maxInBin) maxInBin = val;
			if (val < minInBin) minInBin = val;

			binFrames++;
			if (binFrames >= framesPerBin) {
				int32_t scaledMax = (maxInBin * 14) / 32767;
				int32_t scaledMin = (minInBin * 14) / 32767;
				if (scaledMax > 14) scaledMax = 14;
				if (scaledMin < -14) scaledMin = -14;

				waveformOverviewMax_[binIdx] = (int8_t)scaledMax;
				waveformOverviewMin_[binIdx] = (int8_t)scaledMin;
				uint32_t absPeak = std::max(std::abs(maxInBin), std::abs(minInBin));
				waveformOverview_[binIdx] = (uint8_t)((absPeak * 14) / 32767);

				binIdx++;
				maxInBin = -32768;
				minInBin = 32767;
				binFrames = 0;
				if (binIdx >= kOverviewColumns) break;
			}
		}
	}

	while (binIdx < kOverviewColumns) {
		int32_t scaledMax = (maxInBin * 14) / 32767;
		int32_t scaledMin = (minInBin * 14) / 32767;
		if (scaledMax > 14) scaledMax = 14;
		if (scaledMin < -14) scaledMin = -14;

		waveformOverviewMax_[binIdx] = (int8_t)scaledMax;
		waveformOverviewMin_[binIdx] = (int8_t)scaledMin;
		uint32_t absPeak = std::max(std::abs(maxInBin), std::abs(minInBin));
		waveformOverview_[binIdx] = (uint8_t)((absPeak * 14) / 32767);
		binIdx++;
	}

	scanned_ = true;
	scannedSample_ = sample;
	return true;
}

static void drawOfflineWaveformCanvas(const char* titleText,
                                     const char* valueText,
                                     Sample* currentSample,
                                     bool showThreshold = false,
                                     float thresholdDb = 0.0f)
{
	deluge::hid::display::oled_canvas::Canvas& canvas = deluge::hid::display::OLED::main;
	const int32_t topY = OLED_MAIN_TOPMOST_PIXEL + 1;
	const int32_t yBase = OLED_MAIN_TOPMOST_PIXEL + 22;

	if (titleText) {
		canvas.drawString(titleText, 0, topY, kTextSpacingX, kTextSpacingY);
	}
	if (valueText) {
		canvas.drawStringAlignRight(valueText, topY, kTextSpacingX, kTextSpacingY);
	}

	if (currentSample) {
		if (!gOfflineWaveformCache.hasValidScan(currentSample)) {
			gOfflineWaveformCache.scanSample(currentSample);
		}
	}

	if (gOfflineWaveformCache.hasValidScan(currentSample)) {
		int8_t const* minOverview = gOfflineWaveformCache.getOverviewMin();
		int8_t const* maxOverview = gOfflineWaveformCache.getOverviewMax();

		for (int32_t x = 0; x < 128; x++) {
			int32_t maxH = maxOverview[x];
			int32_t minH = minOverview[x];
			int32_t yTop = yBase - maxH;
			int32_t yBot = yBase - minH;
			if (yTop > yBot) {
				std::swap(yTop, yBot);
			}
			if (yTop == yBot) {
				canvas.drawVerticalLine(x, yBase, yBase);
			}
			else {
				canvas.drawVerticalLine(x, yTop, yBot);
			}
		}
	}
	else {
		canvas.drawStringCentred("PRESS REC TO SCAN", yBase - 4, kTextSpacingX, kTextSpacingY);
	}

	if (showThreshold) {
		float ratio = powf(10.0f, thresholdDb / 20.0f);
		int32_t threshOffset = (int32_t)(ratio * 14.0f);
		if (threshOffset < 1) {
			threshOffset = 1;
		}

		int32_t yThreshTop = yBase - threshOffset;
		int32_t yThreshBot = yBase + threshOffset;

		canvas.drawHorizontalLine(yThreshTop, 0, 127);
		canvas.drawHorizontalLine(yThreshBot, 0, 127);
	}
}

SaveModeMenuItem::SaveModeMenuItem()
    : ToggleBool(deluge::l10n::String::EMPTY_STRING, deluge::l10n::String::EMPTY_STRING, gOfflineJobOverwrite) {
}

NormalizeActionMenuItem::NormalizeActionMenuItem() : Integer(deluge::l10n::String::EMPTY_STRING) {
	setValue(0);
}

void NormalizeActionMenuItem::selectEncoderAction(int32_t offset) {
	Integer::selectEncoderAction(offset * 10);
}

void NormalizeActionMenuItem::drawValue() {
	float db = (float)getValue() / 100.0f;
	DEF_STACK_STRING_BUF(buffer, 16);
	if (db >= 0.0f) {
		buffer.append("+");
	}
	buffer.appendFloat(db, 2, 2);
	buffer.append("DB");
	display->setText(buffer.c_str());
}

void NormalizeActionMenuItem::drawPixelsForOled() {
	float db = (float)getValue() / 100.0f;
	DEF_STACK_STRING_BUF(buffer, 16);
	if (db >= 0.0f) {
		buffer.append("+");
	}
	buffer.appendFloat(db, 2, 2);
	buffer.append(" dB");

	drawOfflineWaveformCanvas("NORMALIZE", buffer.c_str(), getCurrentSample(), true, db);
}

ActionResult NormalizeActionMenuItem::buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) {
	if (b == deluge::hid::button::RECORD && on) {
		Sample* s = getCurrentSample();
		if (s) {
			gOfflineWaveformCache.scanSample(s);
			if (display->haveOLED()) {
				renderUIsForOled();
			}
		}
		return ActionResult::DEALT_WITH;
	}
	return MenuItem::buttonAction(b, on, inCardRoutine);
}

MenuItem* NormalizeActionMenuItem::selectButtonPress() {
	Sample* currentSample = getCurrentSample();

	if (!currentSample) {
		display->displayPopup("NO SAMPLE");
		return nullptr;
	}

	float ceilingDb = (float)getValue() / 100.0f;
	normalizeJob().start(currentSample, ceilingDb, gOfflineJobOverwrite);
	return nullptr;
}

GainActionMenuItem::GainActionMenuItem() : Integer(deluge::l10n::String::EMPTY_STRING) {
	setValue(0);
}

void GainActionMenuItem::selectEncoderAction(int32_t offset) {
	Integer::selectEncoderAction(offset * 10);
}

void GainActionMenuItem::drawValue() {
	float db = (float)getValue() / 100.0f;
	DEF_STACK_STRING_BUF(buffer, 16);
	if (db >= 0.0f) {
		buffer.append("+");
	}
	buffer.appendFloat(db, 2, 2);
	buffer.append("DB");
	display->setText(buffer.c_str());
}

void GainActionMenuItem::drawPixelsForOled() {
	float db = (float)getValue() / 100.0f;
	DEF_STACK_STRING_BUF(buffer, 16);
	if (db >= 0.0f) {
		buffer.append("+");
	}
	buffer.appendFloat(db, 2, 2);
	buffer.append(" dB");

	drawOfflineWaveformCanvas("GAIN", buffer.c_str(), getCurrentSample(), true, db);
}

ActionResult GainActionMenuItem::buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) {
	if (b == deluge::hid::button::RECORD && on) {
		Sample* s = getCurrentSample();
		if (s) {
			gOfflineWaveformCache.scanSample(s);
			if (display->haveOLED()) {
				renderUIsForOled();
			}
		}
		return ActionResult::DEALT_WITH;
	}
	return MenuItem::buttonAction(b, on, inCardRoutine);
}

MenuItem* GainActionMenuItem::selectButtonPress() {
	Sample* currentSample = getCurrentSample();

	if (!currentSample) {
		display->displayPopup("NO SAMPLE");
		return nullptr;
	}

	float gainDb = (float)getValue() / 100.0f;
	gainJob().start(currentSample, gainDb, gOfflineJobOverwrite);
	return nullptr;
}

ReverseActionMenuItem::ReverseActionMenuItem() : MenuItem(deluge::l10n::String::EMPTY_STRING) {
}

void ReverseActionMenuItem::readValueAgain() {
	display->setText("REVERSE");
}

void ReverseActionMenuItem::drawPixelsForOled() {
	drawOfflineWaveformCanvas("REVERSE", "EXECUTE", getCurrentSample(), false, 0.0f);
}

ActionResult ReverseActionMenuItem::buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) {
	if (b == deluge::hid::button::RECORD && on) {
		Sample* s = getCurrentSample();
		if (s) {
			gOfflineWaveformCache.scanSample(s);
			if (display->haveOLED()) {
				renderUIsForOled();
			}
		}
		return ActionResult::DEALT_WITH;
	}
	return MenuItem::buttonAction(b, on, inCardRoutine);
}

MenuItem* ReverseActionMenuItem::selectButtonPress() {
	Sample* currentSample = getCurrentSample();

	if (!currentSample) {
		display->displayPopup("NO SAMPLE");
		return nullptr;
	}

	reverseJob().start(currentSample, gOfflineJobOverwrite);
	return nullptr;
}

LevelerActionMenuItem::LevelerActionMenuItem() : MenuItem(deluge::l10n::String::EMPTY_STRING) {
	thresholdVal_ = -600;
}

void LevelerActionMenuItem::beginSession(MenuItem* navigatedBackwardFrom) {
	MenuItem::beginSession(navigatedBackwardFrom);
	Sample* currentSample = getCurrentSample();
	if (currentSample && !gOfflineWaveformCache.hasValidScan(currentSample)) {
		gOfflineWaveformCache.scanSample(currentSample);
	}
}

void LevelerActionMenuItem::readValueAgain() {
	display->setText("LEVELER");
}

void LevelerActionMenuItem::selectEncoderAction(int32_t offset) {
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

void LevelerActionMenuItem::drawPixelsForOled() {
	float db = (float)thresholdVal_ / 100.0f;
	DEF_STACK_STRING_BUF(buffer, 16);
	buffer.appendFloat(db, 1, 1);
	buffer.append(" dB");

	drawOfflineWaveformCanvas("T THRESH", buffer.c_str(), getCurrentSample(), true, db);
}

ActionResult LevelerActionMenuItem::buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) {
	if (b == deluge::hid::button::RECORD && on) {
		Sample* s = getCurrentSample();
		if (s) {
			gOfflineWaveformCache.scanSample(s);
			if (display->haveOLED()) {
				renderUIsForOled();
			}
		}
		return ActionResult::DEALT_WITH;
	}
	return MenuItem::buttonAction(b, on, inCardRoutine);
}

MenuItem* LevelerActionMenuItem::selectButtonPress() {
	Sample* currentSample = getCurrentSample();

	if (!currentSample) {
		display->displayPopup("NO SAMPLE");
		return nullptr;
	}

	float threshDb = (float)thresholdVal_ / 100.0f;
	levelerJob().setThresholdDb(threshDb);
	levelerJob().start(currentSample, gOfflineJobOverwrite);
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
