/*
 * Copyright (c) 2014-2023 Synthstrom Audible Limited
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

#include <algorithm>
#include <cmath>
#include <cstdint>
#include "util/functions.h"

namespace deluge::dsp::physical_model {

enum class PhysicalModelType : uint8_t {
	MEMBRANE = 0,
	WOOD,
	BOWED_METAL,
	REAL_CYMBAL,
	DIGITAL_CYMBAL,
	STRING,
	NUM_MODELS
};

class PhysicalModelEngine {
public:
	PhysicalModelEngine() { reset(); }

	void reset() {
		for (int i = 0; i < 6; i++) {
			y1_[i] = 0.0f;
			y2_[i] = 0.0f;
			current_b0_[i] = 0.0f;
			current_a1_[i] = 0.0f;
			current_a2_[i] = 0.0f;
		}
		initializedCoeffs_ = false;
		lfsr_ = 0x12345678;
		bowPhase_ = 0.0f;
		strikeCounter_ = 0;
		strikeHardness_ = 0.5f;
		tensionEnv_ = 0.0f;
		dcBlockX_ = 0.0f;
		dcBlockY_ = 0.0f;
		exciterLPF_ = 0.0f;
		lastBodyOut_ = 0.0f;
		attackFade_ = 1.0f;
	}

	void trigger(uint32_t retriggerPhase = 0xFFFFFFFF) {
		strikeCounter_ = 1000; // Excitation duration (~22ms)
		tensionEnv_ = 1.0f;    // Tension pitch-bend drop on strike

		for (int i = 0; i < 6; i++) {
			y1_[i] = 0.0f;
			y2_[i] = 0.0f;
			current_b0_[i] = 0.0f;
			current_a1_[i] = 0.0f;
			current_a2_[i] = 0.0f;
		}
		initializedCoeffs_ = false;
		dcBlockX_ = 0.0f;
		dcBlockY_ = 0.0f;
		exciterLPF_ = 0.0f;
		lastBodyOut_ = 0.0f;
		attackFade_ = 0.0f;

		if (retriggerPhase == 0xFFFFFFFF) {
			strikeHardness_ = 0.5f; // Default natural wooden stick
		}
		else {
			float phaseNormalized = (float)retriggerPhase / 4294967295.0f;
			strikeHardness_ = std::clamp(phaseNormalized, 0.05f, 1.0f);
		}
	}

	void renderBlock(int32_t* bufferStart, int32_t numSamples, uint32_t phaseIncrement,
	                 uint32_t pulseWidth, int32_t feedback, int32_t damping, PhysicalModelType modelType,
	                 int32_t exciterPunch, int32_t exciterPitch,
	                 int32_t amplitude, bool applyAmplitude, int32_t amplitudeIncrement,
	                 const int32_t* fmInputBuffer = nullptr) {

		float baseHz = (float)phaseIncrement * (44100.0f / 4294967296.0f);
		baseHz = std::clamp(baseHz, 20.0f, 10000.0f);

		// Normalize pulseWidth (-2147483648 to 2147483647) to structure ratio [0.5, 2.5]
		float structRatio = 1.0f + ((float)(int32_t)pulseWidth / 2147483648.0f) * 0.75f;

		// Calculate Q exponentially from feedback parameter [-1.0, +1.0] -> Q in [1.5, 1800.0]
		float fbNorm = std::clamp((float)feedback / 2147483648.0f, -0.99f, 0.99f);
		float baseQ = 60.0f * std::pow(30.0f, fbNorm);

		// Acoustic body recirculation feedback gain [0.0, 0.45]
		float recirculationGain = (fbNorm > 0.0f) ? (fbNorm * 0.45f) : 0.0f;

		// Unipolar parameter scalings from [-2147483648, +2147483647] -> [0.0, 1.0]
		float uPunch = ((float)(int32_t)exciterPunch + 2147483648.0f) / 4294967295.0f;
		float uPitch = ((float)(int32_t)exciterPitch + 2147483648.0f) / 4294967295.0f;
		float uDamp  = ((float)(int32_t)damping      + 2147483648.0f) / 4294967295.0f;

		// Mallet Punch Gain & Nonlinear Drive
		float punchGain = 0.5f + uPunch * 4.5f;

		// Damping factor for modal partials
		float dampFactor = 0.02f + uDamp * 3.5f;

		// Exciter noise cutoff (Exciter Color)
		float noiseCutoff = std::clamp(0.02f + uPitch * 0.95f, 0.01f, 0.99f);

		// Pitch shift multiplier including strike tension drop
		float pitchShiftMult = std::pow(2.0f, (uPitch - 0.5f) * 2.0f);
		float pitchMultiplier = pitchShiftMult * (1.0f + (tensionEnv_ * 0.20f * uPunch * strikeHardness_));
		float fundamentalHz = baseHz * pitchMultiplier;

		// Frequency-dependent Q compensation relative to 220Hz
		float pitchQFactor = std::pow(std::clamp(fundamentalHz / 220.0f, 0.1f, 20.0f), 0.35f);
		pitchQFactor = std::clamp(pitchQFactor, 0.5f, 2.5f);
		float effectiveBaseQ = baseQ * pitchQFactor;

		// Select partial ratios per model
		float ratios[6];
		float modalWeights[6];
		int numPartials = 4;

		switch (modelType) {
		case PhysicalModelType::MEMBRANE:
			ratios[0] = 1.000f; ratios[1] = 1.586f * structRatio; ratios[2] = 2.136f * structRatio; ratios[3] = 2.653f * structRatio;
			modalWeights[0] = 1.0f; modalWeights[1] = 0.7f; modalWeights[2] = 0.5f; modalWeights[3] = 0.3f;
			numPartials = 4;
			break;

		case PhysicalModelType::WOOD:
			ratios[0] = 1.000f; ratios[1] = 2.756f * structRatio; ratios[2] = 5.404f * structRatio; ratios[3] = 8.933f * structRatio;
			modalWeights[0] = 1.0f; modalWeights[1] = 0.8f; modalWeights[2] = 0.4f; modalWeights[3] = 0.2f;
			numPartials = 4;
			break;

		case PhysicalModelType::BOWED_METAL:
			ratios[0] = 1.000f; ratios[1] = 2.000f * structRatio; ratios[2] = 3.006f * structRatio; ratios[3] = 4.015f * structRatio;
			modalWeights[0] = 0.8f; modalWeights[1] = 1.0f; modalWeights[2] = 0.9f; modalWeights[3] = 0.7f;
			numPartials = 4;
			break;

		case PhysicalModelType::REAL_CYMBAL:
			ratios[0] = 1.000f; ratios[1] = 1.478f * structRatio; ratios[2] = 1.934f * structRatio; ratios[3] = 2.541f * structRatio; ratios[4] = 3.210f; ratios[5] = 4.172f;
			modalWeights[0] = 0.5f; modalWeights[1] = 0.8f; modalWeights[2] = 1.0f; modalWeights[3] = 0.9f; modalWeights[4] = 0.7f; modalWeights[5] = 0.5f;
			numPartials = 6;
			break;

		case PhysicalModelType::DIGITAL_CYMBAL:
			ratios[0] = 1.000f; ratios[1] = 1.483f * structRatio; ratios[2] = 1.800f * structRatio; ratios[3] = 2.546f; ratios[4] = 2.634f; ratios[5] = 3.902f;
			modalWeights[0] = 0.6f; modalWeights[1] = 0.9f; modalWeights[2] = 1.0f; modalWeights[3] = 0.8f; modalWeights[4] = 0.6f; modalWeights[5] = 0.4f;
			numPartials = 6;
			break;

		case PhysicalModelType::STRING:
		default:
			ratios[0] = 1.000f; ratios[1] = 2.000f * structRatio; ratios[2] = 3.000f * structRatio; ratios[3] = 4.000f * structRatio;
			modalWeights[0] = 1.0f; modalWeights[1] = 0.6f; modalWeights[2] = 0.4f; modalWeights[3] = 0.2f;
			numPartials = 4;
			break;
		}

		// BLOCK-RATE COEFFICIENT PRE-COMPUTATION
		float fs = 44100.0f;
		float target_b0[6] = {0}, target_a1[6] = {0}, target_a2[6] = {0};

		for (int p = 0; p < numPartials; p++) {
			float freq = std::clamp(fundamentalHz * ratios[p], 20.0f, fs * 0.45f);
			float w0 = 2.0f * 3.14159265358979323846f * freq / fs;
			float cosW = std::cos(w0);
			float sinW = std::sin(w0);
			float Q = effectiveBaseQ / (1.0f + dampFactor * p * 0.4f);
			Q = std::clamp(Q, 0.8f, 2500.0f);
			float alpha = sinW / (2.0f * Q);

			target_b0[p] = alpha * modalWeights[p];
			target_a1[p] = -2.0f * cosW;
			target_a2[p] = 1.0f - alpha;
		}

		if (!initializedCoeffs_) {
			for (int p = 0; p < 6; p++) {
				current_b0_[p] = target_b0[p];
				current_a1_[p] = target_a1[p];
				current_a2_[p] = target_a2[p];
			}
			initializedCoeffs_ = true;
		}

		float step_b0[6] = {0}, step_a1[6] = {0}, step_a2[6] = {0};
		float invNumSamples = 1.0f / (float)numSamples;
		for (int p = 0; p < numPartials; p++) {
			step_b0[p] = (target_b0[p] - current_b0_[p]) * invNumSamples;
			step_a1[p] = (target_a1[p] - current_a1_[p]) * invNumSamples;
			step_a2[p] = (target_a2[p] - current_a2_[p]) * invNumSamples;
		}

		// Decay tension envelope once per block
		if (tensionEnv_ > 0.001f) {
			tensionEnv_ *= std::pow(0.9992f, (float)numSamples);
		} else {
			tensionEnv_ = 0.0f;
		}

		int32_t ampNow = amplitude << 3;
		int32_t ampInc = amplitudeIncrement << 3;

		// Inner per-sample DSP loop
		for (int i = 0; i < numSamples; i++) {
			ampNow += ampInc;

			// Exciter generator with Mallet Hardness, Exciter Color & Nonlinear Impact Drive
			float exciter = 0.0f;
			if (modelType == PhysicalModelType::BOWED_METAL) {
				bowPhase_ += fundamentalHz / fs;
				if (bowPhase_ >= 1.0f) bowPhase_ -= 1.0f;
				float duty = std::clamp(0.5f + strikeHardness_ * 0.2f, 0.1f, 0.9f);
				float stickSignal = (bowPhase_ < duty) ? 0.8f : -0.8f;
				lfsr_ = lfsr_ * 1664525u + 1013904223u;
				float noiseRaw = ((float)(int32_t)lfsr_ / 2147483648.0f);
				exciterLPF_ += (noiseRaw - exciterLPF_) * noiseCutoff;
				exciter = (stickSignal * punchGain) + (exciterLPF_ * punchGain * 0.4f);
			}
			else {
				// Impulsive strike + color filtered noise burst
				lfsr_ = lfsr_ * 1664525u + 1013904223u;
				float noiseRaw = ((float)(int32_t)lfsr_ / 2147483648.0f);
				exciterLPF_ += (noiseRaw - exciterLPF_) * noiseCutoff;

				if (strikeCounter_ > 0) {
					// Smooth half-sine mallet contact impulse curve
					float envPhase = (float)(1000 - strikeCounter_) / (100.0f + uPunch * 100.0f);
					float strikeImpulse = 0.0f;
					if (envPhase < 3.14159f) {
						float sinePulse = std::sin(envPhase);
						// Nonlinear mallet compression curve
						strikeImpulse = (3.5f * sinePulse * punchGain * strikeHardness_) / (1.0f + 0.3f * punchGain * sinePulse);
					}
					float decayEnv = std::clamp((float)strikeCounter_ / 1000.0f, 0.0f, 1.0f);
					float noiseBurst = exciterLPF_ * strikeHardness_ * (0.2f + punchGain * 0.8f) * decayEnv;
					exciter = strikeImpulse + noiseBurst;
					strikeCounter_--;
				}
			}

			if (fmInputBuffer) {
				float fmSig = ((float)fmInputBuffer[i] / 2147483648.0f);
				// Soft-saturate FM exciter input to prevent sharp impulse click spikes
				fmSig = fmSig / (1.0f + 0.33f * std::abs(fmSig));
				exciter += fmSig * 1.2f;
			}

			// Mix acoustic body recirculation feedback signal into exciter
			if (recirculationGain > 0.001f) {
				float fbSig = lastBodyOut_ * recirculationGain;
				fbSig = fbSig / (1.0f + std::abs(fbSig)); // Soft-clamped feedback
				exciter += fbSig;
			}

			// Fast Parallel Biquad Resonator Filter Bank with per-sample coefficient interpolation
			float sampleSum = 0.0f;
			for (int p = 0; p < numPartials; p++) {
				current_b0_[p] += step_b0[p];
				current_a1_[p] += step_a1[p];
				current_a2_[p] += step_a2[p];

				float y = current_b0_[p] * exciter - current_a1_[p] * y1_[p] - current_a2_[p] * y2_[p];
				if (std::abs(y) > 15.0f || std::isnan(y)) {
					y = std::clamp(y, -15.0f, 15.0f);
				}
				y2_[p] = y1_[p];
				y1_[p] = y;
				sampleSum += y;
			}

			// DC Blocker (eliminates envelope attack step click transients)
			float dcBlockOut = sampleSum - dcBlockX_ + 0.995f * dcBlockY_;
			if (std::isnan(dcBlockOut) || std::abs(dcBlockOut) > 50.0f) {
				dcBlockOut = std::clamp(dcBlockOut, -50.0f, 50.0f);
			}
			dcBlockX_ = sampleSum;
			dcBlockY_ = dcBlockOut;

			// Fast soft-clipping saturation
			float x = dcBlockOut * 2.5f;
			float sat = x * (27.0f + x * x) / (27.0f + 9.0f * x * x);
			sat = std::clamp(sat, -1.0f, 1.0f);

			if (attackFade_ < 1.0f) {
				sat *= attackFade_;
				attackFade_ += (1.0f / 96.0f);
				if (attackFade_ > 1.0f) attackFade_ = 1.0f;
			}

			lastBodyOut_ = sat;

			int32_t outSample = (int32_t)(sat * 1073741824.0f);

			if (applyAmplitude) {
				int32_t scaled = multiply_32x32_rshift32(outSample, ampNow);
				bufferStart[i] += scaled;
			}
			else {
				bufferStart[i] += outSample;
			}
		}
	}

private:
	float y1_[6] = {0};
	float y2_[6] = {0};
	float current_b0_[6] = {0};
	float current_a1_[6] = {0};
	float current_a2_[6] = {0};
	bool initializedCoeffs_ = false;
	uint32_t lfsr_ = 0x12345678;
	float bowPhase_ = 0.0f;
	int32_t strikeCounter_ = 0;
	float strikeHardness_ = 0.5f;
	float tensionEnv_ = 0.0f;
	float dcBlockX_ = 0.0f;
	float dcBlockY_ = 0.0f;
	float exciterLPF_ = 0.0f;
	float lastBodyOut_ = 0.0f;
	float attackFade_ = 1.0f;
};

} // namespace deluge::dsp::physical_model
