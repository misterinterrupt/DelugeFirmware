/*
 * KDFX-style flanger (manual algorithms 154/155, 1-2 PAU).
 *
 * Single modulated delay tap per channel with bipolar feedback, short base
 * delay (0.1-7 ms) and quadrature stereo phase offset — the chorus chassis
 * reduced to one voice with flange ranges. Send-style wet-add output.
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

class KdfxFlanger : public Base {
public:
	KdfxFlanger() = default;
	~KdfxFlanger() override = default;

	void process(std::span<int32_t> in, std::span<StereoSample> output) override {
		for (size_t frame = 0; frame < in.size(); frame++) {
			StereoSample& s = output[frame];
			q31_t inputSample = in[frame];

			lfoPhase_ += lfoInc_;

			q31_t lfoL = kdfx::parabolicLfo(lfoPhase_);
			q31_t lfoR = kdfx::parabolicLfo(lfoPhase_ + phaseQuadrature_);

			q31_t wetL = readTap(bufL_, lfoL);
			q31_t wetR = readTap(bufR_, lfoR);

			// Bipolar feedback into the line, saturating.
			bufL_[writePos_] = add_saturation(inputSample, kdfx::q31_mul(wetL, feedbackQ31_));
			bufR_[writePos_] = add_saturation(inputSample, kdfx::q31_mul(wetR, feedbackQ31_));

			// DC block.
			kdfx::smoothDamp(hpfL_, wetL, hpfCoeff_);
			wetL -= hpfL_;
			kdfx::smoothDamp(hpfR_, wetR, hpfCoeff_);
			wetR -= hpfR_;

			kdfx::mixWet(s.l, wetL, getPanLeft());
			kdfx::mixWet(s.r, wetR, getPanRight());

			++age_;
			writePos_ = (writePos_ + 1) % kMaxDelay;
		}
	}

	void setRoomSize(float value) override {
		rate_val_ = value;
		// LFO rate: 0.05 Hz to 8.00 Hz
		float freqHz = 0.05f + value * 7.95f;
		lfoInc_ = static_cast<uint32_t>((freqHz / 44100.0f) * 4294967296.0f);
	}
	[[nodiscard]] float getRoomSize() const override { return rate_val_; }

	void setDecay(float value) override {
		depth_val_ = value;
		// Mod depth: 0.0 to 3.0 ms (up to 147 samples in Q16)
		depthQ16_ = static_cast<int32_t>(value * (147 << 16));
	}
	[[nodiscard]] float getDecay() const override { return depth_val_; }

	void setDamping(float value) override {
		// Bipolar feedback mapped from 0..1 to -0.95 .. +0.95
		feedback_val_ = std::clamp((value - 0.5f) * 1.90f, -0.95f, 0.95f);
		feedbackQ31_ = static_cast<q31_t>(feedback_val_ * ONE_Q31f);
	}
	[[nodiscard]] float getDamping() const override { return (feedback_val_ / 1.90f) + 0.5f; }

	void setDiffusion(float value) override {
		spread_val_ = value;
		phaseQuadrature_ = static_cast<uint32_t>(value * 0x80000000u); // 0 to 180 deg
	}
	[[nodiscard]] float getDiffusion() const override { return spread_val_; }

	void setHPF(float f) override {
		hpf_val_ = f;
		float fc_hz = 20.0f + f * 980.0f;
		float fc = fc_hz / float(kSampleRate);
		float wc = fc / (1.0f + fc);
		hpfCoeff_ = static_cast<q31_t>(std::clamp(wc, 0.0005f, 0.45f) * ONE_Q31f);
	}
	[[nodiscard]] float getHPF() const override { return hpf_val_; }

private:
	static constexpr size_t kMaxDelay = 1024; // ~23 ms

	// One fractional modulated tap. Both interpolation points must fall inside
	// the initialized region of the line (see kdfx_util circReadCapped).
	q31_t readTap(const std::array<q31_t, kMaxDelay>& buf, q31_t lfo) {
		int32_t modQ16 = static_cast<int32_t>((static_cast<int64_t>(lfo) * depthQ16_) >> 31);
		int32_t delayQ16 = std::clamp<int32_t>(baseDelayQ16_ + modQ16, 65536, static_cast<int32_t>((kMaxDelay - 2) << 16));
		size_t intPart = static_cast<size_t>(delayQ16 >> 16);
		uint32_t frac = static_cast<uint32_t>(delayQ16 & 0xFFFF);
		if (intPart >= age_) {
			return 0;
		}
		q31_t s0 = kdfx::circReadCapped(buf, writePos_, intPart, age_);
		q31_t s1 = kdfx::circReadCapped(buf, writePos_, intPart + 1, age_);
		return s0 + static_cast<q31_t>((static_cast<int64_t>(s1 - s0) * frac) >> 16);
	}

	// Deliberately uninitialized (kdfx_util convention); reads are age-capped.
	std::array<q31_t, kMaxDelay> bufL_;
	std::array<q31_t, kMaxDelay> bufR_;

	size_t writePos_ = 0;
	uint32_t age_ = 0;
	int32_t baseDelayQ16_ = 100 << 16; // ~2.3 ms center for flanging
	int32_t depthQ16_ = 60 << 16;
	uint32_t lfoPhase_ = 0;
	uint32_t lfoInc_ = 100000;
	uint32_t phaseQuadrature_ = 0x40000000u; // 90 deg

	q31_t feedbackQ31_ = 0;
	q31_t hpfL_ = 0, hpfR_ = 0;
	q31_t hpfCoeff_ = 0;

	float rate_val_ = 0.3f, depth_val_ = 0.4f, feedback_val_ = 0.5f, spread_val_ = 0.5f, hpf_val_ = 0.0f;
};

} // namespace deluge::dsp::reverb
