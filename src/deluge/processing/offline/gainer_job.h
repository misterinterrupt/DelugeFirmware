/*
 * Copyright © 2026 Synthstrom Audible Limited. All rights reserved.
 *
 * Gainer DSP Job header.
 */

#pragma once

#include "definitions_cxx.hpp"
#include "fatfs/fatfs.hpp"
#include "util/d_string.h"
#include <cstdint>
#include <optional>

class Sample;

enum class DistortionType : uint8_t {
	OFF = 0,
	SATURATE,
	FOLD,
	BITCRUSH,
	RECTIFY,
	FM_FEEDBACK,
	RINGMOD_OVERUNITY,
};

struct DistortionConfig {
	DistortionType type = DistortionType::OFF;
	float param1 = 0.0f;
	float param2 = 0.0f;
	float outputGainDb = 0.0f;
};

class GainerJob {
public:
	enum class Phase {
		IDLE,
		OPEN,
		SCAN,
		SELECT_GAIN,
		COPY_HEADER,
		GAIN_DATA,
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

	bool start(Sample* sample, bool overwrite = false);
	void tick();

	bool isActive() const { return phase != Phase::IDLE; }
	bool isWaitingForGain() const { return phase == Phase::SELECT_GAIN; }

	void setGainDb(float db) { gainDb_ = db; }
	float getGainDb() const { return gainDb_; }

	void setDistortionConfig(DistortionConfig const& config) { distConfig_ = config; }
	DistortionConfig const& getDistortionConfig() const { return distConfig_; }

	void confirmGainAndRun();
	void cancel();
	void reset();

	uint8_t const* getWaveformOverview() const { return waveformOverview; }
	uint32_t getPeakValue() const { return peakValue; }
	float getFilePeakNorm() const {
		if (peakValue == 0) return 1.0f;
		uint64_t maxPossible = (byteDepth == 1) ? 127ULL : (byteDepth == 2) ? 32767ULL : (byteDepth == 3) ? 8388607ULL : 2147483647ULL;
		float peak = (float)peakValue / (float)maxPossible;
		if (peak < 0.001f) peak = 0.001f;
		return peak;
	}

	static float applySampleDistortion(float sample, DistortionConfig const& cfg, uint8_t channel = 0);
	static void resetDistortionState();

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
	uint8_t rawDataFormat = 0;

	float gainDb_ = 0.0f;
	float linearGain_ = 1.0f;
	DistortionConfig distConfig_;
	bool overwriteTarget_ = false;

	uint32_t peakValue = 0;
	uint32_t audioBytesWritten = 0;

	uint8_t waveformOverview[kOverviewColumns]{0};
};

GainerJob& gainerJob();
