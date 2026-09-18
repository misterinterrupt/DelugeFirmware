/*
 * Copyright © 2023-2024 Mark Adams
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

#include "dsp/compressor/rms_feedback.h"
#include "util/fixedpoint.h"
StereoSample dryBuffer[SSI_TX_BUFFER_NUM_SAMPLES]{0};

RMSFeedbackCompressor::RMSFeedbackCompressor() {
	setAttack(5 << 24);
	setRelease(5 << 24);
	setThreshold(0);
	setRatio(0);
	setSidechain(0);
	setBlend(ONE_Q31);
	setInputGain(0);
	setMakeupGain(1073741824);
	baseGain_ = 0.0f;
}

void RMSFeedbackCompressor::updateER(float numSamples, q31_t finalVolume) {
	er = 0.0f;
}

void RMSFeedbackCompressor::renderVolNeutral(StereoSample* buffer, uint16_t numSamples, q31_t finalVolume) {
	render(buffer, numSamples, 1 << 30, 1 << 30, finalVolume);
}
constexpr uint8_t saturationAmount = 0;

[[gnu::always_inline]] inline int32_t applyWarmCompressorSaturation(int32_t sample, uint32_t* lastWorkingValue) {
	// Soft saturation with clean headroom (saturationAmount = 0)
	int32_t sat = getTanHAntialiased(sample, lastWorkingValue, saturationAmount);

	// Add subtle 2nd-order asymmetric bias for analog tube warmth and bulbous bass response
	int64_t mag = (sample < 0) ? -(int64_t)sample : (int64_t)sample;
	int32_t x2 = (int32_t)(((int64_t)sample * mag) >> 31);
	int32_t warm = sat + (x2 >> 3);

	return std::clamp<int32_t>(warm, -1073741824, 1073741824);
}

void RMSFeedbackCompressor::render(StereoSample* buffer, uint16_t numSamples, q31_t volAdjustL, q31_t volAdjustR,
                                   q31_t finalVolume) {
	// make a copy for blending if we need to
	if (wet != ONE_Q31) {
		memcpy(dryBuffer, buffer, numSamples * sizeof(StereoSample));
	}

	if (!onLastTime) {
		// sets the "working level" for interpolation and anti aliasing
		lastSaturationTanHWorkingValue[0] =
		    (uint32_t)lshiftAndSaturateUnknown(buffer->l, saturationAmount) + 2147483648u;
		lastSaturationTanHWorkingValue[1] =
		    (uint32_t)lshiftAndSaturateUnknown(buffer->r, saturationAmount) + 2147483648u;
		onLastTime = true;
	}
	// we update this every time since we won't know if the song volume changed
	updateER(numSamples, finalVolume);

	float effectiveRMS = rms + inputGaindB;
	float over = std::max<float>(0.0f, (effectiveRMS - threshdb));

	state = runEnvelope(state, over, numSamples);

	float reduction = -state * fraction;

	float dbGain = baseGain_ + er + reduction + makeupGaindB + inputGaindB;

	float gain = std::exp(dbGain);
	gain = std::min<float>(gain, 31.0f);

	// Compute linear volume adjustments as 13.18 signed fixed-point numbers (though stored as floats due to their
	// use as an intermediate value prior to the increment computation below)
	float finalVolumeL = gain * float(volAdjustL >> 9);
	float finalVolumeR = gain * float(volAdjustR >> 9);

	// The amount we need to step the current volume so that by the end of the rendering window
	q31_t amplitudeIncrementL = ((int32_t)((finalVolumeL - (currentVolumeL >> 8)) / float(numSamples))) << 8;
	q31_t amplitudeIncrementR = ((int32_t)((finalVolumeR - (currentVolumeR >> 8)) / float(numSamples))) << 8;

	StereoSample* thisSample = buffer;
	StereoSample* drySample = dryBuffer;
	StereoSample* bufferEnd = buffer + numSamples;

	do {
		currentVolumeL += amplitudeIncrementL;
		currentVolumeR += amplitudeIncrementR;
		// Need to shift left by 4 because currentVolumeL is a 5.26 signed number rather than a 1.30 signed.
		thisSample->l = multiply_32x32_rshift32(thisSample->l, currentVolumeL) << 4;
		thisSample->l = applyWarmCompressorSaturation(thisSample->l, &lastSaturationTanHWorkingValue[0]);

		thisSample->r = multiply_32x32_rshift32(thisSample->r, currentVolumeR) << 4;
		thisSample->r = applyWarmCompressorSaturation(thisSample->r, &lastSaturationTanHWorkingValue[1]);
		// wet/dry blend
		if (wet != ONE_Q31) {
			thisSample->l = multiply_32x32_rshift32(thisSample->l, wet);
			thisSample->l = multiply_accumulate_32x32_rshift32_rounded(thisSample->l, drySample->l, dry);
			thisSample->l <<= 1; // correct for the two multiplications
			// same for r because StereoSample is a dumb class
			thisSample->r = multiply_32x32_rshift32(thisSample->r, wet);
			thisSample->r = multiply_accumulate_32x32_rshift32_rounded(thisSample->r, drySample->r, dry);
			thisSample->r <<= 1;
			++drySample; // this is a little gross but it's fine
		}

	} while (++thisSample != bufferEnd);
	// for LEDs
	// 4 converts to dB, then quadrupled for display range since a 30db reduction is basically killing the signal
	gainReduction = std::clamp<int32_t>(-(reduction) * 4 * 4, 0, 127);
	// calc compression for next round (feedback compressor)
	rms = calcRMS(buffer, numSamples);
}

float RMSFeedbackCompressor::runEnvelope(float current, float desired, float numSamples) const {
	float s{0};
	if (desired > current) {
		s = desired + std::exp(a_ * numSamples) * (current - desired);
	}
	else {
		s = desired + std::exp(r_ * numSamples) * (current - desired);
	}
	return s;
}

// output range is 0-21 (2^31)
// dac clipping is at 16
float RMSFeedbackCompressor::calcRMS(StereoSample* buffer, uint16_t numSamples) {
	StereoSample* thisSample = buffer;
	StereoSample* bufferEnd = buffer + numSamples;
	q31_t sum = 0;
	q31_t offset = 0; // to remove dc offset
	float lastMean = mean;
	do {
		q31_t l = thisSample->l - hpfL.doFilter(thisSample->l, hpfA_);
		q31_t r = thisSample->r - hpfL.doFilter(thisSample->r, hpfA_);
		q31_t s = std::max(std::abs(l), std::abs(r));
		sum += multiply_32x32_rshift32(s, s);

	} while (++thisSample != bufferEnd);

	float ns = float(numSamples * 2);
	mean = (float(sum) / ONE_Q31f) / ns;
	// warning this is not good math but it's pretty close and way cheaper than doing it properly
	// good math would use a long FIR, this is a one pole IIR instead
	// the more samples we have, the more weight we put on the current mean to avoid response slowing down
	// at high cpu loads
	mean = (mean * ns + lastMean) / (1 + ns);
	float rms = ONE_Q31 * std::sqrt(mean);

	float logmean = std::log(std::max(rms, 1.0f));

	return logmean;
}
