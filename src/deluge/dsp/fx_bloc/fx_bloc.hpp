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
	/// tone: roomSize=drive, diffusion=level). Param ids cover the union of the
	/// engines' extended suites; the menu presents each model's table below.
	enum class Param : uint8_t {
		None = 0,
		RoomSize,
		Damping,
		Width,
		Decay,
		Diffusion,
		HPF,
		Level,        // chain wet level (send blocs) / effect blend (insert blocs)
		PreDelay,     // kdfx reverb
		ERLevel,      // kdfx reverb
		BassDecay,    // kdfx reverb
		DelayTimeR,   // kdfx delay
		DelayDrive,   // kdfx delay
		BaseDelay,    // chorus / flanger
		ChorusVoices, // int
		ChorusWaveform,
		ShaperMode, // kdfx tone, int
		BassGain,   // kdfx tone EQ
		BassFreq,
		MidGain,
		MidFreq,
		TrebleGain,
		TrebleFreq,
		kNumParams
	};
	static_assert(util::to_underlying(Param::kNumParams) <= 32, "dirty bitmask is uint32");

	struct ParamDef {
		Param param;                       // None → "--", column inert
		const char* label;                 // ≤5 chars, fits a 32px column
		bool isInt = false;
		int32_t intMin = 0;
		int32_t intMax = 1;
		const char* const* intOptions = nullptr; // display strings for int params

		// Real-unit display for continuous params, matching the engine's own
		// mapping (read from its setters): shown value = bipolar
		// ? ±(t-0.5)*2*realMax : realMin + t*(realMax-realMin), t = the 0..1 cache
		// value. A null unit means the param has no physical calibration — shown
		// as 0-100%.
		const char* unit = nullptr;
		float realMin = 0.f;
		float realMax = 1.f;
		uint8_t decimals = 0;
		bool bipolar = false;
	};

	/// Per-model menu/persistence table. Indices 0-3 render on the Slot page
	/// (3 headline + LEVEL at index 3, always), indices 4-7 on the "+ page".
	static constexpr size_t kMaxParamDefs = 8;

	// Display option strings for the int params, and the LEVEL column definition.
	static constexpr const char* kWaveformOptionStrings[] = {"SINE", "TRI", "S&H"};
	static constexpr const char* kShaperOptionStrings[] = {"SOFT", "TAPE", "HARD", "FOLD"};

	static constexpr ParamDef LEVEL_DEF() { return {Param::Level, "LEVEL"}; }
	static constexpr const char* const* kWaveformOptions() { return kWaveformOptionStrings; }
	static constexpr const char* const* kShaperOptions() { return kShaperOptionStrings; }

	// Real-unit helpers for the table below (defaults leave out the trailing fields).
	static constexpr ParamDef hz(Param p, const char* label, float minHz, float maxHz, uint8_t dec = 2) {
		return {p, label, false, 0, 1, nullptr, "HZ", minHz, maxHz, dec, false};
	}
	static constexpr ParamDef ms(Param p, const char* label, float minMs, float maxMs, uint8_t dec = 1) {
		return {p, label, false, 0, 1, nullptr, "MS", minMs, maxMs, dec, false};
	}
	static constexpr ParamDef db(Param p, const char* label, float minDb, float maxDb, bool bipolar = false) {
		return {p, label, false, 0, 1, nullptr, "DB", minDb, maxDb, 1, bipolar};
	}
	/// Bipolar percent (feedback): t=0.5 ↔ 0%, ends ±maxPercent.
	static constexpr ParamDef bipolarPct(Param p, const char* label, float maxPercent) {
		return {p, label, false, 0, 1, nullptr, "%", 0.f, maxPercent, 0, true};
	}

	static constexpr std::array<ParamDef, kMaxParamDefs> paramDefs(Model m) {
		switch (m) {
		case Model::FREEVERB:
		case Model::MUTABLE:
			return {{{Param::RoomSize, "SIZE"},
			         {Param::Damping, "DAMP"},
			         {Param::Width, "WIDTH"},
			         LEVEL_DEF(),
			         {Param::HPF, "HPF", false, 0, 1, nullptr, "HZ", 20.f, 1000.f, 0, false}}};
		case Model::KDFX_REVERB:
			return {{{Param::RoomSize, "SIZE"},
			         {Param::Decay, "DECAY"},
			         {Param::Damping, "DAMP"},
			         LEVEL_DEF(),
			         {Param::Width, "WIDTH"},
			         ms(Param::PreDelay, "PREDL", 0.f, 300.f, 0),
			         {Param::ERLevel, "ER"},
			         {Param::BassDecay, "BASS", false, 0, 1, nullptr, "X", 0.2f, 2.5f, 2, false}}};
		case Model::KDFX_DELAY:
			// Both delay lines and the feedback live on the Slot page together —
			// they are the primary controls of the effect.
			return {{{ms(Param::RoomSize, "TIM L", 1.f, 1480.f, 0)},
			         {ms(Param::DelayTimeR, "TIM R", 1.f, 1480.f, 0)},
			         {Param::Decay, "FDBK", false, 0, 1, nullptr, "%", 0.f, 99.f, 0, false},
			         LEVEL_DEF(),
			         {Param::Damping, "TONE"},
			         {Param::Diffusion, "XFEED"},
			         {db(Param::DelayDrive, "DRV", 0.f, 18.f)},
			         {Param::HPF, "HPF", false, 0, 1, nullptr, "HZ", 20.f, 1000.f, 0, false}}};
		case Model::KDFX_CHORUS:
			return {{{hz(Param::RoomSize, "RATE", 0.05f, 15.f)},
			         ms(Param::Decay, "DEPTH", 0.f, 10.f),
			         bipolarPct(Param::Damping, "FDBK", 95.f),
			         LEVEL_DEF(),
			         ms(Param::BaseDelay, "BASE", 0.1f, 40.f),
			         {Param::ChorusVoices, "VOICE", true, 2, 6},
			         {Param::ChorusWaveform, "WAVE", true, 0, 2, kWaveformOptions()},
			         {Param::Diffusion, "SPRD", false, 0, 1, nullptr, "DEG", 0.f, 180.f, 0, false}}};
		case Model::KDFX_TONE:
			return {{{db(Param::RoomSize, "DRIVE", 0.f, 24.f)},
			         db(Param::BassGain, "BASS", -15.f, 15.f, true),
			         db(Param::MidGain, "MID", -15.f, 15.f, true),
			         LEVEL_DEF(),
			         {Param::ShaperMode, "SHPR", true, 0, 3, kShaperOptions()},
			         {Param::BassFreq, "BFREQ", false, 0, 1, nullptr, "HZ", 30.f, 400.f, 0, false},
			         db(Param::TrebleGain, "TRBL", -15.f, 15.f, true),
			         {Param::Diffusion, "TLVL"}}};
		case Model::KDFX_FLANGER:
			return {{{hz(Param::RoomSize, "RATE", 0.05f, 8.f)},
			         ms(Param::Decay, "DEPTH", 0.f, 3.f, 2),
			         bipolarPct(Param::Damping, "FDBK", 95.f),
			         LEVEL_DEF(),
			         ms(Param::BaseDelay, "BASE", 0.1f, 7.f),
			         {Param::Diffusion, "SPRD", false, 0, 1, nullptr, "DEG", 0.f, 180.f, 0, false},
			         {Param::HPF, "HPF", false, 0, 1, nullptr, "HZ", 20.f, 1000.f, 0, false}}};
		case Model::KDFX_PHASER:
			return {{{hz(Param::RoomSize, "RATE", 0.05f, 8.f)},
			         {Param::Decay, "DEPTH"},
			         {Param::Damping, "FDBK", false, 0, 1, nullptr, "%", 0.f, 70.f, 0, false},
			         LEVEL_DEF(),
			         {Param::Diffusion, "SPRD", false, 0, 1, nullptr, "DEG", 0.f, 180.f, 0, false},
			         {Param::HPF, "HPF", false, 0, 1, nullptr, "HZ", 20.f, 1000.f, 0, false}}};
		case Model::KDFX_TREMOLO:
			return {{{hz(Param::RoomSize, "RATE", 0.10f, 16.f)}, {Param::Decay, "DEPTH"}, {}, LEVEL_DEF()}};
		case Model::KDFX_AUTOPAN:
			return {{{hz(Param::RoomSize, "RATE", 0.05f, 10.f)}, {Param::Decay, "WIDTH"}, {}, LEVEL_DEF()}};
		case Model::EMPTY:
		case Model::COMPRESSOR:
		case Model::FILTER:
		default:
			return {{}};
		}
	}

	/// Whether a param id holds a plain integer (menu steps and XML round-trip as
	/// ints rather than 0-100%).
	static constexpr bool paramIsInt(Param p) {
		return p == Param::ChorusVoices || p == Param::ChorusWaveform || p == Param::ShaperMode;
	}

	/// Canonical XML name for a param id (single source for write and read).
	static constexpr const char* paramName(Param p) {
		switch (p) {
		case Param::RoomSize: return "roomSize";
		case Param::Damping: return "damping";
		case Param::Width: return "width";
		case Param::Decay: return "decay";
		case Param::Diffusion: return "diffusion";
		case Param::HPF: return "hpf";
		case Param::Level: return "level";
		case Param::PreDelay: return "preDelay";
		case Param::ERLevel: return "erLevel";
		case Param::BassDecay: return "bassDecay";
		case Param::DelayTimeR: return "delayTimeR";
		case Param::DelayDrive: return "delayDrive";
		case Param::BaseDelay: return "baseDelay";
		case Param::ChorusVoices: return "voices";
		case Param::ChorusWaveform: return "waveform";
		case Param::ShaperMode: return "shaperMode";
		case Param::BassGain: return "bassGain";
		case Param::BassFreq: return "bassFreq";
		case Param::MidGain: return "midGain";
		case Param::MidFreq: return "midFreq";
		case Param::TrebleGain: return "trebleGain";
		case Param::TrebleFreq: return "trebleFreq";
		default: return nullptr;
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
		// its getters is safe. LEVEL is the chain's own wet/blend control, not an
		// engine param — it resets to unity on every model change.
		if (replacement != nullptr) {
			// mixWet() scales by panLevel << 5; Base's pan defaults to 0, which mutes
			// every send-style engine — blocs were silently passthrough until this.
			replacement->setPanLevels(reverb::kdfx::unityWetPan(), reverb::kdfx::unityWetPan());
			for (const auto& def : paramDefs(m)) {
				if (def.param == Param::None || def.param == Param::Level) {
					continue;
				}
				cache_[util::to_underlying(def.param)] = readEngineParam(replacement, def.param);
			}
		}
		cache_[util::to_underlying(Param::Level)] = 1.0f;
		dirty_ = 0;
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
		if (engine_ != nullptr && dirty_ != 0) {
			uint32_t dirty = dirty_;
			dirty_ = 0;
			for (uint32_t id = 1; dirty != 0 && id < util::to_underlying(Param::kNumParams); id++, dirty >>= 1) {
				if (!(dirty & 1u)) {
					continue;
				}
				if (static_cast<Param>(id) == Param::Level) {
					// LEVEL on send engines is a wet-output scale, applied through the
					// same pan-level input mixWet() already uses. Insert engines blend
					// in process() and need nothing here.
					if (!engine_->isInsert()) {
						float level = std::clamp(cache_[id], 0.0f, 1.0f);
						int32_t pan = static_cast<int32_t>(level * 67108864.0f); // level × 2^26
						engine_->setPanLevels(pan, pan);
					}
				}
				else {
					applyParamToEngine(engine_, static_cast<Param>(id), cache_[id]);
				}
			}
		}
	}

	/// UI thread. Cache-and-defer for the bloc's params — mirrors the deferred
	/// application the global reverb wrapper uses. Floats are 0..1 normalized;
	/// int params are stored as their plain integer value.
	[[nodiscard]] float getParam(Param p) const {
		if (p == Param::None || p >= Param::kNumParams) {
			return 0.f;
		}
		return cache_[util::to_underlying(p)];
	}

	void setParam(Param p, float v) {
		if (p == Param::None || p >= Param::kNumParams) {
			return;
		}
		cache_[util::to_underlying(p)] = v;
		dirty_ |= (1u << util::to_underlying(p));
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
			float level = std::clamp(cache_[util::to_underlying(Param::Level)], 0.0f, 1.0f);
			if (level >= 0.999f) {
				engine_->processInsert(std::span<StereoSample>{buffer, numSamples});
				return true;
			}
			// Wet/dry blend: dry + level × (processed − dry), chunked against the
			// fixed dry scratch.
			q31_t levelQ31 = static_cast<q31_t>(level * ONE_Q31f);
			for (size_t done = 0; done < numSamples;) {
				size_t n = std::min(kMaxChunk, numSamples - done);
				StereoSample* buf = buffer + done;
				std::memcpy(insertDry_, buf, n * sizeof(StereoSample));
				engine_->processInsert(std::span<StereoSample>{buf, n});
				for (size_t i = 0; i < n; i++) {
					buf[i].l = add_saturation(insertDry_[i].l,
					                          reverb::kdfx::q31_mul(
					                              reverb::kdfx::qcollapse(
					                                  static_cast<int64_t>(buf[i].l) - insertDry_[i].l),
					                              levelQ31));
					buf[i].r = add_saturation(insertDry_[i].r,
					                          reverb::kdfx::q31_mul(
					                              reverb::kdfx::qcollapse(
					                                  static_cast<int64_t>(buf[i].r) - insertDry_[i].r),
					                              levelQ31));
				}
				done += n;
			}
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
	/// Push one cached param into an engine (audio thread, render boundary).
	static void applyParamToEngine(reverb::Base* e, Param p, float v) {
		switch (p) {
		case Param::RoomSize: e->setRoomSize(v); break;
		case Param::Damping: e->setDamping(v); break;
		case Param::Width: e->setWidth(v); break;
		case Param::Decay: e->setDecay(v); break;
		case Param::Diffusion: e->setDiffusion(v); break;
		case Param::HPF: e->setHPF(v); break;
		case Param::PreDelay: e->setPreDelay(v); break;
		case Param::ERLevel: e->setEarlyReflections(v); break;
		case Param::BassDecay: e->setBassDecay(v); break;
		case Param::DelayTimeR: e->setDelayTimeR(v); break;
		case Param::DelayDrive: e->setDelayDrive(v); break;
		case Param::BaseDelay: e->setBaseDelay(v); break;
		case Param::ChorusVoices: e->setChorusVoices(static_cast<int32_t>(v)); break;
		case Param::ChorusWaveform: e->setChorusWaveform(static_cast<int32_t>(v)); break;
		case Param::ShaperMode: e->setShaperMode(static_cast<int32_t>(v)); break;
		case Param::BassGain: e->setBassGain(v); break;
		case Param::BassFreq: e->setBassFreq(v); break;
		case Param::MidGain: e->setMidGain(v); break;
		case Param::MidFreq: e->setMidFreq(v); break;
		case Param::TrebleGain: e->setTrebleGain(v); break;
		case Param::TrebleFreq: e->setTrebleFreq(v); break;
		default: break;
		}
	}

	/// Read an engine's current value for cache seeding (UI thread, engine private).
	static float readEngineParam(reverb::Base const* e, Param p) {
		switch (p) {
		case Param::RoomSize: return e->getRoomSize();
		case Param::Damping: return e->getDamping();
		case Param::Width: return e->getWidth();
		case Param::Decay: return e->getDecay();
		case Param::Diffusion: return e->getDiffusion();
		case Param::HPF: return e->getHPF();
		case Param::PreDelay: return e->getPreDelay();
		case Param::ERLevel: return e->getEarlyReflections();
		case Param::BassDecay: return e->getBassDecay();
		case Param::DelayTimeR: return e->getDelayTimeR();
		case Param::DelayDrive: return e->getDelayDrive();
		case Param::BaseDelay: return e->getBaseDelay();
		case Param::ChorusVoices: return static_cast<float>(e->getChorusVoices());
		case Param::ChorusWaveform: return static_cast<float>(e->getChorusWaveform());
		case Param::ShaperMode: return static_cast<float>(e->getShaperMode());
		case Param::BassGain: return e->getBassGain();
		case Param::BassFreq: return e->getBassFreq();
		case Param::MidGain: return e->getMidGain();
		case Param::MidFreq: return e->getMidFreq();
		case Param::TrebleGain: return e->getTrebleGain();
		case Param::TrebleFreq: return e->getTrebleFreq();
		default: return 0.f;
		}
	}

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

	/// Param cache indexed by Param id. UI thread writes + the render boundary reads;
	/// floats for continuous params, plain integers stored as floats for int params.
	float cache_[util::to_underlying(Param::kNumParams)]{};
	uint32_t dirty_ = 0; // bit per param id — UI writes, audio reads+clears

	reverb::Base* engine_ = nullptr;
	reverb::Base* pendingEngine_ = nullptr;
	reverb::Base* retiredEngine_ = nullptr;

	/// Bumped by the audio side every applyPending(); read by the UI side for the
	/// retirement grace check. Torn reads are impossible (aligned uint32) and a stale
	/// read only delays reclamation by one interaction.
	uint32_t renderGeneration_ = 0;
	uint32_t retiredAtGeneration_ = 0;

	int32_t monoScratch_[kMaxChunk]{};
	StereoSample insertDry_[kMaxChunk]{}; // dry snapshot for the insert wet/dry blend
};

} // namespace deluge::dsp
