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
	setAttack(536870912);
	setRelease(1073741824);
	setThreshold(0);
	setRatio(1073741824);
	setSidechain(0);
	setBlend(0);
	setInputGain(0);
	setMakeupGain(1073741824);
	baseGain_ = 0.0f;
	currentCompGain = 1 << 24;
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

	// 2. Continuous anti-aliased soft saturation (shifted << 1 to preserve exact 1.0x unity gain)
	int32_t sat = getTanHAntialiased(inputForTanH, lastWorkingValue, saturationAmount) << 1;

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
	// Copy clean dry signal conceptually pre-input-gain for blending
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
		currentVolumeL = volAdjustL >> 6;
		currentVolumeR = volAdjustR >> 6;
		currentCompGain = 1 << 24;
		onLastTime = true;
	}

	updateER(numSamples, finalVolume);

	float effectiveRMS = rms;
	float over = std::max<float>(0.0f, (effectiveRMS - threshdb));

	state = runEnvelope(state, over, numSamples);

	float reduction = -state * fraction;

	// Compressor gain reduction + makeup gain
	float compGaindB = baseGain_ + er + reduction + makeupGaindB;
	float compGainLinear = std::exp(compGaindB);
	compGainLinear = std::min<float>(compGainLinear, 31.0f);

	// Target compressor gain in 1.24 fixed point
	float targetCompGain = compGainLinear * 16777216.0f;
	int32_t compGainIncrement = (int32_t)((targetCompGain - float(currentCompGain)) / float(numSamples));

	// Linear input gain in 1.24 fixed point to drive the saturation stage
	float inputGainLinear = std::exp(inputGaindB);
	int32_t inputGainMult = (int32_t)(inputGainLinear * 16777216.0f);

	// Volume adjustments in 1.24 fixed point applied to final blended output
	float targetVolL = float(volAdjustL >> 6);
	float targetVolR = float(volAdjustR >> 6);
	int32_t amplitudeIncrementL = (int32_t)((targetVolL - float(currentVolumeL)) / float(numSamples));
	int32_t amplitudeIncrementR = (int32_t)((targetVolR - float(currentVolumeR)) / float(numSamples));

	StereoSample* thisSample = buffer;
	StereoSample* drySample = dryBuffer;
	StereoSample* bufferEnd = buffer + numSamples;
	int64_t sum = 0;

	do {
		int32_t rawL = thisSample->l;
		int32_t rawR = thisSample->r;

		// 1. Input Gain stage
		int64_t drivenL = (int64_t(rawL) * inputGainMult) >> 24;
		int64_t drivenR = (int64_t(rawR) * inputGainMult) >> 24;

		// 2. Saturation stage (between input gain and compressor)
		int32_t satL = applyWarmCompressorSaturation(drivenL, &lastSaturationTanHWorkingValue[0], &lastDampValue[0]);
		int32_t satR = applyWarmCompressorSaturation(drivenR, &lastSaturationTanHWorkingValue[1], &lastDampValue[1]);

		// 3. Compressor Gain Reduction & Makeup Gain stage
		currentCompGain += compGainIncrement;
		int32_t wetL = (int64_t(satL) * currentCompGain) >> 24;
		int32_t wetR = (int64_t(satR) * currentCompGain) >> 24;

		// 4. Sidechain RMS tracking on wet compressed signal
		q31_t hpfOutL = wetL - hpfL.doFilter(wetL, hpfA_);
		q31_t hpfOutR = wetR - hpfR.doFilter(wetR, hpfA_);
		q31_t s = std::max(std::abs(hpfOutL), std::abs(hpfOutR));
		sum += multiply_32x32_rshift32(s, s);

		// 5. Wet/Dry Blend (dry is raw signal pre-input gain)
		int32_t blendedL, blendedR;
		if (wet != ONE_Q31) {
			int32_t dryL = drySample->l;
			int32_t dryR = drySample->r;
			blendedL = multiply_32x32_rshift32(wetL, wet);
			blendedL = multiply_accumulate_32x32_rshift32_rounded(blendedL, dryL, dry) << 1;
			blendedR = multiply_32x32_rshift32(wetR, wet);
			blendedR = multiply_accumulate_32x32_rshift32_rounded(blendedR, dryR, dry) << 1;
			++drySample;
		}
		else {
			blendedL = wetL;
			blendedR = wetR;
		}

		// 6. Output volume adjustment applied to blended signal
		currentVolumeL += amplitudeIncrementL;
		currentVolumeR += amplitudeIncrementR;
		thisSample->l = (int64_t(blendedL) * currentVolumeL) >> 24;
		thisSample->r = (int64_t(blendedR) * currentVolumeR) >> 24;

	} while (++thisSample != bufferEnd);

	// Update gain reduction display for LEDs
	gainReduction = std::clamp<int32_t>(-(reduction) * 4 * 4, 0, 127);

	// Update feedback RMS calculation for next render window
	float lastMean = mean;
	float ns = float(numSamples * 2);
	mean = (float(sum) / ONE_Q31f) / ns;
	mean = (mean * ns + lastMean) / (1.0f + ns);
	float rmsLinear = ONE_Q31 * std::sqrt(mean);
	rms = std::log(std::max(rmsLinear, 1.0f));
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
	int64_t sum = 0;
	float lastMean = mean;
	do {
		q31_t l = thisSample->l - hpfL.doFilter(thisSample->l, hpfA_);
		q31_t r = thisSample->r - hpfR.doFilter(thisSample->r, hpfA_);
		q31_t s = std::max(std::abs(l), std::abs(r));
		sum += multiply_32x32_rshift32(s, s);

	} while (++thisSample != bufferEnd);

	float ns = float(numSamples * 2);
	mean = (float(sum) / ONE_Q31f) / ns;
	mean = (mean * ns + lastMean) / (1.0f + ns);
	float rmsLinear = ONE_Q31 * std::sqrt(mean);

	float logmean = std::log(std::max(rmsLinear, 1.0f));

	return logmean;
}
