/*
 * KDFX Spectral 4-Tap (manual algorithm 135) — the multitap delay whose output
 * taps are each shaped by a tuned comb filter, a shaper, and balance/level
 * controls; the feedback path runs a diffuser, hipass, lopass, and imager
 * (Fdbk Image). Manual params: Tap n Shapr 0.10-6.00x, Tap n Pitch C-1..C8
 * (semitone comb frequency), Tap n PtAmt 0-100%, Tap n Bal/Lvl, Fdbk 0-100%
 * (100% sustains), Fdbk Image -100..100%, Tempo 1-255 BPM or System.
 */

#pragma once

#include "definitions_cxx.hpp"
#include "dsp/reverb/base.hpp"
#include "dsp/reverb/kdfx/kdfx_util.hpp"
#include "modulation/params/param.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace deluge::dsp::reverb {

class KdfxSpectral4Tap : public Base {
public:
	KdfxSpectral4Tap() = default;
	~KdfxSpectral4Tap() override = default;

	void process(std::span<int32_t> in, std::span<StereoSample> output) override {
		const size_t n = in.size();
		for (size_t frame = 0; frame < n; frame++) {
			StereoSample& s = output[frame];
			q31_t inputSample = in[frame];

			// Feedback read (loop tap), diffused, filtered, imaged
			size_t d = std::clamp<size_t>(loopDelay_, 1, kMaxLoop - 1);
			q31_t loopL = kdfx::circReadCapped(bufL_, writePos_, d, age_);
			q31_t loopR = kdfx::circReadCapped(bufR_, writePos_, d, age_);

			// Diffuser in the feedback path (one-pole smear)
			kdfx::smoothDamp(diffL_, loopL, diffCoeff_);
			kdfx::smoothDamp(diffR_, loopR, diffCoeff_);
			loopL = diffL_;
			loopR = diffR_;

			// Hipass in the feedback path
			kdfx::smoothDamp(hpL_, loopL, hpfCoeff_);
			loopL -= hpL_;
			kdfx::smoothDamp(hpR_, loopR, hpfCoeff_);
			loopR -= hpR_;

			// Lopass in the feedback path
			kdfx::smoothDamp(lpL_, loopL, lpfCoeff_);
			loopL = lpL_;
			kdfx::smoothDamp(lpR_, loopR, lpfCoeff_);
			loopR = lpR_;

			// Fdbk Image: rotate the stereo image each pass (-100..100%)
			q31_t cross = imageQ31_;
			q31_t fbInL = add_saturation(kdfx::q31_mul(loopL, ONE_Q31 - cross), kdfx::q31_mul(loopR, cross));
			q31_t fbInR = add_saturation(kdfx::q31_mul(loopR, ONE_Q31 - cross), kdfx::q31_mul(loopL, cross));
			fbInL = kdfx::q31_mul(fbInL, feedback_);
			fbInR = kdfx::q31_mul(fbInR, feedback_);

			q31_t nextL = add_saturation(inputSample, fbInL);
			q31_t nextR = add_saturation(inputSample, fbInR);
			bufL_[writePos_] = nextL;
			bufR_[writePos_] = nextR;
			++age_;
			writePos_ = (writePos_ + 1) % kMaxLoop;

			// Output taps: each reads the loop at a fraction of the loop time,
			// then through its tuned comb, shaper, balance, level.
			q31_t wetL = 0;
			q31_t wetR = 0;
			for (int t = 0; t < 4; t++) {
				size_t tapDelay = std::clamp<size_t>(static_cast<size_t>(tapFrac_[t] * (float)(d - 1)) + 1, 1,
				                                     kMaxLoop - 1);
				q31_t tapL = kdfx::circReadCapped(bufL_, writePos_, tapDelay, age_);
				q31_t tapR = kdfx::circReadCapped(bufR_, writePos_, tapDelay, age_);

				// Shaper: polynomial soft clip scaled by intensity (0.10-6.00x)
				q31_t shapedL = tapL;
				q31_t shapedR = tapR;
				if (shaperGain_[t] > 4096) {
					shapedL = shapeTap(tapL, shaperGain_[t]);
					shapedR = shapeTap(tapR, shaperGain_[t]);
				}

				// Comb filter: one-pole tuned resonator at the tap's pitch.
				// A comb at frequency f over tap delay T is a delay of
				// sampleRate/f samples; approximate with a one-pole
				// resonant bandpass (half-bandwidth fixed Q) whose gain
				// is set by PtAmt.
				q31_t combL = runComb(combLP_[t], combHP_[t], shapedL, combCoeff_[t]);
				q31_t combR = runComb(combLP2_[t], combHP2_[t], shapedR, combCoeff_[t]);
				q31_t amt = ptAmtQ31_[t];
				shapedL = add_saturation(kdfx::q31_mul(shapedL, ONE_Q31 - amt), kdfx::q31_mul(combL, amt));
				shapedR = add_saturation(kdfx::q31_mul(shapedR, ONE_Q31 - amt), kdfx::q31_mul(combR, amt));

				// Balance (-100..100% hard pan L..R) and level
				q31_t balL = balQ31_[t] >= 0 ? (ONE_Q31 - balQ31_[t]) : ONE_Q31; // >0: weight left down
				q31_t balR = balQ31_[t] <= 0 ? (ONE_Q31 + balQ31_[t]) : ONE_Q31;  // <0: weight right down
				wetL = add_saturation(wetL, kdfx::q31_mul(kdfx::q31_mul(shapedL, balL), tapLevel_[t]));
				wetR = add_saturation(wetR, kdfx::q31_mul(kdfx::q31_mul(shapedR, balR), tapLevel_[t]));
			}

			kdfx::mixWet(s.l, wetL, getPanLeft());
			kdfx::mixWet(s.r, wetR, getPanRight());
		}
	}

	// --- Params (Param aliases from the house table below) ---
	void setRoomSize(float v) override { // Loop time (0..1 -> 0..2550ms)
		loopDelay_ = static_cast<size_t>(1 + v * (kMaxLoop - 2));
	}
	[[nodiscard]] float getRoomSize() const override { return (float)(loopDelay_ - 1) / (kMaxLoop - 2); }

	void setDecay(float v) override { // Fdbk 0-100%
		feedback_ = static_cast<q31_t>(std::clamp(v, 0.0f, 1.0f) * ONE_Q31f);
	}
	[[nodiscard]] float getDecay() const override { return feedback_val_; }

	void setDiffusion(float v) override { // Fdbk Image -100..100%
		image_val_ = v;
		imageQ31_ = static_cast<q31_t>(std::clamp(v, -1.0f, 1.0f) * ONE_Q31f);
	}
	[[nodiscard]] float getDiffusion() const override { return image_val_; }

	void setHPF(float v) override { // feedback hipass cutoff 0..1 (20-1000Hz)
		hpf_val_ = v;
		float fc = 20.0f + v * 980.0f;
		float fc_n = fc / float(kSampleRate);
		hpfCoeff_ = static_cast<q31_t>(std::clamp(fc_n / (1.0f + fc_n), 0.0005f, 0.45f) * ONE_Q31f);
	}
	[[nodiscard]] float getHPF() const override { return hpf_val_; }

	void setDamping(float v) override { // feedback lopass tone
		lpf_val_ = v;
		float fc = 351.0f + v * 6668.0f;
		float fc_n = fc / float(kSampleRate);
		lpfCoeff_ = static_cast<q31_t>(std::clamp(fc_n, 0.001f, 0.45f) * ONE_Q31f);
	}
	[[nodiscard]] float getDamping() const override { return lpf_val_; }

	// Tap frac: tap n reads the loop at this fraction (0..1)
	void setWidth(float v) override { tapFrac_[0] = std::clamp(v, 0.02f, 1.0f); }
	[[nodiscard]] float getWidth() const override { return tapFrac_[0]; }
	void setPreDelay(float v) override { tapFrac_[1] = std::clamp(v, 0.02f, 1.0f); }
	[[nodiscard]] float getPreDelay() const override { return tapFrac_[1]; }
	void setEarlyReflections(float v) override { tapFrac_[2] = std::clamp(v, 0.02f, 1.0f); }
	[[nodiscard]] float getEarlyReflections() const override { return tapFrac_[2]; }
	void setBassDecay(float v) override { tapFrac_[3] = std::clamp(v, 0.02f, 1.0f); }
	[[nodiscard]] float getBassDecay() const override { return tapFrac_[3]; }

	// Per-tap comb pitch (semitone freq, 0..1 -> C-1..C8)
	void setBassGain(float v) override { setTapPitch(0, v); }
	[[nodiscard]] float getBassGain() const override { return pitch_[0]; }
	void setBassFreq(float v) override { setTapPitch(1, v); }
	[[nodiscard]] float getBassFreq() const override { return pitch_[1]; }
	void setMidGain(float v) override { setTapPitch(2, v); }
	[[nodiscard]] float getMidGain() const override { return pitch_[2]; }
	void setMidFreq(float v) override { setTapPitch(3, v); }
	[[nodiscard]] float getMidFreq() const override { return pitch_[3]; }

	// Per-tap PtAmt (0..1)
	void setTrebleGain(float v) override { setTapAmt(0, v); }
	[[nodiscard]] float getTrebleGain() const override { return amt_[0]; }
	void setTrebleFreq(float v) override { setTapAmt(1, v); }
	[[nodiscard]] float getTrebleFreq() const override { return amt_[1]; }
	void setAirShelf(float v) override { setTapAmt(2, v); }
	[[nodiscard]] float getAirShelf() const override { return amt_[2]; }
	void setDelayTimeR(float v) override { setTapAmt(3, v); }
	[[nodiscard]] float getDelayTimeR() const override { return amt_[3]; }

	// Per-tap Shapr (0..1 -> 0.10-6.00x)
	void setDelayDrive(float v) override { setTapShapr(0, v); }
	[[nodiscard]] float getDelayDrive() const override { return shapr_[0]; }
	void setChorusVoices(int32_t v) override { setTapShapr(1, std::clamp<int32_t>(v, 0, 100) / 100.0f); }
	void setShaperMode(int32_t v) override { setTapShapr(2, std::clamp<int32_t>(v, 0, 100) / 100.0f); }
	void setComboCfg(int32_t v) override { setTapShapr(3, std::clamp<int32_t>(v, 0, 100) / 100.0f); }

	[[nodiscard]] uint32_t tailSamples() const override {
		if (feedback_val_ < 0.0001f) {
			return 0;
		}
		constexpr uint32_t kCeiling = 60u * 44100u;
		if (feedback_val_ >= 0.9999f) {
			return kCeiling;
		}
		float n = 6.0f / -std::log(feedback_val_);
		double tail = static_cast<double>(n) * static_cast<double>(loopDelay_);
		if (tail >= static_cast<double>(kCeiling)) {
			return kCeiling;
		}
		return static_cast<uint32_t>(tail);
	}

private:
	static constexpr size_t kMaxLoop = 65536; // ~1486ms @ 44.1k — loop tap budget

	static q31_t shapeTap(q31_t x, int32_t gainQ12) {
		int64_t driven = (static_cast<int64_t>(x) * gainQ12) >> 12;
		q31_t c = kdfx::qcollapse(driven);
		int64_t x_sq = (static_cast<int64_t>(c) * c) >> 31;
		int64_t x_cu = (x_sq * c) >> 31;
		int64_t y = ((3LL * c) >> 1) - (x_cu >> 1);
		return kdfx::shaperClamp(y);
	}

	// One-pole resonant bandpass (LP then subtract HP of LP) — the comb voice
	q31_t runComb(q31_t& lp, q31_t& hp, q31_t x, q31_t coeff) {
		kdfx::smoothDamp(lp, x, coeff);
		kdfx::smoothDamp(hp, lp, coeff >> 1);
		return lp - hp;
	}

	void setTapPitch(int t, float v) {
		pitch_[t] = std::clamp(v, 0.0f, 1.0f);
		// C-1 (8.18Hz at 44.1k? manual: semitone steps from C-1) to C8 (~4186Hz)
		float semis = pitch_[t] * 108.0f; // 9 octaves
		float hz = 8.175f * std::pow(2.0f, semis / 12.0f);
		float fc_n = std::clamp(hz / float(kSampleRate), 0.0002f, 0.45f);
		combCoeff_[t] = static_cast<q31_t>(fc_n / (1.0f + fc_n) * ONE_Q31f);
	}
	void setTapAmt(int t, float v) {
		amt_[t] = std::clamp(v, 0.0f, 1.0f);
		ptAmtQ31_[t] = static_cast<q31_t>(amt_[t] * ONE_Q31f);
	}
	void setTapShapr(int t, float v) {
		shapr_[t] = std::clamp(v, 0.0f, 1.0f);
		float x = 0.10f + shapr_[t] * 5.90f;
		shaperGain_[t] = static_cast<int32_t>(x * 4096.0f);
	}

	// Buffers deliberately uninitialized: circReadCapped keeps reads within the
	// filled region; clearing 512KB would blow the SSI TX buffer on swap.
	std::array<q31_t, kMaxLoop> bufL_{};
	std::array<q31_t, kMaxLoop> bufR_{};

	size_t writePos_ = 0;
	uint32_t age_ = 0;
	size_t loopDelay_ = 22050; // ~500ms default loop

	q31_t feedback_ = static_cast<q31_t>(0.5f * ONE_Q31f);
	q31_t imageQ31_ = 0;
	q31_t diffCoeff_ = static_cast<q31_t>(0.5f * ONE_Q31f);
	q31_t hpfCoeff_ = 0;
	q31_t lpfCoeff_ = static_cast<q31_t>(0.3f * ONE_Q31f);

	float feedback_val_ = 0.5f, image_val_ = 0.0f, hpf_val_ = 0.0f, lpf_val_ = 0.5f;
	float tapFrac_[4] = {0.15f, 0.35f, 0.6f, 0.85f};
	float pitch_[4] = {0.5f, 0.6f, 0.7f, 0.8f};
	float amt_[4] = {0.5f, 0.5f, 0.5f, 0.5f};
	float shapr_[4] = {0.f, 0.f, 0.f, 0.f};
	q31_t combCoeff_[4] = {};
	q31_t ptAmtQ31_[4] = {};
	int32_t shaperGain_[4] = {4096, 4096, 4096, 4096};
	q31_t balQ31_[4] = {};
	q31_t tapLevel_[4] = {static_cast<q31_t>(0.5f * ONE_Q31f), static_cast<q31_t>(0.5f * ONE_Q31f),
	                     static_cast<q31_t>(0.5f * ONE_Q31f), static_cast<q31_t>(0.5f * ONE_Q31f)};

	q31_t diffL_ = 0, diffR_ = 0, hpL_ = 0, hpR_ = 0, lpL_ = 0, lpR_ = 0;
	q31_t combLP_[4] = {}, combHP_[4] = {}, combLP2_[4] = {}, combHP2_[4] = {};
};

} // namespace deluge::dsp::reverb
