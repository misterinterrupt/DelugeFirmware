/*
 * KDFX-style auto-panner (manual algorithm 963, 1 PAU).
 *
 * Complementary LFO-driven crossfade between the stereo channels —
 * insert-style, processes the buffer in place. Linear crossfade keeps the
 * channel sum constant; the center position sits ~3 dB below equal-power
 * (a first-cut trade for lookup-free fixed point).
 */

#pragma once

#include "definitions_cxx.hpp"
#include "dsp/reverb/base.hpp"
#include "dsp/reverb/kdfx/kdfx_util.hpp"
#include <algorithm>
#include <cstdint>
#include <span>

namespace deluge::dsp::reverb {

class KdfxAutoPanner : public Base {
public:
	KdfxAutoPanner() = default;
	~KdfxAutoPanner() override = default;

	[[nodiscard]] bool isInsert() const override { return true; }

	// Never called for insert engines (FxBloc::process branches on isInsert()).
	void process(std::span<int32_t> input, std::span<StereoSample> output) override {}

	void processInsert(std::span<StereoSample> buffer) override {
		static constexpr q31_t kHalf = 0x3FFFFFFF;
		for (auto& s : buffer) {
			lfoPhase_ += lfoInc_;
			q31_t lfo = kdfx::parabolicLfo(lfoPhase_);
			q31_t p = kdfx::q31_mul(depthQ31_, lfo); // pan position in [-depth, +depth]
			q31_t gL = kHalf + (p >> 1);
			q31_t gR = kHalf - (p >> 1);
			s.l = kdfx::q31_mul(s.l, gL);
			s.r = kdfx::q31_mul(s.r, gR);
		}
	}

	void setRoomSize(float value) override {
		rate_val_ = value;
		// LFO rate: 0.05 Hz to 10.00 Hz
		float freqHz = 0.05f + value * 9.95f;
		lfoInc_ = static_cast<uint32_t>((freqHz / 44100.0f) * 4294967296.0f);
	}
	[[nodiscard]] float getRoomSize() const override { return rate_val_; }

	void setDecay(float value) override {
		width_val_ = std::clamp(value, 0.0f, 1.0f);
		depthQ31_ = static_cast<q31_t>(width_val_ * ONE_Q31f);
	}
	[[nodiscard]] float getDecay() const override { return width_val_; }

private:
	uint32_t lfoPhase_ = 0;
	uint32_t lfoInc_ = 100000;
	q31_t depthQ31_ = static_cast<q31_t>(0.7f * ONE_Q31f);

	float rate_val_ = 0.3f;
	float width_val_ = 0.7f;
};

} // namespace deluge::dsp::reverb
