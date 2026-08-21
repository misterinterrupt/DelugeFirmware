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

static void showPopup(char const* text) {
	display->displayPopup(text);
}

static void showProgressPopup(uint32_t current, uint32_t total, uint32_t passOffset = 0, uint32_t totalPasses = 1) {
	if (total == 0) {
		return;
	}
	uint32_t passPercent = (current * 100) / total;
	uint32_t overallPercent = (passOffset * 100 + passPercent) / totalPasses;
	DEF_STACK_STRING_BUF(buf, 16);
	buf.appendInt(overallPercent);
	buf.append("%");
	display->displayPopup(buf.c_str(), 1);
}

LevelerJob& levelerJob() {
	static LevelerJob instance;
	return instance;
}

bool LevelerJob::start(Sample* sample, bool overwrite) {
	if (isActive()) {
		showPopup("BUSY");
		return false;
	}

	if (!sample->audioDataLengthBytes || !sample->byteDepth || !sample->audioDataStartPosBytes) {
		showPopup("CANT LEVEL");
		return false;
	}
	if (sample->filePath.isEmpty()) {
		showPopup("CANT LEVEL");
		return false;
	}

	if (sample->byteDepth < 1 || sample->byteDepth > 4) {
		showPopup("BAD DEPTH");
		return false;
	}

	uint64_t total = sample->audioDataLengthBytes;
	if (total > 0xFFFFFFFFu) {
		showPopup("CANT LEVEL");
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
	rawDataFormat = sample->rawDataFormat;
	numChannels = sample->numChannels > 0 ? sample->numChannels : 1;

	uint32_t frameSize = byteDepth * numChannels;
	dataChunkSize = (kChunkSize / frameSize) * frameSize;

	peakValue = 0;
	audioBytesWritten = 0;
	std::memset(waveformOverview, 0, sizeof(waveformOverview));

	phase = Phase::OPEN;
	showPopup("SCANNING...");
	return true;
}

void LevelerJob::confirmThresholdAndRun() {
	if (phase == Phase::SELECT_THRESHOLD) {
		beginCopyPass();
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
		auto opened = FatFS::File::open(sourcePath.get(), FA_READ);
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
		if (w2.value() != r1->size()) {
			fail("WRITE ERR");
			return;
		}

		headerBytesRemaining -= w2.value();
		if (headerBytesRemaining == 0) {
			phase = Phase::LEVEL_DATA;
			dataBytesRemaining = totalDataBytes;
		}
		return;
	}

	case Phase::LEVEL_DATA: {
		uint32_t chunk = (dataBytesRemaining < dataChunkSize) ? dataBytesRemaining : dataChunkSize;
		if (chunk != 0) {
			auto readRes = sourceFile->read({(std::byte*)chunkBuffer, chunk});
			if (!readRes) {
				failIO(Site::R3, readRes.error());
				return;
			}
			uint32_t got = readRes->size();
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
		uint32_t riffSize = audioBytesWritten + dataStart - 8;
		auto w1 = destFile->write({(std::byte*)&riffSize, 4});
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

void LevelerJob::scanChunkForPeak(uint32_t bytesRead) {
	uint32_t frameSize = byteDepth * numChannels;
	uint32_t numFrames = bytesRead / frameSize;

	uint32_t bytesScannedSoFar = totalDataBytes - dataBytesRemaining;

	for (size_t i = 0; i < numFrames; i++) {
		uint8_t const* p = chunkBuffer + (i * frameSize);
		for (size_t c = 0; c < numChannels; c++) {
			int32_t sampleVal = 0;
			if (byteDepth == 1) {
				int32_t raw = (int32_t)p[0] - 128;
				sampleVal = (raw < 0) ? -raw : raw;
			}
			else if (byteDepth == 2) {
				int32_t raw = (int16_t)(p[0] | (p[1] << 8));
				sampleVal = (raw < 0) ? -raw : raw;
			}
			else if (byteDepth == 3) {
				int32_t raw = (int32_t)(p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16));
				if (raw & 0x800000) {
					raw |= 0xFF000000;
				}
				sampleVal = (raw < 0) ? -raw : raw;
			}
			else if (byteDepth == 4) {
				if (rawDataFormat == RAW_DATA_FLOAT) {
					float f;
					memcpy(&f, p, 4);
					sampleVal = (int32_t)(fabsf(f) * 2147483647.0f);
				}
				else {
					int32_t raw = (int32_t)(p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24));
					sampleVal = (raw == (int32_t)0x80000000) ? 2147483647 : ((raw < 0) ? -raw : raw);
				}
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
				uint64_t maxPossible = (byteDepth == 1) ? 127ULL : (byteDepth == 2) ? 32767ULL : (byteDepth == 3) ? 8388607ULL : 2147483647ULL;
				uint32_t scaledPeak = (uint32_t)(((uint64_t)val * 32ULL) / maxPossible);
				if (scaledPeak > 32) {
					scaledPeak = 32;
				}
				if (scaledPeak > waveformOverview[col]) {
					waveformOverview[col] = (uint8_t)scaledPeak;
				}
			}
		}
	}
}

void LevelerJob::beginCopyPass() {
	char const* src = sourcePath.get();
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
			fail("SD ERR");
			return;
		}

		finalPath.set(&finalPathScratch);
		tempPath.set(&tempPathScratch);
	}

	auto rewindRes = sourceFile->lseek(0);
	if (!rewindRes) {
		failIO(Site::SK, rewindRes.error());
		return;
	}

	auto destOpened = FatFS::File::open(tempPath.get(), FA_CREATE_ALWAYS | FA_WRITE);
	if (!destOpened) {
		failIO(Site::OP, destOpened.error());
		return;
	}
	destFile.emplace(std::move(*destOpened));

	phase = Phase::COPY_HEADER;
	headerBytesRemaining = dataStart;
}

void LevelerJob::processChunk(uint8_t* buffer, uint32_t bytes, uint8_t depth, uint8_t channels) {
	float targetMax = (depth == 1) ? 127.0f : (depth == 2) ? 32767.0f : (depth == 3) ? 8388607.0f : 2147483647.0f;
	float threshVal = targetMax * powf(10.0f, thresholdDb_ / 20.0f);
	if (threshVal < 1.0f) {
		threshVal = 1.0f;
	}

	float peakDiff = (float)peakValue - threshVal;
	float maxCompressedPeak = (peakDiff > 0.0f) ? (threshVal + peakDiff * 0.25f) : threshVal;
	float makeupGain = targetMax / maxCompressedPeak;

	uint32_t frameSize = depth * channels;
	uint32_t numFrames = bytes / frameSize;

	for (size_t i = 0; i < numFrames; i++) {
		uint8_t* p = buffer + (i * frameSize);
		for (size_t c = 0; c < channels; c++) {
			int32_t val = 0;
			float normVal = 0.0f;

			if (depth == 1) {
				val = (int16_t)p[0] - 128;
				normVal = (float)val;
			}
			else if (depth == 2) {
				val = (int16_t)(p[0] | (p[1] << 8));
				normVal = (float)val;
			}
			else if (depth == 3) {
				val = (int32_t)(p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16));
				if (val & 0x800000) {
					val |= 0xFF000000;
				}
				normVal = (float)val;
			}
			else if (depth == 4) {
				if (rawDataFormat == RAW_DATA_FLOAT) {
					float f;
					memcpy(&f, p, 4);
					normVal = f * 2147483647.0f;
				}
				else {
					val = (int32_t)(p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24));
					normVal = (float)val;
				}
			}

			float absF = fabsf(normVal);
			float sign = (normVal >= 0.0f) ? 1.0f : -1.0f;

			float compressedF = absF;
			if (absF > threshVal && peakDiff > 0.0f) {
				compressedF = threshVal + (absF - threshVal) * 0.25f;
			}

			float finalF = compressedF * makeupGain * sign;

			if (finalF > targetMax) {
				finalF = targetMax;
			}
			else if (finalF < -targetMax) {
				finalF = -targetMax;
			}

			if (depth == 1) {
				int32_t outVal = (int32_t)lroundf(finalF) + 128;
				if (outVal > 255) outVal = 255;
				else if (outVal < 0) outVal = 0;
				p[0] = (uint8_t)outVal;
			}
			else if (depth == 2) {
				int32_t outVal = (int32_t)lroundf(finalF);
				if (outVal > 32767) outVal = 32767;
				else if (outVal < -32768) outVal = -32768;
				p[0] = (uint8_t)(outVal & 0xFF);
				p[1] = (uint8_t)((outVal >> 8) & 0xFF);
			}
			else if (depth == 3) {
				int32_t outVal = (int32_t)lroundf(finalF);
				if (outVal > 8388607) outVal = 8388607;
				else if (outVal < -8388608) outVal = -8388608;
				p[0] = (uint8_t)(outVal & 0xFF);
				p[1] = (uint8_t)((outVal >> 8) & 0xFF);
				p[2] = (uint8_t)((outVal >> 16) & 0xFF);
			}
			else if (depth == 4) {
				if (rawDataFormat == RAW_DATA_FLOAT) {
					float outF = finalF / 2147483647.0f;
					memcpy(p, &outF, 4);
				}
				else {
					int64_t outVal = (int64_t)lroundf(finalF);
					if (outVal > 2147483647LL) outVal = 2147483647LL;
					else if (outVal < -2147483648LL) outVal = -2147483648LL;
					p[0] = (uint8_t)(outVal & 0xFF);
					p[1] = (uint8_t)((outVal >> 8) & 0xFF);
					p[2] = (uint8_t)((outVal >> 16) & 0xFF);
					p[3] = (uint8_t)((outVal >> 24) & 0xFF);
				}
			}

			p += depth;
		}
	}
}

char const* LevelerJob::siteName(Site site) {
	switch (site) {
	case Site::OP: return "OP";
	case Site::SK: return "SK";
	case Site::R1: return "R1";
	case Site::R2: return "R2";
	case Site::W2: return "W2";
	case Site::R3: return "R3";
	case Site::W3: return "W3";
	case Site::FZ: return "FZ";
	case Site::RN: return "RN";
	}
	return "?";
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
		tempPath.clear();
	}
	finalPath.clear();
	sourcePath.clear();
	phase = Phase::IDLE;
	showPopup(message);
}

void LevelerJob::failIO(Site site, FatFS::Error error) {
	DEF_STACK_STRING_BUF(msg, 16);
	msg.append(siteName(site));
	msg.append("  ");
	msg.appendInt((int32_t)error);

	sourceFile.reset();
	destFile.reset();
	if (chunkBuffer) {
		delugeDealloc(chunkBuffer);
		chunkBuffer = nullptr;
	}
	if (!tempPath.isEmpty()) {
		FatFS::unlink(tempPath.get());
		tempPath.clear();
	}
	finalPath.clear();
	sourcePath.clear();
	phase = Phase::IDLE;
	showPopup(msg.c_str());
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
	audioFileManager.tryToDeleteAudioFileFromMemoryIfItExists(finalPath.get());

	Error error = Error::NONE;
	Sample* newSample =
	    (Sample*)audioFileManager.getAudioFileFromFilename(&finalPath, true, &error, nullptr, AudioFileType::SAMPLE);
	if (newSample) {
		if (holder) {
			holder->filePath.set(&finalPath);
			holder->setAudioFile(newSample, false, true);
		}
		sampleBrowser.lastFilePathLoaded.set(&finalPath);
	}

	tempPath.clear();
	finalPath.clear();
	sourcePath.clear();

	if (overwriteTarget_) {
		showPopup("OVERWRITTEN");
	}
	else {
		showPopup("LEVELED");
	}
}
