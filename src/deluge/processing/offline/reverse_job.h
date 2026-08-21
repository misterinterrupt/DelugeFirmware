/*
 * Copyright © 2023 Synthstrom Audible Limited. All rights reserved.
 *
 * Reverse DSP Job.
 */

#pragma once

#include "definitions_cxx.hpp"
#include "fatfs/fatfs.hpp"
#include "util/d_string.h"
#include <cstdint>
#include <optional>

class Sample;

class ReverseJob {
public:
	ReverseJob() = default;

	/// Returns false if job could not be started.
	bool start(Sample* sample, bool overwrite = false);

	/// Called regularly from task scheduler.
	void tick();

	bool isActive() const { return phase != Phase::IDLE; }

private:
	enum class Phase {
		IDLE,
		OPEN,
		COPY_HEADER,
		REVERSE_DATA,
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
	void reverseChunk(uint8_t* buffer, uint32_t bytes, uint8_t byteDepth, uint8_t channels);
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
	uint8_t numChannels = 1;
	bool overwriteTarget_ = false;

	std::optional<FatFS::File> sourceFile;
	std::optional<FatFS::File> destFile;

	uint8_t* chunkBuffer = nullptr;
};

/// Global singleton.
ReverseJob& reverseJob();
