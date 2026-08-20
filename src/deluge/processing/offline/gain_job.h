/*
 * Copyright © 2023 Synthstrom Audible Limited. All rights reserved.
 *
 * Offline sample manual gain job.
 *
 * Copies a sample's audio file from the SD card to a new file on the card,
 * applying user-specified decibel gain adjustment (from -6.00 dB to +6.00 dB)
 * to the PCM data on the way.
 */

#pragma once

#include "definitions_cxx.hpp"
#include "fatfs/fatfs.hpp"
#include "util/d_string.h"
#include <cstdint>
#include <optional>

class Sample;

class GainJob {
public:
	GainJob() = default;

	/// Returns false (and shows a popup explaining why) if the job could not
	/// be started. On success, the job proceeds via tick().
	bool start(Sample* sample, float gainDb);

	/// Called regularly from the task scheduler.
	void tick();

	bool isActive() const { return phase != Phase::IDLE; }

private:
	enum class Phase {
		IDLE,
		OPEN,
		COPY_HEADER,
		COPY_DATA,
		FINALIZE,
	};

	enum class Site {
		OP,
		SK,
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
	void applyGainToChunk(uint32_t bytes);
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

	uint32_t audioBytesWritten = 0;

	uint8_t byteDepth = 2;
	float userGainDb_ = 0.0f;
	float linearGain_ = 1.0f;

	std::optional<FatFS::File> sourceFile;
	std::optional<FatFS::File> destFile;

	uint8_t* chunkBuffer = nullptr;
};

/// Global singleton, ticked from the task scheduler.
GainJob& gainJob();
