/*
 * KDFX resonant filter bank (manual algorithms 900 Env Follow Filt, 902 LFO
 * Sweep Filter, 903 Resonant Filter; 2 PAU each).
 *
 * Common manual spec: Filter Type = Lowpass / Highpass / Bandpass / Notch,
 * Freq 16..8372 Hz, Resonance 0..50 dB. The variants differ only in what
 * moves the frequency:
 *   - Resonant (903): fixed frequency.
 *   - Env Follow (900): follows the input level — SWEEP ±100% sets how far the
 *     resonant frequency travels (up toward 8372 Hz at positive settings, down
 *     toward 16 Hz at negative), ATK/REL slew the envelope in dB/second.
 *   - LFO Sweep (902): LFO expressed in BPM and beats — clock FIXED (own BPM
 *     1-255) or SYSTEM (follows song tempo via the per-block tempo push);
 *     cycle length 1-16 beats; sweep runs between MIN and MAX limits
 *     (16-8372 Hz each — set MIN above MAX to sweep upside down); shape
 *     Sine/Saw+/Saw-/Pulse/Tri with pulse width and smoothing.
 *
 * Topology: per-channel TPT state-variable filter (Simper) — unconditionally
 * stable, no feedback loop, so the loop-gain rule holds trivially. Resonance
 * self-oscillates only at the 50 dB extreme (poles on the unit circle —
 * sustained ringing, the same "INF at the top detent" behaviour as the
 * reverb). A 1/sqrt(Q) makeup keeps the near-cutoff peak from blowing up.
 *
 * Common manual spec: Filter Type = Lowpass / Highpass / Bandpass / Notch,
 * Freq 16..8372 Hz, Resonance 0..50 dB. The variants differ only in what
 * moves the frequency:
 *   - Resonant (903): fixed frequency.
 *   - Env Follow (900): follows the input level — SWEEP ±100% sets how far the
 *     resonant frequency travels (up toward 8372 Hz at positive settings, down
 *     toward 16 Hz at negative), ATK/REL slew the envelope in dB/second.
 *   - LFO Sweep (902): an LFO (MRATE 0.01-10 HZ, MDEPTH 0-6 OCT) sweeps
 *     around the centre frequency.
 *
 * Topology: per-channel TPT state-variable filter (Simper) — unconditionally
 * stable, no feedback loop, so the loop-gain rule holds trivially. Resonance
 * self-oscillates only at the 50 dB extreme (poles on the unit circle —
 * sustained ringing, the same "INF at the top detent" behaviour as the
 * reverb). A 1/sqrt(Q) makeup keeps the near-cutoff peak from blowing up.
 */

#pragma once

#include "definitions_cxx.hpp"
#include "dsp/filter/filter_set.h"
#include "dsp/reverb/base.hpp"
#include "dsp/reverb/kdfx/kdfx_util.hpp"
#include "modulation/params/param.h"
#include "util/functions.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>

namespace deluge::dsp::reverb {

class KdfxFilter : public Base {
public:
	enum class Sweep : uint8_t { FIXED = 0, ENV_FOLLOW, LFO };

	explicit KdfxFilter(Sweep mode) : mode_(mode) {}
	~KdfxFilter() override = default;

	[[nodiscard]] bool isInsert() const override { return true; }

	// Never called for insert engines (FxBloc::process branches on isInsert()).
	void process(std::span<int32_t> input, std::span<StereoSample> output) override {}

	void processInsert(std::span<StereoSample> buffer) override {
		const float dt = 1.0f / float(kSampleRate);
		const int32_t n = static_cast<int32_t>(buffer.size());
		float bpm = (lfoClock_ != 0) ? std::max(1.0f, systemTempoBpm()) : lfoBpm_;
		{
			float hz = bpm / (60.0f * std::max(1.0f, lfoBeats_));
			lfoInc_ = static_cast<uint32_t>((hz / float(kSampleRate)) * 4294967296.0f);
		}

		// Pass 1: advance the envelope / LFO from the (unfiltered) input.
		for (auto& s : buffer) {
			float peak = std::max(std::fabs(s.l / 2147483648.0f), std::fabs(s.r / 2147483648.0f));
			float targetDb = 20.0f * std::log10(peak + 1e-9f);
			targetDb = std::clamp(targetDb, -60.0f, 0.0f);
			float slew = targetDb - envDb_;
			float limit = (slew >= 0.0f ? atkRateDb_ : relRateDb_) * dt;
			envDb_ += std::clamp(slew, -limit, limit);
			lfoPhase_ += lfoInc_;
		}
		float env = std::pow(10.0f, envDb_ / 20.0f);

		// Per-block config — the same rate the per-voice subtractive filters
		// reconfigure at. FREQ/RES ride the synth's exact param conversion, so
		// the bloc IS the subtractive-mode filter.
		float freqHz = centreHz_;
		switch (mode_) {
		case Sweep::ENV_FOLLOW: {
			float sweep = sweep_val_ * 2.0f - 1.0f;
			float minHz = freqFromNorm(minHz_val_);
			float ratio = std::max(minHz / kMinHz, 1.0f);
			freqHz = minHz * std::pow(ratio, env * sweep);
			break;
		}
		case Sweep::LFO: {
			float lfoN = 0.5f + 0.5f * kdfx::oscWave(lfoShape_, lfoPhase_, lfoPlsw_, lfoSmooth_);
			float minHz = freqFromNorm(minHz_val_);
			float maxHz = freqFromNorm(centre_val_);
			float lo = std::log(std::min(minHz, maxHz));
			float hi = std::log(std::max(minHz, maxHz));
			freqHz = std::exp(lo + lfoN * (hi - lo));
			break;
		}
		case Sweep::FIXED:
		default:
			break;
		}
		freqHz = std::clamp(freqHz, kMinHz, kMaxHz);

		auto mode = static_cast<FilterMode>(std::clamp<int32_t>(type_, 0, kNumLPFModes - 1));
		int32_t rawF = static_cast<int32_t>((centreToNorm(freqHz) * 2.0f - 1.0f) * 1073741824.0f);
		q31_t freqParam = getFinalParameterValueExp(2000000, cableToExpParamShortcut(rawF));
		int32_t rawR = static_cast<int32_t>((res_val_ * 2.0f - 1.0f) * 1073741824.0f);
		q31_t resParam = getFinalParameterValueLinear(25 * 10737418, cableToLinearParamShortcut(rawR));
		int32_t gain = filterSet_.setConfig(freqParam, resParam, mode, 0, 2147483647, 0, FilterMode::OFF, 0,
		                                     134217728, FilterRoute::LOW_TO_HIGH, false, nullptr);
		// setConfig returns the resonance-compensated gain (134217728 = unity);
		// convert 2^27-space to Q31 and pre-scale the block.
		q31_t gainQ31 = static_cast<q31_t>(std::clamp<int64_t>((int64_t)gain * 16, INT32_MIN, INT32_MAX));
		for (auto& s : buffer) {
			s.l = multiply_32x32_rshift32_rounded(s.l, gainQ31) << 1;
			s.r = multiply_32x32_rshift32_rounded(s.r, gainQ31) << 1;
		}
		filterSet_.renderLongStereo(reinterpret_cast<int32_t*>(buffer.data()),
		                             reinterpret_cast<int32_t*>(buffer.data() + n));
	}

	// ---- Common params ----
	void setFiltType(int32_t v) override { type_ = std::clamp<int32_t>(v, 0, kNumLPFModes - 1); }
	[[nodiscard]] int32_t getFiltType() const override { return type_; }

	void setFiltRes(float v) override { res_val_ = std::clamp(v, 0.0f, 1.0f); }
	[[nodiscard]] float getFiltRes() const override { return res_val_; }

	// width -> centre/primary frequency (log 16..8372 HZ)
	void setWidth(float v) override {
		centre_val_ = std::clamp(v, 0.0f, 1.0f);
		centreHz_ = freqFromNorm(centre_val_);
	}
	[[nodiscard]] float getWidth() const override { return centre_val_; }

	void setLoCut(float v) override { /* unused slot on some layouts */ (void)v; }
	[[nodiscard]] float getLoCut() const override { return 0.0f; }

	// ---- Env Follow params (900) ----
	// hpf -> MIN Freq (log 16..8372), the at-rest frequency
	void setHPF(float v) override { minHz_val_ = std::clamp(v, 0.0f, 1.0f); }
	[[nodiscard]] float getHPF() const override { return minHz_val_; }
	// diffusion -> SWEEP, bipolar ±100%
	void setDiffusion(float v) override { sweep_val_ = std::clamp(v, 0.0f, 1.0f); }
	[[nodiscard]] float getDiffusion() const override { return sweep_val_; }
	// damping -> ATK rate 0..300 DB/S; decay -> REL rate
	void setDamping(float v) override { atk_val_ = std::clamp(v, 0.0f, 1.0f); }
	[[nodiscard]] float getDamping() const override { return atk_val_; }
	void setDecay(float v) override { rel_val_ = std::clamp(v, 0.0f, 1.0f); }
	[[nodiscard]] float getDecay() const override { return rel_val_; }

	// ---- LFO Sweep params (902), full manual set ----
	void setFiltClk(int32_t v) override { lfoClock_ = (v != 0) ? 1 : 0; }
	[[nodiscard]] int32_t getFiltClk() const override { return lfoClock_; }
	void setFiltBpm(float v) override { lfoBpm_ = 1.0f + std::clamp(v, 0.0f, 1.0f) * 254.0f; }
	[[nodiscard]] float getFiltBpm() const override { return (lfoBpm_ - 1.0f) / 254.0f; }
	void setFiltBeats(float v) override { lfoBeats_ = 1.0f + std::round(std::clamp(v, 0.0f, 1.0f) * 15.0f); }
	[[nodiscard]] float getFiltBeats() const override { return (lfoBeats_ - 1.0f) / 15.0f; }
	void setFiltShape(int32_t v) override { lfoShape_ = std::clamp<int32_t>(v, 0, 4); }
	[[nodiscard]] int32_t getFiltShape() const override { return lfoShape_; }
	void setFiltPlsw(float v) override { lfoPlsw_ = std::clamp(v, 0.0f, 1.0f); }
	[[nodiscard]] float getFiltPlsw() const override { return lfoPlsw_; }
	void setFiltSmooth(float v) override { lfoSmooth_ = std::clamp(v, 0.0f, 1.0f); }
	[[nodiscard]] float getFiltSmooth() const override { return lfoSmooth_; }

	static float systemTempoBpm() { return kSystemTempoBPM.load(std::memory_order_relaxed); }

	// ---- LFO Sweep params (902): ModRate/ModDepth (unused by this engine) ----
	void setModRate(float v) override {
		modRate_val_ = std::clamp(v, 0.0f, 1.0f);
		float hz = 0.01f + modRate_val_ * 9.99f; // manual LFO range 0.01..10 HZ
		lfoInc_ = static_cast<uint32_t>((hz / float(kSampleRate)) * 4294967296.0f);
	}
	[[nodiscard]] float getModRate() const override { return modRate_val_; }
	void setModDepth(float v) override { modDepth_val_ = std::clamp(v, 0.0f, 1.0f); }
	[[nodiscard]] float getModDepth() const override { return modDepth_val_; }

private:
	static constexpr float kMinHz = 10.0f;
	static constexpr float kMaxHz = 20000.0f;

	static float freqFromNorm(float t) { return kMinHz * std::pow(kMaxHz / kMinHz, std::clamp(t, 0.0f, 1.0f)); }
	// Inverse of freqFromNorm — for mapping the swept Hz back to knob space
	// before the synth param conversion.
	static float centreToNorm(float hz) {
		return std::log(std::clamp(hz, kMinHz, kMaxHz) / kMinHz) / std::log(kMaxHz / kMinHz);
	}


	Sweep mode_;
	int32_t type_ = 0;

	dsp::filter::FilterSet filterSet_{};

	float res_val_ = 0.2f;
	float centre_val_ = 0.5f, centreHz_ = freqFromNorm(0.5f);

	float envDb_ = -60.0f;
	float atk_val_ = 0.5f, rel_val_ = 0.5f;
	float minHz_val_ = 0.3f, sweep_val_ = 0.5f;
	// Rates in dB/second: quadratic like the compressor times (fast near the top).
	float atkRateDb_ = 60.0f, relRateDb_ = 60.0f;

	float modRate_val_ = 0.3f, modDepth_val_ = 0.3f;
	uint32_t lfoPhase_ = 0, lfoInc_ = 100000;


	int32_t lfoClock_ = 1; // 0 FIXED, 1 SYSTEM (manual default: follow tempo)
	float lfoBpm_ = 120.0f;
	float lfoBeats_ = 1.0f;
	int32_t lfoShape_ = 0;
	float lfoPlsw_ = 0.5f, lfoSmooth_ = 0.0f;
};

} // namespace deluge::dsp::reverb
