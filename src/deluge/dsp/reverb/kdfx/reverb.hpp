/*
 * KDFX-style 4-line orthogonal FDN reverb tank (the "2-PAU" equivalent).
 *
 * Q31 throughout. Four prime-length delay lines feed an O(N) Householder
 * matrix (energy-preserving: loop gain is set purely by the per-line decay
 * scalar and damping filter, never by the matrix). Three cascaded input
 * allpass diffusers smear transients *before* the network — fed raw, a 4-line
 * FDN chatters on snares/rimshots because modal density is low. Per-line
 * absorption is a smooth sweepable Q31 1-pole (not octave-shifted) with a
 * partial "air" blend so the tail keeps a hint of sparkle, plus TPDF dither
 * to keep integer limit cycles from idling.
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

class KdfxReverb : public Base {
public:
	KdfxReverb() = default;
	~KdfxReverb() override = default;

	void process(std::span<int32_t> in, std::span<StereoSample> output) override {
		for (size_t frame = 0; frame < in.size(); frame++) {
			StereoSample& s = output[frame];

			// --- Input diffusers (KDFX "In Diff"): 3 cascaded allpasses ---
			q31_t diffused = ap1_.process(in[frame] >> 1, kApCoef1);
			diffused = ap2_.process(diffused, kApCoef2);
			diffused = ap3_.process(diffused, kApCoef1);

			// --- Write into the four line heads with TPDF dither ---
			int32_t pos0 = writePos_;
			buf_[0][pos0] = add_saturation(diffused, dither0_);
			buf_[1][pos0] = add_saturation(diffused, dither1_);
			buf_[2][pos0] = add_saturation(diffused, dither2_);
			buf_[3][pos0] = add_saturation(diffused, dither3_);

			// --- Read the four line midpoints, apply per-line absorption ---
			q31_t x[4];
			for (uint32_t l = 0; l < 4; l++) {
				size_t len = lineLen_[l];
				x[l] = kdfx::circRead(buf_[l], pos0, len >> 1);

				// smooth sweepable HF absorption + "air" shelf blend
				kdfx::shelfDamp(dampState_[l], airState_[l], x[l], dampCoeff_, air_);
				x[l] = add_saturation(dampState_[l],
				                      multiply_32x32_rshift32_rounded(airState_[l], air_));

				// decay scalar: the *only* gain control in the loop
				x[l] = multiply_32x32_rshift32_rounded(x[l], decay_) << 1;
			}

			// --- Householder feedback matrix, O(N): y_i = x_i - (sum >> 1) ---
			int64_t sum = static_cast<int64_t>(x[0]) + x[1] + x[2] + x[3];
			q31_t fb[4];
			for (uint32_t l = 0; l < 4; l++) {
				fb[l] = kdfx::householder4(x[l], sum);
			}

			// --- Feed matrix outputs back into the line tails ---
			for (uint32_t l = 0; l < 4; l++) {
				size_t len = lineLen_[l];
				size_t writePos = (pos0 + kBufSize - len) % kBufSize;
				buf_[l][writePos] = add_saturation(buf_[l][writePos], fb[l]);
			}

			// --- Stereo output taps: decorrelated points on the four lines.
			// Base offset is each line's midpoint; tapSpread_ (from Width)
			// pushes the L/R tap points apart for decorrelation, clamped
			// inside each line so narrow settings read near-simultaneous.
			auto tapOffset = [&](uint32_t l, uint32_t spread) {
				size_t len = lineLen_[l];
				size_t off = len / 2 + ((len * spread * tapSpread_) >> 5);
				return off % len;
			};
			q31_t l0 = kdfx::circRead(buf_[0], pos0, tapOffset(0, 5));
			q31_t l1 = kdfx::circRead(buf_[2], pos0, tapOffset(2, 7));
			q31_t r0 = kdfx::circRead(buf_[1], pos0, tapOffset(1, 6));
			q31_t r1 = kdfx::circRead(buf_[3], pos0, tapOffset(3, 8));
			q31_t wetL = kdfx::qcollapse((int64_t)l0 + l1);
			q31_t wetR = kdfx::qcollapse((int64_t)r0 + r1);

			// --- Output rumble/DC filter ---
			kdfx::smoothDamp(hpfL_, wetL, outHpfCoeff_);
			wetL -= hpfL_;
			kdfx::smoothDamp(hpfR_, wetR, outHpfCoeff_);
			wetR -= hpfR_;

			// --- advance dither states ---
			dither0_ = kdfx::tpdf_dither(rng0_);
			dither2_ = kdfx::tpdf_dither(rng1_);
			dither1_ = dither0_;
			dither3_ = dither2_;

			// --- Mix with the engine's pan levels ---
			kdfx::mixInto(s.l, multiply_32x32_rshift32_rounded(wetL, getPanLeft()));
			kdfx::mixInto(s.r, multiply_32x32_rshift32_rounded(wetR, getPanRight()));

			writePos_ = (writePos_ + 1) % kBufSize;
		}
	}

	// --- reverb::Base parameter hooks (full-resolution mapping to Q31) ---

	void setRoomSize(float value) override {
		room_size_ = value;
		// Scale prime line lengths between tight (x1) and vast (x8) rooms;
		// prime ratios keep the modes decorrelated at every scale.
		const float scale = 1.f + value * 7.f;
		for (uint32_t l = 0; l < 4; l++) {
			lineLen_[l] = std::max<size_t>(kMinLine, (size_t)(kPrimeLen[l] * scale));
		}
	}
	[[nodiscard]] float getRoomSize() const override { return room_size_; }

	void setDamping(float value) override {
		damping_ = value;
		// Continuous one-pole coefficient: value 0 → coeff 0.06 (~470 Hz @44.1k,
		// dark tail), value 1 → coeff 1.0 (open). y += (x-y)*coeff →
		// corner ≈ coeff * fs / (2π) for small coeff.
		const float coeff = 0.06f + value * 0.94f;
		dampCoeff_ = (q31_t)(coeff * ONE_Q31f);
		// Air band: reintroduce up to 25% of the removed highs when open.
		air_ = (q31_t)(value * 0.25f * ONE_Q31f);
		// Decay: loop gain below unity; value 1 = longest tail.
		decay_ = (q31_t)(0.70f * ONE_Q31f + value * 0.29f * ONE_Q31f); // 0.70 .. 0.99
	}
	[[nodiscard]] float getDamping() const override { return damping_; }

	void setWidth(float value) override {
		width_ = value;
		// Width controls stereo decorrelation via the output tap spread:
		// narrow = near-simultaneous taps (mono-ish), wide = maximally
		// decorrelated offsets. Implemented as a scale on the tap offsets.
		tapSpread_ = 1u + (uint32_t)(value * 3u); // 1..4
	}
	[[nodiscard]] float getWidth() const override { return width_; }

	void setHPF(float f) override {
		hpf_ = f;
		// 20..520 Hz corner from Base::calcFilterCutoff → continuous coeff
		float wc = calcFilterCutoff(f); // normalized 0..~0.5
		outHpfCoeff_ = (q31_t)(std::clamp(wc, 0.0005f, 0.45f) * ONE_Q31f);
	}
	[[nodiscard]] float getHPF() const override { return hpf_; }

private:
	static constexpr size_t kBufSize = 17408; // per line; must exceed max prime len * 8 scale (2137*8 = 17096)
	static constexpr size_t kMinLine = 128;
	// prime delays (samples): the classic 4-line FDN set
	static constexpr size_t kPrimeLen[4] = {1087, 1453, 1789, 2137};
	// input diffuser coefficients (0.618 = the diffusion sweet spot)
	static constexpr q31_t kApCoef1 = (q31_t)(0.618f * ONE_Q31f);
	static constexpr q31_t kApCoef2 = (q31_t)(0.5f * ONE_Q31f);

	std::array<std::array<q31_t, kBufSize>, 4> buf_{}; // 4 lines × 64 KB

	size_t writePos_ = 0;
	size_t lineLen_[4] = {1087, 1453, 1789, 2137};
	uint32_t tapSpread_ = 2;

	// cascaded input diffusers (KDFX "In Diff"): 142/277/193 samples
	kdfx::AllPass<142> ap1_;
	kdfx::AllPass<277> ap2_;
	kdfx::AllPass<193> ap3_;

	// per-line absorption state
	q31_t dampState_[4] = {0, 0, 0, 0};
	q31_t airState_[4] = {0, 0, 0, 0};
	q31_t dampCoeff_ = (q31_t)(0.5f * ONE_Q31f);
	q31_t air_ = 0;
	q31_t decay_ = (q31_t)(0.85f * ONE_Q31f);

	// output DC/rumble filter state
	q31_t hpfL_ = 0, hpfR_ = 0;
	q31_t outHpfCoeff_ = 0;

	// dither states
	uint32_t rng0_ = 0x9E3779B9u, rng1_ = 0x853C49E7u;
	q31_t dither0_ = 0, dither1_ = 0, dither2_ = 0, dither3_ = 0;

	float room_size_ = 0.5f, damping_ = 0.5f, width_ = 0.5f, hpf_ = 0.f;
};

} // namespace deluge::dsp::reverb
