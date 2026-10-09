#include "model/sync.h"
#include "definitions_cxx.hpp"
#include "util/const_functions.h"

#ifdef IN_UNIT_TESTS
#include "display_mock.h"
#include "function_mocks.h"
#include "song_mock.h"
#else
#include "hid/display/display.h"
#include "gui/l10n/l10n.h"
#include "model/song/song.h"
#include "util/functions.h"
#endif

int32_t wrapSwingIntervalSyncLevel(int32_t value) {
	// This is tricky because we don't want zero: that would be "off".
	return mod(value - 1, NUM_SWING_INTERVALS - 1) + 1;
}

int32_t clampSwingIntervalSyncLevel(int32_t value) {
	if (value < MIN_SWING_INERVAL) {
		return MIN_SWING_INERVAL;
	}
	else if (value > MAX_SWING_INTERVAL) {
		return MAX_SWING_INTERVAL;
	}
	else {
		return value;
	}
}

struct SyncOptionInfo {
	SyncType type;
	SyncLevel level;
};

static const SyncOptionInfo syncOptionsByTime[34] = {
	{SYNC_TYPE_EVEN, SYNC_LEVEL_NONE},      // 0: OFF
	{SYNC_TYPE_DOTTED, SYNC_LEVEL_WHOLE},   // 1: 1/1d
	{SYNC_TYPE_EVEN, SYNC_LEVEL_WHOLE},    // 2: 1/1
	{SYNC_TYPE_TRIPLET, SYNC_LEVEL_WHOLE},  // 3: 1/1t
	{SYNC_TYPE_DOTTED, SYNC_LEVEL_2ND},     // 4: 1/2d
	{SYNC_TYPE_EVEN, SYNC_LEVEL_2ND},       // 5: 1/2
	{SYNC_TYPE_TRIPLET, SYNC_LEVEL_2ND},    // 6: 1/2t
	{SYNC_TYPE_DOTTED, SYNC_LEVEL_4TH},     // 7: 1/4d
	{SYNC_TYPE_EVEN, SYNC_LEVEL_4TH},       // 8: 1/4
	{SYNC_TYPE_TRIPLET, SYNC_LEVEL_4TH},    // 9: 1/4t
	{SYNC_TYPE_DOTTED, SYNC_LEVEL_8TH},     // 10: 1/8d
	{SYNC_TYPE_EVEN, SYNC_LEVEL_8TH},       // 11: 1/8
	{SYNC_TYPE_TRIPLET, SYNC_LEVEL_8TH},    // 12: 1/8t
	{SYNC_TYPE_DOTTED, SYNC_LEVEL_16TH},    // 13: 1/16d
	{SYNC_TYPE_EVEN, SYNC_LEVEL_16TH},      // 14: 1/16
	{SYNC_TYPE_TRIPLET, SYNC_LEVEL_16TH},   // 15: 1/16t
	{SYNC_TYPE_DOTTED, SYNC_LEVEL_32ND},    // 16: 1/32d
	{SYNC_TYPE_EVEN, SYNC_LEVEL_32ND},      // 17: 1/32
	{SYNC_TYPE_TRIPLET, SYNC_LEVEL_32ND},   // 18: 1/32t
	{SYNC_TYPE_DOTTED, SYNC_LEVEL_64TH},    // 19: 1/64d
	{SYNC_TYPE_EVEN, SYNC_LEVEL_64TH},      // 20: 1/64
	{SYNC_TYPE_TRIPLET, SYNC_LEVEL_64TH},   // 21: 1/64t
	{SYNC_TYPE_DOTTED, SYNC_LEVEL_128TH},   // 22: 1/128d
	{SYNC_TYPE_EVEN, SYNC_LEVEL_128TH},     // 23: 1/128
	{SYNC_TYPE_TRIPLET, SYNC_LEVEL_128TH},  // 24: 1/128t
	{SYNC_TYPE_DOTTED, SYNC_LEVEL_256TH},   // 25: 1/256d
	{SYNC_TYPE_EVEN, SYNC_LEVEL_256TH},     // 26: 1/256
	{SYNC_TYPE_TRIPLET, SYNC_LEVEL_256TH},  // 27: 1/256t
	{SYNC_TYPE_DOTTED, SYNC_LEVEL_512TH},   // 28: 1/512d
	{SYNC_TYPE_EVEN, SYNC_LEVEL_512TH},     // 29: 1/512
	{SYNC_TYPE_TRIPLET, SYNC_LEVEL_512TH},  // 30: 1/512t
	{SYNC_TYPE_DOTTED, SYNC_LEVEL_1024TH},  // 31: 1/1024d
	{SYNC_TYPE_EVEN, SYNC_LEVEL_1024TH},    // 32: 1/1024
	{SYNC_TYPE_TRIPLET, SYNC_LEVEL_1024TH}, // 33: 1/1024t
};

enum SyncType syncValueToSyncType(int32_t value) {
	if (value <= 0 || value >= 34) {
		return SYNC_TYPE_EVEN;
	}
	return syncOptionsByTime[value].type;
}

enum SyncLevel syncValueToSyncLevel(int32_t option) {
	if (option <= 0 || option >= 34) {
		return SYNC_LEVEL_NONE;
	}
	return syncOptionsByTime[option].level;
}

int32_t syncTypeAndLevelToMenuOption(enum SyncType type, enum SyncLevel level) {
	if (level == SYNC_LEVEL_NONE) {
		return 0;
	}
	for (int32_t i = 1; i < 34; i++) {
		if (syncOptionsByTime[i].type == type && syncOptionsByTime[i].level == level) {
			return i;
		}
	}
	return 0;
}

int32_t getChronologicalSyncOption(enum SyncType type, enum SyncLevel level) {
	return syncTypeAndLevelToMenuOption(type, level);
}

void getSyncTypeAndLevelFromChronologicalOption(int32_t index, enum SyncType& outType, enum SyncLevel& outLevel) {
	outType = syncValueToSyncType(index);
	outLevel = syncValueToSyncLevel(index);
}

void syncValueToString(uint32_t value, StringBuf& buffer, int32_t tickMagnitude) {
	if (value <= 0 || value >= 34) {
		buffer.append(l10n::get(l10n::String::STRING_FOR_OFF));
		return;
	}

	SyncType type = syncOptionsByTime[value].type;
	SyncLevel level = syncOptionsByTime[value].level;

	uint32_t denominator = 1;
	switch (level) {
	case SYNC_LEVEL_WHOLE:  denominator = 1; break;
	case SYNC_LEVEL_2ND:    denominator = 2; break;
	case SYNC_LEVEL_4TH:    denominator = 4; break;
	case SYNC_LEVEL_8TH:    denominator = 8; break;
	case SYNC_LEVEL_16TH:   denominator = 16; break;
	case SYNC_LEVEL_32ND:   denominator = 32; break;
	case SYNC_LEVEL_64TH:   denominator = 64; break;
	case SYNC_LEVEL_128TH:  denominator = 128; break;
	case SYNC_LEVEL_256TH:  denominator = 256; break;
	case SYNC_LEVEL_512TH:  denominator = 512; break;
	case SYNC_LEVEL_1024TH: denominator = 1024; break;
	default: break;
	}

	buffer.append("1/");
	buffer.appendInt(denominator);

	if (type == SYNC_TYPE_TRIPLET) {
		buffer.append(display->have7SEG() ? "T" : "t");
	}
	else if (type == SYNC_TYPE_DOTTED) {
		buffer.append(display->have7SEG() ? "D" : "d");
	}
}
