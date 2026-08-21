/*
 * Copyright © 2026 Synthstrom Audible Limited. All rights reserved.
 *
 * Leveler DSP Job header.
 */

#pragma once

#include "definitions_cxx.hpp"
#include "fatfs/fatfs.hpp"
#include "util/d_string.h"
#include <cstdint>
#include <optional>

class Sample;

class LevelerJob {
public:
	enum class Phase {
		IDLE,
		OPEN,
		SCAN,
		SELECT_THRESHOLD,
		COPY_HEADER,
		LEVEL_DATA,
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

	static constexpr uint32_t kChunkSize = 4096;
	static constexpr uint32_t kOverviewColumns = 128;

	bool setupWaveformPreview(Sample* sample, bool overwrite = false);
	bool start(Sample* sample, bool overwrite = false);
	void tick();

	bool isActive() const { return phase != Phase::IDLE; }
	bool isWaitingForThreshold() const { return phase == Phase::SELECT_THRESHOLD; }

	void setThresholdDb(float db) { thresholdDb_ = db; }
	float getThresholdDb() const { return thresholdDb_; }

	void confirmThresholdAndRun();
	void cancel();

	uint8_t const* getWaveformOverview() const { return waveformOverview; }
	uint32_t getPeakValue() const { return peakValue; }

private:
	void beginCopyPass();
	void processChunk(uint8_t* buffer, uint32_t bytes, uint8_t depth, uint8_t channels);
	void scanChunkForPeak(uint32_t bytesRead);

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

	float thresholdDb_ = -6.0f;
	float linearGain_ = 1.0f;
	bool overwriteTarget_ = false;

	uint32_t peakValue = 0;
	uint32_t audioBytesWritten = 0;

	uint8_t waveformOverview[kOverviewColumns]{0};
};

LevelerJob& levelerJob();
