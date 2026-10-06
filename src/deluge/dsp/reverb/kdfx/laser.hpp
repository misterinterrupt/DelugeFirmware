/*
 * KDFX-style LaserVerb (manual algorithms 911-913, 2-3 PAU).
 *
 * "A delayed train of closely spaced reflections, or impulses. As time passes,
 * the spacing between the impulses gets wider, which creates a discernible
 * buzzy pitch that gets lower as the spacing increases."
 *
 * Implementation: one feedback delay line read through a train of taps whose
 * offsets GROW geometrically (offset_n = base + spacing * n(n+1)/2 samples) —
 * equal-per-step widening is exactly the descending-buzz behaviour the manual
 * describes. Tap gains follow the contour parameter: at 0 the algorithm is a
 * simple delay (single tap), from ~34% up it is a reverse-reverb-like build,
 * and high values give the normal starts-loud-slowly-decays shape. FDBK
 * recirculates the tap sum (loop gain = FDBK, ≤ 1.0 — sustaining, never
 * building; kill = FDBK down).
 *
 * Send-style wet-add output; params: TIME 0-2.0 S (3-PAU range), SPAC
 * 0.0-40.0 SMP (0.2 resolution), CONT 0-100%, FDBK 0-100%, LEVEL.
 */

#pragma once

#include "definitions_cxx.hpp"
#include "dsp/reverb/base.hpp"
#include "dsp/reverb/kdfx/kdfx_util.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <span>

namespace deluge::dsp::reverb {

class KdfxLaserVerb : public Base {
public:
	KdfxLaserVerb() = default;
	~KdfxLaserVerb() override = default;

	void process(std::span<int32_t> in, std::span<StereoSample> output) override {
		for (size_t frame = 0; frame < in.size(); frame++) {
			StereoSample& s = output[frame];
			q31_t inputSample = in[frame];

			// 1. Tap offsets: base delay plus geometrically widening gaps.
			q31_t tapSum = 0;
			int32_t tapCount = activeTaps();
			for (int32_t n = 0; n < tapCount; n++) {
				size_t off = tapOffset(n);
				q31_t t = kdfx::circReadCapped(buf_, writePos_, off, age_);
				tapSum = kdfx::qsum64(tapSum, kdfx::q31_mul(t, tapGain(n)));
			}

			// 2. Feedback: the tap sum recirculates, loop gain = FDBK (≤ 1.0).
			q31_t fb = kdfx::q31_mul(kdfx::q31_mul(tapSum, feedbackQ31_), kTapSumScale);
			buf_[writePos_] = add_saturation(inputSample, fb);

			// 3. Output HPF and wet-add (send style).
			kdfx::smoothDamp(hpfL_, tapSum, hpfCoeff_);
			q31_t wetL = tapSum - hpfL_;
			kdfx::smoothDamp(hpfR_, tapSum, hpfCoeff_);
			q31_t wetR = tapSum - hpfR_;

			kdfx::mixWet(s.l, wetL, getPanLeft());
			kdfx::mixWet(s.r, wetR, getPanRight());

			++age_;
			writePos_ = (writePos_ + 1) % kMaxDelay;
		}
	}

	// roomSize -> TIME, 0-2.0 seconds (the 3-PAU LaserVerb's range)
	void setRoomSize(float value) override {
		time_val_ = value;
		delayBase_ = 32u + static_cast<uint32_t>(value * (kMaxDelay - 64));
	}
	[[nodiscard]] float getRoomSize() const override { return time_val_; }

	// decay -> FDBK, 0-100% (100% = unity loop gain, sustaining)
	void setDecay(float value) override {
		fdbk_val_ = value;
		feedbackQ31_ = static_cast<q31_t>(std::clamp(value, 0.0f, 1.0f) * ONE_Q31f);
	}
	[[nodiscard]] float getDecay() const override { return fdbk_val_; }

	// diffusion -> CONT, 0-100% (0 = simple delay, ~34% = reverse, high = decaying)
	void setDiffusion(float value) override { contour_val_ = std::clamp(value, 0.0f, 1.0f); }
	[[nodiscard]] float getDiffusion() const override { return contour_val_; }

	// width -> SPAC, 0.0-40.0 samples in 0.2 steps (impulse-gap growth)
	void setWidth(float value) override {
		spac_val_ = value;
		spacingQ16_ = static_cast<uint32_t>(std::clamp(value, 0.0f, 40.0f) * 5.0f * 65536.0f); // 0.2 steps, Q16
	}
	[[nodiscard]] float getWidth() const override { return spac_val_; }

	void setHPF(float f) override {
		hpf_val_ = f;
		float fc_hz = 20.0f + f * 980.0f;
		float fc = fc_hz / float(kSampleRate);
		float wc = fc / (1.0f + fc);
		hpfCoeff_ = static_cast<q31_t>(std::clamp(wc, 0.0005f, 0.45f) * ONE_Q31f);
	}
	[[nodiscard]] float getHPF() const override { return hpf_val_; }

private:
	/// Tap n's read offset: base delay plus the widening gap sum 0.2*spacing*n(n+1)/2.
	size_t tapOffset(int32_t n) const {
		uint64_t grow = static_cast<uint64_t>(spacingQ16_ >> 16) * static_cast<uint32_t>(n * (n + 1) / 2);
		size_t off = delayBase_ + static_cast<size_t>(grow);
		return std::min(off, kMaxDelay - 1);
	}

	/// Number of audible taps: 1 at contour 0 (simple delay), 12 at ~34%+.
	int32_t activeTaps() const {
		if (contour_val_ <= 0.0f) {
			return 1;
		}
		float frac = std::clamp(contour_val_ * 3.0f, 0.0f, 1.0f);
		return 1 + static_cast<int32_t>(frac * (kNumTaps - 1));
	}

	/// Q31 tap gain for tap n from the contour shape.
	q31_t tapGain(int32_t n) const {
		if (n <= 0) {
			return ONE_Q31f;
		}
		float base;
		if (contour_val_ >= kReversePoint) {
			// Normal: starts loud, decays; slower decay toward full contour.
			float t = (contour_val_ - kReversePoint) / (1.0f - kReversePoint);
			base = 0.99f - t * 0.11f; // 0.88 .. 0.99 per step
		}
		else {
			// Reverse side: builds up to a hit.
			base = 1.0f + (kReversePoint - contour_val_) * 0.8f; // 1.0 .. ~1.27
		}
		float g = std::pow(base, static_cast<float>(n));
		if (g > 1.0f) {
			g = 1.0f; // the build taps clip at unity
		}
		return static_cast<q31_t>(g * ONE_Q31f);
	}

	static constexpr size_t kMaxDelay = 88200; // 2.0 s @ 44.1k (3-PAU LaserVerb)
	static constexpr int32_t kNumTaps = 12;
	static constexpr float kReversePoint = 0.34f;   // manual: ~34% behaves like reverse reverb
	static constexpr q31_t kTapSumScale = static_cast<q31_t>(0.22f * ONE_Q31f); // 12-tap sum headroom

	// Deliberately uninitialized (kdfx_util convention); reads are age-capped.
	std::array<q31_t, kMaxDelay> buf_{};

	size_t writePos_ = 0;
	uint32_t age_ = 0;
	size_t delayBase_ = 22050;
	uint32_t spacingQ16_ = static_cast<uint32_t>(4.0f * 5.0f * 65536.0f); // 4.0 samples

	q31_t feedbackQ31_ = 0;
	q31_t hpfL_ = 0, hpfR_ = 0;
	q31_t hpfCoeff_ = 0;

	float time_val_ = 0.25f, fdbk_val_ = 0.4f, contour_val_ = 0.6f, spac_val_ = 0.1f, hpf_val_ = 0.0f;
};

} // namespace deluge::dsp::reverb
