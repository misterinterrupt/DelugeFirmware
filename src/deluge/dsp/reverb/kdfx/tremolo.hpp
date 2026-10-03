/*
 * KDFX-style tremolo (manual algorithm 961, 1 PAU).
 *
 * Amplitude modulation of the dry signal — insert-style, processes the stereo
 * buffer in place. Gain sweeps between (1 - depth) and 1 following the LFO.
 */

#pragma once

#include "definitions_cxx.hpp"
#include "dsp/reverb/base.hpp"
#include "dsp/reverb/kdfx/kdfx_util.hpp"
#include <algorithm>
#include <cstdint>
#include <span>

namespace deluge::dsp::reverb {

class KdfxTremolo : public Base {
public:
	KdfxTremolo() = default;
	~KdfxTremolo() override = default;

	[[nodiscard]] bool isInsert() const override { return true; }

	// Never called for insert engines (FxBloc::process branches on isInsert()).
	void process(std::span<int32_t> input, std::span<StereoSample> output) override {}

	void processInsert(std::span<StereoSample> buffer) override {
		static constexpr q31_t kOne = 0x7FFFFFFF;
		for (auto& s : buffer) {
			lfoPhase_ += lfoInc_;
			q31_t lfo = kdfx::parabolicLfo(lfoPhase_);
			// u in [0, 1] tracks the LFO half-period; gain = 1 - depth*(1 - u).
			uint32_t u = (static_cast<uint32_t>(lfo) + 0x7FFFFFFFu) >> 1;
			q31_t gain = kOne - kdfx::q31_mul(depthQ31_, kOne - static_cast<q31_t>(u));
			s.l = kdfx::q31_mul(s.l, gain);
			s.r = kdfx::q31_mul(s.r, gain);
		}
	}

	void setRoomSize(float value) override {
		rate_val_ = value;
		// LFO rate: 0.10 Hz to 16.00 Hz
		float freqHz = 0.10f + value * 15.9f;
		lfoInc_ = static_cast<uint32_t>((freqHz / 44100.0f) * 4294967296.0f);
	}
	[[nodiscard]] float getRoomSize() const override { return rate_val_; }

	void setDecay(float value) override {
		depth_val_ = std::clamp(value, 0.0f, 1.0f);
		depthQ31_ = static_cast<q31_t>(depth_val_ * ONE_Q31f);
	}
	[[nodiscard]] float getDecay() const override { return depth_val_; }

private:
	uint32_t lfoPhase_ = 0;
	uint32_t lfoInc_ = 100000;
	q31_t depthQ31_ = static_cast<q31_t>(0.5f * ONE_Q31f);

	float rate_val_ = 0.3f;
	float depth_val_ = 0.5f;
};

} // namespace deluge::dsp::reverb
