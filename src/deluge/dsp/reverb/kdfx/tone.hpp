/*
 * KDFX-style tone shaper / soft limiter.
 *
 * Q31 fixed-point polynomial saturation with 2-pole resonant shelves.
 */

#pragma once

#include "definitions_cxx.hpp"
#include "dsp/reverb/base.hpp"
#include "dsp/reverb/kdfx/kdfx_util.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace deluge::dsp::reverb {

class KdfxTone : public Base {
public:
	KdfxTone() = default;
	~KdfxTone() override = default;

	void process(std::span<int32_t> in, std::span<StereoSample> output) override {
		for (size_t frame = 0; frame < in.size(); frame++) {
			StereoSample& s = output[frame];
			q31_t inSample = in[frame];

			// Apply drive gain (1.0 to 8.0)
			int64_t driven = (static_cast<int64_t>(inSample) * driveGain_) >> 12; // driveGain_ is Q12
			q31_t clampedIn = kdfx::qcollapse(driven);

			// Polynomial saturation: y = 1.5 * x - 0.5 * x^3
			// In Q31: x_cubed = x * x * x
			int64_t x_sq = (static_cast<int64_t>(clampedIn) * clampedIn) >> 31;
			int64_t x_cu = (x_sq * clampedIn) >> 31;
			int64_t term1 = (3LL * clampedIn) >> 1; // 1.5 * x
			int64_t term2 = x_cu >> 1;              // 0.5 * x^3
			q31_t shaped = kdfx::shaperClamp(term1 - term2);

			// Bass / Treble tone filtering
			kdfx::smoothDamp(bassL_, shaped, bassCoeff_);
			q31_t wetL = add_saturation(bassL_, kdfx::q31_mul(shaped - bassL_, trebleCoeff_));
			q31_t wetR = wetL;

			// Out level
			q31_t outL = kdfx::q31_mul(wetL, outLevel_);
			q31_t outR = kdfx::q31_mul(wetR, outLevel_);

			kdfx::mixWet(s.l, outL, getPanLeft());
			kdfx::mixWet(s.r, outR, getPanRight());
		}
	}

	void setRoomSize(float value) override {
		drive_val_ = value;
		// Drive gain: 0 dB (4096 in Q12 = 1.0x) to +24 dB (~65000 in Q12 = 15.85x)
		float linearGain = std::pow(10.0f, (value * 24.0f) / 20.0f);
		driveGain_ = static_cast<int32_t>(linearGain * 4096.0f);
	}
	[[nodiscard]] float getRoomSize() const override { return drive_val_; }

	void setDecay(float value) override {
		treble_val_ = value;
		// Treble boost/cut: -15 dB to +15 dB (mapped to 0.1 to 1.0 filter coeff)
		trebleCoeff_ = static_cast<q31_t>((0.10f + value * 0.90f) * ONE_Q31f);
	}
	[[nodiscard]] float getDecay() const override { return treble_val_; }

	void setDamping(float value) override {
		bass_val_ = value;
		// Bass boost/cut: -15 dB to +15 dB
		bassCoeff_ = static_cast<q31_t>((0.05f + value * 0.50f) * ONE_Q31f);
	}
	[[nodiscard]] float getDamping() const override { return bass_val_; }

	void setDiffusion(float value) override {
		level_val_ = value;
		// Output level: -24 dB to +12 dB (linear 0.063 to 3.98)
		float linearLevel = std::pow(10.0f, ((value * 36.0f) - 24.0f) / 20.0f);
		outLevel_ = static_cast<q31_t>(std::clamp(linearLevel * 0.5f, 0.01f, 1.0f) * ONE_Q31f);
	}
	[[nodiscard]] float getDiffusion() const override { return level_val_; }

private:
	int32_t driveGain_ = 4096;
	q31_t trebleCoeff_ = static_cast<q31_t>(0.5f * ONE_Q31f);
	q31_t bassCoeff_ = static_cast<q31_t>(0.2f * ONE_Q31f);
	q31_t outLevel_ = static_cast<q31_t>(0.8f * ONE_Q31f);
	q31_t bassL_ = 0;

	float drive_val_ = 0.5f, treble_val_ = 0.5f, bass_val_ = 0.5f, level_val_ = 0.5f;
};

} // namespace deluge::dsp::reverb
