/*
 * Copyright © 2014-2023 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include "definitions_cxx.hpp"
#include "model/sample/sample_controls.h"
#include "storage/multi_range/multi_range_array.h"
#include "util/phase_increment_fine_tuner.h"

class Sound;
class ParamManagerForTimeline;
class WaveTable;
class SampleHolder;
class DxPatch;

class Source {
public:
	Source();
	~Source();

	SampleControls sampleControls;

	OscType oscType;

	// These are not valid for Samples
	int16_t transpose;
	int8_t cents;
	PhaseIncrementFineTuner fineTuner;

	MultiRangeArray ranges;

	DxPatch* dxPatch;
	bool dxPatchChanged = false;
	SampleRepeatMode repeatMode;

	int8_t timeStretchAmount;

	// BPM-based time stretching (Akai-MPC-style).
	// originalBPM: the source tempo of the sample, stored as BPM*10 (e.g. 1200 = 120.0 BPM).
	//              0 means unset / disabled.
	// timeStretchPercent: duration multiplier relative to the song tempo, stored as percent*10
	//                     (e.g. 1000 = 100.0%). 50% = 2x speed, 200% = 0.5x speed.
	uint16_t originalBPM = 0;       // BPM * 10, 0 = unset
	uint16_t timeStretchPercent = 0; // percent * 10, 0 = unset (treated as 100%)

	int16_t defaultRangeI; // -1 means none yet

	bool renderInStereo(Sound* s, SampleHolder* sampleHolder = NULL);
	void setCents(int32_t newCents);
	void recalculateFineTuner();
	int32_t getLengthInSamplesAtSystemSampleRate(int32_t note, bool forTimeStretching = false);
	void detachAllAudioFiles();
	Error loadAllSamples(bool mayActuallyReadFiles);
	void setReversed(bool newReversed);
	int32_t getRangeIndex(int32_t note);
	MultiRange* getRange(int32_t note);
	MultiRange* getOrCreateFirstRange();
	bool hasAtLeastOneAudioFileLoaded();
	void doneReadingFromFile(Sound* sound);
	bool hasAnyLoopEndPoint();
	void setOscType(OscType newType);

	// Returns true if BPM-based time stretching is active (originalBPM != 0).
	bool hasBPMTimeStretch() const { return originalBPM != 0; }

	DxPatch* ensureDxPatch();

private:
	void destructAllMultiRanges();
};
