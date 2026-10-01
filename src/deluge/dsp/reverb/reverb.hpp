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
};

} // namespace deluge::dsp
