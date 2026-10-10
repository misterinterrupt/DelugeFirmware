/*
 * KDFX pair combinations (manual algorithms 706-724 range: Flange+Delay,
 * Chorus<>Reverb, Flange<>Reverb, Flange<>LasrDly, Shaper<>Reverb, …).
 *
 * The manual's "<>" combinations are two processors in one preset with an
 * A->B cfg parameter swapping the order; "+" variants are the same with a
 * FIXED order (no cfg column — rule 3: no params the engine doesn't have).
 *
 * One generic insert engine hosts any pair of the existing engines. Param
 * routing follows each sub-engine's natural ids where they don't collide; the
 * modulation trio (MRATE/MDEPTH/MFDBK) always drives the MODULATED half
 * (chorus/flanger/phaser), the primary half keeps TIME/SIZE-class ids. CFG
 * (when present) swaps which half processes first.
 */

#pragma once

#include "definitions_cxx.hpp"
#include "dsp/reverb/base.hpp"
#include "dsp/reverb/kdfx/chorus.hpp"
#include "dsp/reverb/kdfx/delay.hpp"
#include "dsp/reverb/kdfx/flanger.hpp"
#include "dsp/reverb/kdfx/kdfx_util.hpp"
#include "dsp/reverb/kdfx/laser.hpp"
#include "dsp/reverb/kdfx/quantize.hpp"
#include "dsp/reverb/kdfx/reverb.hpp"
#include "dsp/reverb/kdfx/tone.hpp"
#include "dsp/reverb/kdfx/tubeamp.hpp"
#include "dsp/fx_bloc/stock_adapters.hpp"
#include <algorithm>
#include <cstdint>
#include <memory>
#include <span>
#include <variant>

namespace deluge::dsp::reverb {

/// One slot of a pair: owns whichever engine the combination needs.
struct PairEngine {
	std::variant<std::monostate, KdfxChorus, KdfxFlanger, KdfxDelay, KdfxReverb, KdfxLaserVerb, KdfxTone,
	             KdfxQuantize, KdfxTubeAmp, CompressorAdapter>
	    engine;

	enum class Kind : uint8_t { NONE, CHORUS, FLANGER, DELAY, REVERB, LASER, TONE, QUANT, TUBEAMP, COMPRESSOR };

	void emplace(Kind k) {
		switch (k) {
		case Kind::CHORUS: engine.emplace<KdfxChorus>(); break;
		case Kind::FLANGER: engine.emplace<KdfxFlanger>(); break;
		case Kind::DELAY: engine.emplace<KdfxDelay>(); break;
		case Kind::REVERB: engine.emplace<KdfxReverb>(); break;
		case Kind::LASER: engine.emplace<KdfxLaserVerb>(); break;
		case Kind::TONE: engine.emplace<KdfxTone>(); break;
		case Kind::QUANT: engine.emplace<KdfxQuantize>(); break;
		case Kind::TUBEAMP: engine.emplace<KdfxTubeAmp>(); break;
		case Kind::COMPRESSOR: engine.emplace<CompressorAdapter>(); break;
		default: engine.emplace<std::monostate>(); break;
		}
		if (Base* b = base()) {
			b->setPanLevels(kdfx::unityWetPan(), kdfx::unityWetPan());
		}
	}

	[[nodiscard]] Base* base() {
		return std::visit(
		    [](auto& e) -> Base* {
			    using T = std::decay_t<decltype(e)>;
			    if constexpr (std::is_same_v<T, std::monostate>) {
				    return nullptr;
			    }
			    else {
				    return &e;
			    }
		    },
		    engine);
	}
	[[nodiscard]] const Base* base() const {
		return const_cast<PairEngine*>(this)->base();
	}

	/// Insert halves process in place; send halves (delay/reverb/laser) wet-add
	/// from a mono downmix of the buffer.
	void runInPlace(std::span<StereoSample> buffer) {
		Base* b = base();
		if (b == nullptr) {
			return;
		}
		if (b->isInsert()) {
			b->processInsert(buffer);
			return;
		}
		for (size_t done = 0; done < buffer.size(); done += kChunk) {
			size_t n = std::min<size_t>(kChunk, buffer.size() - done);
			StereoSample* buf = buffer.data() + done;
			for (size_t i = 0; i < n; i++) {
				mono_[i] = (static_cast<int64_t>(buf[i].l) + buf[i].r) >> 1;
			}
			b->process(std::span<int32_t>{mono_, n}, std::span<StereoSample>{buf, n});
		}
	}

private:
	static constexpr size_t kChunk = 64;
	int32_t mono_[kChunk]{};
};

template <PairEngine::Kind A, PairEngine::Kind B, bool HasCfg>
class KdfxPair final : public Base {
public:
	KdfxPair() {
		a_.emplace(A);
		b_.emplace(B);
	}
	~KdfxPair() override = default;

	[[nodiscard]] bool isInsert() const override { return true; }

	// Never called for insert engines (FxBloc::process branches on isInsert()).
	void process(std::span<int32_t> input, std::span<StereoSample> output) override {}

	void processInsert(std::span<StereoSample> buffer) override {
		if (cfg_ == 0) {
			a_.runInPlace(buffer);
			b_.runInPlace(buffer);
		}
		else {
			b_.runInPlace(buffer);
			a_.runInPlace(buffer);
		}
	}

	// ---- Primary-half params (A) ----
	void setRoomSize(float v) override { setParam(A, v, &Base::setRoomSize, &Base::getRoomSize); }
	[[nodiscard]] float getRoomSize() const override { return getParam(A, &Base::getRoomSize); }
	void setDecay(float v) override { setParam(A, v, &Base::setDecay, &Base::getDecay); }
	[[nodiscard]] float getDecay() const override { return getParam(A, &Base::getDecay); }
	void setDamping(float v) override { setParam(A, v, &Base::setDamping, &Base::getDamping); }
	[[nodiscard]] float getDamping() const override { return getParam(A, &Base::getDamping); }
	void setDiffusion(float v) override { setParam(A, v, &Base::setDiffusion, &Base::getDiffusion); }
	[[nodiscard]] float getDiffusion() const override { return getParam(A, &Base::getDiffusion); }
	void setWidth(float v) override { setParam(A, v, &Base::setWidth, &Base::getWidth); }
	[[nodiscard]] float getWidth() const override { return getParam(A, &Base::getWidth); }
	void setHPF(float v) override { setParam(A, v, &Base::setHPF, &Base::getHPF); }
	[[nodiscard]] float getHPF() const override { return getParam(A, &Base::getHPF); }
	void setBaseDelay(float v) override { setParam(A, v, &Base::setBaseDelay, &Base::getBaseDelay); }
	[[nodiscard]] float getBaseDelay() const override { return getParam(A, &Base::getBaseDelay); }

	// ---- Modulated-half params (B): the Mod* trio ----
	void setModRate(float v) override { setParam(B, v, &Base::setRoomSize, &Base::getRoomSize); }
	[[nodiscard]] float getModRate() const override { return getParam(B, &Base::getRoomSize); }
	void setModDepth(float v) override { setParam(B, v, &Base::setDecay, &Base::getDecay); }
	[[nodiscard]] float getModDepth() const override { return getParam(B, &Base::getDecay); }
	void setModFdbk(float v) override { setParam(B, v, &Base::setDamping, &Base::getDamping); }
	[[nodiscard]] float getModFdbk() const override { return getParam(B, &Base::getDamping); }

	void setComboCfg(int32_t v) override { cfg_ = (v != 0) ? 1 : 0; }
	[[nodiscard]] int32_t getComboCfg() const override { return cfg_; }

private:
	template <typename Setter, typename Getter>
	void setParam(PairEngine::Kind k, float v, Setter s, Getter) {
		Base* target = (k == B) ? b_.base() : a_.base();
		if (target != nullptr) {
			(target->*s)(v);
		}
	}
	template <typename Getter>
	float getParam(PairEngine::Kind k, Getter g) const {
		const Base* target = (k == B) ? b_.base() : a_.base();
		return target != nullptr ? (target->*g)() : 0.0f;
	}

	PairEngine a_{};
	PairEngine b_{};
	int32_t cfg_ = 0;
};

} // namespace deluge::dsp::reverb
