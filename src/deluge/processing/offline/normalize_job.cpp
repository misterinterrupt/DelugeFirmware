/*
 * Copyright © 2023 Synthstrom Audible Limited. All rights reserved.
 */

#include "processing/offline/normalize_job.h"

#include "definitions_cxx.hpp"
#include "fatfs/fatfs.hpp"
#include "hid/display/display.h"
#include "io/debug/log.h"
#include "model/sample/sample.h"
#include "storage/audio/audio_file_holder.h"
#include "storage/audio/audio_file_manager.h"
#include "memory/memory_allocator_interface.h"

extern "C" {
// TEMPORARY instrumentation from src/RZA1/diskio.c - see the comment block
// there. lastSdDriverError is the SDHI driver's own code for the last
// transaction (negative SD_ERR_*), sdReentryDetected counts card IOs that
// began while another was in flight (the E258/E259 condition).
extern volatile int lastSdDriverError;
extern volatile int sdReentryDetected;
// Stock diskio.c flag: set while a card transaction is in flight (sd_read_sect
// / sd_write_sect). The stock E258/E259 guard only freezes on alpha builds;
// in release it is silently ignored - which is how overlapping transactions
// wedge the SDHI controller (SD_ERR_CBSY_ERROR, observed as R1 1169E).
extern uint8_t currentlyAccessingCard;
// Stock FatFS port: re-initializes the SDHI driver and clears diskStatus.
// The recovery path for a wedged controller (same re-init the card-insert
// interrupt path performs).
extern int disk_initialize(unsigned char pdrv);
}

namespace {

void showPopup(char const* text) {
	display->displayPopup(text);
}

void showProgressPopup(uint32_t bytesDone, uint32_t bytesTotal) {
	if (bytesTotal == 0) {
		return;
	}
	uint32_t percent = bytesDone * 100 / bytesTotal;
	if (percent > 99) {
		percent = 99;
	}
	DEF_STACK_STRING_BUF(buffer, 16);
	buffer.append("NORM ");
	buffer.appendInt(percent);
	display->displayPopup(buffer.c_str());
}

} // namespace

NormalizeJob& normalizeJob() {
	static NormalizeJob instance;
	return instance;
}

bool NormalizeJob::start(Sample* sample) {
	if (isActive()) {
		showPopup("BUSY");
		return false;
	}

	// Basic sanity checks on the sample.
	if (!sample->audioDataLengthBytes || !sample->byteDepth || !sample->audioDataStartPosBytes) {
		showPopup("CANT NORM");
		return false;
	}
	if (sample->filePath.isEmpty()) {
		showPopup("CANT NORM");
		return false;
	}

	// We only know how to shift standard little-endian 16-bit or 24-bit data.
	// (rawDataFormat != 0 means the file's raw bytes are not that - e.g. float
	// or wrong-endian data - so refuse those rather than corrupt them.)
	if ((sample->byteDepth != 2 && sample->byteDepth != 3) || sample->rawDataFormat != 0) {
		showPopup("CANT NORM");
		return false;
	}

	uint64_t total = sample->audioDataLengthBytes;
	if (total > 0xFFFFFFFFu) {
		showPopup("CANT NORM");
		return false;
	}

	// Allocate the I/O buffer from the general allocator (external RAM).
	// Freed on every exit path (fail() and succeed()).
	chunkBuffer = (uint8_t*)delugeAlloc(kChunkSize, false);
	if (!chunkBuffer) {
		showPopup("MEMORY");
		return false;
	}

	// Copy everything we need out of the Sample. From here on, the Sample may
	// be destroyed at any time without affecting us.
	//
	// NOTE: no card access here! selectButtonPress() runs in UI button
	// context; opening the file happens in tick() (scheduler context), which
	// is the firmware's sanctioned window for SD work. Opening here would
	// collide with in-flight cluster loads - e.g. right after a sample has
	// just been loaded from the browser.
	sourcePath.set(&sample->filePath); // Cannot fail (see SampleRecorder)

	dataStart = (uint32_t)sample->audioDataStartPosBytes;
	totalDataBytes = (uint32_t)total;
	dataBytesRemaining = totalDataBytes;
	headerBytesRemaining = dataStart;
	byteDepth = sample->byteDepth;
	dataChunkSize = (kChunkSize / byteDepth) * byteDepth;
	shiftAmount = 0;
	peakValue = 0;
	chunksSinceProgressPopup = 0;
	audioBytesWritten = 0;

	// TEMPORARY instrumentation: reset the diskio.c counters so the numbers
	// we report at failure time refer to this job only.
	lastSdDriverError = 0;
	sdReentryDetected = 0;

	phase = Phase::OPEN;
	showPopup("NORM...");
	return true;
}

void NormalizeJob::tick() {
	// Gate: only touch the card when the audio engine doesn't want it.
	// disk_read() (which every f_read/f_write funnels through) pumps the
	// cluster loader first, so doing our I/O while cluster loads are pending
	// re-enters the SDHI driver state and produces genuine FatFS errors
	// (observed as READ ERR). Standing down whenever the load queue is
	// non-empty keeps the card exclusively the loader's until it drains.
	// FINALIZE is exempt: it only patches the output header (a few bytes)
	// and finishing is better than a stranded temp file.
	// Also stand down while a card transaction is actually in flight
	// (currentlyAccessingCard, set by diskio.c around sd_read_sect /
	// sd_write_sect). The load-queue check above catches *pending* cluster
	// loads; this catches one already handed to the driver - the exact
	// overlap that wedged the controller (see E258/E259).
	// TEMPORARY instrumentation: when a gate stands us down, say which one
	// (once per 100 stand-downs, so the popup does not fight the progress
	// display). WAIT 1 = cluster-load queue non-empty; WAIT 2 = a card
	// transaction is in flight. If neither ever appears while NORM sits
	// still, the job is starved for a different reason and we look at the
	// scheduler registration instead.
	if (phase != Phase::IDLE && phase != Phase::FINALIZE) {
		if (audioFileManager.loadingQueue.getNumElements() != 0) {
			standDownCount++;
			if ((standDownCount % 100) == 1) {
				showPopup("WAIT 1");
			}
			return;
		}
		if (currentlyAccessingCard) {
			standDownCount++;
			if ((standDownCount % 100) == 1) {
				showPopup("WAIT 2");
			}
			return;
		}
	}

	switch (phase) {

		case Phase::IDLE:
			return;

		case Phase::OPEN: {
			// Open the source file and jump to the start of its audio data.
			// This runs in tick() (scheduler/card-routine context) - never
			// from the UI - so it cannot collide with cluster loading.
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

			// The header's data length is only a claim. Deluge-recorded
			// files routinely have lengths that don't line up with the bytes
			// actually on disk, so clamp to the real file size and process
			// only whole sample frames from there.
			uint32_t fileSize = (uint32_t)sourceFile->size();
			if (fileSize <= dataStart) {
				fail("CANT NORM"); // no audio data at all
				return;
			}
			uint32_t bytesOnDisk = fileSize - dataStart;
			if (bytesOnDisk < totalDataBytes) {
				totalDataBytes = bytesOnDisk;
			}
			totalDataBytes = (totalDataBytes / byteDepth) * byteDepth; // whole frames only
			if (totalDataBytes == 0) {
				fail("SILENCE");
				return;
			}
			dataBytesRemaining = totalDataBytes;

			phase = Phase::SCAN;
			return;
		}

		case Phase::SCAN: {
			// Scan one frame-aligned chunk for the true peak value.
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
					// File is shorter than its header claimed - scan only
					// what actually exists.
					totalDataBytes -= dataBytesRemaining;
					dataBytesRemaining = 0;
				}
				else {
					uint32_t usable = (got / byteDepth) * byteDepth;
					scanChunkForPeak(usable);
					dataBytesRemaining -= usable;

					// Occasional progress popup, same cadence as the copy pass.
					chunksSinceProgressPopup++;
					if (chunksSinceProgressPopup >= (65536 / dataChunkSize)) {
						chunksSinceProgressPopup = 0;
						showProgressPopup(totalDataBytes - dataBytesRemaining, totalDataBytes);
					}
				}
			}
			if (dataBytesRemaining == 0) {
				beginCopyPass();
			}
			return;
		}

		case Phase::COPY_HEADER: {
			// Copy header bytes verbatim.
			uint32_t chunk = kChunkSize;
			if (chunk > headerBytesRemaining) {
				chunk = headerBytesRemaining;
			}
			auto readRes = sourceFile->read({(std::byte*)chunkBuffer, chunk});
			if (!readRes) {
				failIO(Site::R2, readRes.error());
				return;
			}
			uint32_t got = readRes->size();
			if (got == 0) {
				// Source ended before the header did - copy what exists.
				headerBytesRemaining = 0;
				phase = Phase::COPY_DATA;
				return;
			}
			auto writeRes = destFile->write({(std::byte*)chunkBuffer, got});
			if (!writeRes) {
				failIO(Site::W2, writeRes.error());
				return;
			}
			if (*writeRes != got) {
				fail("WRITE ERR");
				return;
			}
			headerBytesRemaining -= got;
			if (headerBytesRemaining == 0) {
				phase = Phase::COPY_DATA;
			}
			return;
		}

		case Phase::COPY_DATA: {
			// Copy one frame-aligned chunk of audio data, shifting it up.
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
					// File ran out early - finish with what we got.
					dataBytesRemaining = 0;
					phase = Phase::FINALIZE;
					return;
				}
				uint32_t usable = (got / byteDepth) * byteDepth;
				applyShiftToChunk(usable);
				auto writeRes = destFile->write({(std::byte*)chunkBuffer, usable});
				if (!writeRes) {
					failIO(Site::W3, writeRes.error());
					return;
				}
				if (*writeRes != usable) {
					fail("WRITE ERR");
					return;
				}
				dataBytesRemaining -= usable;
				audioBytesWritten += usable;
			}

			// Occasional progress popup, roughly once per 64KB.
			chunksSinceProgressPopup++;
			if (chunksSinceProgressPopup >= (65536 / dataChunkSize)) {
				chunksSinceProgressPopup = 0;
				showProgressPopup(totalDataBytes - dataBytesRemaining, totalDataBytes);
			}

			if (dataBytesRemaining == 0) {
				phase = Phase::FINALIZE;
			}
			return;
		}

		case Phase::FINALIZE: {
			// Patch the output header to the true sizes so the file we leave
			// behind is self-consistent - the same fields
			// SampleRecorder::updateDataLengthInFirstCluster maintains for
			// its own recordings: RIFF chunk size at offset 4, data chunk
			// size at (dataStart - 4). With Deluge-recorded sources the
			// honest length can differ from what the copied header claims.
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
			destFile.reset(); // destructor closes the file
			sourceFile.reset();
			if (!synced) {
				failIO(Site::FZ, synced.error());
				return;
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
	// Work out the normalization shift from the true peak we just found.
	if (peakValue == 0) {
		fail("SILENCE");
		return;
	}
	int32_t shift = __builtin_clz(peakValue);
	if (shift <= 0) {
		// Already at full scale - nothing to gain.
		fail("NO GAIN");
		return;
	}
	shiftAmount = shift;

	// Work out destination paths: write to a temp file, rename at the end
	// (same pattern as the audio recorder).
	String tempPathScratch;
	uint32_t fileNum = 0;
	Error error = audioFileManager.getUnusedAudioRecordingFilePath(
	    &finalPath, &tempPathScratch, AudioRecordingFolder::RESAMPLE, &fileNum, "NORM", nullptr);
	if (error != Error::NONE || finalPath.isEmpty() || tempPathScratch.isEmpty()) {
		fail("SD ERR");
		return;
	}
	tempPath.set(&tempPathScratch); // Cannot fail (see SampleRecorder)

	// Rewind the source to the very start of the file for the copy pass.
	auto rewindRes = sourceFile->lseek(0);
	if (!rewindRes) {
		failIO(Site::SK, rewindRes.error());
		return;
	}

	// Create the destination temp file.
	auto dstOpened = FatFS::File::open(tempPath.get(), FA_CREATE_ALWAYS | FA_WRITE);
	if (!dstOpened) {
		failIO(Site::OP, dstOpened.error());
		return;
	}
	destFile.emplace(std::move(*dstOpened));

	phase = Phase::COPY_HEADER;
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
	else { // byteDepth == 3
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
	int32_t limit;
	if (byteDepth == 2) {
		limit = 32767;
		while (p + 2 <= end) {
			int32_t value = (int32_t)(int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
			int32_t shifted = value << shiftAmount;
			if (shifted > limit) {
				shifted = limit;
			}
			else if (shifted < -32768) {
				shifted = -32768;
			}
			p[0] = (uint8_t)(shifted & 0xFF);
			p[1] = (uint8_t)((shifted >> 8) & 0xFF);
			p += 2;
		}
	}
	else { // byteDepth == 3
		limit = 8388607;
		while (p + 3 <= end) {
			int32_t value = ((int32_t)p[0]) | ((int32_t)p[1] << 8) | ((int32_t)(int8_t)p[2] << 16);
			int32_t shifted = value << shiftAmount;
			if (shifted > limit) {
				shifted = limit;
			}
			else if (shifted < -8388608) {
				shifted = -8388608;
			}
			p[0] = (uint8_t)(shifted & 0xFF);
			p[1] = (uint8_t)((shifted >> 8) & 0xFF);
			p[2] = (uint8_t)((shifted >> 16) & 0xFF);
			p += 3;
		}
	}
}

char const* NormalizeJob::siteName(Site site) {
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

void NormalizeJob::fail(char const* message) {
	if (destFile) {
		destFile.reset(); // closes
		(void)FatFS::unlink(tempPath.get());
	}
	sourceFile.reset();
	if (chunkBuffer) {
		delugeDealloc(chunkBuffer);
		chunkBuffer = nullptr;
	}
	phase = Phase::IDLE;
	showPopup(message);
}

void NormalizeJob::failIO(Site site, FatFS::Error error) {
	// Temporary instrumentation: distinguish which card operation failed and
	// with which FatFS error, in the popup ("R1  1" = scan read, DISK_ERR)
	// and on UART (Debug builds only - D_PRINTLN compiles out in Release).
	int code = static_cast<int>(error);
	// TEMPORARY: also surface the low-level SDHI driver error (negative
	// SD_ERR_*: -4 response timeout, -5 card timeout, -7 CRC, -9 host
	// timeout) and the re-entry count. Re-entry > 0 here means our card IO
	// overlapped another in-flight card IO - the E258/E259 condition that
	// release builds silently survive.
	D_PRINTLN("NormalizeJob I/O failure: site %s, FatFS error %d", siteName(site), code);
	D_PRINTLN("NormalizeJob SDHI driver error %d, reentries %d", lastSdDriverError, sdReentryDetected);
	DEF_STACK_STRING_BUF(buffer, 16);
	buffer.append(siteName(site));
	buffer.append(" ");
	buffer.appendInt(code);              // FatFS FRESULT, e.g. 1
	buffer.appendInt(lastSdDriverError); // e.g. "R1 1-4": FatFS 1, SDHI -4
	if (sdReentryDetected != 0) {
		// "R1 1-4 E": card IO began while another was already in flight
		// (the E258/E259 condition) at least once since this job started.
		buffer.append(" E");
	}
	fail(buffer.c_str());

	// NOTE: no recovery here. disk_initialize() performs card IO of its
	// own, so calling it from this failure path can overlap a live
	// transaction - the very overlap the gate exists to prevent (observed:
	// the gate+recovery build's second attempt produced a stock SD card
	// error). The gate alone is under test; recovery, if still needed,
	// must be deferred to a clean window in tick(), never from here.
}

void NormalizeJob::succeed() {
	sourceFile.reset();
	if (chunkBuffer) {
		delugeDealloc(chunkBuffer);
		chunkBuffer = nullptr;
	}
	phase = Phase::IDLE;

	showPopup("NORM DONE");
}
