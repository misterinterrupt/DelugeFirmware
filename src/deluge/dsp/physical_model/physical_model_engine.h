#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include "processing/engines/audio_engine.h"
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
		std::fill_n(delayLine_, 2048, 0.0f);
		delayPtr_ = 0;
		lastLpf_ = 0.0f;
		lastAllpass_ = 0.0f;
		oscPhase_[0] = 0.0f; oscPhase_[1] = 0.0f; oscPhase_[2] = 0.0f;
		oscPhase_[3] = 0.0f; oscPhase_[4] = 0.0f; oscPhase_[5] = 0.0f;
		
		lfsr_ = 0x12345678;
		strikeCounter_ = 0;
		strikeHardness_ = 0.5f;
		envState_ = 0.0f;
		lastBodyOut_ = 0.0f;
		dcBlockX_ = 0.0f;
		dcBlockY_ = 0.0f;
	}

	void trigger(uint32_t retriggerPhase = 0xFFFFFFFF) {
		std::fill_n(delayLine_, 2048, 0.0f);
		delayPtr_ = 0;
		lastLpf_ = 0.0f;
		lastAllpass_ = 0.0f;
		lastAllpassIn_ = 0.0f;
		lastFractionalOut_ = 0.0f;
		dcBlockFBX_ = 0.0f;
		dcBlockFBY_ = 0.0f;
		bowFilter_ = 0.0f;
		
		triggered_ = true;
		strikeCounter_ = 0;
		envState_ = 1.0f;

		if (retriggerPhase == 0xFFFFFFFF) {
			strikeHardness_ = 0.5f;
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
		baseHz = std::clamp(baseHz, 21.6f, 10000.0f);
		float fs = 44100.0f;

		// Unipolar parameter scalings [0.0, 1.0]
		float uPunch = ((float)(int32_t)exciterPunch + 2147483648.0f) / 4294967295.0f;
		float uPitch = ((float)(int32_t)exciterPitch + 2147483648.0f) / 4294967295.0f;
		float uDamp  = ((float)(int32_t)damping      + 2147483648.0f) / 4294967295.0f;
		float uFeedback = ((float)(int32_t)feedback  + 2147483648.0f) / 4294967295.0f;

		int32_t ampNow = amplitude << 3;
		int32_t ampInc = amplitudeIncrement << 3;
		const float invFixed32 = 1.0f / 2147483648.0f;

		// Route based on model class
		bool isWaveguide = (modelType == PhysicalModelType::STRING || modelType == PhysicalModelType::WOOD || modelType == PhysicalModelType::BOWED_METAL);
		
		if (isWaveguide) {
			float targetDelaySamples = fs / baseHz;
			targetDelaySamples = std::clamp(targetDelaySamples, 2.0f, 2047.0f);
			
			if (triggered_) {
				currentDelaySamples_ = targetDelaySamples;
				if (modelType == PhysicalModelType::WOOD) {
					strikeCounter_ = 5; // Very sharp mallet click
				} else if (modelType == PhysicalModelType::STRING) {
					strikeCounter_ = (int32_t)targetDelaySamples; // Exactly one period for a perfect pluck
				} else {
					strikeCounter_ = 0; // Bow is continuous
				}
				triggered_ = false;
			}
			
			float delaySamplesInc = (targetDelaySamples - currentDelaySamples_) / (float)numSamples;
			
			// Damping LPF coefficient (Exponential mapping: 0.0 = bright/1.0, 1.0 = dark/0.001)
			float lpfCoef = std::clamp(std::pow(0.001f, uDamp), 0.005f, 1.0f);
			
			// Feedback decay mapping (Exponential mapping: 0.0 = 50ms, 0.5 = 1s, 1.0 = infinite)
			float loss = 0.05f * std::pow(0.0001f, uFeedback);
			float decayFeedback = std::clamp(1.0f - loss, 0.5f, 0.99999f);
			if (modelType == PhysicalModelType::WOOD) decayFeedback *= 0.95f; // Faster decay for wood
			
			// Exciter Punch mapping for String/Wood
			float punchCoef = std::clamp(std::pow(0.01f, 1.0f - uPunch), 0.01f, 1.0f);

			for (int i = 0; i < numSamples; i++) {
				ampNow += ampInc;
				currentDelaySamples_ += delaySamplesInc;

				// 1. Exciter (Pluck / Strike)
				float exciter = 0.0f;
				if (strikeCounter_ > 0) {
					lfsr_ = lfsr_ * 1664525u + 1013904223u;
					float noise = ((float)(int32_t)lfsr_ * invFixed32);
					
					// Lowpass filter the noise burst based on 'Punch' (soft finger vs hard pick)
					lastBodyOut_ += punchCoef * (noise - lastBodyOut_);
					
					// Envelope the burst
					float burstEnv = (float)strikeCounter_ / (modelType == PhysicalModelType::WOOD ? 5.0f : currentDelaySamples_);
					exciter = lastBodyOut_ * strikeHardness_ * burstEnv;
					strikeCounter_--;
				}

				if (fmInputBuffer) {
					exciter += ((float)fmInputBuffer[i] * invFixed32) * 1.5f;
				}

				// 2. Read from delay line with Thiran All-Pass Interpolation
				float readPos = (float)delayPtr_ - currentDelaySamples_;
				if (readPos < 0.0f) readPos += 2048.0f;
				
				int indexInt = (int)readPos;
				float frac = readPos - (float)indexInt;
				int nextIndex = (indexInt + 1) & 2047;
				
				// frac is the distance from indexInt to readPos.
				// actual fractional delay past `delayLine_[nextIndex]` is `1.0f - frac`.
				float d = 1.0f - frac;
				float c = (1.0f - d) / (1.0f + d);
				
				float x_n = delayLine_[nextIndex];
				float x_n_minus_1 = delayLine_[indexInt];
				float delayOut = c * x_n + x_n_minus_1 - c * lastFractionalOut_;
				lastFractionalOut_ = delayOut;

				// 3. Loss filter (1-pole LPF)
				lastLpf_ = lastLpf_ + lpfCoef * (delayOut - lastLpf_);
				float filtered = lastLpf_ * decayFeedback;
				
				// 3.0 DC Blocker inside feedback loop to prevent clipping lockups
				float dcBlockOut = filtered - dcBlockFBX_ + 0.995f * dcBlockFBY_;
				dcBlockFBX_ = filtered;
				dcBlockFBY_ = dcBlockOut;
				filtered = dcBlockOut;
				
				// 3.1 Wood Inharmonicity (All-pass Filter)
				if (modelType == PhysicalModelType::WOOD) {
					float c = 0.6f; // Stiff bar coefficient
					float allpassOut = c * filtered + lastAllpassIn_ - c * lastAllpass_;
					lastAllpassIn_ = filtered;
					lastAllpass_ = allpassOut;
					filtered = allpassOut;
				}
				
				// 3.2 Bow Friction (Stick-Slip Model)
				if (modelType == PhysicalModelType::BOWED_METAL) {
					float bowVelocity = 0.05f + uPunch * 0.2f; 
					float bowPressure = 0.1f + strikeHardness_ * 0.9f;
					float velocityDelta = bowVelocity - filtered;
					
					float absDelta = std::abs(velocityDelta);
					float friction = 0.0f;
					if (absDelta < 1.0f) {
						// Fast approx of Smith's non-linear friction curve
						friction = velocityDelta * (1.0f - absDelta) * 2.0f; 
					}
					
					// Contact point lowpass filter for stability
					bowFilter_ += 0.2f * (friction - bowFilter_);
					exciter += bowFilter_ * bowPressure;
				}

				// 4. Write to delay line
				float satIn = filtered + exciter;
				// Fast soft clip
				float sat = satIn > 1.0f ? 1.0f : (satIn < -1.0f ? -1.0f : satIn * (1.5f - 0.5f * satIn * satIn));
				
				delayLine_[delayPtr_] = sat;
				delayPtr_ = (delayPtr_ + 1) & 2047;

				// Output scaling
				int32_t outSample = (int32_t)(sat * 1073741824.0f);
				if (applyAmplitude) {
					bufferStart[i] += multiply_32x32_rshift32(outSample, ampNow);
				} else {
					bufferStart[i] += outSample;
				}
			}
		} 
		else {
			// Analog Modeling (Cymbals / Membranes)
			// Use Feedback for Envelope Decay
			float loss = 0.02f * std::pow(0.0001f, uFeedback);
			float envDecay = std::clamp(1.0f - loss, 0.8f, 0.99999f); 
			
			// Use Damping for Filter Cutoff
			float lpfCoef = std::clamp(std::pow(0.001f, uDamp), 0.005f, 1.0f);
			
			if (triggered_) {
				envState_ = 1.0f;
				bowFilter_ = 1.0f; // Reuse bowFilter_ for click envelope
				triggered_ = false;
			}
			
			for (int i = 0; i < numSamples; i++) {
				ampNow += ampInc;
				envState_ *= envDecay; // Exponential decay
				bowFilter_ *= 0.8f; // Fast click decay
				
				float out = 0.0f;

				if (modelType == PhysicalModelType::MEMBRANE) {
					// 808 Kick style: Pitch-drop sine wave
					float pitchDrop = 1.0f + envState_ * uPitch * 10.0f;
					oscPhase_[0] += (baseHz * pitchDrop) / fs;
					if (oscPhase_[0] >= 1.0f) oscPhase_[0] -= 1.0f;
					
					// Fast Sine approx using triangle-to-sine shaping
					float tri = 4.0f * std::abs(oscPhase_[0] - 0.5f) - 1.0f;
					float oscOut = tri * (1.5f - 0.5f * tri * tri) * envState_;
					
					// Noise click mixed by Punch
					lfsr_ = lfsr_ * 1664525u + 1013904223u;
					float noiseClick = ((float)(int32_t)lfsr_ * invFixed32) * bowFilter_ * uPunch * 2.0f;
					
					out = (oscOut + noiseClick) * 0.7f;
					
				} else {
					// 808 Cymbal style: Inharmonic square wave mix
					float ratios[6] = {1.0f, 1.48f, 1.93f, 2.54f, 3.21f, 4.17f};
					float cymbalMix = 0.0f;
					for (int j = 0; j < 6; j++) {
						oscPhase_[j] += (baseHz * ratios[j]) / fs;
						if (oscPhase_[j] >= 1.0f) oscPhase_[j] -= 1.0f;
						cymbalMix += (oscPhase_[j] > 0.5f) ? 0.3f : -0.3f;
					}
					
					// Simple bandpass (highpass then lowpass mapped to Damping)
					lastLpf_ += lpfCoef * (cymbalMix - lastLpf_);
					float hpOut = lastLpf_ - lastAllpass_;
					lastAllpass_ = lastLpf_;
					
					out = hpOut * envState_ * (0.5f + uPunch);
				}

				if (fmInputBuffer) {
					out += ((float)fmInputBuffer[i] * invFixed32) * envState_;
				}

				// DC Blocker
				float dcBlockOut = out - dcBlockX_ + 0.995f * dcBlockY_;
				dcBlockX_ = out;
				dcBlockY_ = dcBlockOut;
				
				// Fast soft clip
				float sat = dcBlockOut > 1.0f ? 1.0f : (dcBlockOut < -1.0f ? -1.0f : dcBlockOut * (1.5f - 0.5f * dcBlockOut * dcBlockOut));
				
				int32_t outSample = (int32_t)(sat * 1073741824.0f);
				if (applyAmplitude) {
					bufferStart[i] += multiply_32x32_rshift32(outSample, ampNow);
				} else {
					bufferStart[i] += outSample;
				}
			}
		}
	}

private:
	float delayLine_[2048] = {0};
	uint32_t delayPtr_ = 0;
	float lastLpf_ = 0.0f;
	float lastAllpass_ = 0.0f; // reused for cymbal highpass
	float lastAllpassIn_ = 0.0f;
	float lastFractionalOut_ = 0.0f;
	float oscPhase_[6] = {0};
	
	uint32_t lfsr_ = 0x12345678;
	bool triggered_ = false;
	float currentDelaySamples_ = 200.0f;
	int32_t strikeCounter_ = 0;
	float strikeHardness_ = 0.5f;
	float envState_ = 0.0f;
	
	float dcBlockX_ = 0.0f;
	float dcBlockY_ = 0.0f;
	float dcBlockFBX_ = 0.0f;
	float dcBlockFBY_ = 0.0f;
	float bowFilter_ = 0.0f;
	float lastBodyOut_ = 0.0f;
};

} // namespace deluge::dsp::physical_model
