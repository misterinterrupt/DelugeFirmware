/*
 * Stock-DSP adapters for the modular FX chain.
 *
 * Rule 0 applies: the wrapped engines (RMSFeedbackCompressor, later FilterSet)
 * are stock code and are NEVER edited — the adapters call their public API only.
 *
 * Params ride the same aliased-setter pattern every bloc engine uses: the generic
 * Param ids map onto compressor controls (roomSize=threshold, decay=ratio,
 * damping=attack, diffusion=release, hpf=sidechain HPF, width=blend). A slot's
 * XML therefore stores the generic names with compressor semantics, exactly as a
 * chorus slot stores rate under "roomSize" — the model attribute defines the
 * meaning.
 */

#pragma once

#include "definitions_cxx.hpp"
#include "dsp/compressor/rms_feedback.h"
#include "dsp/reverb/base.hpp"
#include "dsp/reverb/kdfx/kdfx_util.hpp"
#include <algorithm>
#include <cstdint>
#include <span>

namespace deluge::dsp::reverb {

class CompressorAdapter : public Base {
public:
	CompressorAdapter() {
		// Sensible transparent-ish starting point: ~-22 dB threshold, ~2.4:1 ratio,
		// moderate times, full-wet blend. baseGain 1.0 (max headroom; the song-level
		// stock compressor uses 0.8 for legacy loudness compatibility, which does not
		// apply to a user-inserted bloc).
		comp_.setup(q31(0.35f), q31(0.45f), q31(0.45f), q31(0.62f), q31(0.30f), ONE_Q31, 1.0f);
	}
	~CompressorAdapter() override = default;

	[[nodiscard]] bool isInsert() const override { return true; }

	// Never called for insert engines (FxBloc::process branches on isInsert()).
	void process(std::span<int32_t> input, std::span<StereoSample> output) override {}

	void processInsert(std::span<StereoSample> buffer) override {
		comp_.renderVolNeutral(buffer.data(), static_cast<uint16_t>(buffer.size()), ONE_Q31);
	}

	// roomSize -> threshold (0 dBFS .. -48 dBFS, linear in dB)
	void setRoomSize(float value) override { comp_.setThreshold(q31(value)); }
	[[nodiscard]] float getRoomSize() const override { return -comp_.getThresholdDB() / 48.0f; }

	// decay -> ratio (bipolar: 1:0.1 .. 1:1 .. 1:128, exponential around 1:1)
	void setDecay(float value) override { comp_.setRatio(q31(value)); }
	[[nodiscard]] float getDecay() const override { return float(comp_.getRatio()) / ONE_Q31f; }

	// damping -> attack (0.5 .. 100 ms, quadratic)
	void setDamping(float value) override { comp_.setAttack(q31(value)); }
	[[nodiscard]] float getDamping() const override {
		float ms = comp_.getAttackMSFloat();
		return std::sqrt(std::clamp((ms - 0.5f) / 99.5f, 0.0f, 1.0f));
	}

	// diffusion -> release (10 .. 1000 ms, quadratic)
	void setDiffusion(float value) override { comp_.setRelease(q31(value)); }
	[[nodiscard]] float getDiffusion() const override {
		float ms = comp_.getReleaseMSFloat();
		return std::sqrt(std::clamp((ms - 10.0f) / 990.0f, 0.0f, 1.0f));
	}

	// hpf -> sidechain HPF cutoff (~0 .. 100 Hz, exponential)
	void setHPF(float value) override { comp_.setSidechain(q31(value)); }
	[[nodiscard]] float getHPF() const override { return float(comp_.getSidechain()) / ONE_Q31f; }

	// width -> wet/dry blend of the compression
	void setWidth(float value) override { comp_.setBlend(q31(value)); }
	[[nodiscard]] float getWidth() const override { return float(comp_.getBlend()) / ONE_Q31f; }

private:
	static q31_t q31(float v) { return static_cast<q31_t>(std::clamp(v, 0.0f, 1.0f) * ONE_Q31f); }

	// mutable: some stock getters are not const-qualified; the adapter's const
	// getters still only read state.
	mutable RMSFeedbackCompressor comp_{};
};

} // namespace deluge::dsp::reverb
