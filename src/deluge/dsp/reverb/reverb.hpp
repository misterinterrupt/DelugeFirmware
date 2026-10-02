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
		// Deferred on purpose: emplacing the variant destroys the DSP object the audio routine
		// may currently be inside of. Record the request; the audio thread applies it at the next
		// render boundary via applyPending().
		pendingModel_ = m;
		modelChangePending_ = true;
	}

	Model getModel() const { return modelChangePending_ ? pendingModel_ : model_; }

	/// Audio thread only. Apply any pending model change / parameter writes at a render
	/// boundary, where it is safe to reconstruct or retune the underlying DSP object.
	void applyPending() {
		if (modelChangePending_) {
			modelChangePending_ = false;
			constructBase(pendingModel_);
			paramsDirty_ = false; // constructBase pushes every cached param
		}
		else if (paramsDirty_) {
			paramsDirty_ = false;
			pushAllParamsToBase();
		}
	}

	void process(std::span<int32_t> input, std::span<StereoSample> output) override {
		if (base_) {
			base_->process(input, output);
		}
	}

	void setPanLevels(const int32_t amplitude_left, const int32_t amplitude_right) {
		amplitude_left_ = amplitude_left;
		amplitude_right_ = amplitude_right;
		// Only the audio thread calls this (per render block, in renderReverb), so applying
		// immediately is safe and keeps output panning free of one block of latency.
		if (base_) {
			base_->setPanLevels(amplitude_left, amplitude_right);
		}
	}

	void setRoomSize(float value) override {
		room_size_ = value;
		paramsDirty_ = true;
	}

	[[nodiscard]] float getRoomSize() const override {
		return room_size_;
	}

	void setDecay(float value) override {
		decay_ = value;
		paramsDirty_ = true;
	}

	[[nodiscard]] float getDecay() const override {
		return decay_;
	}

	void setDamping(float value) override {
		damping_ = value;
		paramsDirty_ = true;
	}

	[[nodiscard]] float getDamping() const override {
		return damping_;
	}

	void setDiffusion(float value) override {
		diffusion_ = value;
		paramsDirty_ = true;
	}

	[[nodiscard]] float getDiffusion() const override {
		return diffusion_;
	}

	void setWidth(float value) override {
		width_ = value;
		paramsDirty_ = true;
	}

	[[nodiscard]] float getWidth() const override {
		return width_;
	}

	void setHPF(float f) override {
		hpf_ = f;
		paramsDirty_ = true;
	}

	[[nodiscard]] float getHPF() const override {
		return hpf_;
	}

	void setPreDelay(float value) override {
		pre_delay_ = value;
		paramsDirty_ = true;
	}
	[[nodiscard]] float getPreDelay() const override {
		return pre_delay_;
	}

	void setEarlyReflections(float value) override {
		er_level_ = value;
		paramsDirty_ = true;
	}
	[[nodiscard]] float getEarlyReflections() const override {
		return er_level_;
	}

	void setBassDecay(float value) override {
		bass_decay_ = value;
		paramsDirty_ = true;
	}
	[[nodiscard]] float getBassDecay() const override {
		return bass_decay_;
	}

	void setAirShelf(float value) override {
		air_shelf_ = value;
		paramsDirty_ = true;
	}
	[[nodiscard]] float getAirShelf() const override {
		return air_shelf_;
	}

	void setDelayTimeL(float value) override {
		delay_time_l_ = value;
		paramsDirty_ = true;
	}
	[[nodiscard]] float getDelayTimeL() const override {
		return delay_time_l_;
	}

	void setDelayTimeR(float value) override {
		delay_time_r_ = value;
		paramsDirty_ = true;
	}
	[[nodiscard]] float getDelayTimeR() const override {
		return delay_time_r_;
	}

	void setDelayDrive(float value) override {
		delay_drive_ = value;
		paramsDirty_ = true;
	}
	[[nodiscard]] float getDelayDrive() const override {
		return delay_drive_;
	}

	void setPhaseInvert(bool value) override {
		phase_invert_ = value;
		paramsDirty_ = true;
	}
	[[nodiscard]] bool getPhaseInvert() const override {
		return phase_invert_;
	}

	void setBaseDelay(float value) override {
		base_delay_ = value;
		paramsDirty_ = true;
	}
	[[nodiscard]] float getBaseDelay() const override {
		return base_delay_;
	}

	void setChorusVoices(int32_t value) override {
		chorus_voices_ = value;
		paramsDirty_ = true;
	}
	[[nodiscard]] int32_t getChorusVoices() const override {
		return chorus_voices_;
	}

	void setChorusWaveform(int32_t value) override {
		chorus_waveform_ = value;
		paramsDirty_ = true;
	}
	[[nodiscard]] int32_t getChorusWaveform() const override {
		return chorus_waveform_;
	}

	void setChorusFeedback(float value) override {
		chorus_feedback_ = value;
		paramsDirty_ = true;
	}
	[[nodiscard]] float getChorusFeedback() const override {
		return chorus_feedback_;
	}

	void setDrive(float value) override {
		drive_ = value;
		paramsDirty_ = true;
	}
	[[nodiscard]] float getDrive() const override {
		return drive_;
	}

	void setShaperMode(int32_t value) override {
		shaper_mode_ = value;
		paramsDirty_ = true;
	}
	[[nodiscard]] int32_t getShaperMode() const override {
		return shaper_mode_;
	}

	void setBassGain(float value) override {
		bass_gain_ = value;
		paramsDirty_ = true;
	}
	[[nodiscard]] float getBassGain() const override {
		return bass_gain_;
	}

	void setBassFreq(float value) override {
		bass_freq_ = value;
		paramsDirty_ = true;
	}
	[[nodiscard]] float getBassFreq() const override {
		return bass_freq_;
	}

	void setMidGain(float value) override {
		mid_gain_ = value;
		paramsDirty_ = true;
	}
	[[nodiscard]] float getMidGain() const override {
		return mid_gain_;
	}

	void setMidFreq(float value) override {
		mid_freq_ = value;
		paramsDirty_ = true;
	}
	[[nodiscard]] float getMidFreq() const override {
		return mid_freq_;
	}

	void setTrebleGain(float value) override {
		treble_gain_ = value;
		paramsDirty_ = true;
	}
	[[nodiscard]] float getTrebleGain() const override {
		return treble_gain_;
	}

	void setTrebleFreq(float value) override {
		treble_freq_ = value;
		paramsDirty_ = true;
	}
	[[nodiscard]] float getTrebleFreq() const override {
		return treble_freq_;
	}

	void setOutLevel(float value) override {
		out_level_ = value;
		paramsDirty_ = true;
	}
	[[nodiscard]] float getOutLevel() const override {
		return out_level_;
	}

	template <typename T>
	constexpr T& reverb_as() {
		return std::get<T>(reverb_);
	}

private:
	void constructBase(Model m) {
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
		pushAllParamsToBase();
	}

	void pushAllParamsToBase() {
		if (!base_) {
			return;
		}
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

	bool modelChangePending_ = false;
	bool paramsDirty_ = false;
	Model pendingModel_ = Model::FREEVERB;
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
