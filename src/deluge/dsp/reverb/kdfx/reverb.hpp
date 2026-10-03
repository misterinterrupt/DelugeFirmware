/*
 * KDFX-style 4-line orthogonal FDN reverb tank (the "2-PAU" equivalent).
 *
 * Q31 throughout.
 * Features:
 * - Pre-delay buffer (0..300 ms with sub-sample precision)
 * - 6-tap Early Reflections (ER) decorrelation matrix
 * - 3-stage cascaded input allpass diffusers
 * - 4-line prime FDN tank with Householder orthogonal feedback matrix
 * - Per-line sweepable HF absorption + Air shelf blend + Bass decay multiplier
 * - TPDF dither
 * - Output rumble HPF
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
			q31_t inSample = in[frame];

			// 1. Pre-delay line write & read
			preDelayBuf_[preWritePos_] = inSample;
			q31_t preDelayed = kdfx::circReadCapped(preDelayBuf_, preWritePos_, preDelaySamples_, age_);
			preWritePos_ = (preWritePos_ + 1) % kPreBufSize;

			// 2. Early Reflections (ER) 6-tap matrix from pre-delayed signal
			q31_t erL = 0;
			q31_t erR = 0;
			if (erLevelQ31_ > 0) {
				q31_t er1 = kdfx::circReadCapped(preDelayBuf_, preWritePos_, std::clamp<size_t>(preDelaySamples_ + 317, 1, kPreBufSize - 1), age_);
				q31_t er2 = kdfx::circReadCapped(preDelayBuf_, preWritePos_, std::clamp<size_t>(preDelaySamples_ + 563, 1, kPreBufSize - 1), age_);
				q31_t er3 = kdfx::circReadCapped(preDelayBuf_, preWritePos_, std::clamp<size_t>(preDelaySamples_ + 919, 1, kPreBufSize - 1), age_);
				q31_t er4 = kdfx::circReadCapped(preDelayBuf_, preWritePos_, std::clamp<size_t>(preDelaySamples_ + 1459, 1, kPreBufSize - 1), age_);
				q31_t er5 = kdfx::circReadCapped(preDelayBuf_, preWritePos_, std::clamp<size_t>(preDelaySamples_ + 2087, 1, kPreBufSize - 1), age_);
				q31_t er6 = kdfx::circReadCapped(preDelayBuf_, preWritePos_, std::clamp<size_t>(preDelaySamples_ + 2699, 1, kPreBufSize - 1), age_);

				int64_t sumL = (int64_t)er1 * 600 + (int64_t)er3 * 450 + (int64_t)er5 * 300;
				int64_t sumR = (int64_t)er2 * 600 + (int64_t)er4 * 450 + (int64_t)er6 * 300;
				erL = kdfx::q31_mul(kdfx::qcollapse(sumL >> 10), erLevelQ31_);
				erR = kdfx::q31_mul(kdfx::qcollapse(sumR >> 10), erLevelQ31_);
			}

			// 3. Cascaded input diffusers (smear transients into bloom)
			q31_t diffused = ap1_.process(preDelayed, apCoef1_);
			diffused = ap2_.process(diffused, apCoef2_);
			diffused = ap3_.process(diffused, apCoef1_);

			// 4. Read delay line endpoints & apply absorption/decay/bass multiplier
			q31_t x[4];
			for (uint32_t l = 0; l < 4; l++) {
				q31_t delayed = kdfx::circReadCapped(buf_[l], writePos_, lineLen_[l], age_);

				// Smooth sweepable HF absorption + "air" shelf blend
				kdfx::shelfDamp(dampState_[l], airState_[l], delayed, dampCoeff_, air_);
				q31_t filtered = add_saturation(dampState_[l],
				                                kdfx::q31_mul(airState_[l], air_));

				// Bass decay multiplier: low frequency retention
				kdfx::smoothDamp(bassState_[l], delayed, bassDampCoeff_);
				q31_t bassBoosted = add_saturation(filtered, kdfx::q31_mul(bassState_[l], bassDecayMultQ31_));

				// Decay scalar in Q31
				x[l] = kdfx::q31_mul(bassBoosted, decay_);
			}

			// 5. O(N) Householder feedback matrix
			int64_t sum = static_cast<int64_t>(x[0]) + x[1] + x[2] + x[3];
			q31_t fb[4];
			fb[0] = kdfx::householder4(x[0], sum);
			fb[1] = kdfx::householder4(x[1], sum);
			fb[2] = kdfx::householder4(x[2], sum);
			fb[3] = kdfx::householder4(x[3], sum);

			// 6. Write: Sum Input + Feedback + Dither into line heads
			buf_[0][writePos_] = add_saturation(diffused, add_saturation(fb[0], dither0_));
			buf_[1][writePos_] = add_saturation(diffused, add_saturation(fb[1], dither1_));
			buf_[2][writePos_] = add_saturation(diffused, add_saturation(fb[2], dither2_));
			buf_[3][writePos_] = add_saturation(diffused, add_saturation(fb[3], dither3_));

			// 7. Stereo output taps
			auto safeTapOffset = [&](uint32_t l, uint32_t spread) -> size_t {
				size_t len = lineLen_[l];
				size_t off = (len >> 1) + ((len * spread * tapSpread_) >> 6);
				if (off >= len) {
					off = len - 1;
				}
				return (off < 1) ? 1 : off;
			};

			q31_t l0 = kdfx::circReadCapped(buf_[0], writePos_, safeTapOffset(0, 5), age_);
			q31_t l1 = kdfx::circReadCapped(buf_[2], writePos_, safeTapOffset(2, 7), age_);
			q31_t r0 = kdfx::circReadCapped(buf_[1], writePos_, safeTapOffset(1, 6), age_);
			q31_t r1 = kdfx::circReadCapped(buf_[3], writePos_, safeTapOffset(3, 8), age_);

			q31_t wetL = add_saturation(kdfx::qcollapse((int64_t)l0 + l1), erL);
			q31_t wetR = add_saturation(kdfx::qcollapse((int64_t)r0 + r1), erR);

			// 8. Output rumble/DC filter
			kdfx::smoothDamp(hpfL_, wetL, outHpfCoeff_);
			wetL -= hpfL_;
			kdfx::smoothDamp(hpfR_, wetR, outHpfCoeff_);
			wetR -= hpfR_;

			// 9. Advance dither
			dither0_ = kdfx::tpdf_dither(rng0_);
			dither2_ = kdfx::tpdf_dither(rng1_);
			dither1_ = dither0_;
			dither3_ = dither2_;

			// 10. Mix with engine pan/send levels
			kdfx::mixWet(s.l, wetL, getPanLeft());
			kdfx::mixWet(s.r, wetR, getPanRight());

			++age_;
			writePos_ = (writePos_ + 1) % kBufSize;
		}
	}

	void setRoomSize(float value) override {
		room_size_ = value;
		const float scale = 1.f + value * 2.7f;
		for (uint32_t l = 0; l < 4; l++) {
			lineLen_[l] = std::max<size_t>(kMinLine, (size_t)(kPrimeLen[l] * scale));
		}
	}
	[[nodiscard]] float getRoomSize() const override { return room_size_; }

	void setDecay(float value) override {
		decay_val_ = value;
		// 0.40..0.97, reaching the cap exactly at full travel (the low-band path
		// also multiplies by the bass-boost factor; see updateBassClamp).
		decayFloat_ = std::min(0.40f + value * 0.57f, 0.97f);
		decay_ = static_cast<q31_t>(decayFloat_ * ONE_Q31f);
		updateBassClamp();
	}
	[[nodiscard]] float getDecay() const override { return decay_val_; }

	void setDamping(float value) override {
		damping_ = value;
		const float coeff = 0.05f + value * 0.95f;
		dampCoeff_ = static_cast<q31_t>(coeff * ONE_Q31f);
	}
	[[nodiscard]] float getDamping() const override { return damping_; }

	void setDiffusion(float value) override {
		diffusion_val_ = value;
		apCoef1_ = static_cast<q31_t>(value * 0.70f * ONE_Q31f);
		apCoef2_ = static_cast<q31_t>(value * 0.55f * ONE_Q31f);
	}
	[[nodiscard]] float getDiffusion() const override { return diffusion_val_; }

	void setWidth(float value) override {
		width_ = value;
		// Continuous Q6 stereo tap-spread factor 1.0x..4.0x (the >>6 in safeTapOffset
		// consumes the fraction). Was 1u + (int)(v*3u): four discrete steps, so the
		// knob read as dead between them.
		tapSpread_ = 64u + static_cast<uint32_t>(value * 192.0f);
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

	void setPreDelay(float value) override {
		pre_delay_val_ = value;
		// 0 ms to 300 ms (0 to 13230 samples @ 44.1k)
		preDelaySamples_ = static_cast<size_t>(value * 13230.0f);
		if (preDelaySamples_ >= kPreBufSize) {
			preDelaySamples_ = kPreBufSize - 1;
		}
	}
	[[nodiscard]] float getPreDelay() const override { return pre_delay_val_; }

	void setEarlyReflections(float value) override {
		er_level_val_ = value;
		erLevelQ31_ = static_cast<q31_t>(value * ONE_Q31f);
	}
	[[nodiscard]] float getEarlyReflections() const override { return er_level_val_; }

	void setBassDecay(float value) override {
		bass_decay_val_ = value;
		// 0.2x to 2.5x multiplier wanted; the actually-applied factor is clamped
		// (updateBassClamp) so the low-band loop gain stays below unity.
		bassMultWanted_ = 0.2f + value * 2.3f;
		updateBassClamp();
		bassDampCoeff_ = static_cast<q31_t>(0.15f * ONE_Q31f); // ~300 Hz crossover
	}
	[[nodiscard]] float getBassDecay() const override { return bass_decay_val_; }

	void setAirShelf(float value) override {
		air_shelf_val_ = value;
		air_ = static_cast<q31_t>(value * 0.40f * ONE_Q31f);
	}
	[[nodiscard]] float getAirShelf() const override { return air_shelf_val_; }

private:
	static constexpr size_t kBufSize = 8192;
	static constexpr size_t kPreBufSize = 16384;
	static constexpr size_t kMinLine = 128;
	static constexpr size_t kPrimeLen[4] = {1087, 1453, 1789, 2137};

	/// The low-band loop gain is bassFactor x decay where bassFactor reaches
	/// (mult + 1) / 2 — up to 1.75 with the multiplier at 2.5x, which pushed the
	/// product past unity and made the reverb build up. Clamp the factor so the
	/// product stays below 1; high decay settings eat into the available boost.
	void updateBassClamp() {
		float factor = std::min((bassMultWanted_ + 1.0f) * 0.5f, 0.99f / decayFloat_);
		bassDecayMultQ31_ = static_cast<q31_t>((factor - 1.0f) * ONE_Q31f);
	}

	// Deliberately uninitialized (see kdfx_util.hpp circReadCapped); reads are age-capped.
	std::array<std::array<q31_t, kBufSize>, 4> buf_;
	std::array<q31_t, kPreBufSize> preDelayBuf_;

	size_t writePos_ = 0;
	size_t preWritePos_ = 0;
	uint32_t age_ = 0; // frames completed since construction
	size_t preDelaySamples_ = 0;
	size_t lineLen_[4] = {1087, 1453, 1789, 2137};
	uint32_t tapSpread_ = 2;

	// Cascaded input diffusers
	kdfx::AllPass<142> ap1_;
	kdfx::AllPass<277> ap2_;
	kdfx::AllPass<193> ap3_;
	q31_t apCoef1_ = static_cast<q31_t>(0.618f * ONE_Q31f);
	q31_t apCoef2_ = static_cast<q31_t>(0.50f * ONE_Q31f);

	// Absorption & decay
	q31_t dampState_[4] = {0, 0, 0, 0};
	q31_t airState_[4] = {0, 0, 0, 0};
	q31_t bassState_[4] = {0, 0, 0, 0};
	q31_t dampCoeff_ = static_cast<q31_t>(0.5f * ONE_Q31f);
	q31_t air_ = 0;
	q31_t bassDampCoeff_ = static_cast<q31_t>(0.15f * ONE_Q31f);
	q31_t bassDecayMultQ31_ = 0;
	q31_t decay_ = static_cast<q31_t>(0.85f * ONE_Q31f);
	float decayFloat_ = 0.85f;
	float bassMultWanted_ = 1.35f;
	q31_t erLevelQ31_ = static_cast<q31_t>(0.4f * ONE_Q31f);

	// DC HPF
	q31_t hpfL_ = 0, hpfR_ = 0;
	q31_t outHpfCoeff_ = 0;

	// Dither
	uint32_t rng0_ = 0x9E3779B9u, rng1_ = 0x853C49E7u;
	q31_t dither0_ = 0, dither1_ = 0, dither2_ = 0, dither3_ = 0;

	float room_size_ = 0.5f, decay_val_ = 0.5f, damping_ = 0.5f, diffusion_val_ = 0.5f, width_ = 0.5f, hpf_ = 0.f;
	float pre_delay_val_ = 0.0f, er_level_val_ = 0.4f, bass_decay_val_ = 0.5f, air_shelf_val_ = 0.2f;
};

} // namespace deluge::dsp::reverb
