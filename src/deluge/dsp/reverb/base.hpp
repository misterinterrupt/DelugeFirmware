#pragma once
#include "dsp/stereo_sample.h"
#include <cmath>
#include <span>

namespace deluge::dsp::reverb {
struct Base {
	Base() = default;
	virtual ~Base() = default;

	virtual void process(std::span<int32_t> input, std::span<StereoSample> output) = 0;

	/// Insert-style engines transform the stereo buffer in place instead of adding a
	/// mono-summed wet output — required by effects that must act on the dry signal
	/// (tremolo, auto-pan; later the compressor/filter adapters). The host branches
	/// on isInsert() and never calls process() for them.
	[[nodiscard]] virtual bool isInsert() const { return false; }
	virtual void processInsert(std::span<StereoSample> buffer) {}

	constexpr void setPanLevels(const int32_t amplitude_left, const int32_t amplitude_right) {
		amplitude_right_ = amplitude_right;
		amplitude_left_ = amplitude_left;
	}

	static constexpr float calcFilterCutoff(float f) {
		// this exp will be between 1 and 4.48, half the knob range is about 2
		// the result will then be from 0 to 500Hz with half the knob range at 300hz
		// then shift to 20-520Hz as there is a low end buildup in the reverb that should always be filtered out
		float fc_hz = 20 + (std::exp(1.5f * f) - 1) * 150;
		float fc = fc_hz / float(kSampleRate);
		float wc = fc / (1 + fc);
		return wc;
	}

	// Dummy functions
	virtual void setRoomSize(float value) {}
	[[nodiscard]] virtual float getRoomSize() const { return 0; };

	virtual void setDecay(float value) {}
	[[nodiscard]] virtual float getDecay() const { return 0; }

	virtual void setHPF(float f) {}
	[[nodiscard]] virtual float getHPF() const { return 0; }

	virtual void setDamping(float value) {}
	[[nodiscard]] virtual float getDamping() const { return 0; }

	virtual void setDiffusion(float value) {}
	[[nodiscard]] virtual float getDiffusion() const { return 0; }

	virtual void setWidth(float value) {}
	[[nodiscard]] virtual float getWidth() const { return 0; };

	// Extended KDFX Reverb parameters
	virtual void setPreDelay(float value) {}
	[[nodiscard]] virtual float getPreDelay() const { return 0.0f; }

	virtual void setEarlyReflections(float value) {}
	[[nodiscard]] virtual float getEarlyReflections() const { return 0.0f; }

	virtual void setBassDecay(float value) {}
	[[nodiscard]] virtual float getBassDecay() const { return 0.5f; }

	virtual void setAirShelf(float value) {}
	[[nodiscard]] virtual float getAirShelf() const { return 0.0f; }

	// Extended KDFX Delay parameters
	virtual void setDelayTimeL(float value) {}
	[[nodiscard]] virtual float getDelayTimeL() const { return 0.5f; }

	virtual void setDelayTimeR(float value) {}
	[[nodiscard]] virtual float getDelayTimeR() const { return 0.5f; }

	virtual void setDelayDrive(float value) {}
	[[nodiscard]] virtual float getDelayDrive() const { return 0.0f; }

	virtual void setPhaseInvert(bool value) {}
	[[nodiscard]] virtual bool getPhaseInvert() const { return false; }

	// Extended KDFX Chorus parameters
	virtual void setBaseDelay(float value) {}
	[[nodiscard]] virtual float getBaseDelay() const { return 0.2f; }

	virtual void setChorusVoices(int32_t value) {}
	[[nodiscard]] virtual int32_t getChorusVoices() const { return 2; }

	virtual void setChorusWaveform(int32_t value) {}
	[[nodiscard]] virtual int32_t getChorusWaveform() const { return 0; }

	virtual void setChorusFeedback(float value) {}
	[[nodiscard]] virtual float getChorusFeedback() const { return 0.0f; }

	// Extended KDFX Tone parameters
	virtual void setDrive(float value) {}
	[[nodiscard]] virtual float getDrive() const { return 0.0f; }

	// Extended KDFX Distortion parameters
	virtual void setLoCut(float value) {}
	[[nodiscard]] virtual float getLoCut() const { return 0.0f; }
	virtual void setCab(float value) {}
	[[nodiscard]] virtual float getCab() const { return 0.0f; }
	virtual void setHiCut(float value) {}
	[[nodiscard]] virtual float getHiCut() const { return 1.0f; }

	// Combination-bloc parameters (TubeAmp<>MD>Chor class): order config plus the
	// modulation LFO trio, routed by each combination to its own sub-engines.
	virtual void setComboCfg(int32_t value) {}
	[[nodiscard]] virtual int32_t getComboCfg() const { return 0; }
	virtual void setModRate(float value) {}
	[[nodiscard]] virtual float getModRate() const { return 0.5f; }
	virtual void setModDepth(float value) {}
	[[nodiscard]] virtual float getModDepth() const { return 0.5f; }
	virtual void setModFdbk(float value) {}
	[[nodiscard]] virtual float getModFdbk() const { return 0.5f; }

	// KDFX resonant filter bank (900/902/903)
	virtual void setFiltType(int32_t value) {}
	[[nodiscard]] virtual int32_t getFiltType() const { return 0; }
	virtual void setFiltRes(float value) {}
	[[nodiscard]] virtual float getFiltRes() const { return 0.0f; }
	virtual void setShaperMode(int32_t value) {}
	[[nodiscard]] virtual int32_t getShaperMode() const { return 0; }

	virtual void setBassGain(float value) {}
	[[nodiscard]] virtual float getBassGain() const { return 0.5f; }

	virtual void setBassFreq(float value) {}
	[[nodiscard]] virtual float getBassFreq() const { return 0.5f; }

	virtual void setMidGain(float value) {}
	[[nodiscard]] virtual float getMidGain() const { return 0.5f; }

	virtual void setMidFreq(float value) {}
	[[nodiscard]] virtual float getMidFreq() const { return 0.5f; }

	virtual void setTrebleGain(float value) {}
	[[nodiscard]] virtual float getTrebleGain() const { return 0.5f; }

	virtual void setTrebleFreq(float value) {}
	[[nodiscard]] virtual float getTrebleFreq() const { return 0.5f; }

	virtual void setOutLevel(float value) {}
	[[nodiscard]] virtual float getOutLevel() const { return 0.5f; }

	[[nodiscard]] constexpr int32_t getPanLeft() const { return amplitude_left_; }
	[[nodiscard]] constexpr int32_t getPanRight() const { return amplitude_right_; }

private:
	int32_t amplitude_right_ = 0;
	int32_t amplitude_left_ = 0;
};
} // namespace deluge::dsp::reverb
