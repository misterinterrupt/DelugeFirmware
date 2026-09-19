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
constexpr uint8_t saturationAmount = 3;

[[gnu::always_inline]] inline int32_t applyWarmCompressorSaturation(int64_t scaledInput, uint32_t* lastWorkingValue,
                                                                    int32_t* lastDampValue) {
	// 1. Soft-knee asymptotic compression for driven peaks to prevent hard table clipping (flat square tops)
	int32_t inputForTanH;
	int64_t absInput = (scaledInput < 0) ? -scaledInput : scaledInput;
	constexpr int64_t T = 180000000LL; // Soft knee threshold (~ -3.5 dB below table limit)
	constexpr int64_t K = 75000000LL;  // Asymptote headroom (max input = 255M < 268.4M table max)

	if (absInput <= T) {
		inputForTanH = (int32_t)scaledInput;
	}
	else {
		int64_t delta = absInput - T;
		int64_t compressedDelta = (delta * K) / (K + delta);
		int64_t compressedAbs = T + compressedDelta;
		inputForTanH = (scaledInput < 0) ? (int32_t)(-compressedAbs) : (int32_t)compressedAbs;
	}

	// 2. Continuous anti-aliased soft saturation (saturationAmount = 3 preserves exact native volume level)
	int32_t sat = getTanHAntialiased(inputForTanH, lastWorkingValue, saturationAmount);

	// 3. Add subtle 2nd-order asymmetric bias for analog tube warmth and bulbous bass response
	int64_t mag = (sat < 0) ? -(int64_t)sat : (int64_t)sat;
	int32_t x2 = (int32_t)(((int64_t)sat * mag) >> 31);
	int32_t warm = sat + (x2 >> 5);

	// 4. Analog high-frequency warming filter when heavily driven
	// Gently rolls off high-frequency digital noise/fuzz when driven hard to keep tone fat & rounded
	if (absInput > T) {
		int32_t diff = warm - *lastDampValue;
		warm = *lastDampValue + (diff >> 1);
	}
	*lastDampValue = warm;

	return warm;
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
		lastDampValue[0] = 0;
		lastDampValue[1] = 0;
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

	// Compute target gain multiplier with 1.0 = (1 << 24)
	// volAdjustL is 1.30 signed fixed-point where 1 << 30 is 0.5 (neutral)
	float finalVolumeL = gain * float(volAdjustL >> 6);
	float finalVolumeR = gain * float(volAdjustR >> 6);

	// The amount we need to step currentVolume per sample
	int32_t amplitudeIncrementL = (int32_t)((finalVolumeL - float(currentVolumeL)) / float(numSamples));
	int32_t amplitudeIncrementR = (int32_t)((finalVolumeR - float(currentVolumeR)) / float(numSamples));

	StereoSample* thisSample = buffer;
	StereoSample* drySample = dryBuffer;
	StereoSample* bufferEnd = buffer + numSamples;

	do {
		currentVolumeL += amplitudeIncrementL;
		currentVolumeR += amplitudeIncrementR;

		// 64-bit multiplication without 32-bit integer overflow or bit-wrapping
		int64_t scaledL = (int64_t(thisSample->l) * currentVolumeL) >> 24;
		thisSample->l = applyWarmCompressorSaturation(scaledL, &lastSaturationTanHWorkingValue[0], &lastDampValue[0]);

		int64_t scaledR = (int64_t(thisSample->r) * currentVolumeR) >> 24;
		thisSample->r = applyWarmCompressorSaturation(scaledR, &lastSaturationTanHWorkingValue[1], &lastDampValue[1]);

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
