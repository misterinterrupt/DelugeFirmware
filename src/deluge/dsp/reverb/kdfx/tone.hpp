/*
 * KDFX-style tone shaper / soft limiter / preamp.
 *
 * Features:
 * - Drive gain (0..24 dB)
 * - 4 Saturation / Shaper Curves:
 *   0: Soft Tube (cubic polynomial)
 *   1: Tape Warmth (asymmetric even+odd saturation)
 *   2: Hard Clip (soft-knee clipper)
 *   3: Wavefold (sinusoidal foldback)
 * - 3-Band Parametric EQ:
 *   - Bass shelf (Gain -15..+15 dB, Freq 30..400 Hz)
 *   - Parametric Mid band (Gain -15..+15 dB, Freq 200..8000 Hz)
 *   - Treble shelf (Gain -15..+15 dB, Freq 1..16 kHz)
 * - Output Level (-24..+12 dB)
 */

#pragma once

#include "definitions_cxx.hpp"
#include "dsp/reverb/base.hpp"
#include "dsp/reverb/kdfx/kdfx_util.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace deluge::dsp::reverb {

class KdfxTone : public Base {
public:
	KdfxTone() = default;
	~KdfxTone() override = default;

	void process(std::span<int32_t> in, std::span<StereoSample> output) override {
		for (size_t frame = 0; frame < in.size(); frame++) {
			StereoSample& s = output[frame];
			q31_t inSample = in[frame];

			// 1. Apply drive gain
			int64_t driven = (static_cast<int64_t>(inSample) * driveGain_) >> 12;
			q31_t clampedIn = kdfx::qcollapse(driven);

			// 2. Multi-mode Saturation Curves
			q31_t shaped = 0;
			switch (shaperMode_) {
			case 1: { // Tape Warmth (asymmetric with even harmonic presence)
				int64_t x_sq = (static_cast<int64_t>(clampedIn) * clampedIn) >> 31;
				int64_t x_cu = (x_sq * clampedIn) >> 31;
				int64_t y = clampedIn - (x_sq >> 2) - (x_cu >> 2);
				shaped = kdfx::shaperClamp(y);
				break;
			}
			case 2: { // Hard Clip with soft knee
				if (clampedIn > 0x60000000) {
					shaped = 0x60000000 + ((clampedIn - 0x60000000) >> 3);
				}
				else if (clampedIn < -0x60000000) {
					shaped = -0x60000000 + ((clampedIn + 0x60000000) >> 3);
				}
				else {
					shaped = clampedIn;
				}
				break;
			}
			case 3: { // Wavefold (sinusoidal folding in Q31)
				uint32_t phase = static_cast<uint32_t>(clampedIn) ^ 0x80000000u;
				int32_t x = static_cast<int32_t>(phase) - 0x40000000;
				int64_t absX = std::abs(x);
				int64_t oneMinusAbsX = 0x7FFFFFFF - absX;
				int64_t y = (static_cast<int64_t>(x) * oneMinusAbsX) >> 29;
				shaped = kdfx::qcollapse(y);
				break;
			}
			case 0: // Soft Tube (1.5 * x - 0.5 * x^3)
			default: {
				int64_t x_sq = (static_cast<int64_t>(clampedIn) * clampedIn) >> 31;
				int64_t x_cu = (x_sq * clampedIn) >> 31;
				int64_t term1 = (3LL * clampedIn) >> 1;
				int64_t term2 = x_cu >> 1;
				shaped = kdfx::shaperClamp(term1 - term2);
				break;
			}
			}

			// 3. 3-Band Parametric Filter Section
			// Bass filter
			kdfx::smoothDamp(bassL_, shaped, bassCoeff_);
			q31_t bassBand = bassL_;
			q31_t bassBoosted = add_saturation(shaped, kdfx::q31_mul(bassBand, bassGainQ31_));

			// Mid band (bandpass resonator: hp of lp)
			kdfx::smoothDamp(midLpL_, bassBoosted, midCoeff_);
			kdfx::smoothDamp(midHpL_, midLpL_, midCoeff_ >> 1);
			q31_t midBand = midLpL_ - midHpL_;
			q31_t midBoosted = add_saturation(bassBoosted, kdfx::q31_mul(midBand, midGainQ31_));

			// Treble filter (high shelf)
			kdfx::smoothDamp(trebleL_, midBoosted, trebleCoeff_);
			q31_t trebleBand = midBoosted - trebleL_;
			q31_t eqOut = add_saturation(midBoosted, kdfx::q31_mul(trebleBand, trebleGainQ31_));

			// 4. Normalize the drive gain away, then the user output level
			q31_t outL = kdfx::q31_mul(kdfx::q31_mul(eqOut, normGain_), outLevel_);
			q31_t outR = outL;

			kdfx::mixWet(s.l, outL, getPanLeft());
			kdfx::mixWet(s.r, outR, getPanRight());
		}
	}

	void setRoomSize(float value) override {
		setDrive(value);
	}
	[[nodiscard]] float getRoomSize() const override { return drive_val_; }

	void setDrive(float value) override {
		drive_val_ = value;
		// 0 dB (4096 in Q12) to +24 dB (~65000 in Q12)
		float linearGain = std::pow(10.0f, (value * 24.0f) / 20.0f);
		driveGain_ = static_cast<int32_t>(linearGain * 4096.0f);
		// Post-normalization: undo the drive's linear gain at the output so the
		// bloc hands the next bloc a sane level no matter how hot the DRIVE.
		// (Only ever attenuates; 0 dB drive = unity.)
		normGain_ = static_cast<q31_t>((1.0f / linearGain) * ONE_Q31f);
	}
	[[nodiscard]] float getDrive() const override { return drive_val_; }

	void setShaperMode(int32_t value) override {
		shaperMode_ = std::clamp<int32_t>(value, 0, 3);
	}
	[[nodiscard]] int32_t getShaperMode() const override { return shaperMode_; }

	void setBassGain(float value) override {
		bass_gain_val_ = value;
		// -15 dB (0) to +15 dB (1.0) -> centered at 0.5 (0 dB)
		bassGainQ31_ = static_cast<q31_t>((value - 0.5f) * 2.0f * ONE_Q31f);
	}
	[[nodiscard]] float getBassGain() const override { return bass_gain_val_; }

	void setBassFreq(float value) override {
		bass_freq_val_ = value;
		// 30 Hz to 400 Hz
		float fc_hz = 30.0f + value * 370.0f;
		float fc = fc_hz / float(kSampleRate);
		float wc = fc / (1.0f + fc);
		bassCoeff_ = static_cast<q31_t>(std::clamp(wc, 0.0005f, 0.45f) * ONE_Q31f);
	}
	[[nodiscard]] float getBassFreq() const override { return bass_freq_val_; }

	void setMidGain(float value) override {
		mid_gain_val_ = value;
		midGainQ31_ = static_cast<q31_t>((value - 0.5f) * 2.0f * ONE_Q31f);
	}
	[[nodiscard]] float getMidGain() const override { return mid_gain_val_; }

	void setMidFreq(float value) override {
		mid_freq_val_ = value;
		// 200 Hz to 8000 Hz
		float fc_hz = 200.0f + value * 7800.0f;
		float fc = fc_hz / float(kSampleRate);
		float wc = fc / (1.0f + fc);
		midCoeff_ = static_cast<q31_t>(std::clamp(wc, 0.001f, 0.48f) * ONE_Q31f);
	}
	[[nodiscard]] float getMidFreq() const override { return mid_freq_val_; }

	void setTrebleGain(float value) override {
		treble_gain_val_ = value;
		trebleGainQ31_ = static_cast<q31_t>((value - 0.5f) * 2.0f * ONE_Q31f);
	}
	[[nodiscard]] float getTrebleGain() const override { return treble_gain_val_; }

	void setTrebleFreq(float value) override {
		treble_freq_val_ = value;
		// 1000 Hz to 16000 Hz
		float fc_hz = 1000.0f + value * 15000.0f;
		float fc = fc_hz / float(kSampleRate);
		float wc = fc / (1.0f + fc);
		trebleCoeff_ = static_cast<q31_t>(std::clamp(wc, 0.01f, 0.48f) * ONE_Q31f);
	}
	[[nodiscard]] float getTrebleFreq() const override { return treble_freq_val_; }

	void setDiffusion(float value) override {
		setOutLevel(value);
	}
	[[nodiscard]] float getDiffusion() const override { return level_val_; }

	void setOutLevel(float value) override {
		level_val_ = value;
		// -24 dB to +12 dB
		float linearLevel = std::pow(10.0f, ((value * 36.0f) - 24.0f) / 20.0f);
		outLevel_ = static_cast<q31_t>(std::clamp(linearLevel * 0.5f, 0.01f, 1.0f) * ONE_Q31f);
	}
	[[nodiscard]] float getOutLevel() const override { return level_val_; }

	void setDecay(float value) override { setTrebleGain(value); }
	[[nodiscard]] float getDecay() const override { return treble_gain_val_; }

	void setDamping(float value) override { setBassGain(value); }
	[[nodiscard]] float getDamping() const override { return bass_gain_val_; }

private:
	int32_t driveGain_ = 4096;
	int32_t shaperMode_ = 0;

	q31_t bassCoeff_ = static_cast<q31_t>(0.02f * ONE_Q31f);
	q31_t bassGainQ31_ = 0;
	q31_t midCoeff_ = static_cast<q31_t>(0.10f * ONE_Q31f);
	q31_t midGainQ31_ = 0;
	q31_t trebleCoeff_ = static_cast<q31_t>(0.30f * ONE_Q31f);
	q31_t trebleGainQ31_ = 0;

	q31_t outLevel_ = static_cast<q31_t>(0.8f * ONE_Q31f);
	q31_t normGain_ = ONE_Q31; // 1/drive, applied at the output

	q31_t bassL_ = 0;
	q31_t midLpL_ = 0, midHpL_ = 0;
	q31_t trebleL_ = 0;

	float drive_val_ = 0.0f;
	float bass_gain_val_ = 0.5f, bass_freq_val_ = 0.3f;
	float mid_gain_val_ = 0.5f, mid_freq_val_ = 0.5f;
	float treble_gain_val_ = 0.5f, treble_freq_val_ = 0.5f;
	float level_val_ = 0.67f;
};

} // namespace deluge::dsp::reverb
