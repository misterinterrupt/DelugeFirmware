/*
 * KDFX-style distortion (manual algorithms 724-728 family, 1-3 PAU).
 *
 * Insert engine: pre locut -> drive gain -> multi-mode shaper (the tone
 * engine's curves: Soft Tube / Tape Warmth / Hard Clip / Wavefold) ->
 * cabinet simulation crossfade -> post hicut, with a DC blocker after the
 * asymmetric Tape mode. The manual's "+EQ" variants are this bloc chained
 * with KDFX TONE in series routing, not a separate algorithm.
 *
 * No feedback loop anywhere — loop-gain rule trivially satisfied; LEVEL on
 * insert engines is the wet/dry blend (parallel distortion = turn it down).
 */

#pragma once

#include "definitions_cxx.hpp"
#include "dsp/reverb/base.hpp"
#include "dsp/reverb/kdfx/kdfx_util.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>

namespace deluge::dsp::reverb {

class KdfxDistortion : public Base {
public:
	KdfxDistortion() = default;
	~KdfxDistortion() override = default;

	[[nodiscard]] bool isInsert() const override { return true; }

	// Never called for insert engines (FxBloc::process branches on isInsert()).
	void process(std::span<int32_t> input, std::span<StereoSample> output) override {}

	void processInsert(std::span<StereoSample> buffer) override {
		for (auto& s : buffer) {
			s.l = shapeChannel(s.l, locutL_, cabLpL_, hicutLpL_, dcL_);
			s.r = shapeChannel(s.r, locutR_, cabLpR_, hicutLpR_, dcR_);
		}
	}

	// roomSize -> drive (0 to +24 dB into the shaper)
	void setRoomSize(float value) override {
		drive_val_ = value;
		float linearGain = std::pow(10.0f, (value * 24.0f) / 20.0f);
		driveGain_ = static_cast<int32_t>(linearGain * 4096.0f);
	}
	[[nodiscard]] float getRoomSize() const override { return drive_val_; }

	void setShaperMode(int32_t value) override { shaperMode_ = std::clamp<int32_t>(value, 0, 3); }
	[[nodiscard]] int32_t getShaperMode() const override { return shaperMode_; }

	// LoCut -> pre-shaper highpass, 20 to 300 Hz
	void setLoCut(float value) override {
		locut_val_ = value;
		locutCoeff_ = coeffForHz(20.0f + value * 280.0f);
	}
	[[nodiscard]] float getLoCut() const override { return locut_val_; }

	// CabSim -> crossfade toward a fixed speaker-cab lowpass (~4.5 kHz) with a
	// slight top-end trim; 0% bypasses the cabinet entirely.
	void setCab(float value) override {
		cab_val_ = std::clamp(value, 0.0f, 1.0f);
		cabQ31_ = static_cast<q31_t>(cab_val_ * ONE_Q31f);
	}
	[[nodiscard]] float getCab() const override { return cab_val_; }

	// HiCut -> post-shaper lowpass, 1 kHz to 16 kHz
	void setHiCut(float value) override {
		hicut_val_ = value;
		hicutCoeff_ = coeffForHz(1000.0f + value * 15000.0f);
	}
	[[nodiscard]] float getHiCut() const override { return hicut_val_; }

private:
	static q31_t coeffForHz(float fcHz) {
		float fc = fcHz / float(kSampleRate);
		float wc = fc / (1.0f + fc);
		return static_cast<q31_t>(std::clamp(wc, 0.00005f, 0.45f) * ONE_Q31f);
	}

	q31_t shapeChannel(q31_t x, q31_t& locutLp, q31_t& cabLp, q31_t& hicutLp, q31_t& dcLp) {
		// 1. Pre locut
		kdfx::smoothDamp(locutLp, x, locutCoeff_);
		q31_t locut = x - locutLp;

		// 2. Drive
		int64_t driven = (static_cast<int64_t>(locut) * driveGain_) >> 12;
		q31_t clampedIn = kdfx::qcollapse(driven);

		// 3. Multi-mode saturation (tone engine curves)
		q31_t shaped;
		switch (shaperMode_) {
		case 1: { // Tape Warmth (asymmetric, even harmonics)
			int64_t x_sq = (static_cast<int64_t>(clampedIn) * clampedIn) >> 31;
			int64_t x_cu = (x_sq * clampedIn) >> 31;
			int64_t y = clampedIn - (x_sq >> 2) - (x_cu >> 2);
			shaped = kdfx::shaperClamp(y);
			break;
		}
		case 2: { // Hard Clip with soft knee
			if (clampedIn > 0x60000000) {
				shaped = 0x60000000 + ((clampedIn - 0x60000000) >> 3);
			}
			else if (clampedIn < -0x60000000) {
				shaped = -0x60000000 + ((clampedIn + 0x60000000) >> 3);
			}
			else {
				shaped = clampedIn;
			}
			break;
		}
		case 3: { // Wavefold
			uint32_t phase = static_cast<uint32_t>(clampedIn) ^ 0x80000000u;
			int32_t p = static_cast<int32_t>(phase) - 0x40000000;
			int64_t absP = std::abs(static_cast<int64_t>(p));
			int64_t oneMinusAbsP = 0x7FFFFFFF - absP;
			int64_t y = (static_cast<int64_t>(p) * oneMinusAbsP) >> 29;
			shaped = kdfx::qcollapse(y);
			break;
		}
		case 0: // Soft Tube (1.5x - 0.5x^3)
		default: {
			int64_t x_sq = (static_cast<int64_t>(clampedIn) * clampedIn) >> 31;
			int64_t x_cu = (x_sq * clampedIn) >> 31;
			int64_t term1 = (3LL * clampedIn) >> 1;
			int64_t term2 = x_cu >> 1;
			shaped = kdfx::shaperClamp(term1 - term2);
			break;
		}
		}

		// 4. Cabinet: crossfade to the cab lowpass with a small trim.
		kdfx::smoothDamp(cabLp, shaped, cabCoeff_);
		q31_t cabbed = kdfx::q31_mul(cabLp, kCabTrimQ31);
		shaped = add_saturation(kdfx::q31_mul(shaped, ONE_Q31 - cabQ31_), kdfx::q31_mul(cabbed, cabQ31_));

		// 5. Post hicut
		kdfx::smoothDamp(hicutLp, shaped, hicutCoeff_);
		q31_t out = hicutLp;

		// 6. DC blocker (Tape mode is asymmetric)
		kdfx::smoothDamp(dcLp, out, kDcCoeff);
		return out - dcLp;
	}

	static constexpr q31_t kCabTrimQ31 = static_cast<q31_t>(0.92f * ONE_Q31f);
	static constexpr q31_t kDcCoeff = static_cast<q31_t>(0.0002f * ONE_Q31f); // ~10 Hz
	static constexpr q31_t cabCoeff_ = []() constexpr {
		float fc = 4500.0f / float(kSampleRate);
		return static_cast<q31_t>((fc / (1.0f + fc)) * ONE_Q31f);
	}();

	int32_t driveGain_ = 4096;
	int32_t shaperMode_ = 0;

	q31_t locutCoeff_ = coeffForHz(20.0f);
	q31_t hicutCoeff_ = coeffForHz(16000.0f);
	q31_t cabQ31_ = 0;

	q31_t locutL_ = 0, locutR_ = 0;
	q31_t cabLpL_ = 0, cabLpR_ = 0;
	q31_t hicutLpL_ = 0, hicutLpR_ = 0;
	q31_t dcL_ = 0, dcR_ = 0;

	float drive_val_ = 0.25f;
	float locut_val_ = 0.f;
	float cab_val_ = 0.f;
	float hicut_val_ = 1.0f;
};

} // namespace deluge::dsp::reverb
