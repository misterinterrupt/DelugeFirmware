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
		}
		lfsr_ = 0x12345678;
		bowPhase_ = 0.0f;
		strikeCounter_ = 0;
		strikeHardness_ = 0.5f;
		tensionEnv_ = 0.0f;
		dcBlockX_ = 0.0f;
		dcBlockY_ = 0.0f;
	}

	void trigger(uint32_t retriggerPhase = 0xFFFFFFFF) {
		strikeCounter_ = 1000; // Excitation duration (~22ms)
		tensionEnv_ = 1.0f;    // Tension pitch-bend drop on strike

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
	                 int32_t exciterPunch, int32_t exciterNoise, int32_t exciterDecay,
	                 int32_t amplitude, bool applyAmplitude, int32_t amplitudeIncrement) {

		float baseHz = (float)phaseIncrement * (44100.0f / 4294967296.0f);
		baseHz = std::clamp(baseHz, 20.0f, 10000.0f);

		// Normalize pulseWidth (-2147483648 to 2147483647) to structure ratio [0.5, 2.5]
		float structRatio = 1.0f + ((float)(int32_t)pulseWidth / 2147483648.0f) * 0.75f;

		// Calculate Q exponentially from feedback parameter [-1.0, +1.0] -> Q in [1.5, 1800.0]
		float fbNorm = std::clamp((float)feedback / 2147483648.0f, -0.99f, 0.99f);
		float baseQ = 60.0f * std::pow(30.0f, fbNorm);

		// Calculate high-frequency damping factor from damping parameter [-1.0, +1.0] -> [0.05, 3.0]
		float dampNorm = std::clamp((float)damping / 2147483648.0f, -0.99f, 0.99f);
		float dampFactor = 0.5f * std::pow(4.0f, dampNorm);

		// Exciter parameter scalings
		float punchNorm = std::clamp((float)exciterPunch / 2147483648.0f, -0.99f, 0.99f);
		float punchGain = std::pow(4.0f, punchNorm);

		float noiseNorm = std::clamp((float)exciterNoise / 2147483648.0f, -0.99f, 0.99f);
		float noiseGain = std::pow(4.0f, noiseNorm);

		float exciterDecayNorm = std::clamp((float)exciterDecay / 2147483648.0f, -0.99f, 0.99f);
		float exciterDecayFactor = std::pow(3.0f, exciterDecayNorm);

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
		float b0[6], a1[6], a2[6];

		float pitchMultiplier = 1.0f + (tensionEnv_ * 0.15f * strikeHardness_);
		float fundamentalHz = baseHz * pitchMultiplier;

		for (int p = 0; p < numPartials; p++) {
			float freq = std::clamp(fundamentalHz * ratios[p], 20.0f, fs * 0.45f);
			float w0 = 2.0f * 3.14159265358979323846f * freq / fs;
			float cosW = std::cos(w0);
			float sinW = std::sin(w0);
			float Q = baseQ / (1.0f + dampFactor * p * 0.4f);
			Q = std::clamp(Q, 0.8f, 2500.0f);
			float alpha = sinW / (2.0f * Q);

			b0[p] = alpha * modalWeights[p];
			a1[p] = -2.0f * cosW;
			a2[p] = 1.0f - alpha;
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

			// Exciter generator with Punch, Noise & Decay control
			float exciter = 0.0f;
			if (modelType == PhysicalModelType::BOWED_METAL) {
				bowPhase_ += fundamentalHz / fs;
				if (bowPhase_ >= 1.0f) bowPhase_ -= 1.0f;
				float duty = std::clamp(0.5f + strikeHardness_ * 0.2f, 0.1f, 0.9f);
				float stickSignal = (bowPhase_ < duty) ? 0.8f : -0.8f;
				lfsr_ = lfsr_ * 1664525u + 1013904223u;
				float noise = ((float)(int32_t)lfsr_ / 2147483648.0f);
				exciter = (stickSignal * punchGain) + (noise * noiseGain * 0.3f);
			}
			else {
				// Impulsive strike + noise burst
				lfsr_ = lfsr_ * 1664525u + 1013904223u;
				float noise = ((float)(int32_t)lfsr_ / 2147483648.0f);
				if (strikeCounter_ > 0) {
					int32_t impulseThreshold = 1000 - (int32_t)(30.0f * exciterDecayFactor);
					float strikeImpulse = (strikeCounter_ > impulseThreshold) ? (3.0f * strikeHardness_ * punchGain) : 0.0f;
					float maxCounter = 1000.0f * exciterDecayFactor;
					float env = std::clamp((float)strikeCounter_ / maxCounter, 0.0f, 1.0f);
					float noiseBurst = noise * strikeHardness_ * noiseGain * env;
					exciter = strikeImpulse + noiseBurst;
					strikeCounter_--;
				}
			}

			// Fast Parallel Biquad Resonator Filter Bank
			float sampleSum = 0.0f;
			for (int p = 0; p < numPartials; p++) {
				float y = b0[p] * exciter - a1[p] * y1_[p] - a2[p] * y2_[p];
				y2_[p] = y1_[p];
				y1_[p] = y;
				sampleSum += y;
			}

			// DC Blocker (eliminates envelope attack step click transients)
			float dcBlockOut = sampleSum - dcBlockX_ + 0.995f * dcBlockY_;
			dcBlockX_ = sampleSum;
			dcBlockY_ = dcBlockOut;

			// Fast soft-clipping saturation
			float x = dcBlockOut * 2.5f;
			float sat = x * (27.0f + x * x) / (27.0f + 9.0f * x * x);
			sat = std::clamp(sat, -1.0f, 1.0f);

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
	uint32_t lfsr_ = 0x12345678;
	float bowPhase_ = 0.0f;
	int32_t strikeCounter_ = 0;
	float strikeHardness_ = 0.5f;
	float tensionEnv_ = 0.0f;
	float dcBlockX_ = 0.0f;
	float dcBlockY_ = 0.0f;
};

} // namespace deluge::dsp::physical_model
