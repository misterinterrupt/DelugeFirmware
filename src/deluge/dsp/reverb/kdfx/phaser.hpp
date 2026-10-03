/*
 * KDFX-style phaser (manual algorithms 156-160, 1 PAU).
 *
 * Four cascaded one-pole allpass sections per channel (H(z) = (a + z^-1) /
 * (1 + a z^-1)), LFO-swept coefficient, moderated cascade feedback, staggered
 * LFO phase between channels. State-only — no delay tank, zero buffer cost.
 * Send-style wet-add output.
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

class KdfxPhaser : public Base {
public:
	KdfxPhaser() = default;
	~KdfxPhaser() override = default;

	void process(std::span<int32_t> in, std::span<StereoSample> output) override {
		for (size_t frame = 0; frame < in.size(); frame++) {
			StereoSample& s = output[frame];
			q31_t inputSample = in[frame];

			lfoPhase_ += lfoInc_;

			q31_t lfoL = kdfx::parabolicLfo(lfoPhase_);
			q31_t lfoR = kdfx::parabolicLfo(lfoPhase_ + phaseQuadrature_);

			// Cascade input with moderated feedback.
			q31_t driveL = add_saturation(inputSample, kdfx::q31_mul(lastOutL_, feedbackQ31_));
			q31_t driveR = add_saturation(inputSample, kdfx::q31_mul(lastOutR_, feedbackQ31_));

			q31_t wetL = cascadeL_.process(driveL, sweepCoeff(lfoL));
			q31_t wetR = cascadeR_.process(driveR, sweepCoeff(lfoR));
			lastOutL_ = wetL;
			lastOutR_ = wetR;

			// DC block (feedback loops accumulate offset).
			kdfx::smoothDamp(hpfL_, wetL, hpfCoeff_);
			wetL -= hpfL_;
			kdfx::smoothDamp(hpfR_, wetR, hpfCoeff_);
			wetR -= hpfR_;

			kdfx::mixWet(s.l, wetL, getPanLeft());
			kdfx::mixWet(s.r, wetR, getPanRight());
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
		depth_val_ = value; // sweep range 0..1; folded into sweepCoeff()
	}
	[[nodiscard]] float getDecay() const override { return depth_val_; }

	void setDamping(float value) override {
		// Cascade feedback 0..0.7
		feedback_val_ = std::clamp(value, 0.0f, 1.0f) * 0.7f;
		feedbackQ31_ = static_cast<q31_t>(feedback_val_ * ONE_Q31f);
	}
	[[nodiscard]] float getDamping() const override { return feedback_val_ / 0.7f; }

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
	/// Four cascaded one-pole allpass sections: y = a*x + x1 - a*y1.
	struct Cascade {
		struct Stage {
			q31_t x1 = 0;
			q31_t y1 = 0;
		};
		std::array<Stage, 4> stages{};

		q31_t process(q31_t x, q31_t a) {
			for (auto& st : stages) {
				q31_t y = add_saturation(add_saturation(kdfx::q31_mul(a, x), st.x1), -kdfx::q31_mul(a, st.y1));
				st.x1 = x;
				st.y1 = y;
				x = y;
			}
			return x;
		}
	};

	/// Sweep the allpass coefficient around 0.55; depth scales the excursion.
	q31_t sweepCoeff(q31_t lfo) {
		float a = 0.55f + 0.35f * depth_val_ * (static_cast<float>(lfo) / 2147483647.0f);
		return static_cast<q31_t>(std::clamp(a, 0.05f, 0.95f) * ONE_Q31f);
	}

	Cascade cascadeL_;
	Cascade cascadeR_;

	uint32_t lfoPhase_ = 0;
	uint32_t lfoInc_ = 100000;
	uint32_t phaseQuadrature_ = 0x40000000u; // 90 deg

	q31_t lastOutL_ = 0;
	q31_t lastOutR_ = 0;
	q31_t feedbackQ31_ = 0;
	q31_t hpfL_ = 0, hpfR_ = 0;
	q31_t hpfCoeff_ = 0;

	float rate_val_ = 0.3f, depth_val_ = 0.5f, feedback_val_ = 0.2f, spread_val_ = 0.5f, hpf_val_ = 0.0f;
};

} // namespace deluge::dsp::reverb
