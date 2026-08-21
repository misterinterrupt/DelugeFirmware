/*
 * Copyright © 2023 Synthstrom Audible Limited. All rights reserved.
 *
 * Reverse DSP Job implementation.
 */

#include "processing/engines/audio_engine.h"
#include "processing/offline/reverse_job.h"

#include "definitions_cxx.hpp"
#include "fatfs/fatfs.hpp"
#include "gui/menu_item/offline_operation_menu.h"
#include "gui/ui/browser/sample_browser.h"
#include "gui/ui/sound_editor.h"
#include "hid/display/display.h"
#include "io/debug/log.h"
#include "memory/memory_allocator_interface.h"
#include "model/sample/sample.h"
#include "processing/sound/sound.h"
#include "storage/audio/audio_file_holder.h"
#include "storage/audio/audio_file_manager.h"
#include <algorithm>
#include <cstring>
#include <utility>

extern "C" {
extern uint8_t currentlyAccessingCard;
}

static void showPopup(char const* text) {
	display->displayPopup(text);
}

static void showProgressPopup(uint32_t current, uint32_t total) {
	if (total == 0) {
		return;
	}
	uint32_t percent = (current * 100) / total;
	DEF_STACK_STRING_BUF(buf, 16);
	buf.appendInt(percent);
	buf.append("%");
	display->displayPopup(buf.c_str(), 1);
}

ReverseJob& reverseJob() {
	static ReverseJob instance;
	return instance;
}

bool ReverseJob::start(Sample* sample, bool overwrite) {
	if (isActive()) {
		showPopup("BUSY");
		return false;
	}

	if (!sample->audioDataLengthBytes || !sample->byteDepth || !sample->audioDataStartPosBytes) {
		showPopup("CANT REV");
		return false;
	}
	if (sample->filePath.isEmpty()) {
		showPopup("CANT REV");
		return false;
	}

	if ((sample->byteDepth != 2 && sample->byteDepth != 3) || sample->rawDataFormat != 0) {
		showPopup("CANT REV");
		return false;
	}

	uint64_t total = sample->audioDataLengthBytes;
	if (total > 0xFFFFFFFFu) {
		showPopup("CANT REV");
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
	chunksSinceProgressPopup = 0;
	audioBytesWritten = 0;

	phase = Phase::OPEN;
	showPopup("REVERSE...");
	return true;
}

void ReverseJob::tick() {
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
		auto opened = FatFS::File::open(sourcePath.get(), FA_READ);
		if (!opened) {
			failIO(Site::OP, opened.error());
			return;
		}
		sourceFile.emplace(std::move(*opened));
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
		if (w2.value() != r1->size()) {
			fail("WRITE ERR");
			return;
		}

		headerBytesRemaining -= w2.value();
		if (headerBytesRemaining == 0) {
			phase = Phase::REVERSE_DATA;
			dataBytesRemaining = totalDataBytes;
			chunksSinceProgressPopup = 0;
		}
		return;
	}

	case Phase::REVERSE_DATA: {
		uint32_t frameSize = byteDepth * numChannels;
		uint32_t chunk = dataChunkSize;
		if (chunk > dataBytesRemaining) {
			chunk = (dataBytesRemaining / frameSize) * frameSize;
		}

		if (chunk != 0) {
			uint32_t readOffset = dataStart + dataBytesRemaining - chunk;
			auto seekRes = sourceFile->lseek(readOffset);
			if (!seekRes) {
				failIO(Site::SK, seekRes.error());
				return;
			}

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
				got = (got / frameSize) * frameSize;
				reverseChunk(chunkBuffer, got, byteDepth, numChannels);

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

		showProgressPopup(totalDataBytes - dataBytesRemaining, totalDataBytes);

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

void ReverseJob::beginCopyPass() {
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
			candidateBuf.append("_");
			char suffixStr[2] = {c, '\0'};
			candidateBuf.append(suffixStr);
			candidateBuf.append(ext);

			auto testFile = FatFS::File::open(candidateBuf.c_str(), FA_READ);
			if (!testFile) {
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

				auto testFile = FatFS::File::open(candidateBuf.c_str(), FA_READ);
				if (!testFile) {
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

void ReverseJob::reverseChunk(uint8_t* buffer, uint32_t bytes, uint8_t depth, uint8_t channels) {
	uint32_t frameSize = depth * channels;
	uint32_t numFrames = bytes / frameSize;
	if (numFrames <= 1) {
		return;
	}

	uint8_t tempFrame[16];
	for (size_t i = 0; i < numFrames / 2; i++) {
		uint8_t* left = buffer + (i * frameSize);
		uint8_t* right = buffer + ((numFrames - 1 - i) * frameSize);
		std::memcpy(tempFrame, left, frameSize);
		std::memcpy(left, right, frameSize);
		std::memcpy(right, tempFrame, frameSize);
	}
}

char const* ReverseJob::siteName(Site site) {
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

void ReverseJob::fail(char const* message) {
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

void ReverseJob::failIO(Site site, FatFS::Error error) {
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

void ReverseJob::succeed() {
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
	deluge::gui::menu_item::gOfflineWaveformCache.invalidate();

	if (overwriteTarget_) {
		showPopup("OVERWRITTEN");
	}
	else {
		showPopup("REV DONE");
	}
}
