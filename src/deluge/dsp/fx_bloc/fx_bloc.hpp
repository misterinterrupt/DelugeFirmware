/*
 * FxBloc — one effect-slot wrapper for the modular master/track FX chains.
 *
 * Design (the "PAU" model):
 *  - EMPTY is a transparent passthrough costing zero CPU and zero memory.
 *  - Engines are allocated lazily on model selection, RIGHT-SIZED to the concrete
 *    engine (a KdfxReverb tank costs ~200KB, not the 715KB variant union), from the
 *    general allocator (external SDRAM; the sample cache yields under pressure).
 *  - Selection happens on the UI thread; the engine POINTER is only swapped at a
 *    render boundary (applyPending, audio side); the previous engine is retired and
 *    may be freed by the UI once at least one further render boundary has passed.
 *    Nothing the audio routine is inside of is ever freed or mutated mid-render.
 *
 * The same lifecycle serves master slots and (milestone two) track slots; per-track
 * PAU caps are policy layered on top by the caller, not mechanics in here.
 *
 * COMPRESSOR / FILTER adapters land with integration; until then selecting them
 * behaves as EMPTY.
 */

#pragma once

#include "dsp/reverb/base.hpp"
#include "dsp/reverb/freeverb/freeverb.hpp"
#include "dsp/reverb/kdfx/autopanner.hpp"
#include "dsp/reverb/kdfx/chorus.hpp"
#include "dsp/reverb/kdfx/delay.hpp"
#include "dsp/reverb/kdfx/flanger.hpp"
#include "dsp/reverb/kdfx/phaser.hpp"
#include "dsp/reverb/kdfx/reverb.hpp"
#include "dsp/reverb/kdfx/tone.hpp"
#include "dsp/reverb/kdfx/tremolo.hpp"
#include "dsp/reverb/mutable/reverb.hpp"
#include "dsp/stereo_sample.h"
#include "memory/general_memory_allocator.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <new>
#include <span>

namespace deluge::dsp {

class FxBloc {
public:
	enum class Model : uint8_t {
		EMPTY = 0,
		COMPRESSOR,
		FILTER,
		FREEVERB,
		MUTABLE,
		KDFX_REVERB,
		KDFX_DELAY,
		KDFX_CHORUS,
		KDFX_TONE,
		KDFX_FLANGER,
		KDFX_PHASER,
		KDFX_TREMOLO,
		KDFX_AUTOPAN,
	};

	static constexpr size_t kMaxChunk = 64;

	static constexpr uint8_t kDirtyRoomSize = 1 << 0;
	static constexpr uint8_t kDirtyDamping = 1 << 1;
	static constexpr uint8_t kDirtyWidth = 1 << 2;
	static constexpr uint8_t kDirtyDecay = 1 << 3;
	static constexpr uint8_t kDirtyDiffusion = 1 << 4;
	static constexpr uint8_t kDirtyHPF = 1 << 5;

	~FxBloc() {
		// Only runs at global teardown with the audio task long stopped.
		destroyEngine(engine_);
		destroyEngine(pendingEngine_);
		destroyEngine(retiredEngine_);
	}

	Model getModel() const { return model_; }
	bool isEmpty() const { return model_ == Model::EMPTY; }

	/// The engine interface is reverb-shaped, and every engine aliases its real
	/// parameters onto those setters (delay: roomSize=time-L, decay=feedback,
	/// damping=tone; chorus: roomSize=rate, decay=depth, damping=feedback;
	/// tone: roomSize=drive, diffusion=level). The menu presents three headline
	/// parameters per model through this mapping.
	enum class Param : uint8_t {
		None = 0,
		RoomSize,
		Damping,
		Width,
		Decay,
		Diffusion,
		HPF,
	};

	struct ParamInfo {
		Param param;
		const char* label; // nullptr → "--"
	};

	/// Canonical param order for XML persistence of a slot's cache.
	static constexpr std::array<Param, 6> kPersistParams = {
	    Param::RoomSize, Param::Damping, Param::Width, Param::Decay, Param::Diffusion, Param::HPF};

	static constexpr std::array<ParamInfo, 3> headlineParams(Model m) {
		switch (m) {
		case Model::FREEVERB:
		case Model::MUTABLE:
			return {{{Param::RoomSize, "SIZE"}, {Param::Damping, "DAMP"}, {Param::Width, "WIDTH"}}};
		case Model::KDFX_REVERB:
			return {{{Param::RoomSize, "SIZE"}, {Param::Decay, "DECAY"}, {Param::Damping, "DAMP"}}};
		case Model::KDFX_DELAY:
			return {{{Param::RoomSize, "TIME"}, {Param::Decay, "FDBK"}, {Param::Damping, "TONE"}}};
		case Model::KDFX_CHORUS:
			return {{{Param::RoomSize, "RATE"}, {Param::Decay, "DEPTH"}, {Param::Damping, "FDBK"}}};
		case Model::KDFX_TONE:
			return {{{Param::RoomSize, "DRIVE"}, {Param::Diffusion, "LEVEL"}, {Param::None, nullptr}}};
		case Model::KDFX_FLANGER:
			return {{{Param::RoomSize, "RATE"}, {Param::Decay, "DEPTH"}, {Param::Damping, "FDBK"}}};
		case Model::KDFX_PHASER:
			return {{{Param::RoomSize, "RATE"}, {Param::Decay, "DEPTH"}, {Param::Damping, "FDBK"}}};
		case Model::KDFX_TREMOLO:
			return {{{Param::RoomSize, "RATE"}, {Param::Decay, "DEPTH"}, {Param::None, nullptr}}};
		case Model::KDFX_AUTOPAN:
			return {{{Param::RoomSize, "RATE"}, {Param::Decay, "WIDTH"}, {Param::None, nullptr}}};
		case Model::EMPTY:
		case Model::COMPRESSOR:
		case Model::FILTER:
		default:
			return {{{Param::None, nullptr}, {Param::None, nullptr}, {Param::None, nullptr}}};
		}
	}

	/// UI thread. Allocates and fully constructs the new engine; the swap itself is
	/// deferred to the next render boundary. Returns false (state unchanged) if the
	/// allocation failed — callers surface the out-of-memory popup.
	bool setModel(Model m) {
		if (m == model_ && !pendingSwap_) {
			return true;
		}
		reclaimRetired();

		reverb::Base* replacement = nullptr;
		if (m != Model::EMPTY && m != Model::COMPRESSOR && m != Model::FILTER) {
			replacement = constructEngine(m);
			if (replacement == nullptr) {
				return false; // pool exhausted; keep the current bloc as-is
			}
		}

		// A previous pending swap never reached the audio side — nothing references it.
		destroyEngine(pendingEngine_);
		pendingEngine_ = replacement;
		pendingModel_ = m;
		pendingSwap_ = true;

		// Seed the param cache from the fresh engine's own defaults so the menu shows
		// values matching what will actually render, and nothing needs pushing until
		// the user touches a param. pendingEngine_ is still UI-private here, so calling
		// its getters is safe.
		if (replacement != nullptr) {
			// mixWet() scales by panLevel << 5; Base's pan defaults to 0, which mutes
			// every send-style engine — blocs were silently passthrough until this.
			replacement->setPanLevels(reverb::kdfx::unityWetPan(), reverb::kdfx::unityWetPan());
			params_.roomSize = replacement->getRoomSize();
			params_.damping = replacement->getDamping();
			params_.width = replacement->getWidth();
			params_.decay = replacement->getDecay();
			params_.diffusion = replacement->getDiffusion();
			params_.hpf = replacement->getHPF();
		}
		paramsDirty_ = 0;
		return true;
	}

	/// Audio thread only, at the top of each render block.
	void applyPending() {
		renderGeneration_++;
		if (pendingSwap_) {
			pendingSwap_ = false;
			// Retire whatever the renderer may currently be inside of; it becomes
			// freeable once a FURTHER boundary has passed (see reclaimRetired).
			destroyEngine(retiredEngine_);
			retiredEngine_ = engine_;
			retiredAtGeneration_ = renderGeneration_;
			engine_ = pendingEngine_;
			pendingEngine_ = nullptr;
			model_ = pendingModel_;
		}
		// Deferred parameter application: menus write the cache from the UI thread at
		// any time; the engine is only touched here, at the render boundary. Only the
		// params actually written get pushed — the rest keep the engine's own state.
		if (engine_ != nullptr && paramsDirty_ != 0) {
			uint8_t dirty = paramsDirty_;
			paramsDirty_ = 0;
			if (dirty & kDirtyRoomSize) {
				engine_->setRoomSize(params_.roomSize);
			}
			if (dirty & kDirtyDamping) {
				engine_->setDamping(params_.damping);
			}
			if (dirty & kDirtyWidth) {
				engine_->setWidth(params_.width);
			}
			if (dirty & kDirtyDecay) {
				engine_->setDecay(params_.decay);
			}
			if (dirty & kDirtyDiffusion) {
				engine_->setDiffusion(params_.diffusion);
			}
			if (dirty & kDirtyHPF) {
				engine_->setHPF(params_.hpf);
			}
		}
	}

	/// UI thread. Cache-and-defer for the bloc's headline params — mirrors the deferred
	/// application the global reverb wrapper uses. Values are 0..1 normalized floats.
	struct Params {
		float roomSize = 0.5f;
		float damping = 0.5f;
		float width = 0.5f;
		float decay = 0.5f;
		float diffusion = 0.5f;
		float hpf = 0.f;
	};

	[[nodiscard]] Params const& params() const { return params_; }

	[[nodiscard]] float getParam(Param p) const {
		switch (p) {
		case Param::RoomSize:
			return params_.roomSize;
		case Param::Damping:
			return params_.damping;
		case Param::Width:
			return params_.width;
		case Param::Decay:
			return params_.decay;
		case Param::Diffusion:
			return params_.diffusion;
		case Param::HPF:
			return params_.hpf;
		default:
			return 0.f;
		}
	}

	void setParam(Param p, float v) {
		uint8_t bit;
		switch (p) {
		case Param::RoomSize:
			params_.roomSize = v;
			bit = kDirtyRoomSize;
			break;
		case Param::Damping:
			params_.damping = v;
			bit = kDirtyDamping;
			break;
		case Param::Width:
			params_.width = v;
			bit = kDirtyWidth;
			break;
		case Param::Decay:
			params_.decay = v;
			bit = kDirtyDecay;
			break;
		case Param::Diffusion:
			params_.diffusion = v;
			bit = kDirtyDiffusion;
			break;
		case Param::HPF:
			params_.hpf = v;
			bit = kDirtyHPF;
			break;
		default:
			return;
		}
		paramsDirty_ |= bit;
	}

	/// In-place stereo processing. Returns false for passthrough blocs (EMPTY, or an
	/// adapter that has not landed yet) — the router then skips them entirely. Insert
	/// engines (tremolo, auto-pan) transform the buffer directly; send engines consume
	/// a mono input and wet-add into the stereo buffer.
	bool process(StereoSample* buffer, size_t numSamples) {
		if (engine_ == nullptr) {
			return false;
		}
		if (engine_->isInsert()) {
			engine_->processInsert(std::span<StereoSample>{buffer, numSamples});
			return true;
		}
		// Reverb-class engines consume a mono input and add wet output into the stereo
		// buffer (send-style wet-add). Bounded chunks; scratch lives in the chain.
		for (size_t done = 0; done < numSamples;) {
			size_t n = std::min(kMaxChunk, numSamples - done);
			StereoSample* buf = buffer + done;
			for (size_t i = 0; i < n; i++) {
				monoScratch_[i] = (static_cast<int64_t>(buf[i].l) + buf[i].r) >> 1;
			}
			engine_->process(std::span<int32_t>{monoScratch_, n}, std::span<StereoSample>{buf, n});
			done += n;
		}
		return true;
	}

	/// Raw engine access for parameter plumbing (integration step). Null when the bloc
	/// is EMPTY or a pending swap hasn't landed yet.
	reverb::Base* engine() { return engine_; }

private:
	static reverb::Base* constructEngine(Model m) {
		size_t size;
		switch (m) {
		case Model::KDFX_REVERB:
			size = sizeof(reverb::KdfxReverb);
			break;
		case Model::KDFX_DELAY:
			size = sizeof(reverb::KdfxDelay);
			break;
		case Model::KDFX_CHORUS:
			size = sizeof(reverb::KdfxChorus);
			break;
		case Model::KDFX_TONE:
			size = sizeof(reverb::KdfxTone);
			break;
		case Model::KDFX_FLANGER:
			size = sizeof(reverb::KdfxFlanger);
			break;
		case Model::KDFX_PHASER:
			size = sizeof(reverb::KdfxPhaser);
			break;
		case Model::KDFX_TREMOLO:
			size = sizeof(reverb::KdfxTremolo);
			break;
		case Model::KDFX_AUTOPAN:
			size = sizeof(reverb::KdfxAutoPanner);
			break;
		case Model::MUTABLE:
			size = sizeof(reverb::Mutable);
			break;
		case Model::FREEVERB:
		default:
			size = sizeof(reverb::Freeverb);
			break;
		}
		void* memory = GeneralMemoryAllocator::get().allocLowSpeed(size);
		if (memory == nullptr) {
			return nullptr;
		}
		switch (m) {
		case Model::KDFX_REVERB:
			return new (memory) reverb::KdfxReverb();
		case Model::KDFX_DELAY:
			return new (memory) reverb::KdfxDelay();
		case Model::KDFX_CHORUS:
			return new (memory) reverb::KdfxChorus();
		case Model::KDFX_TONE:
			return new (memory) reverb::KdfxTone();
		case Model::KDFX_FLANGER:
			return new (memory) reverb::KdfxFlanger();
		case Model::KDFX_PHASER:
			return new (memory) reverb::KdfxPhaser();
		case Model::KDFX_TREMOLO:
			return new (memory) reverb::KdfxTremolo();
		case Model::KDFX_AUTOPAN:
			return new (memory) reverb::KdfxAutoPanner();
		case Model::MUTABLE:
			return new (memory) reverb::Mutable();
		case Model::FREEVERB:
		default:
			return new (memory) reverb::Freeverb();
		}
	}

	static void destroyEngine(reverb::Base*& e) {
		if (e != nullptr) {
			e->~Base(); // virtual: destroys the concrete engine
			GeneralMemoryAllocator::get().dealloc(e);
			e = nullptr;
		}
	}

	/// UI thread. Frees the retired engine only after the audio side has completed at
	/// least one further render boundary since the swap, so no in-flight process() can
	/// still be inside it.
	void reclaimRetired() {
		if (retiredEngine_ != nullptr && renderGeneration_ > retiredAtGeneration_) {
			destroyEngine(retiredEngine_);
		}
	}

	Model model_ = Model::EMPTY;
	bool pendingSwap_ = false;
	Model pendingModel_ = Model::EMPTY;
	Params params_{};
	uint8_t paramsDirty_ = 0; // bitmask of kDirty* — UI writes, audio reads+clears (single byte)

	reverb::Base* engine_ = nullptr;
	reverb::Base* pendingEngine_ = nullptr;
	reverb::Base* retiredEngine_ = nullptr;

	/// Bumped by the audio side every applyPending(); read by the UI side for the
	/// retirement grace check. Torn reads are impossible (aligned uint32) and a stale
	/// read only delays reclamation by one interaction.
	uint32_t renderGeneration_ = 0;
	uint32_t retiredAtGeneration_ = 0;

	int32_t monoScratch_[kMaxChunk]{};
};

} // namespace deluge::dsp
