#pragma once
#include "base.hpp"
#include "freeverb/freeverb.hpp"
#include "kdfx/chorus.hpp"
#include "kdfx/delay.hpp"
#include "kdfx/reverb.hpp"
#include "kdfx/tone.hpp"
#include "mutable/reverb.hpp"
#include <algorithm>
#include <cstdint>
#include <variant>

namespace deluge::dsp {

class Reverb : public reverb::Base {
public:
	enum class Model {
		FREEVERB = 0, // Freeverb is the original
		MUTABLE,
		KDFX_REVERB, // KDFX-style 4-line FDN tank (Q31)
		KDFX_DELAY,  // KDFX-style 4-tap stereo delay
		KDFX_CHORUS, // KDFX-style multi-tap chorus / flanger
		KDFX_TONE,   // KDFX-style tone shaper / soft limiter
	};

	Reverb() {
		updateBasePointer();
		room_size_ = base_->getRoomSize();
		damping_ = base_->getDamping();
		width_ = base_->getWidth();
		hpf_ = base_->getHPF();
	}
	~Reverb() override = default;

	void setModel(Model m) {
		switch (m) {
		case Model::FREEVERB:
			reverb_.emplace<reverb::Freeverb>();
			break;
		case Model::MUTABLE:
			reverb_.emplace<reverb::Mutable>();
			break;
		case Model::KDFX_REVERB:
			reverb_.emplace<reverb::KdfxReverb>();
			break;
		case Model::KDFX_DELAY:
			reverb_.emplace<reverb::KdfxDelay>();
			break;
		case Model::KDFX_CHORUS:
			reverb_.emplace<reverb::KdfxChorus>();
			break;
		case Model::KDFX_TONE:
			reverb_.emplace<reverb::KdfxTone>();
			break;
		}
		model_ = m;
		updateBasePointer();

		base_->setPanLevels(amplitude_left_, amplitude_right_);
		base_->setRoomSize(room_size_);
		base_->setDecay(decay_);
		base_->setDamping(damping_);
		base_->setDiffusion(diffusion_);
		base_->setWidth(width_);
		base_->setHPF(hpf_);

		base_->setPreDelay(pre_delay_);
		base_->setEarlyReflections(er_level_);
		base_->setBassDecay(bass_decay_);
		base_->setAirShelf(air_shelf_);

		base_->setDelayTimeL(delay_time_l_);
		base_->setDelayTimeR(delay_time_r_);
		base_->setDelayDrive(delay_drive_);
		base_->setPhaseInvert(phase_invert_);

		base_->setBaseDelay(base_delay_);
		base_->setChorusVoices(chorus_voices_);
		base_->setChorusWaveform(chorus_waveform_);
		base_->setChorusFeedback(chorus_feedback_);

		base_->setDrive(drive_);
		base_->setShaperMode(shaper_mode_);
		base_->setBassGain(bass_gain_);
		base_->setBassFreq(bass_freq_);
		base_->setMidGain(mid_gain_);
		base_->setMidFreq(mid_freq_);
		base_->setTrebleGain(treble_gain_);
		base_->setTrebleFreq(treble_freq_);
		base_->setOutLevel(out_level_);
	}

	Model getModel() const { return model_; }

	void process(std::span<int32_t> input, std::span<StereoSample> output) override {
		if (base_) {
			base_->process(input, output);
		}
	}

	void setPanLevels(const int32_t amplitude_left, const int32_t amplitude_right) {
		amplitude_left_ = amplitude_left;
		amplitude_right_ = amplitude_right;
		if (base_) {
			base_->setPanLevels(amplitude_left, amplitude_right);
		}
	}

	void setRoomSize(float value) override {
		room_size_ = value;
		if (base_) {
			base_->setRoomSize(value);
		}
	}

	[[nodiscard]] float getRoomSize() const override {
		return base_ ? base_->getRoomSize() : room_size_;
	}

	void setDecay(float value) override {
		decay_ = value;
		if (base_) {
			base_->setDecay(value);
		}
	}

	[[nodiscard]] float getDecay() const override {
		return base_ ? base_->getDecay() : decay_;
	}

	void setDamping(float value) override {
		damping_ = value;
		if (base_) {
			base_->setDamping(value);
		}
	}

	[[nodiscard]] float getDamping() const override {
		return base_ ? base_->getDamping() : damping_;
	}

	void setDiffusion(float value) override {
		diffusion_ = value;
		if (base_) {
			base_->setDiffusion(value);
		}
	}

	[[nodiscard]] float getDiffusion() const override {
		return base_ ? base_->getDiffusion() : diffusion_;
	}

	void setWidth(float value) override {
		width_ = value;
		if (base_) {
			base_->setWidth(value);
		}
	}

	[[nodiscard]] float getWidth() const override {
		return base_ ? base_->getWidth() : width_;
	}

	void setHPF(float f) override {
		hpf_ = f;
		if (base_) {
			base_->setHPF(f);
		}
	}

	[[nodiscard]] float getHPF() const override {
		return base_ ? base_->getHPF() : hpf_;
	}

	void setPreDelay(float value) override {
		pre_delay_ = value;
		if (base_) {
			base_->setPreDelay(value);
		}
	}
	[[nodiscard]] float getPreDelay() const override {
		return base_ ? base_->getPreDelay() : pre_delay_;
	}

	void setEarlyReflections(float value) override {
		er_level_ = value;
		if (base_) {
			base_->setEarlyReflections(value);
		}
	}
	[[nodiscard]] float getEarlyReflections() const override {
		return base_ ? base_->getEarlyReflections() : er_level_;
	}

	void setBassDecay(float value) override {
		bass_decay_ = value;
		if (base_) {
			base_->setBassDecay(value);
		}
	}
	[[nodiscard]] float getBassDecay() const override {
		return base_ ? base_->getBassDecay() : bass_decay_;
	}

	void setAirShelf(float value) override {
		air_shelf_ = value;
		if (base_) {
			base_->setAirShelf(value);
		}
	}
	[[nodiscard]] float getAirShelf() const override {
		return base_ ? base_->getAirShelf() : air_shelf_;
	}

	void setDelayTimeL(float value) override {
		delay_time_l_ = value;
		if (base_) {
			base_->setDelayTimeL(value);
		}
	}
	[[nodiscard]] float getDelayTimeL() const override {
		return base_ ? base_->getDelayTimeL() : delay_time_l_;
	}

	void setDelayTimeR(float value) override {
		delay_time_r_ = value;
		if (base_) {
			base_->setDelayTimeR(value);
		}
	}
	[[nodiscard]] float getDelayTimeR() const override {
		return base_ ? base_->getDelayTimeR() : delay_time_r_;
	}

	void setDelayDrive(float value) override {
		delay_drive_ = value;
		if (base_) {
			base_->setDelayDrive(value);
		}
	}
	[[nodiscard]] float getDelayDrive() const override {
		return base_ ? base_->getDelayDrive() : delay_drive_;
	}

	void setPhaseInvert(bool value) override {
		phase_invert_ = value;
		if (base_) {
			base_->setPhaseInvert(value);
		}
	}
	[[nodiscard]] bool getPhaseInvert() const override {
		return base_ ? base_->getPhaseInvert() : phase_invert_;
	}

	void setBaseDelay(float value) override {
		base_delay_ = value;
		if (base_) {
			base_->setBaseDelay(value);
		}
	}
	[[nodiscard]] float getBaseDelay() const override {
		return base_ ? base_->getBaseDelay() : base_delay_;
	}

	void setChorusVoices(int32_t value) override {
		chorus_voices_ = value;
		if (base_) {
			base_->setChorusVoices(value);
		}
	}
	[[nodiscard]] int32_t getChorusVoices() const override {
		return base_ ? base_->getChorusVoices() : chorus_voices_;
	}

	void setChorusWaveform(int32_t value) override {
		chorus_waveform_ = value;
		if (base_) {
			base_->setChorusWaveform(value);
		}
	}
	[[nodiscard]] int32_t getChorusWaveform() const override {
		return base_ ? base_->getChorusWaveform() : chorus_waveform_;
	}

	void setChorusFeedback(float value) override {
		chorus_feedback_ = value;
		if (base_) {
			base_->setChorusFeedback(value);
		}
	}
	[[nodiscard]] float getChorusFeedback() const override {
		return base_ ? base_->getChorusFeedback() : chorus_feedback_;
	}

	void setDrive(float value) override {
		drive_ = value;
		if (base_) {
			base_->setDrive(value);
		}
	}
	[[nodiscard]] float getDrive() const override {
		return base_ ? base_->getDrive() : drive_;
	}

	void setShaperMode(int32_t value) override {
		shaper_mode_ = value;
		if (base_) {
			base_->setShaperMode(value);
		}
	}
	[[nodiscard]] int32_t getShaperMode() const override {
		return base_ ? base_->getShaperMode() : shaper_mode_;
	}

	void setBassGain(float value) override {
		bass_gain_ = value;
		if (base_) {
			base_->setBassGain(value);
		}
	}
	[[nodiscard]] float getBassGain() const override {
		return base_ ? base_->getBassGain() : bass_gain_;
	}

	void setBassFreq(float value) override {
		bass_freq_ = value;
		if (base_) {
			base_->setBassFreq(value);
		}
	}
	[[nodiscard]] float getBassFreq() const override {
		return base_ ? base_->getBassFreq() : bass_freq_;
	}

	void setMidGain(float value) override {
		mid_gain_ = value;
		if (base_) {
			base_->setMidGain(value);
		}
	}
	[[nodiscard]] float getMidGain() const override {
		return base_ ? base_->getMidGain() : mid_gain_;
	}

	void setMidFreq(float value) override {
		mid_freq_ = value;
		if (base_) {
			base_->setMidFreq(value);
		}
	}
	[[nodiscard]] float getMidFreq() const override {
		return base_ ? base_->getMidFreq() : mid_freq_;
	}

	void setTrebleGain(float value) override {
		treble_gain_ = value;
		if (base_) {
			base_->setTrebleGain(value);
		}
	}
	[[nodiscard]] float getTrebleGain() const override {
		return base_ ? base_->getTrebleGain() : treble_gain_;
	}

	void setTrebleFreq(float value) override {
		treble_freq_ = value;
		if (base_) {
			base_->setTrebleFreq(value);
		}
	}
	[[nodiscard]] float getTrebleFreq() const override {
		return base_ ? base_->getTrebleFreq() : treble_freq_;
	}

	void setOutLevel(float value) override {
		out_level_ = value;
		if (base_) {
			base_->setOutLevel(value);
		}
	}
	[[nodiscard]] float getOutLevel() const override {
		return base_ ? base_->getOutLevel() : out_level_;
	}

	template <typename T>
	constexpr T& reverb_as() {
		return std::get<T>(reverb_);
	}

private:
	void updateBasePointer() {
		std::visit([this](auto& r) { base_ = static_cast<reverb::Base*>(&r); }, reverb_);
	}

	std::variant<
	    reverb::Freeverb,
	    reverb::Mutable,
	    reverb::KdfxReverb,
	    reverb::KdfxDelay,
	    reverb::KdfxChorus,
	    reverb::KdfxTone>
	    reverb_{};

	Model model_ = Model::FREEVERB;

	reverb::Base* base_ = nullptr;

	int32_t amplitude_left_ = 0;
	int32_t amplitude_right_ = 0;

	float room_size_ = 0.5f;
	float decay_ = 0.5f;
	float damping_ = 0.5f;
	float diffusion_ = 0.5f;
	float width_ = 0.5f;
	float hpf_ = 0.0f;

	float pre_delay_ = 0.0f;
	float er_level_ = 0.4f;
	float bass_decay_ = 0.5f;
	float air_shelf_ = 0.2f;

	float delay_time_l_ = 0.35f;
	float delay_time_r_ = 0.50f;
	float delay_drive_ = 0.0f;
	bool phase_invert_ = false;

	float base_delay_ = 0.2f;
	int32_t chorus_voices_ = 4;
	int32_t chorus_waveform_ = 0;
	float chorus_feedback_ = 0.0f;

	float drive_ = 0.0f;
	int32_t shaper_mode_ = 0;
	float bass_gain_ = 0.5f;
	float bass_freq_ = 0.3f;
	float mid_gain_ = 0.5f;
	float mid_freq_ = 0.5f;
	float treble_gain_ = 0.5f;
	float treble_freq_ = 0.5f;
	float out_level_ = 0.67f;
};

} // namespace deluge::dsp
