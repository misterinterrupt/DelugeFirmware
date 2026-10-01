/*
 * KDFX-style multi-tap chorus / flanger.
 *
 * Q31 fixed-point modulated delay lines with cross-coupling and
 * stereo spread.
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

			// LFO phase advance
			lfoPhase_ += lfoInc_;

			// Parabolic smooth sine approximation in Q31: y = 4 * x * (1 - |x|)
			auto fastSineQ31 = [](uint32_t phase) -> q31_t {
				int32_t x = static_cast<int32_t>(phase) - 0x40000000;
				int64_t absX = std::abs(x);
				int64_t oneMinusAbsX = 0x7FFFFFFF - absX;
				int64_t y = (static_cast<int64_t>(x) * oneMinusAbsX) >> 29;
				return kdfx::qcollapse(y);
			};

			uint32_t phaseL = lfoPhase_;
			uint32_t phaseR = lfoPhase_ + phaseOffset_;

			q31_t sineL = fastSineQ31(phaseL);
			q31_t sineR = fastSineQ31(phaseR);

			// Delay in Q16 (integer delay in high 16 bits, sub-sample fraction in low 16 bits)
			int32_t modQ16L = static_cast<int32_t>((static_cast<int64_t>(sineL) * depthQ16_) >> 31);
			int32_t modQ16R = static_cast<int32_t>((static_cast<int64_t>(sineR) * depthQ16_) >> 31);

			int32_t delayQ16L = std::clamp<int32_t>(baseDelayQ16_ + modQ16L, 65536, static_cast<int32_t>((kMaxDelay - 2) << 16));
			int32_t delayQ16R = std::clamp<int32_t>(baseDelayQ16_ + modQ16R, 65536, static_cast<int32_t>((kMaxDelay - 2) << 16));

			// Fractional linear interpolation for sub-sample Doppler pitch modulation
			auto readInterp = [this](const std::array<q31_t, kMaxDelay>& buf, int32_t delayQ16) -> q31_t {
				size_t intPart = static_cast<size_t>(delayQ16 >> 16);
				uint32_t fracPart = static_cast<uint32_t>(delayQ16 & 0xFFFF);
				q31_t s0 = kdfx::circRead(buf, writePos_, intPart);
				q31_t s1 = kdfx::circRead(buf, writePos_, intPart + 1);
				return s0 + static_cast<q31_t>((static_cast<int64_t>(s1 - s0) * fracPart) >> 16);
			};

			q31_t tapL = readInterp(bufL_, delayQ16L);
			q31_t tapR = readInterp(bufR_, delayQ16R);

			// Feedback with saturation
			bufL_[writePos_] = add_saturation(inputSample, kdfx::q31_mul(tapL, feedback_));
			bufR_[writePos_] = add_saturation(inputSample, kdfx::q31_mul(tapR, feedback_));

			q31_t wetL = tapL;
			q31_t wetR = tapR;

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
		rate_val_ = value;
		// LFO rate: 0.1 Hz to 10.0 Hz
		float freqHz = 0.1f + value * 9.9f;
		lfoInc_ = static_cast<uint32_t>((freqHz / 44100.0f) * 4294967296.0f);
	}
	[[nodiscard]] float getRoomSize() const override { return rate_val_; }

	void setDecay(float value) override {
		depth_val_ = value;
		// Depth: 0 to ~7 ms mod depth (up to 300 samples in Q16)
		depthQ16_ = static_cast<int32_t>(value * (300 << 16));
	}
	[[nodiscard]] float getDecay() const override { return depth_val_; }

	void setDamping(float value) override {
		feedback_val_ = value;
		// Resonance/feedback: 0 to 0.90
		feedback_ = static_cast<q31_t>(std::clamp(value * 0.90f, 0.0f, 0.90f) * ONE_Q31f);
	}
	[[nodiscard]] float getDamping() const override { return feedback_val_; }

	void setDiffusion(float value) override {
		width_val_ = value;
		// Stereo L/R phase quadrature: 0 to 180 degrees
		phaseOffset_ = static_cast<uint32_t>(value * 0x80000000u);
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
	static constexpr size_t kMaxDelay = 2048; // ~46 ms buffer

	std::array<q31_t, kMaxDelay> bufL_{};
	std::array<q31_t, kMaxDelay> bufR_{};

	size_t writePos_ = 0;
	int32_t baseDelayQ16_ = 350 << 16; // ~8 ms base delay
	int32_t depthQ16_ = 150 << 16;
	uint32_t lfoPhase_ = 0;
	uint32_t lfoInc_ = 100000;
	uint32_t phaseOffset_ = 0x40000000u; // 90 deg quadrature default

	q31_t feedback_ = 0;
	q31_t hpfL_ = 0, hpfR_ = 0;
	q31_t hpfCoeff_ = 0;

	float rate_val_ = 0.5f, depth_val_ = 0.5f, feedback_val_ = 0.0f, width_val_ = 0.5f, hpf_val_ = 0.0f;
};

} // namespace deluge::dsp::reverb
