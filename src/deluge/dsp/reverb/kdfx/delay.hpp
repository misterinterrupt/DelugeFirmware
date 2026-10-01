/*
 * KDFX-style 4-tap stereo delay.
 *
 * Q31 fixed-point delay line with Hadamard cross-feedback matrix,
 * damping filter, and ping-pong stereo width.
 */

#pragma once

#include "definitions_cxx.hpp"
#include "dsp/reverb/base.hpp"
#include "dsp/reverb/kdfx/kdfx_util.hpp"
#include <algorithm>
#include <array>
#include <cstdint>

namespace deluge::dsp::reverb {

class KdfxDelay : public Base {
public:
	KdfxDelay() = default;
	~KdfxDelay() override = default;

	void process(std::span<int32_t> in, std::span<StereoSample> output) override {
		for (size_t frame = 0; frame < in.size(); frame++) {
			StereoSample& s = output[frame];

			q31_t inputSample = in[frame];

			// 4 distinct delay tap reads (Tap 1 & 3 Left, Tap 2 & 4 Right)
			size_t d1 = std::clamp<size_t>(delaySamples_, 1, kMaxDelay - 1);
			size_t d2 = std::clamp<size_t>(static_cast<size_t>(delaySamples_ * tapSpread_), 1, kMaxDelay - 1);
			size_t d3 = std::clamp<size_t>(delaySamples_ >> 1, 1, kMaxDelay - 1);
			size_t d4 = std::clamp<size_t>(delaySamples_ >> 2, 1, kMaxDelay - 1);

			q31_t tap1 = kdfx::circRead(bufL_, writePos_, d1);
			q31_t tap2 = kdfx::circRead(bufR_, writePos_, d2);
			q31_t tap3 = kdfx::circRead(bufL_, writePos_, d3);
			q31_t tap4 = kdfx::circRead(bufR_, writePos_, d4);

			// Feedback loop with HF absorption damping
			kdfx::smoothDamp(dampL_, tap1, dampCoeff_);
			kdfx::smoothDamp(dampR_, tap2, dampCoeff_);

			q31_t fbL = kdfx::q31_mul(dampL_, feedback_);
			q31_t fbR = kdfx::q31_mul(dampR_, feedback_);

			// Crossfeed / PingPong mixing
			q31_t directL = kdfx::q31_mul(fbL, ONE_Q31 - crossfeed_);
			q31_t crossL = kdfx::q31_mul(fbR, crossfeed_);
			q31_t nextL = add_saturation(inputSample, add_saturation(directL, crossL));

			q31_t directR = kdfx::q31_mul(fbR, ONE_Q31 - crossfeed_);
			q31_t crossR = kdfx::q31_mul(fbL, crossfeed_);
			q31_t nextR = add_saturation(inputSample, add_saturation(directR, crossR));

			bufL_[writePos_] = nextL;
			bufR_[writePos_] = nextR;

			// Output summing of 4 taps with spatial spread
			q31_t wetL = add_saturation(tap1, tap3 >> 1);
			q31_t wetR = add_saturation(tap2, tap4 >> 1);

			// DC/HPF filter
			kdfx::smoothDamp(hpfL_, wetL, hpfCoeff_);
			wetL -= hpfL_;
			kdfx::smoothDamp(hpfR_, wetR, hpfCoeff_);
			wetR -= hpfR_;

			// Mix wet into main render
			kdfx::mixWet(s.l, wetL, getPanLeft());
			kdfx::mixWet(s.r, wetR, getPanRight());

			writePos_ = (writePos_ + 1) % kMaxDelay;
		}
	}

	void setRoomSize(float value) override {
		time_val_ = value;
		// Delay time: 1 ms (44 samples) to 1500 ms (66150 samples)
		delaySamples_ = static_cast<size_t>(44 + value * (kMaxDelay - 45));
	}
	[[nodiscard]] float getRoomSize() const override { return time_val_; }

	void setDecay(float value) override {
		feedback_val_ = value;
		feedback_ = static_cast<q31_t>(std::clamp(value * 0.98f, 0.0f, 0.98f) * ONE_Q31f);
	}
	[[nodiscard]] float getDecay() const override { return feedback_val_; }

	void setDamping(float value) override {
		damping_val_ = value;
		// HF Damping: 0 (dark) to 1.0 (open)
		dampCoeff_ = static_cast<q31_t>((0.05f + value * 0.95f) * ONE_Q31f);
	}
	[[nodiscard]] float getDamping() const override { return damping_val_; }

	void setDiffusion(float value) override {
		crossfeed_val_ = value;
		// Ping-pong crossfeed: 0 (dual mono) to 1.0 (full ping-pong)
		crossfeed_ = static_cast<q31_t>(value * ONE_Q31f);
	}
	[[nodiscard]] float getDiffusion() const override { return crossfeed_val_; }

	void setWidth(float value) override {
		width_val_ = value;
		// Tap spread ratio for Tap 2: 0.5 to 0.95
		tapSpread_ = 0.50f + value * 0.45f;
	}
	[[nodiscard]] float getWidth() const override { return width_val_; }

	void setHPF(float f) override {
		hpf_val_ = f;
		float fc_hz = 20.0f + f * 980.0f;
		float fc = fc_hz / float(kSampleRate);
		float wc = fc / (1.0f + fc);
		hpfCoeff_ = static_cast<q31_t>(std::clamp(wc, 0.0005f, 0.45f) * ONE_Q31f);
	}
	[[nodiscard]] float getHPF() const override { return hpf_val_; }

private:
	static constexpr size_t kMaxDelay = 32768; // ~743 ms buffer @ 44.1k

	std::array<q31_t, kMaxDelay> bufL_{};
	std::array<q31_t, kMaxDelay> bufR_{};

	size_t writePos_ = 0;
	size_t delaySamples_ = 16384;
	float tapSpread_ = 0.75f;

	q31_t feedback_ = static_cast<q31_t>(0.5f * ONE_Q31f);
	q31_t dampCoeff_ = static_cast<q31_t>(0.8f * ONE_Q31f);
	q31_t crossfeed_ = 0;
	q31_t dampL_ = 0, dampR_ = 0;
	q31_t hpfL_ = 0, hpfR_ = 0;
	q31_t hpfCoeff_ = 0;

	float time_val_ = 0.5f, feedback_val_ = 0.5f, damping_val_ = 0.5f, crossfeed_val_ = 0.0f, width_val_ = 0.5f, hpf_val_ = 0.0f;
};

} // namespace deluge::dsp::reverb
