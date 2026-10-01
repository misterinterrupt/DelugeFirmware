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

			// 1. Cascaded input diffusers (smear transients into bloom)
			q31_t diffused = ap1_.process(in[frame], apCoef1_);
			diffused = ap2_.process(diffused, apCoef2_);
			diffused = ap3_.process(diffused, apCoef1_);

			// 2. Read delay line endpoints & apply absorption/decay
			q31_t x[4];
			for (uint32_t l = 0; l < 4; l++) {
				// Read exactly at the line delay length
				q31_t delayed = kdfx::circRead(buf_[l], writePos_, lineLen_[l]);

				// Smooth sweepable HF absorption + "air" shelf blend
				kdfx::shelfDamp(dampState_[l], airState_[l], delayed, dampCoeff_, air_);
				q31_t filtered = add_saturation(dampState_[l],
				                                kdfx::q31_mul(airState_[l], air_));

				// Decay scalar in Q31
				x[l] = kdfx::q31_mul(filtered, decay_);
			}

			// 3. O(N) Householder feedback matrix
			int64_t sum = static_cast<int64_t>(x[0]) + x[1] + x[2] + x[3];
			q31_t fb[4];
			fb[0] = kdfx::householder4(x[0], sum);
			fb[1] = kdfx::householder4(x[1], sum);
			fb[2] = kdfx::householder4(x[2], sum);
			fb[3] = kdfx::householder4(x[3], sum);

			// 4. Write: Sum Input + Feedback + Dither into line heads
			buf_[0][writePos_] = add_saturation(diffused, add_saturation(fb[0], dither0_));
			buf_[1][writePos_] = add_saturation(diffused, add_saturation(fb[1], dither1_));
			buf_[2][writePos_] = add_saturation(diffused, add_saturation(fb[2], dither2_));
			buf_[3][writePos_] = add_saturation(diffused, add_saturation(fb[3], dither3_));

			// 5. Stereo output taps (decorrelated mid-line taps, guaranteed >= 1)
			auto safeTapOffset = [&](uint32_t l, uint32_t spread) -> size_t {
				size_t len = lineLen_[l];
				size_t off = (len >> 1) + ((len * spread * tapSpread_) >> 6);
				if (off >= len) {
					off = len - 1;
				}
				return (off < 1) ? 1 : off;
			};

			q31_t l0 = kdfx::circRead(buf_[0], writePos_, safeTapOffset(0, 5));
			q31_t l1 = kdfx::circRead(buf_[2], writePos_, safeTapOffset(2, 7));
			q31_t r0 = kdfx::circRead(buf_[1], writePos_, safeTapOffset(1, 6));
			q31_t r1 = kdfx::circRead(buf_[3], writePos_, safeTapOffset(3, 8));

			q31_t wetL = kdfx::qcollapse((int64_t)l0 + l1);
			q31_t wetR = kdfx::qcollapse((int64_t)r0 + r1);

			// 6. Output rumble/DC filter
			kdfx::smoothDamp(hpfL_, wetL, outHpfCoeff_);
			wetL -= hpfL_;
			kdfx::smoothDamp(hpfR_, wetR, outHpfCoeff_);
			wetR -= hpfR_;

			// 7. Advance dither
			dither0_ = kdfx::tpdf_dither(rng0_);
			dither2_ = kdfx::tpdf_dither(rng1_);
			dither1_ = dither0_;
			dither3_ = dither2_;

			// 8. Mix with engine pan/send levels (mixWet scales with +12dB makeup gain headroom)
			kdfx::mixWet(s.l, wetL, getPanLeft());
			kdfx::mixWet(s.r, wetR, getPanRight());

			// Increment single write head
			writePos_ = (writePos_ + 1) % kBufSize;
		}
	}

	// --- reverb::Base parameter hooks (full-resolution mapping to Q31) ---

	void setRoomSize(float value) override {
		room_size_ = value;
		// Scale prime line lengths between tight (x1) and vast (x3.7) rooms;
		const float scale = 1.f + value * 2.7f;
		for (uint32_t l = 0; l < 4; l++) {
			lineLen_[l] = std::max<size_t>(kMinLine, (size_t)(kPrimeLen[l] * scale));
		}
	}
	[[nodiscard]] float getRoomSize() const override { return room_size_; }

	void setDecay(float value) override {
		decay_val_ = value;
		// RT60 loop gain: value 0 -> 0.40 (very short dry decay), value 1 -> 0.995 (vast lush wash)
		decay_ = static_cast<q31_t>((0.40f + value * 0.595f) * ONE_Q31f);
	}
	[[nodiscard]] float getDecay() const override { return decay_val_; }

	void setDamping(float value) override {
		damping_ = value;
		// Continuous one-pole coefficient: value 0 → coeff 0.05 (~400 Hz @44.1k, dark),
		// value 1 → coeff 1.0 (open/sparkling)
		const float coeff = 0.05f + value * 0.95f;
		dampCoeff_ = static_cast<q31_t>(coeff * ONE_Q31f);
		// Air band: reintroduce up to 25% highs
		air_ = static_cast<q31_t>(value * 0.25f * ONE_Q31f);
	}
	[[nodiscard]] float getDamping() const override { return damping_; }

	void setDiffusion(float value) override {
		diffusion_val_ = value;
		// Input allpass diffuser coefficient: 0 (dry discrete taps) to 0.75 (dense cloud)
		apCoef1_ = static_cast<q31_t>(value * 0.70f * ONE_Q31f);
		apCoef2_ = static_cast<q31_t>(value * 0.55f * ONE_Q31f);
	}
	[[nodiscard]] float getDiffusion() const override { return diffusion_val_; }

	void setWidth(float value) override {
		width_ = value;
		tapSpread_ = 1u + static_cast<uint32_t>(value * 3u); // 1..4
	}
	[[nodiscard]] float getWidth() const override { return width_; }

	void setHPF(float f) override {
		hpf_ = f;
		float fc_hz = 20.0f + f * 980.0f;
		float fc = fc_hz / float(kSampleRate);
		float wc = fc / (1.0f + fc);
		outHpfCoeff_ = static_cast<q31_t>(std::clamp(wc, 0.0005f, 0.45f) * ONE_Q31f);
	}
	[[nodiscard]] float getHPF() const override { return hpf_; }

private:
	static constexpr size_t kBufSize = 8192; // per line; fits prime len * 3.7 scale in tight SRAM
	static constexpr size_t kMinLine = 128;
	static constexpr size_t kPrimeLen[4] = {1087, 1453, 1789, 2137};

	std::array<std::array<q31_t, kBufSize>, 4> buf_{}; // 4 lines × 64 KB

	size_t writePos_ = 0;
	size_t lineLen_[4] = {1087, 1453, 1789, 2137};
	uint32_t tapSpread_ = 2;

	// cascaded input diffusers (KDFX "In Diff"): 142/277/193 samples
	kdfx::AllPass<142> ap1_;
	kdfx::AllPass<277> ap2_;
	kdfx::AllPass<193> ap3_;
	q31_t apCoef1_ = static_cast<q31_t>(0.618f * ONE_Q31f);
	q31_t apCoef2_ = static_cast<q31_t>(0.50f * ONE_Q31f);

	// per-line absorption state
	q31_t dampState_[4] = {0, 0, 0, 0};
	q31_t airState_[4] = {0, 0, 0, 0};
	q31_t dampCoeff_ = static_cast<q31_t>(0.5f * ONE_Q31f);
	q31_t air_ = 0;
	q31_t decay_ = static_cast<q31_t>(0.85f * ONE_Q31f);

	// output DC/rumble filter state
	q31_t hpfL_ = 0, hpfR_ = 0;
	q31_t outHpfCoeff_ = 0;

	// dither states
	uint32_t rng0_ = 0x9E3779B9u, rng1_ = 0x853C49E7u;
	q31_t dither0_ = 0, dither1_ = 0, dither2_ = 0, dither3_ = 0;

	float room_size_ = 0.5f, decay_val_ = 0.5f, damping_ = 0.5f, diffusion_val_ = 0.5f, width_ = 0.5f, hpf_ = 0.f;
};

} // namespace deluge::dsp::reverb
