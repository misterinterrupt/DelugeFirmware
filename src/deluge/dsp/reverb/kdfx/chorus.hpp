/*
 * KDFX-style multi-tap chorus / flanger.
 *
 * Q31 fixed-point modulated delay lines with cross-coupling,
 * bipolar feedback, multiple voice counts (2, 4, 6), selectable LFO shapes,
 * and quadrature stereo spread.
 */

#pragma once

#include "definitions_cxx.hpp"
#include "dsp/reverb/base.hpp"
#include "dsp/reverb/kdfx/kdfx_util.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace deluge::dsp::reverb {

class KdfxChorus : public Base {
public:
	KdfxChorus() = default;
	~KdfxChorus() override = default;

	void process(std::span<int32_t> in, std::span<StereoSample> output) override {
		for (size_t frame = 0; frame < in.size(); frame++) {
			StereoSample& s = output[frame];
			q31_t inputSample = in[frame];

			// 1. Advance LFO phase
			lfoPhase_ += lfoInc_;

			// Random S&H periodic update check
			if (lfoPhase_ < lastPhase_) {
				// Wrapped cycle -> new random target
				rngState_ ^= rngState_ << 13;
				rngState_ ^= rngState_ >> 17;
				rngState_ ^= rngState_ << 5;
				targetRand_ = static_cast<q31_t>(rngState_ & 0x7FFFFFFF);
			}
			lastPhase_ = lfoPhase_;
			kdfx::smoothDamp(currentRand_, targetRand_, static_cast<q31_t>(0.05f * ONE_Q31f));

			// LFO waveform generator
			auto computeLfo = [this](uint32_t phase) -> q31_t {
				switch (lfoWaveform_) {
				case 1: { // Triangle
					int32_t tri = static_cast<int32_t>(phase) - 0x40000000;
					int32_t absTri = std::abs(tri);
					return (absTri - 0x20000000) << 2;
				}
				case 2: // Random S&H
					return currentRand_;
				case 0: // Sine
				default: {
					int32_t x = static_cast<int32_t>(phase) - 0x40000000;
					int64_t absX = std::abs(x);
					int64_t oneMinusAbsX = 0x7FFFFFFF - absX;
					int64_t y = (static_cast<int64_t>(x) * oneMinusAbsX) >> 29;
					return kdfx::qcollapse(y);
				}
				}
			};

			// Fractional linear interpolation for modulated tap
			auto readInterp = [this](const std::array<q31_t, kMaxDelay>& buf, int32_t delayQ16) -> q31_t {
				size_t intPart = static_cast<size_t>(delayQ16 >> 16);
				uint32_t fracPart = static_cast<uint32_t>(delayQ16 & 0xFFFF);
				// Both taps must fall inside the initialized region of the line
				if (intPart >= age_) {
					return 0;
				}
				q31_t s0 = kdfx::circReadCapped(buf, writePos_, intPart, age_);
				q31_t s1 = kdfx::circReadCapped(buf, writePos_, intPart + 1, age_);
				return s0 + static_cast<q31_t>((static_cast<int64_t>(s1 - s0) * fracPart) >> 16);
			};

			// Compute Voice taps
			int32_t numVoices = std::clamp<int32_t>(voices_, 2, 6);
			int64_t sumL = 0;
			int64_t sumR = 0;

			for (int32_t v = 0; v < numVoices; v++) {
				uint32_t phaseOffset = static_cast<uint32_t>((static_cast<uint64_t>(v) * 0x100000000ULL) / numVoices);
				uint32_t phaseL = lfoPhase_ + phaseOffset;
				uint32_t phaseR = phaseL + phaseQuadrature_;

				q31_t lfoL = computeLfo(phaseL);
				q31_t lfoR = computeLfo(phaseR);

				int32_t modQ16L = static_cast<int32_t>((static_cast<int64_t>(lfoL) * depthQ16_) >> 31);
				int32_t modQ16R = static_cast<int32_t>((static_cast<int64_t>(lfoR) * depthQ16_) >> 31);

				int32_t delayQ16L = std::clamp<int32_t>(baseDelayQ16_ + modQ16L, 65536, static_cast<int32_t>((kMaxDelay - 2) << 16));
				int32_t delayQ16R = std::clamp<int32_t>(baseDelayQ16_ + modQ16R, 65536, static_cast<int32_t>((kMaxDelay - 2) << 16));

				sumL += readInterp(bufL_, delayQ16L);
				sumR += readInterp(bufR_, delayQ16R);
			}

			// Per-voice average: what the feedback loop sees. The old form
			// divided by (numVoices >> 1), a 2x-boosted sum — with correlated
			// content (DC, slow material) the loop gain reached 2x FDBK and
			// the lines ran away at FDBK >= 50%.
			q31_t tapL = kdfx::qcollapse(sumL / numVoices);
			q31_t tapR = kdfx::qcollapse(sumR / numVoices);

			// Bipolar feedback with saturation into delay lines — now bounded
			// by FDBK (<= 0.95) exactly.
			q31_t fbL = kdfx::q31_mul(tapL, feedbackQ31_);
			q31_t fbR = kdfx::q31_mul(tapR, feedbackQ31_);

			bufL_[writePos_] = add_saturation(inputSample, fbL);
			bufR_[writePos_] = add_saturation(inputSample, fbR);

			q31_t wetL = tapL << 1; // the loudness the 2x sum used to give
			q31_t wetR = tapR << 1;

			// DC/HPF filter
			kdfx::smoothDamp(hpfL_, wetL, hpfCoeff_);
			wetL -= hpfL_;
			kdfx::smoothDamp(hpfR_, wetR, hpfCoeff_);
			wetR -= hpfR_;

			// Mix wet into main render
			kdfx::mixWet(s.l, wetL, getPanLeft());
			kdfx::mixWet(s.r, wetR, getPanRight());

			++age_;
			writePos_ = (writePos_ + 1) % kMaxDelay;
		}
	}

	void setRoomSize(float value) override {
		rate_val_ = value;
		// LFO rate: 0.05 Hz to 15.00 Hz
		float freqHz = 0.05f + value * 14.95f;
		lfoInc_ = static_cast<uint32_t>((freqHz / 44100.0f) * 4294967296.0f);
	}
	[[nodiscard]] float getRoomSize() const override { return rate_val_; }

	void setDecay(float value) override {
		depth_val_ = value;
		// Mod Depth: 0.0 to 10.0 ms (up to 441 samples in Q16)
		depthQ16_ = static_cast<int32_t>(value * (441 << 16));
	}
	[[nodiscard]] float getDecay() const override { return depth_val_; }

	void setDamping(float value) override {
		// Bipolar feedback mapped from 0..1 to -0.95 .. +0.95
		setChorusFeedback((value - 0.5f) * 1.90f);
	}
	[[nodiscard]] float getDamping() const override { return (feedback_val_ / 1.90f) + 0.5f; }

	void setChorusFeedback(float value) override {
		feedback_val_ = std::clamp(value, -0.95f, 0.95f);
		feedbackQ31_ = static_cast<q31_t>(feedback_val_ * ONE_Q31f);
	}
	[[nodiscard]] float getChorusFeedback() const override { return feedback_val_; }

	void setBaseDelay(float value) override {
		base_delay_val_ = value;
		// 0.1 ms to 40.0 ms (4.4 samples to 1764 samples in Q16)
		float samples = 5.0f + value * 1750.0f;
		baseDelayQ16_ = static_cast<int32_t>(samples * 65536.0f);
	}
	[[nodiscard]] float getBaseDelay() const override { return base_delay_val_; }

	void setChorusVoices(int32_t value) override {
		// Any count 2..6 works (phase offsets divide the circle evenly); the old
		// snap to 2/4/6 made the in-between menu values do nothing.
		voices_ = std::clamp<int32_t>(value, 2, 6);
	}
	[[nodiscard]] int32_t getChorusVoices() const override { return voices_; }

	void setChorusWaveform(int32_t value) override {
		lfoWaveform_ = std::clamp<int32_t>(value, 0, 2);
	}
	[[nodiscard]] int32_t getChorusWaveform() const override { return lfoWaveform_; }

	void setDiffusion(float value) override {
		width_val_ = value;
		phaseQuadrature_ = static_cast<uint32_t>(value * 0x80000000u); // 0 to 180 deg
	}
	[[nodiscard]] float getDiffusion() const override { return width_val_; }

	void setHPF(float f) override {
		hpf_val_ = f;
		float fc_hz = 20.0f + f * 980.0f;
		float fc = fc_hz / float(kSampleRate);
		float wc = fc / (1.0f + fc);
		hpfCoeff_ = static_cast<q31_t>(std::clamp(wc, 0.0005f, 0.45f) * ONE_Q31f);
	}
	[[nodiscard]] float getHPF() const override { return hpf_val_; }

private:
	static constexpr size_t kMaxDelay = 4096; // ~92 ms buffer

	// Deliberately uninitialized (see kdfx_util.hpp circReadCapped); reads are age-capped.
	std::array<q31_t, kMaxDelay> bufL_;
	std::array<q31_t, kMaxDelay> bufR_;

	size_t writePos_ = 0;
	uint32_t age_ = 0; // frames completed since construction
	int32_t baseDelayQ16_ = 350 << 16;
	int32_t depthQ16_ = 150 << 16;
	uint32_t lfoPhase_ = 0;
	uint32_t lastPhase_ = 0;
	uint32_t lfoInc_ = 100000;
	uint32_t phaseQuadrature_ = 0x40000000u; // 90 deg

	int32_t voices_ = 4;
	int32_t lfoWaveform_ = 0; // 0: Sine, 1: Tri, 2: S&H

	q31_t feedbackQ31_ = 0;
	q31_t hpfL_ = 0, hpfR_ = 0;
	q31_t hpfCoeff_ = 0;

	uint32_t rngState_ = 0x12345678u;
	q31_t currentRand_ = 0;
	q31_t targetRand_ = 0;

	float rate_val_ = 0.5f, depth_val_ = 0.5f, feedback_val_ = 0.0f, width_val_ = 0.5f, hpf_val_ = 0.0f;
	float base_delay_val_ = 0.2f;
};

} // namespace deluge::dsp::reverb
