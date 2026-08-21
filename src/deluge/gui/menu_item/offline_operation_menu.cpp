#include "gui/menu_item/offline_operation_menu.h"
#include "gui/ui/sound_editor.h"
#include "gui/ui/ui.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "model/sample/sample.h"
#include "processing/offline/gain_job.h"
#include "processing/offline/leveler_job.h"
#include "processing/offline/normalize_job.h"
#include "processing/offline/reverse_job.h"
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

static int8_t gOfflineWaveformOverviewMax[128];
static int8_t gOfflineWaveformOverviewMin[128];
static Sample* gOfflineOverviewCachedSample = nullptr;

static void populateOfflineWaveformOverview(Sample* sample) {
	if (!sample || gOfflineOverviewCachedSample == sample) {
		return;
	}
	gOfflineOverviewCachedSample = sample;
	std::memset(gOfflineWaveformOverviewMax, 0, sizeof(gOfflineWaveformOverviewMax));
	std::memset(gOfflineWaveformOverviewMin, 0, sizeof(gOfflineWaveformOverviewMin));

	if (sample->filePath.isEmpty()) {
		return;
	}

	auto opened = FatFS::File::open(sample->filePath.get(), FA_READ);
	if (!opened) {
		return;
	}

	uint32_t dataStart = (uint32_t)sample->audioDataStartPosBytes;
	uint32_t totalDataBytes = (uint32_t)sample->audioDataLengthBytes;
	uint8_t byteDepth = sample->byteDepth ? sample->byteDepth : 2;
	uint8_t numChannels = sample->numChannels > 0 ? sample->numChannels : 1;
	uint32_t frameSize = byteDepth * numChannels;

	if (!dataStart || !totalDataBytes || !frameSize) {
		return;
	}

	opened->lseek(dataStart);
	uint32_t totalFrames = totalDataBytes / frameSize;
	uint32_t framesPerBin = totalFrames > 128 ? (totalFrames / 128) : 1;

	uint8_t staticBuf[1024];
	uint32_t bytesReadTotal = 0;
	int32_t binIdx = 0;
	int32_t maxInBin = -32768;
	int32_t minInBin = 32767;
	uint32_t binFrames = 0;

	while (bytesReadTotal < totalDataBytes && binIdx < 128) {
		uint32_t toRead = std::min(totalDataBytes - bytesReadTotal, (uint32_t)sizeof(staticBuf));
		auto readRes = opened->read({(std::byte*)staticBuf, toRead});
		if (!readRes || readRes->empty()) break;
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
				int32_t scaledMax = (maxInBin * 32) / 32767;
				int32_t scaledMin = (minInBin * 32) / 32767;
				if (scaledMax > 32) scaledMax = 32;
				if (scaledMin < -32) scaledMin = -32;

				gOfflineWaveformOverviewMax[binIdx] = (int8_t)scaledMax;
				gOfflineWaveformOverviewMin[binIdx] = (int8_t)scaledMin;

				binIdx++;
				maxInBin = -32768;
				minInBin = 32767;
				binFrames = 0;
				if (binIdx >= 128) break;
			}
		}
	}
	while (binIdx < 128) {
		int32_t scaledMax = (maxInBin * 32) / 32767;
		int32_t scaledMin = (minInBin * 32) / 32767;
		if (scaledMax > 32) scaledMax = 32;
		if (scaledMin < -32) scaledMin = -32;

		gOfflineWaveformOverviewMax[binIdx] = (int8_t)scaledMax;
		gOfflineWaveformOverviewMin[binIdx] = (int8_t)scaledMin;
		binIdx++;
	}
}

static void renderWaveformOverviewOnCanvas(Sample* sample, int32_t yBase = 43, int32_t maxHalfH = 20) {
	deluge::hid::display::oled_canvas::Canvas& canvas = deluge::hid::display::OLED::main;
	if (!sample) {
		return;
	}
	populateOfflineWaveformOverview(sample);
	for (int32_t x = 0; x < 128; x++) {
		int8_t maxPeak = gOfflineWaveformOverviewMax[x];
		int8_t minPeak = gOfflineWaveformOverviewMin[x];

		int32_t yTop = yBase - ((int32_t)maxPeak * maxHalfH) / 32;
		int32_t yBot = yBase - ((int32_t)minPeak * maxHalfH) / 32;

		if (yTop == yBot) {
			yTop = yBase - 1;
			yBot = yBase + 1;
		}

		canvas.drawVerticalLine(x, yTop, yBot);
	}
}

NormalizeActionMenuItem::NormalizeActionMenuItem() : Integer(deluge::l10n::String::EMPTY_STRING) {
	setValue(0);
}

void NormalizeActionMenuItem::selectEncoderAction(int32_t offset) {
	Integer::selectEncoderAction(offset * 10);
	if (display->haveOLED()) {
		renderUIsForOled();
	}
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
	deluge::hid::display::oled_canvas::Canvas& canvas = deluge::hid::display::OLED::main;
	const int32_t topY = OLED_MAIN_TOPMOST_PIXEL + 1;
	const int32_t yBase = 43;
	const int32_t maxHalfH = 20;

	canvas.drawString("NORM TARGET", 0, topY, kTextSpacingX, kTextSpacingY);

	float db = (float)getValue() / 100.0f;
	DEF_STACK_STRING_BUF(buffer, 16);
	if (db > 0.0f) {
		buffer.append("+");
	}
	buffer.appendFloat(db, 1, 1);
	buffer.append(" dB");
	canvas.drawStringAlignRight(buffer.c_str(), topY, kTextSpacingX, kTextSpacingY);

	Sample* currentSample = getCurrentSample();
	renderWaveformOverviewOnCanvas(currentSample, yBase, maxHalfH);

	float ratio = powf(10.0f, db / 20.0f);
	int32_t targetOffset = (int32_t)(ratio * (float)maxHalfH);
	if (targetOffset < 1) {
		targetOffset = 1;
	}

	int32_t yTop = yBase - targetOffset;
	int32_t yBot = yBase + targetOffset;

	canvas.drawHorizontalLine(yTop, 0, 127);
	canvas.drawHorizontalLine(yBot, 0, 127);
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
	if (display->haveOLED()) {
		renderUIsForOled();
	}
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
	deluge::hid::display::oled_canvas::Canvas& canvas = deluge::hid::display::OLED::main;
	const int32_t topY = OLED_MAIN_TOPMOST_PIXEL + 1;
	const int32_t yBase = 43;
	const int32_t maxHalfH = 20;

	canvas.drawString("GAIN BOOST", 0, topY, kTextSpacingX, kTextSpacingY);

	float db = (float)getValue() / 100.0f;
	DEF_STACK_STRING_BUF(buffer, 16);
	if (db > 0.0f) {
		buffer.append("+");
	}
	buffer.appendFloat(db, 1, 1);
	buffer.append(" dB");
	canvas.drawStringAlignRight(buffer.c_str(), topY, kTextSpacingX, kTextSpacingY);

	Sample* currentSample = getCurrentSample();
	renderWaveformOverviewOnCanvas(currentSample, yBase, maxHalfH);

	float ratio = powf(10.0f, db / 20.0f);
	int32_t gainOffset = (int32_t)(ratio * (float)maxHalfH);
	if (gainOffset < 1) {
		gainOffset = 1;
	}

	int32_t yTop = yBase - gainOffset;
	int32_t yBot = yBase + gainOffset;

	canvas.drawHorizontalLine(yTop, 0, 127);
	canvas.drawHorizontalLine(yBot, 0, 127);
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
	deluge::hid::display::oled_canvas::Canvas& canvas = deluge::hid::display::OLED::main;
	const int32_t topY = OLED_MAIN_TOPMOST_PIXEL + 1;
	const int32_t yBase = 43;
	const int32_t maxHalfH = 20;

	canvas.drawString("REVERSE", 0, topY, kTextSpacingX, kTextSpacingY);
	canvas.drawStringAlignRight("APPLY", topY, kTextSpacingX, kTextSpacingY);

	Sample* currentSample = getCurrentSample();
	renderWaveformOverviewOnCanvas(currentSample, yBase, maxHalfH);

	canvas.drawHorizontalLine(yBase, 0, 127);
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
	if (currentSample) {
		levelerJob().setThresholdDb((float)thresholdVal_ / 100.0f);
		levelerJob().setupWaveformPreview(currentSample, gOfflineJobOverwrite);
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
	const int32_t yBase = 43;
	const int32_t maxHalfH = 20;

	if (levelerJob().isWaitingForThreshold()) {
		canvas.drawString("T THRESH", 0, topY, kTextSpacingX, kTextSpacingY);

		float db = (float)thresholdVal_ / 100.0f;
		DEF_STACK_STRING_BUF(buffer, 16);
		buffer.appendFloat(db, 1, 1);
		buffer.append(" dB");
		canvas.drawStringAlignRight(buffer.c_str(), topY, kTextSpacingX, kTextSpacingY);

		Sample* currentSample = getCurrentSample();
		renderWaveformOverviewOnCanvas(currentSample, yBase, maxHalfH);

		float ratio = powf(10.0f, db / 20.0f);
		int32_t threshOffset = (int32_t)(ratio * (float)maxHalfH);
		if (threshOffset < 1) {
			threshOffset = 1;
		}

		int32_t yThreshTop = yBase - threshOffset;
		int32_t yThreshBot = yBase + threshOffset;

		canvas.drawHorizontalLine(yThreshTop, 0, 127);
		canvas.drawHorizontalLine(yThreshBot, 0, 127);
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
