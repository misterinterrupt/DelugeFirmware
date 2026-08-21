/*
 * Copyright © 2026 Synthstrom Audible Limited. All rights reserved.
 *
 * Leveler DSP Job implementation.
 */

#include "processing/engines/audio_engine.h"
#include "processing/offline/leveler_job.h"

#include "definitions_cxx.hpp"
#include "fatfs/fatfs.hpp"
#include "gui/ui/browser/sample_browser.h"
#include "gui/ui/sound_editor.h"
#include "gui/ui/ui.h"
#include "hid/display/display.h"
#include "io/debug/log.h"
#include "memory/memory_allocator_interface.h"
#include "model/sample/sample.h"
#include "processing/sound/sound.h"
#include "storage/audio/audio_file_holder.h"
#include "storage/audio/audio_file_manager.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

extern "C" {
extern uint8_t currentlyAccessingCard;
}

static uint32_t lastReportedPercent = 999;

static void showPopup(char const* message) {
	if (display->haveOLED()) {
		display->displayPopup(message);
	}
}

static void showProgressPopup(uint32_t current, uint32_t total, uint32_t passOffset, uint32_t totalPasses) {
	if (!display->haveOLED() || total == 0) {
		return;
	}
	uint32_t passPercent = (current * 100) / total;
	uint32_t overallPercent = (passOffset * 100 + passPercent) / totalPasses;
	uint32_t roundedPercent = (overallPercent / 5) * 5;
	if (roundedPercent != lastReportedPercent) {
		lastReportedPercent = roundedPercent;
		DEF_STACK_STRING_BUF(buf, 16);
		buf.appendInt(roundedPercent);
		buf.append("%");
		display->displayPopup(buf.c_str(), 1);
	}
}

LevelerJob& levelerJob() {
	static LevelerJob instance;
	return instance;
}

bool LevelerJob::start(Sample* sample, bool overwrite) {
	if (!sample) {
		showPopup("NO SAMPLE");
		return false;
	}

	if (isActive()) {
		showPopup("BUSY");
		return false;
	}

	if (!sample->audioDataLengthBytes || !sample->byteDepth || !sample->audioDataStartPosBytes) {
		showPopup("CANT LEVEL");
		return false;
	}

	uint64_t total = sample->audioDataLengthBytes;
	if (total == 0) {
		showPopup("EMPTY FILE");
		return false;
	}

	if (sample->byteDepth != 2 && sample->byteDepth != 3) {
		showPopup("BAD DEPTH");
		return false;
	}

	chunkBuffer = (uint8_t*)delugeAlloc(kChunkSize, false);
	if (!chunkBuffer) {
		showPopup("MEMORY");
		return false;
	}

	tempPath.clear();
	finalPath.clear();
	sourcePath.clear();

	sourcePath.set(&sample->filePath);
	overwriteTarget_ = overwrite;

	dataStart = (uint32_t)sample->audioDataStartPosBytes;
	totalDataBytes = (uint32_t)total;
	dataBytesRemaining = totalDataBytes;
	headerBytesRemaining = dataStart;
	byteDepth = sample->byteDepth;
	numChannels = sample->numChannels > 0 ? sample->numChannels : 1;

	uint32_t frameSize = byteDepth * numChannels;
	dataChunkSize = (kChunkSize / frameSize) * frameSize;

	peakValue = 0;
	audioBytesWritten = 0;
	lastReportedPercent = 999;
	std::memset(waveformOverview, 0, sizeof(waveformOverview));

	phase = Phase::OPEN;
	showPopup("SCANNING...");
	return true;
}

void LevelerJob::confirmThresholdAndRun() {
	if (phase == Phase::SELECT_THRESHOLD) {
		phase = Phase::PREPARE_COPY;
	}
}

void LevelerJob::cancel() {
	fail("CANCELLED");
}

void LevelerJob::tick() {
	if (phase == Phase::IDLE || phase == Phase::SELECT_THRESHOLD) {
		return;
	}

	if (currentlyAccessingCard) {
		return;
	}

	switch (phase) {

	case Phase::IDLE:
	case Phase::SELECT_THRESHOLD:
		return;

	case Phase::OPEN: {
		char const* src = sourcePath.get();
		if (!src || *src == '\0') {
			fail("BAD PATH");
			return;
		}

		auto opened = FatFS::File::open(src, FA_READ);
		if (!opened) {
			failIO(Site::OP, opened.error());
			return;
		}
		sourceFile.emplace(std::move(*opened));

		auto seekRes = sourceFile->lseek(dataStart);
		if (!seekRes) {
			failIO(Site::SK, seekRes.error());
			return;
		}

		phase = Phase::SCAN;
		dataBytesRemaining = totalDataBytes;
		return;
	}

	case Phase::SCAN: {
		uint32_t chunk = (dataBytesRemaining < dataChunkSize) ? dataBytesRemaining : dataChunkSize;
		if (chunk != 0) {
			auto readRes = sourceFile->read({(std::byte*)chunkBuffer, chunk});
			if (!readRes) {
				failIO(Site::R1, readRes.error());
				return;
			}
			uint32_t got = readRes->size();
			if (got == 0) {
				dataBytesRemaining = 0;
			}
			else {
				scanChunkForPeak(got);
				dataBytesRemaining -= got;
				showProgressPopup(totalDataBytes - dataBytesRemaining, totalDataBytes, 0, 2);
			}
		}

		if (dataBytesRemaining == 0) {
			if (peakValue == 0) {
				fail("SILENCE");
				return;
			}
			phase = Phase::SELECT_THRESHOLD;
			if (display->haveOLED()) {
				renderUIsForOled();
			}
		}
		return;
	}

	case Phase::PREPARE_COPY: {
		beginCopyPass();
		return;
	}

	case Phase::COPY_HEADER: {
		uint32_t toRead = (headerBytesRemaining < kChunkSize) ? headerBytesRemaining : kChunkSize;
		auto r1 = sourceFile->read({(std::byte*)chunkBuffer, toRead});
		if (!r1) {
			failIO(Site::R2, r1.error());
			return;
		}
		if (r1->empty()) {
			fail("READ EOF");
			return;
		}
		auto w2 = destFile->write(r1.value());
		if (!w2) {
			failIO(Site::W2, w2.error());
			return;
		}

		headerBytesRemaining -= r1->size();
		if (headerBytesRemaining == 0) {
			phase = Phase::LEVEL_DATA;
			dataBytesRemaining = totalDataBytes;
		}
		return;
	}

	case Phase::LEVEL_DATA: {
		uint32_t chunk = (dataBytesRemaining < dataChunkSize) ? dataBytesRemaining : dataChunkSize;
		if (chunk != 0) {
			auto r3 = sourceFile->read({(std::byte*)chunkBuffer, chunk});
			if (!r3) {
				failIO(Site::R3, r3.error());
				return;
			}
			uint32_t got = r3->size();
			if (got == 0) {
				dataBytesRemaining = 0;
			}
			else {
				processChunk(chunkBuffer, got, byteDepth, numChannels);

				auto writeRes = destFile->write({(std::byte*)chunkBuffer, got});
				if (!writeRes) {
					failIO(Site::W3, writeRes.error());
					return;
				}
				if (*writeRes != got) {
					fail("WRITE ERR");
					return;
				}
				audioBytesWritten += *writeRes;
				dataBytesRemaining -= got;
			}
		}

		showProgressPopup(totalDataBytes - dataBytesRemaining, totalDataBytes, 1, 2);

		if (dataBytesRemaining == 0) {
			phase = Phase::FINALIZE;
		}
		return;
	}

	case Phase::FINALIZE: {
		auto seek1 = destFile->lseek(4);
		if (!seek1) {
			failIO(Site::FZ, seek1.error());
			return;
		}
		uint32_t riffLength = audioBytesWritten + dataStart - 8;
		auto w1 = destFile->write({(std::byte*)&riffLength, 4});
		if (!w1) {
			failIO(Site::FZ, w1.error());
			return;
		}
		if (*w1 != 4) {
			fail("WRITE ERR");
			return;
		}
		auto seek2 = destFile->lseek(dataStart - 4);
		if (!seek2) {
			failIO(Site::FZ, seek2.error());
			return;
		}
		auto w2 = destFile->write({(std::byte*)&audioBytesWritten, 4});
		if (!w2) {
			failIO(Site::FZ, w2.error());
			return;
		}
		if (*w2 != 4) {
			fail("WRITE ERR");
			return;
		}
		auto synced = destFile->sync();
		destFile.reset();
		sourceFile.reset();
		if (!synced) {
			failIO(Site::FZ, synced.error());
			return;
		}

		if (overwriteTarget_) {
			FatFS::unlink(finalPath.get());
		}
		auto renamed = FatFS::rename(tempPath.get(), finalPath.get());
		if (!renamed) {
			failIO(Site::RN, renamed.error());
			return;
		}
		succeed();
		return;
	}
	}
}

void LevelerJob::beginCopyPass() {
	linearGain_ = powf(10.0f, thresholdDb_ / 20.0f);

	char const* src = sourcePath.get();
	if (!src || *src == '\0') {
		fail("BAD PATH");
		return;
	}

	char const* lastDot = strrchr(src, '.');
	char const* lastSlash = strrchr(src, '/');

	int32_t stemLength = (lastDot && lastDot > lastSlash) ? (int32_t)(lastDot - src) : (int32_t)strlen(src);
	char const* ext = (lastDot && lastDot > lastSlash) ? lastDot : ".WAV";

	char stemBuf[256];
	if (stemLength >= 256) {
		stemLength = 255;
	}
	memcpy(stemBuf, src, stemLength);
	stemBuf[stemLength] = '\0';

	if (overwriteTarget_) {
		finalPath.set(sourcePath.get());
		DEF_STACK_STRING_BUF(tempBuf, 256);
		tempBuf.append(stemBuf);
		tempBuf.append(".TMP");
		tempPath.set(tempBuf.c_str());
	}
	else {
		bool found = false;
		String finalPathScratch;
		String tempPathScratch;

		for (char c = 'A'; c <= 'Z'; c++) {
			DEF_STACK_STRING_BUF(candidateBuf, 256);
			candidateBuf.append(stemBuf);
			candidateBuf.append("_LVL_");
			char suffixStr[2] = {c, '\0'};
			candidateBuf.append(suffixStr);
			candidateBuf.append(ext);

			auto statRes = FatFS::stat(candidateBuf.c_str());
			if (!statRes.has_value()) {
				if (statRes.error() != FatFS::Error::NO_FILE && statRes.error() != FatFS::Error::NO_PATH) {
					failIO(Site::OP, statRes.error());
					return;
				}
				finalPathScratch.set(candidateBuf.c_str());

				DEF_STACK_STRING_BUF(tempBuf, 256);
				tempBuf.append(stemBuf);
				tempBuf.append("_LVL_");
				tempBuf.append(suffixStr);
				tempBuf.append(".TMP");
				tempPathScratch.set(tempBuf.c_str());

				found = true;
				break;
			}
		}

		if (!found) {
			fail("NO SUFFIX");
			return;
		}

		finalPath.set(finalPathScratch.get());
		tempPath.set(tempPathScratch.get());
	}

	sourceFile.reset();
	destFile.reset();

	auto sFile = FatFS::File::open(sourcePath.get(), FA_READ);
	if (!sFile) {
		failIO(Site::OP, sFile.error());
		return;
	}
	sourceFile.emplace(std::move(*sFile));

	auto dFile = FatFS::File::open(tempPath.get(), FA_WRITE | FA_CREATE_ALWAYS);
	if (!dFile) {
		failIO(Site::FZ, dFile.error());
		return;
	}
	destFile.emplace(std::move(*dFile));

	phase = Phase::COPY_HEADER;
	headerBytesRemaining = dataStart;
	audioBytesWritten = 0;

	showPopup("LEVELING...");
}

void LevelerJob::scanChunkForPeak(uint32_t bytesRead) {
	uint32_t frameSize = byteDepth * numChannels;
	if (frameSize == 0) {
		return;
	}
	uint32_t numFrames = bytesRead / frameSize;

	uint32_t bytesScannedSoFar = totalDataBytes - dataBytesRemaining;

	for (size_t i = 0; i < numFrames; i++) {
		uint8_t const* p = chunkBuffer + (i * frameSize);
		for (size_t c = 0; c < numChannels; c++) {
			int32_t sampleVal = 0;
			if (byteDepth == 2) {
				int16_t raw = (int16_t)(p[0] | (p[1] << 8));
				sampleVal = (raw < 0) ? -raw : raw;
			}
			else if (byteDepth == 3) {
				int32_t raw = (int32_t)(p[0] | (p[1] << 8) | (p[2] << 16));
				if (raw & 0x800000) {
					raw |= 0xFF000000;
				}
				sampleVal = (raw < 0) ? -raw : raw;
			}
			p += byteDepth;

			uint32_t val = (uint32_t)sampleVal;
			if (val > peakValue) {
				peakValue = val;
			}

			// Map sample position to 128 overview columns
			uint32_t totalFramePos = (bytesScannedSoFar + i * frameSize) / frameSize;
			uint32_t totalFrames = totalDataBytes / frameSize;
			if (totalFrames > 0) {
				uint32_t col = (totalFramePos * kOverviewColumns) / totalFrames;
				if (col >= kOverviewColumns) {
					col = kOverviewColumns - 1;
				}
				uint32_t scaledPeak = (val * 32) / ((byteDepth == 2) ? 32767u : 8388607u);
				if (scaledPeak > waveformOverview[col]) {
					waveformOverview[col] = (uint8_t)scaledPeak;
				}
			}
		}
	}
}

void LevelerJob::processChunk(uint8_t* buffer, uint32_t bytes, uint8_t depth, uint8_t channels) {
	uint32_t frameSize = depth * channels;
	if (frameSize == 0) {
		return;
	}
	uint32_t frames = bytes / frameSize;
	uint8_t* ptr = buffer;

	float threshVal = linearGain_ * ((depth == 2) ? 32767.0f : 8388607.0f);
	int32_t limit = (int32_t)threshVal;

	if (depth == 2) {
		for (uint32_t f = 0; f < frames; f++) {
			for (uint8_t c = 0; c < channels; c++) {
				int16_t sampleVal = (int16_t)(ptr[0] | (ptr[1] << 8));
				int32_t outVal = sampleVal;

				if (outVal > limit) {
					outVal = limit;
				}
				else if (outVal < -limit) {
					outVal = -limit;
				}

				ptr[0] = (uint8_t)(outVal & 0xFF);
				ptr[1] = (uint8_t)((outVal >> 8) & 0xFF);
				ptr += 2;
			}
		}
	}
	else if (depth == 3) {
		for (uint32_t f = 0; f < frames; f++) {
			for (uint8_t c = 0; c < channels; c++) {
				int32_t sampleVal = (int32_t)(ptr[0] | (ptr[1] << 8) | (ptr[2] << 16));
				if (sampleVal & 0x800000) {
					sampleVal |= 0xFF000000;
				}
				int32_t outVal = sampleVal;

				if (outVal > limit) {
					outVal = limit;
				}
				else if (outVal < -limit) {
					outVal = -limit;
				}

				ptr[0] = (uint8_t)(outVal & 0xFF);
				ptr[1] = (uint8_t)((outVal >> 8) & 0xFF);
				ptr[2] = (uint8_t)((outVal >> 16) & 0xFF);
				ptr += 3;
			}
		}
	}
}

char const* LevelerJob::siteName(Site site) {
	switch (site) {
	case Site::OP:
		return "OPEN";
	case Site::SK:
		return "SEEK";
	case Site::R1:
		return "READ1";
	case Site::R2:
		return "READ2";
	case Site::W2:
		return "WRITE2";
	case Site::R3:
		return "READ3";
	case Site::W3:
		return "WRITE3";
	case Site::FZ:
		return "FINALIZE";
	case Site::RN:
		return "RENAME";
	}
	return "UNK";
}

void LevelerJob::fail(char const* message) {
	sourceFile.reset();
	destFile.reset();
	if (chunkBuffer) {
		delugeDealloc(chunkBuffer);
		chunkBuffer = nullptr;
	}
	if (!tempPath.isEmpty()) {
		FatFS::unlink(tempPath.get());
	}
	phase = Phase::IDLE;
	showPopup(message);
}

void LevelerJob::failIO(Site site, FatFS::Error error) {
	sourceFile.reset();
	destFile.reset();
	if (chunkBuffer) {
		delugeDealloc(chunkBuffer);
		chunkBuffer = nullptr;
	}
	if (!tempPath.isEmpty()) {
		FatFS::unlink(tempPath.get());
	}
	phase = Phase::IDLE;

	DEF_STACK_STRING_BUF(buf, 24);
	buf.append(siteName(site));
	buf.append(" ");
	buf.appendInt((int32_t)error);
	showPopup(buf.c_str());
}

void LevelerJob::succeed() {
	sourceFile.reset();
	destFile.reset();
	if (chunkBuffer) {
		delugeDealloc(chunkBuffer);
		chunkBuffer = nullptr;
	}
	phase = Phase::IDLE;

	soundEditor.cutSound();
	AudioEngine::unassignAllVoices();

	AudioFileHolder* holder = soundEditor.getCurrentAudioFileHolder();
	if (holder) {
		holder->setAudioFile(nullptr);
	}
	if (!finalPath.isEmpty()) {
		audioFileManager.tryToDeleteAudioFileFromMemoryIfItExists(finalPath.get());
	}

	Error error = Error::NONE;
	Sample* newSample = nullptr;
	if (!finalPath.isEmpty()) {
		newSample = (Sample*)audioFileManager.getAudioFileFromFilename(&finalPath, true, &error, nullptr, AudioFileType::SAMPLE);
	}
	if (newSample) {
		if (holder) {
			holder->filePath.set(&finalPath);
			holder->setAudioFile(newSample, false, true);
		}
		sampleBrowser.lastFilePathLoaded.set(&finalPath);
	}

	showPopup(overwriteTarget_ ? "OVERWRITTEN" : "LEVELED");
}
