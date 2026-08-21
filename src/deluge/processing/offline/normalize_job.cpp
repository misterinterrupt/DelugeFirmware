/*
 * Copyright © 2023 Synthstrom Audible Limited. All rights reserved.
 */

#include "gui/menu_item/offline_operation_menu.h"
#include "processing/engines/audio_engine.h"
#include "processing/offline/normalize_job.h"

#include "definitions_cxx.hpp"
#include "fatfs/fatfs.hpp"
#include "gui/ui/browser/sample_browser.h"
#include "gui/ui/sound_editor.h"
#include "hid/display/display.h"
#include "io/debug/log.h"
#include "memory/memory_allocator_interface.h"
#include "model/sample/sample.h"
#include "processing/sound/sound.h"
#include "storage/audio/audio_file_holder.h"
#include "storage/audio/audio_file_manager.h"
#include <cmath>
#include <utility>

extern "C" {
extern volatile int lastSdDriverError;
extern volatile int sdReentryDetected;
extern uint8_t currentlyAccessingCard;
extern int disk_initialize(unsigned char pdrv);
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

NormalizeJob& normalizeJob() {
	static NormalizeJob instance;
	return instance;
}

bool NormalizeJob::start(Sample* sample, float targetCeilingDb, bool overwrite) {
	if (isActive()) {
		showPopup("BUSY");
		return false;
	}

	if (!sample->audioDataLengthBytes || !sample->byteDepth || !sample->audioDataStartPosBytes) {
		showPopup("CANT NORM");
		return false;
	}
	if (sample->filePath.isEmpty()) {
		showPopup("CANT NORM");
		return false;
	}

	if ((sample->byteDepth != 2 && sample->byteDepth != 3) || sample->rawDataFormat != 0) {
		showPopup("CANT NORM");
		return false;
	}

	uint64_t total = sample->audioDataLengthBytes;
	if (total > 0xFFFFFFFFu) {
		showPopup("CANT NORM");
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
	targetCeilingDb_ = targetCeilingDb;
	overwriteTarget_ = overwrite;

	dataStart = (uint32_t)sample->audioDataStartPosBytes;
	totalDataBytes = (uint32_t)total;
	dataBytesRemaining = totalDataBytes;
	headerBytesRemaining = dataStart;
	byteDepth = sample->byteDepth;
	dataChunkSize = (kChunkSize / byteDepth) * byteDepth;
	chunksSinceProgressPopup = 0;
	audioBytesWritten = 0;

	peakValue = 0;
	linearGain_ = 1.0f;

	lastSdDriverError = 0;
	sdReentryDetected = 0;

	phase = Phase::OPEN;
	showPopup("NORM...");
	return true;
}

void NormalizeJob::tick() {
	if (phase == Phase::IDLE) {
		return;
	}

	if (currentlyAccessingCard) {
		return;
	}

	switch (phase) {

	case Phase::IDLE:
		return;

	case Phase::OPEN: {
		auto srcOpened = FatFS::File::open(sourcePath.get(), FA_READ);
		if (!srcOpened) {
			failIO(Site::OP, srcOpened.error());
			return;
		}
		sourceFile.emplace(std::move(*srcOpened));
		auto seekRes = sourceFile->lseek(dataStart);
		if (!seekRes) {
			failIO(Site::SK, seekRes.error());
			return;
		}

		uint32_t fileSize = (uint32_t)sourceFile->size();
		if (fileSize <= dataStart) {
			fail("CANT NORM");
			return;
		}
		uint32_t bytesOnDisk = fileSize - dataStart;
		if (bytesOnDisk < totalDataBytes) {
			totalDataBytes = bytesOnDisk;
		}
		totalDataBytes = (totalDataBytes / byteDepth) * byteDepth;
		if (totalDataBytes == 0) {
			fail("SILENCE");
			return;
		}
		dataBytesRemaining = totalDataBytes;

		phase = Phase::SCAN;
		return;
	}

	case Phase::SCAN: {
		uint32_t chunk = dataChunkSize;
		if (chunk > dataBytesRemaining) {
			chunk = (dataBytesRemaining / byteDepth) * byteDepth;
		}
		if (chunk != 0) {
			auto readRes = sourceFile->read({(std::byte*)chunkBuffer, chunk});
			if (!readRes) {
				failIO(Site::R1, readRes.error());
				return;
			}
			uint32_t got = readRes->size();
			if (got == 0) {
				totalDataBytes -= dataBytesRemaining;
				dataBytesRemaining = 0;
			}
			else {
				got = (got / byteDepth) * byteDepth;
				scanChunkForPeak(got);
				dataBytesRemaining -= got;

				showProgressPopup(totalDataBytes - dataBytesRemaining, totalDataBytes, 0, 2);
			}
		}

		if (dataBytesRemaining == 0) {
			beginCopyPass();
		}
		return;
	}

	case Phase::COPY_HEADER: {
		uint32_t chunk = kChunkSize;
		if (chunk > headerBytesRemaining) {
			chunk = headerBytesRemaining;
		}
		if (chunk != 0) {
			auto readRes = sourceFile->read({(std::byte*)chunkBuffer, chunk});
			if (!readRes) {
				failIO(Site::R2, readRes.error());
				return;
			}
			if (readRes->size() == 0) {
				fail("READ ERR");
				return;
			}
			auto writeRes = destFile->write({(std::byte*)chunkBuffer, readRes->size()});
			if (!writeRes) {
				failIO(Site::W2, writeRes.error());
				return;
			}
			if (*writeRes != readRes->size()) {
				fail("WRITE ERR");
				return;
			}
			headerBytesRemaining -= *writeRes;
		}

		if (headerBytesRemaining == 0) {
			phase = Phase::COPY_DATA;
			dataBytesRemaining = totalDataBytes;
			chunksSinceProgressPopup = 0;
		}
		return;
	}

	case Phase::COPY_DATA: {
		uint32_t chunk = dataChunkSize;
		if (chunk > dataBytesRemaining) {
			chunk = (dataBytesRemaining / byteDepth) * byteDepth;
		}
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
				got = (got / byteDepth) * byteDepth;
				applyShiftToChunk(got);

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

void NormalizeJob::beginCopyPass() {
	if (peakValue == 0) {
		fail("SILENCE");
		return;
	}
	float targetMax = (byteDepth == 2) ? 32767.0f : 8388607.0f;
	float normGain = targetMax / (float)peakValue;
	float offsetGain = powf(10.0f, targetCeilingDb_ / 20.0f);
	linearGain_ = normGain * offsetGain;

	if (fabsf(linearGain_ - 1.0f) < 0.0001f) {
		fail("NO GAIN");
		return;
	}

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

	bool found = false;
	String finalPathScratch;
	String tempPathScratch;

	if (overwriteTarget_) {
		finalPath.set(sourcePath.get());
		DEF_STACK_STRING_BUF(tempBuf, 256);
		tempBuf.append(stemBuf);
		tempBuf.append(".TMP");
		tempPath.set(tempBuf.c_str());
	}
	else {

	for (char c = 'A'; c <= 'Z'; c++) {
		DEF_STACK_STRING_BUF(candidateBuf, 256);
		candidateBuf.append(stemBuf);
		candidateBuf.append("_");
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
			tempBuf.append("_");
			tempBuf.append(suffixStr);
			tempBuf.append(".TMP");
			tempPathScratch.set(tempBuf.c_str());

			found = true;
			break;
		}
	}

	if (!found) {
		for (int32_t num = 1; num <= 999; num++) {
			DEF_STACK_STRING_BUF(candidateBuf, 256);
			candidateBuf.append(stemBuf);
			candidateBuf.append("_");
			candidateBuf.appendInt(num);
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
				tempBuf.append("_");
				tempBuf.appendInt(num);
				tempBuf.append(".TMP");
				tempPathScratch.set(tempBuf.c_str());

				found = true;
				break;
			}
		}
	}

		if (!found || finalPathScratch.isEmpty() || tempPathScratch.isEmpty()) {
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

void NormalizeJob::scanChunkForPeak(uint32_t bytes) {
	uint8_t const* p = chunkBuffer;
	uint8_t const* end = chunkBuffer + bytes;
	if (byteDepth == 2) {
		while (p + 2 <= end) {
			int32_t value = (int32_t)(int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
			uint32_t magnitude = (value < 0) ? (uint32_t)(-value) : (uint32_t)value;
			if (magnitude > peakValue) {
				peakValue = magnitude;
			}
			p += 2;
		}
	}
	else {
		while (p + 3 <= end) {
			int32_t value = ((int32_t)p[0]) | ((int32_t)p[1] << 8) | ((int32_t)(int8_t)p[2] << 16);
			uint32_t magnitude = (value < 0) ? (uint32_t)(-value) : (uint32_t)value;
			if (magnitude > peakValue) {
				peakValue = magnitude;
			}
			p += 3;
		}
	}
}

void NormalizeJob::applyShiftToChunk(uint32_t bytes) {
	uint8_t* p = chunkBuffer;
	uint8_t* end = chunkBuffer + bytes;
	float gain = linearGain_;

	if (byteDepth == 2) {
		while (p + 2 <= end) {
			int32_t value = (int32_t)(int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
			int32_t normalized = (int32_t)roundf((float)value * gain);
			if (normalized > 32767) {
				normalized = 32767;
			}
			else if (normalized < -32768) {
				normalized = -32768;
			}
			p[0] = (uint8_t)(normalized & 0xFF);
			p[1] = (uint8_t)((normalized >> 8) & 0xFF);
			p += 2;
		}
	}
	else {
		while (p + 3 <= end) {
			int32_t value = ((int32_t)p[0]) | ((int32_t)p[1] << 8) | ((int32_t)(int8_t)p[2] << 16);
			int32_t normalized = (int32_t)roundf((float)value * gain);
			if (normalized > 8388607) {
				normalized = 8388607;
			}
			else if (normalized < -8388608) {
				normalized = -8388608;
			}
			p[0] = (uint8_t)(normalized & 0xFF);
			p[1] = (uint8_t)((normalized >> 8) & 0xFF);
			p[2] = (uint8_t)((normalized >> 16) & 0xFF);
			p += 3;
		}
	}
}

char const* NormalizeJob::siteName(Site site) {
	switch (site) {
	case Site::OP:
		return "OP";
	case Site::SK:
		return "SK";
	case Site::R1:
		return "R1";
	case Site::R2:
		return "R2";
	case Site::W2:
		return "W2";
	case Site::R3:
		return "R3";
	case Site::W3:
		return "W3";
	case Site::FZ:
		return "FZ";
	case Site::RN:
		return "RN";
	}
	return "?";
}

void NormalizeJob::fail(char const* message) {
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

void NormalizeJob::failIO(Site site, FatFS::Error error) {
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

float NormalizeJob::getGainDb() const {
	if (peakValue == 0 || linearGain_ <= 0.0f) {
		return 0.0f;
	}
	return 20.0f * log10f(linearGain_);
}

void NormalizeJob::succeed() {
	float gainDb = getGainDb();

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

	deluge::gui::menu_item::invalidateSharedWaveformOverview();

	DEF_STACK_STRING_BUF(buffer, 32);
	if (gainDb >= 0.0f) {
		buffer.append("+");
	}
	buffer.appendFloat(gainDb, 2, 2);
	buffer.append(" DB");
	showPopup(buffer.c_str());
}
