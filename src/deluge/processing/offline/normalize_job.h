/*
 * Copyright © 2023 Synthstrom Audible Limited. All rights reserved.
 *
 * Offline sample normalize job.
 *
 * Copies a sample's audio file from the SD card to a new file on the card,
 * applying peak normalization to the PCM data on the way. Work is done in
 * small frame-aligned chunks, one chunk per scheduler tick.
 */

#pragma once

#include "definitions_cxx.hpp"
#include "fatfs/fatfs.hpp"
#include "util/d_string.h"
#include <cstdint>
#include <optional>

class Sample;

class NormalizeJob {
public:
	NormalizeJob() = default;

	/// Returns false (and shows a popup explaining why) if the job could not
	/// be started. On success, the job proceeds via tick().
	bool start(Sample* sample);

	/// Called regularly from the task scheduler. Does at most one chunk of
	/// card I/O per call. Cheap no-op when idle.
	void tick();

	bool isActive() const { return phase != Phase::IDLE; }

	/// Returns the peak headroom / normalization gain in dB (to hundredths of a dB).
	float getGainDb() const;

private:
	enum class Phase {
		IDLE,
		OPEN,
		SCAN,
		COPY_HEADER,
		COPY_DATA,
		FINALIZE,
	};

	enum class Site {
		OP,
		SK,
		R1,
		R2,
		W2,
		R3,
		W3,
		FZ,
		RN,
	};

	static char const* siteName(Site site);

	static constexpr uint32_t kChunkSize = 16384;

	void beginCopyPass();
	void scanChunkForPeak(uint32_t bytes);
	void applyShiftToChunk(uint32_t bytes);
	void fail(char const* message);
	void failIO(Site site, FatFS::Error error);
	void succeed();

	Phase phase = Phase::IDLE;

	String sourcePath;
	String finalPath;
	String tempPath;
	uint32_t dataStart = 0;
	uint32_t totalDataBytes = 0;
	uint32_t dataBytesRemaining = 0;
	uint32_t headerBytesRemaining = 0;
	uint32_t dataChunkSize = 0;
	uint32_t chunksSinceProgressPopup = 0;

	uint32_t standDownCount = 0;
	uint32_t audioBytesWritten = 0;

	uint8_t byteDepth = 2;
	float linearGain_ = 1.0f;

	/// Peak absolute raw-file value found during the SCAN pass.
	uint32_t peakValue = 0;

	std::optional<FatFS::File> sourceFile;
	std::optional<FatFS::File> destFile;

	uint8_t* chunkBuffer = nullptr;
};

/// Global singleton, ticked from the task scheduler.
NormalizeJob& normalizeJob();
