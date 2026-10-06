/*
 * KDFX Quantizer (the 714 Quantize+Flange partner; part of special FX).
 *
 * Manual spec, verbatim: "produces digital distortion known as quantization
 * noise, by limiting the number of bits available to the signal."
 *  - DynamRange 0..144 dB: how much level range the quantizer keeps. At 0 dB
 *    the hottest signal toggles between only two levels (a square wave);
 *    every 6 dB doubles the number of quantization levels.
 *  - Headroom 0..144 dB: level available above the signal before digital
 *    clipping — prevents the output from getting ridiculously loud at low
 *    DynamRange. Practical range is headroom <= dynamic range.
 *  - DC Offset: Off or -79.0..0.0 dB, shifting digital zero relative to the
 *    signal ("at low DynamRange settings this can cause the output to
 *    sputter").
 *
 * Insert engine; no feedback. Quantization steps in Q31: levels = 2^bits,
 * bits = dynamRange/6 clamped to [0, 31] (above 31 bits the quantizer is
 * transparent, which is what 144 dB means).
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

class KdfxQuantize : public Base {
public:
	KdfxQuantize() = default;
	~KdfxQuantize() override = default;

	[[nodiscard]] bool isInsert() const override { return true; }

	// Never called for insert engines (FxBloc::process branches on isInsert()).
	void process(std::span<int32_t> input, std::span<StereoSample> output) override {}

	void processInsert(std::span<StereoSample> buffer) override {
		for (auto& s : buffer) {
			s.l = quantize(s.l);
			s.r = quantize(s.r);
		}
	}

	// roomSize -> DynamRange, 0-144 DB (log2 levels; >=138 DB is transparent)
	void setRoomSize(float v) override {
		dyn_val_ = std::clamp(v, 0.0f, 1.0f);
		// bits = dyn/6, capped: the Q31 word has 31 usable bits — past that the
		// quantizer is the identity, which is the point of the top of the range.
		qbits_ = std::min<int32_t>(static_cast<int32_t>(dyn_val_ * 144.0f / 6.0f), 31);
		qmask_ = (qbits_ >= 31) ? 0 : (~0u << (31 - qbits_));
	}
	[[nodiscard]] float getRoomSize() const override { return dyn_val_; }

	// decay -> Headroom, 0-144 DB (scale pre-quantizer so hot signals clip here
	// rather than wrap; practical range headroom <= dynamic range, per manual)
	void setDecay(float v) override {
		head_val_ = std::clamp(v, 0.0f, 1.0f);
		headGain_ = std::pow(10.0f, head_val_ * 144.0f / 20.0f);
		headGain_ = std::min(headGain_, 1.0e7f); // +140 dB ceiling, no inf
	}
	[[nodiscard]] float getDecay() const override { return head_val_; }

	// diffusion -> DC Offset, 0 = Off, else -79..0 DB added to the input
	void setDiffusion(float v) override {
		dc_val_ = std::clamp(v, 0.0f, 1.0f);
		// 0 = off; the manual's range is Off/-79..0 dB, so 1% travel = -79 dB.
		dcOffset_ = (dc_val_ <= 0.0f) ? 0.0f : std::pow(10.0f, -79.0f * (1.0f - dc_val_) / 20.0f);
	}
	[[nodiscard]] float getDiffusion() const override { return dc_val_; }

private:
	q31_t quantize(q31_t in) {
		// Headroom: attenuate into the quantizer's window, restore after. Hot
		// input above the headroom window hard-clips (digital clipping, as
		// specified).
		float x = static_cast<float>(in) / 2147483648.0f;
		x = std::clamp(x * headGain_ + dcOffset_, -1.0f, 1.0f);
		int32_t xi = static_cast<int32_t>(std::clamp(x, -1.0f, 0.999999999f) * 2147483647.0f);
		if (qbits_ >= 31) {
			return in; // transparent at the top of DynamRange
		}
		int32_t q = xi & qmask_;
		// Restore the headroom attenuation (quantization noise stays scaled).
		return static_cast<q31_t>(std::clamp(
		    (static_cast<double>(q) / 2147483648.0) / headGain_, -1.0, 1.0) * 2147483647.0);
	}

	float dyn_val_ = 0.5f;
	float head_val_ = 0.5f;
	float dc_val_ = 0.0f;
	int32_t qbits_ = 12;
	uint32_t qmask_ = ~0u << 19;
	float headGain_ = 1.0f;
	float dcOffset_ = 0.0f;
};

} // namespace deluge::dsp::reverb
