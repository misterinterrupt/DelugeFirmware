/*
 * Copyright © 2026 Synthstrom Audible Limited. All rights reserved.
 *
 * Reverser DSP Job header.
 */

#pragma once

#include "definitions_cxx.hpp"
#include "fatfs/fatfs.hpp"
#include "util/d_string.h"
#include <cstdint>
#include <optional>

class Sample;

class ReverserJob {
public:
	enum class Phase {
		IDLE,
		OPEN,
		SCAN,
		SELECT_MODE,
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

	struct Slice {
		uint32_t startByte;
		uint32_t endByte;
	};

	static constexpr uint32_t kChunkSize = 4096;
	static constexpr uint32_t kOverviewColumns = 128;
	static constexpr uint32_t kMaxTransients = 32;

	bool start(Sample* sample, bool overwrite = false);
	void tick();

	bool isActive() const { return phase != Phase::IDLE; }
	bool isWaitingForMode() const { return phase == Phase::SELECT_MODE; }

	void setModeParam(int32_t mode) { modeParam_ = mode; } // -1 = DETECT, 0 = FULL, 1..100 = 1%..100% slices
	int32_t getModeParam() const { return modeParam_; }

	void confirmModeAndRun();
	void cancel();

	uint8_t const* getWaveformOverview() const { return waveformOverview; }
	uint32_t getPeakValue() const { return peakValue; }

	uint32_t getTransientCount() const { return transientCount_; }
	uint32_t const* getTransientFrames() const { return transientFrames_; }
	uint32_t getTotalFrames() const { return totalDataBytes / (byteDepth * numChannels); }

private:
	void prepareSlices();
	void beginCopyPass();
	void reverseChunk(uint8_t* buffer, uint32_t bytes, uint8_t depth, uint8_t channels);
	void scanChunkForPeakAndTransients(uint32_t bytesRead);

	char const* siteName(Site site);
	void fail(char const* message);
	void failIO(Site site, FatFS::Error error);
	void succeed();

	Phase phase = Phase::IDLE;

	String sourcePath;
	String tempPath;
	String finalPath;

	std::optional<FatFS::File> sourceFile;
	std::optional<FatFS::File> destFile;

	uint8_t* chunkBuffer = nullptr;

	uint32_t dataStart = 0;
	uint32_t totalDataBytes = 0;
	uint32_t dataBytesRemaining = 0;
	uint32_t headerBytesRemaining = 0;

	uint32_t dataChunkSize = 0;
	uint8_t numChannels = 1;
	uint8_t byteDepth = 2;

	int32_t modeParam_ = 0; // 0 = FULL default
	bool overwriteTarget_ = false;

	uint32_t peakValue = 0;
	uint32_t audioBytesWritten = 0;

	uint8_t waveformOverview[kOverviewColumns]{0};

	uint32_t transientFrames_[kMaxTransients]{0};
	uint32_t transientCount_ = 0;
	uint32_t prevSampleMag_ = 0;

	static constexpr uint32_t kMaxSlices = 128;
	Slice slices_[kMaxSlices];
	uint32_t sliceCount_ = 0;
	uint32_t currentSliceIndex_ = 0;
	uint32_t currentSliceReadByte_ = 0;
};

ReverserJob& reverserJob();
