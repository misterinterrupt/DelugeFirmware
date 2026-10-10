/*
 * KDFX Pitcher (manual algorithm 908, 1 PAU) — per docs/pitcher_bloc_spec.md:
 * NOT a pitch shifter. A tuned harmonic resonator bank: a stack of narrow
 * peaks at N x f0 (N = 1..16) that imposes a strong sense of pitch at the
 * selected fundamental. Manual params: PITCH (MIDI note C-1..G9), Ptch Offst
 * ±12.0 st (the FXMod target — pitch bend / ribbon / LFO), and the four
 * drawbars Odd/Pair/Quartr/Half Wts, whose interaction shapes the response:
 *
 *   weight(H) = (H odd ? ODD : PAIR) x (H%4==0 ? QTR : 1) x (H%2==0 ? HALF : 1)
 *
 * Engine: one bank of Chamberlin two-pole resonators per stereo channel (the
 * same resonator voice as the Spectral 4-Tap combs after the audibility fix),
 * unity peak gain each (bp x damp), summed at 1/2. Coefficients are recomputed
 * at the top of every render block from the stored normalized params — setters
 * only store, so PITCH sweeps are click-free and harmonic drop-in/drop-out as
 * f0 moves rides the same per-block path. No feedback loop exists.
 */

#pragma once

#include "definitions_cxx.hpp"
#include "dsp/reverb/base.hpp"
#include "dsp/reverb/kdfx/kdfx_util.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace deluge::dsp::reverb {

class KdfxPitcher : public Base {
public:
	KdfxPitcher() = default;
	~KdfxPitcher() override = default;

	[[nodiscard]] bool isInsert() const override { return true; }

	// Never called for insert engines (FxBloc::process branches on isInsert()).
	void process(std::span<int32_t> input, std::span<StereoSample> output) override {}

	void processInsert(std::span<StereoSample> buffer) override {
		tune(); // per-block coefficient update — setters only store

		for (size_t frame = 0; frame < buffer.size(); frame++) {
			StereoSample& s = buffer[frame];
			int64_t sumL = 0;
			int64_t sumR = 0;
			for (int h = 0; h < kHarmonics; h++) {
				if (gainQ31_[h] == 0) {
					continue; // group muted or harmonic above the ceiling — bypass
				}
				q31_t f = f_[h];
				q31_t damp = damp_[h];
				Chamberlin& cl = bankL_[h];
				q31_t hp = add_saturation(add_saturation(-cl.lp, -kdfx::q31_mul(damp, cl.bp)), s.l);
				cl.bp += kdfx::q31_mul(f, hp);
				cl.lp += kdfx::q31_mul(f, cl.bp);
				Chamberlin& cr = bankR_[h];
				hp = add_saturation(add_saturation(-cr.lp, -kdfx::q31_mul(damp, cr.bp)), s.r);
				cr.bp += kdfx::q31_mul(f, hp);
				cr.lp += kdfx::q31_mul(f, cr.bp);
				q31_t w = gainQ31_[h];
				sumL += kdfx::q31_mul(kdfx::q31_mul(cl.bp, damp), w);
				sumR += kdfx::q31_mul(kdfx::q31_mul(cr.bp, damp), w);
			}
			s.l = kdfx::qcollapse(sumL >> 1); // 1/2 bank scale — bounds broadband content
			s.r = kdfx::qcollapse(sumR >> 1);
		}
	}

	// PITCH: C-1..G9 as MIDI note numbers (0..127)
	void setRoomSize(float v) override { pitchVal_ = std::clamp(v, 0.0f, 1.0f); }
	[[nodiscard]] float getRoomSize() const override { return pitchVal_; }

	// PTCH-OFF: ±12.0 semitones, bipolar (0.5 = 0 st) — the FXMod target
	void setDecay(float v) override { offVal_ = std::clamp(v, 0.0f, 1.0f); }
	[[nodiscard]] float getDecay() const override { return offVal_; }

	// Drawbars, 0..100% (group weights combine per the header formula)
	void setTrebleGain(float v) override { oddW_ = std::clamp(v, 0.0f, 1.0f); } // Odd Wts
	[[nodiscard]] float getTrebleGain() const override { return oddW_; }
	void setTrebleFreq(float v) override { pairW_ = std::clamp(v, 0.0f, 1.0f); } // Pair Wts
	[[nodiscard]] float getTrebleFreq() const override { return pairW_; }
	void setAirShelf(float v) override { qtrW_ = std::clamp(v, 0.0f, 1.0f); } // Quartr Wts
	[[nodiscard]] float getAirShelf() const override { return qtrW_; }
	void setDelayTimeR(float v) override { halfW_ = std::clamp(v, 0.0f, 1.0f); } // Half Wts
	[[nodiscard]] float getDelayTimeR() const override { return halfW_; }

	/// The resonators ring well under a second after input stops (Q~25, but
	/// the high harmonics are low-Q); the TAILS gate needs a non-zero estimate.
	[[nodiscard]] uint32_t tailSamples() const override { return 44100 / 2; }

private:
	static constexpr int kHarmonics = 16; // N = 1..16 peaks at N x f0
	static constexpr float kQ = 25.0f;

	struct Chamberlin {
		q31_t lp = 0, bp = 0;
	};

	// Per-block tuning: f0 from PITCH + PTCH-OFF, then per-harmonic coefficients
	// and drawbar weights. Harmonics at or above ~15.4 kHz (0.35 x sr) drop out.
	void tune() {
		float note = pitchVal_ * 127.0f + (offVal_ - 0.5f) * 24.0f; // ±12 st offset
		float f0 = 440.0f * std::pow(2.0f, (note - 69.0f) / 12.0f);
		for (int h = 0; h < kHarmonics; h++) {
			int n = h + 1;
			float weight = (n % 2 == 1 ? oddW_ : pairW_);
			if (n % 4 == 0) {
				weight *= qtrW_;
			}
			if (n % 2 == 0) {
				weight *= halfW_;
			}
			float fc = f0 * static_cast<float>(n);
			if (fc > float(kSampleRate) * 0.35f) {
				weight = 0.0f; // harmonic above the tuned band — drop it
			}
			gainQ31_[h] = static_cast<q31_t>(weight * ONE_Q31f);
			// Chamberlin: f = 2 sin(pi fc/sr), damp = 1/Q held at >= f/2 for stability
			float f = std::clamp(2.0f * std::sin(3.14159265f * fc / float(kSampleRate)), 0.0f, 1.5f);
			float damp = std::max(1.0f / kQ, f * 0.5f);
			f_[h] = static_cast<q31_t>(f * ONE_Q31f);
			damp_[h] = static_cast<q31_t>(damp * ONE_Q31f);
		}
	}

	Chamberlin bankL_[kHarmonics]{};
	Chamberlin bankR_[kHarmonics]{};
	q31_t f_[kHarmonics] = {};
	q31_t damp_[kHarmonics] = {};
	q31_t gainQ31_[kHarmonics] = {};

	// Setters only store; tune() reads these at the top of every block.
	float pitchVal_ = 48.0f / 127.0f; // C3
	float offVal_ = 0.5f;             // 0 st
	float oddW_ = 1.0f, pairW_ = 1.0f, qtrW_ = 1.0f, halfW_ = 1.0f;
};

} // namespace deluge::dsp::reverb
