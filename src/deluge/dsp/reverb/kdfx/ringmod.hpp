/*
 * KDFX Ring Modulator (manual algorithm 907, 1 PAU).
 *
 * Two modes, per the manual:
 *  - L*R: the two input channels modulate each other; Wet/Dry passes ONLY the
 *    left signal dry (the right is not passed dry at all).
 *  - OSC: the stereo input is modulated by the sum of five oscillators —
 *    Oscillator 1 with selectable shape (Sine/Saw+/Saw-/Pulse/Tri), pulse
 *    width and smoothing, plus four sine oscillators; each has Lvl 0-100% and
 *    Freq 16-25088 HZ.
 *
 * Insert engine (transforms the buffer in place). No feedback anywhere.
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

class KdfxRingMod : public Base {
public:
	KdfxRingMod() = default;
	~KdfxRingMod() override = default;

	[[nodiscard]] bool isInsert() const override { return true; }

	// Never called for insert engines (FxBloc::process branches on isInsert()).
	void process(std::span<int32_t> input, std::span<StereoSample> output) override {}

	void processInsert(std::span<StereoSample> buffer) override {
		if (mode_ == 0) {
			// L*R: channels modulate each other; only LEFT passes dry.
			for (auto& s : buffer) {
				float l = static_cast<float>(s.l) / 2147483648.0f;
				float r = static_cast<float>(s.r) / 2147483648.0f;
				float prod = l * r;
				s.l = toQ31(blend(prod, l));
				s.r = toQ31(prod);
			}
		}
		else {
			// OSC: stereo input times the summed oscillator bank.
			for (auto& s : buffer) {
				advancePhases();
				float carrier = carrierValue();
				s.l = toQ31(static_cast<float>(s.l) / 2147483648.0f * carrier);
				s.r = toQ31(static_cast<float>(s.r) / 2147483648.0f * carrier);
			}
		}
	}

	void setRmMode(int32_t v) override { mode_ = (v != 0) ? 1 : 0; }
	[[nodiscard]] int32_t getRmMode() const override { return mode_; }

	/// which: 0 lvl (0-1), 1 freq (0-1 log 16-25088), 2 shape (0-5 incl Expon), 3 pls width
	/// (0-1), 4 smooth (0-1). osc: 1..5.
	void setRmOsc(int32_t osc, int32_t which, float v) override {
		v = std::clamp(v, 0.0f, 1.0f);
		switch (which) {
		case 0: oscLvl_[osc - 1] = v; break;
		case 1:
			oscFreqT_[osc - 1] = v;
			oscInc_[osc - 1] = static_cast<uint32_t>(
			    (logHzFromT(v) / float(kSampleRate)) * 4294967296.0f);
			break;
		case 2: oscShape_[osc - 1] = std::clamp<int32_t>(static_cast<int32_t>(std::lround(v)), 0, 5); break;
		case 3: oscPls_[osc - 1] = v; break;
		case 4: oscSmooth_[osc - 1] = v; break;
		default: break;
		}
	}
	[[nodiscard]] float getRmOsc(int32_t osc, int32_t which) const override {
		switch (which) {
		case 0: return oscLvl_[osc - 1];
		case 1: return oscFreqT_[osc - 1];
		case 2: return static_cast<float>(oscShape_[osc - 1]);
		case 3: return oscPls_[osc - 1];
		case 4: return oscSmooth_[osc - 1];
		default: return 0.0f;
		}
	}

	// LEVEL = the manual's Wet/Dry (dry-pass blend in L*R; wet scale in OSC).
	void setWidth(float v) override { wet_val_ = std::clamp(v, 0.0f, 1.0f); }
	[[nodiscard]] float getWidth() const override { return wet_val_; }

private:
	static constexpr float kMinHz = 16.0f;
	static constexpr float kMaxHz = 25088.0f;
	static float logHzFromT(float t) { return kMinHz * std::pow(kMaxHz / kMinHz, std::clamp(t, 0.0f, 1.0f)); }
	static q31_t toQ31(float v) {
		return static_cast<q31_t>(std::clamp(v, -1.0f, 1.0f) * 2147483647.0f);
	}
	float blend(float wetv, float dryv) const { return wet_val_ * wetv + (1.0f - wet_val_) * dryv; }

	void advancePhases() {
		for (int32_t i = 0; i < 5; i++) {
			oscPhase_[i] += oscInc_[i];
		}
	}

	/// Oscillator bank sum: osc1 (any shape) + four sines, each scaled by its Lvl.
	float carrierValue() {
		float sum = 0.0f;
		for (int32_t i = 0; i < 5; i++) {
			float w = oscWave(i, oscPhase_[i]);
			sum += oscLvl_[i] * w;
		}
		return std::clamp(sum, -1.0f, 1.0f);
	}

	/// Shape via the shared KDFX generator (Sine/Saw+/Saw-/Pulse/Tri); Sine2..5
	/// are always sines, per the manual.
	float oscWave(int32_t i, uint32_t phase) {
		int32_t shape = (i > 0) ? 0 : oscShape_[i];
		return kdfx::oscWave(shape, phase, oscPls_[i], oscSmooth_[i]);
	}

	int32_t mode_ = 1;
	float wet_val_ = 1.0f;

	float oscLvl_[5] = {0.8f, 0.5f, 0.5f, 0.5f, 0.5f};
	float oscFreqT_[5] = {0.5f, 0.5f, 0.5f, 0.5f, 0.5f};
	int32_t oscShape_[5] = {0, 0, 0, 0, 0};
	float oscPls_[5] = {0.5f, 0.5f, 0.5f, 0.5f, 0.5f};
	float oscSmooth_[5] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
	uint32_t oscPhase_[5] = {0, 0, 0, 0, 0};
	uint32_t oscInc_[5] = {0, 0, 0, 0, 0};
};

} // namespace deluge::dsp::reverb
