/*
 * KDFX-style 4-tap stereo delay.
 *
 * Q31 fixed-point delay line with Hadamard cross-feedback matrix,
 * tape saturation drive, phase inversion toggle, damping filter,
 * and ping-pong stereo width.
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

class KdfxDelay : public Base {
public:
	KdfxDelay() = default;
	~KdfxDelay() override = default;

	void process(std::span<int32_t> in, std::span<StereoSample> output) override {
		for (size_t frame = 0; frame < in.size(); frame++) {
			StereoSample& s = output[frame];
			q31_t inputSample = in[frame];

			// 4 distinct delay tap reads (Tap 1 & 3 Left, Tap 2 & 4 Right)
			size_t d1 = std::clamp<size_t>(delaySamplesL_, 1, kMaxDelay - 1);
			size_t d2 = std::clamp<size_t>(delaySamplesR_, 1, kMaxDelay - 1);
			size_t d3 = std::clamp<size_t>(static_cast<size_t>(delaySamplesL_ * tapSpread_), 1, kMaxDelay - 1);
			size_t d4 = std::clamp<size_t>(static_cast<size_t>(delaySamplesR_ * tapSpread_), 1, kMaxDelay - 1);

			q31_t tap1 = kdfx::circRead(bufL_, writePos_, d1);
			q31_t tap2 = kdfx::circRead(bufR_, writePos_, d2);
			q31_t tap3 = kdfx::circRead(bufL_, writePos_, d3);
			q31_t tap4 = kdfx::circRead(bufR_, writePos_, d4);

			// Feedback loop with HF absorption damping
			kdfx::smoothDamp(dampL_, tap1, dampCoeff_);
			kdfx::smoothDamp(dampR_, tap2, dampCoeff_);

			// Tape saturation drive on feedback path: y = 1.5 * x - 0.5 * x^3
			auto applyTapeDrive = [this](q31_t x) -> q31_t {
				if (tapeDriveGain_ <= 4096) {
					return x;
				}
				int64_t driven = (static_cast<int64_t>(x) * tapeDriveGain_) >> 12;
				q31_t c = kdfx::qcollapse(driven);
				int64_t x_sq = (static_cast<int64_t>(c) * c) >> 31;
				int64_t x_cu = (x_sq * c) >> 31;
				int64_t y = ((3LL * c) >> 1) - (x_cu >> 1);
				return kdfx::shaperClamp(y);
			};

			q31_t satL = applyTapeDrive(dampL_);
			q31_t satR = applyTapeDrive(dampR_);

			q31_t fbL = kdfx::q31_mul(satL, feedback_);
			q31_t fbR = kdfx::q31_mul(satR, feedback_);

			// Crossfeed / PingPong mixing with optional phase inversion
			q31_t crossInvertL = phaseInvert_ ? -fbR : fbR;
			q31_t crossInvertR = phaseInvert_ ? -fbL : fbL;

			q31_t directL = kdfx::q31_mul(fbL, ONE_Q31 - crossfeed_);
			q31_t crossL = kdfx::q31_mul(crossInvertL, crossfeed_);
			q31_t nextL = add_saturation(inputSample, add_saturation(directL, crossL));

			q31_t directR = kdfx::q31_mul(fbR, ONE_Q31 - crossfeed_);
			q31_t crossR = kdfx::q31_mul(crossInvertR, crossfeed_);
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
		setDelayTimeL(value);
	}
	[[nodiscard]] float getRoomSize() const override { return time_l_val_; }

	void setDelayTimeL(float value) override {
		time_l_val_ = value;
		// 1.0 ms (44 samples) to 1480.0 ms (65268 samples)
		delaySamplesL_ = static_cast<size_t>(44 + value * (kMaxDelay - 200));
	}
	[[nodiscard]] float getDelayTimeL() const override { return time_l_val_; }

	void setDelayTimeR(float value) override {
		time_r_val_ = value;
		delaySamplesR_ = static_cast<size_t>(44 + value * (kMaxDelay - 200));
	}
	[[nodiscard]] float getDelayTimeR() const override { return time_r_val_; }

	void setDecay(float value) override {
		feedback_val_ = value;
		feedback_ = static_cast<q31_t>(std::clamp(value * 0.99f, 0.0f, 0.99f) * ONE_Q31f);
	}
	[[nodiscard]] float getDecay() const override { return feedback_val_; }

	void setDamping(float value) override {
		damping_val_ = value;
		dampCoeff_ = static_cast<q31_t>((0.05f + value * 0.95f) * ONE_Q31f);
	}
	[[nodiscard]] float getDamping() const override { return damping_val_; }

	void setDiffusion(float value) override {
		crossfeed_val_ = value;
		crossfeed_ = static_cast<q31_t>(value * ONE_Q31f);
	}
	[[nodiscard]] float getDiffusion() const override { return crossfeed_val_; }

	void setWidth(float value) override {
		width_val_ = value;
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

	void setDelayDrive(float value) override {
		drive_val_ = value;
		// 0.0 dB (4096) to +18.0 dB (~32488 in Q12 = 7.94x)
		float linearGain = std::pow(10.0f, (value * 18.0f) / 20.0f);
		tapeDriveGain_ = static_cast<int32_t>(linearGain * 4096.0f);
	}
	[[nodiscard]] float getDelayDrive() const override { return drive_val_; }

	void setPhaseInvert(bool value) override {
		phaseInvert_ = value;
	}
	[[nodiscard]] bool getPhaseInvert() const override { return phaseInvert_; }

private:
	static constexpr size_t kMaxDelay = 65536; // ~1486 ms buffer @ 44.1k

	std::array<q31_t, kMaxDelay> bufL_{};
	std::array<q31_t, kMaxDelay> bufR_{};

	size_t writePos_ = 0;
	size_t delaySamplesL_ = 16384;
	size_t delaySamplesR_ = 24576;
	float tapSpread_ = 0.75f;

	q31_t feedback_ = static_cast<q31_t>(0.5f * ONE_Q31f);
	q31_t dampCoeff_ = static_cast<q31_t>(0.8f * ONE_Q31f);
	q31_t crossfeed_ = 0;
	int32_t tapeDriveGain_ = 4096;
	bool phaseInvert_ = false;

	q31_t dampL_ = 0, dampR_ = 0;
	q31_t hpfL_ = 0, hpfR_ = 0;
	q31_t hpfCoeff_ = 0;

	float time_l_val_ = 0.35f, time_r_val_ = 0.50f, feedback_val_ = 0.5f;
	float damping_val_ = 0.5f, crossfeed_val_ = 0.0f, width_val_ = 0.5f, hpf_val_ = 0.0f;
	float drive_val_ = 0.0f;
};

} // namespace deluge::dsp::reverb
