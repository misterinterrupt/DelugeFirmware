/*
 * Copyright © 2023 Synthstrom Audible Limited. All rights reserved.
 *
 * Offline sample normalize job.
 *
 * Copies a sample's audio file from the SD card to a new file on the card,
 * applying bit-shift normalization to the PCM data on the way. Work is done in
 * small frame-aligned chunks, one chunk per scheduler tick, so:
 *
 *  - The UI stays responsive: the menu item that starts the job returns
 *    immediately.
 *  - SD card access happens only from the main-loop scheduler, in small
 *    doses, so it interleaves safely with cluster loading and audio
 *    recording, exactly like every other card user in this firmware.
 *
 * Once started, the job is fully self-contained: every parameter is copied
 * out of the Sample in start(), so the Sample (or its whole song) may be
 * destroyed while the job runs without danger.
 *
 * TEMPORARY INSTRUMENTATION (hunting the "READ ERR" on freshly loaded
 * samples): each card-I/O failure site has its own tag; failIO() shows the
 * tag plus the FatFS error code in the popup (e.g. "R1  1" = scan read,
 * FR_DISK_ERR) and logs both to UART in Debug builds. FatFS codes are FRESULT
 * values: 1=DISK_ERR, 2=INT_ERR, 3=NOT_READY, 4=NO_FILE, 5=NO_PATH,
 * 6=INVALID_NAME, 7=DENIED, 8=EXIST, 9=INVALID_OBJECT, 10=WRITE_PROTECTED,
 * 11=INVALID_DRIVE, 12=NOT_ENABLED, 13=NO_FILESYSTEM, 14=MKFS_ABORTED,
 * 15=TIMEOUT, 16=LOCKED, 17=NOT_ENOUGH_CORE, 18=TOO_MANY_OPEN_FILES,
 * 19=INVALID_PARAMETER.
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

private:
	enum class Phase {
		IDLE,
		OPEN,
		SCAN,
		COPY_HEADER,
		COPY_DATA,
		FINALIZE,
	};

	/// Which card-I/O operation failed. Temporary instrumentation: lets each
	/// failure site be told apart in the popup / UART log.
	enum class Site {
		OP, ///< FatFS::File::open (source or destination)
		SK, ///< lseek
		R1, ///< read in the SCAN pass
		R2, ///< read in the COPY_HEADER pass
		W2, ///< write in the COPY_HEADER pass
		R3, ///< read in the COPY_DATA pass
		W3, ///< write in the COPY_DATA pass
		FZ, ///< finalize (seeks / header writes / sync)
		RN, ///< rename into place
	};

	static char const* siteName(Site site);

	/// Max bytes of card I/O per tick. Comparable to one cluster write, keeps
	/// each tick down in the low-millisecond range.
	static constexpr uint32_t kChunkSize = 16384;

	void beginCopyPass();
	void scanChunkForPeak(uint32_t bytes);
	void applyShiftToChunk(uint32_t bytes);
	void fail(char const* message);
	void failIO(Site site, FatFS::Error error);
	void succeed();

	Phase phase = Phase::IDLE;

	// All of the below are copied from the Sample in start() and never read
	// from the Sample again afterwards.
	String sourcePath;
	String finalPath;
	String tempPath;
	uint32_t dataStart = 0;
	uint32_t totalDataBytes = 0;
	uint32_t dataBytesRemaining = 0;
	uint32_t headerBytesRemaining = 0;
	uint32_t dataChunkSize = 0;
	uint32_t chunksSinceProgressPopup = 0;

	/// TEMPORARY instrumentation: times tick() stood down without doing
	/// card IO (gate 1: cluster loads pending; gate 2: transaction in
	/// flight). Surfaced as occasional "WAIT 1"/"WAIT 2" popups so a
	/// starved job is visible on the device.
	uint32_t standDownCount = 0;

	/// Actual audio bytes written to the destination (frame-aligned). Used at
	/// FINALIZE to write honest RIFF/data sizes into the output header.
	uint32_t audioBytesWritten = 0;

	uint8_t byteDepth = 2;
	int32_t shiftAmount = 0;

	/// Peak absolute raw-file value found during the SCAN pass.
	uint32_t peakValue = 0;

	std::optional<FatFS::File> sourceFile;
	std::optional<FatFS::File> destFile;

	/// Card I/O buffer, allocated from the general memory allocator (external
	/// RAM) in start() and freed on every exit path. Never held while idle -
	/// this is a memory-constrained environment and 16KB of permanent BSS for
	/// an occasional job would be unacceptable.
	uint8_t* chunkBuffer = nullptr;
};

/// Global singleton, ticked from the task scheduler.
NormalizeJob& normalizeJob();
