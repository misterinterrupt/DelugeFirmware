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
#include "dsp/fx_bloc/stock_adapters.hpp"
#include "dsp/reverb/kdfx/autopanner.hpp"
#include "dsp/reverb/kdfx/chorus.hpp"
#include "dsp/reverb/kdfx/delay.hpp"
#include "dsp/reverb/kdfx/distortion.hpp"
#include "dsp/reverb/kdfx/filterbank.hpp"
#include "dsp/reverb/kdfx/flanger.hpp"
#include "dsp/reverb/kdfx/laser.hpp"
#include "dsp/reverb/kdfx/pair.hpp"
#include "dsp/reverb/kdfx/phaser.hpp"
#include "dsp/reverb/kdfx/ringmod.hpp"
#include "dsp/reverb/kdfx/quantize.hpp"
#include "dsp/reverb/kdfx/reverb.hpp"
#include "dsp/reverb/kdfx/tone.hpp"
#include "dsp/reverb/kdfx/tremolo.hpp"
#include "dsp/reverb/kdfx/tubeamp.hpp"
#include "dsp/reverb/mutable/reverb.hpp"
#include "dsp/stereo_sample.h"
#include "memory/general_memory_allocator.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <new>
#include <span>

namespace deluge::dsp {

void accumulateMasterSend(size_t busIdx, const StereoSample* src, size_t numSamples, float gain, float panPercent);

class FxBloc {
public:
	enum class Model : uint8_t {
		EMPTY = 0,
		COMPRESSOR,
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
		KDFX_DIST,
		KDFX_TUBEAMP,
		KDFX_LASER,
		KDFX_CHO_DLY,
		KDFX_CHO_REV,
		KDFX_FLG_DLY,
		KDFX_FLG_REV,
		KDFX_FLG_LASER,
		KDFX_SHPR_REV,
		KDFX_RESFILT,
		KDFX_ENVFILT,
		KDFX_LFOFILT,
		KDFX_RINGMOD,
		KDFX_QUANT,
		KDFX_QNT_FLG,
		GLOBAL_SEND,
	};

	static constexpr size_t kMaxChunk = 64;

	~FxBloc() {
		// Only runs at global teardown with the audio task long stopped.
		destroyEngine(engine_);
		destroyEngine(pendingEngine_);
		destroyEngine(retiredEngine_);
	}

	Model getModel() const { return pendingSwap_ ? pendingModel_ : model_; }
	bool isEmpty() const { return getModel() == Model::EMPTY; }

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
		InGain,       // input volume: -96dB (-inf) to +3dB (default 0dB)
		SendBus,      // global send target bus index: 0..2
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
		AirShelf,   // kdfx reverb
		DelayPhase, // kdfx delay, int (0 off / 1 invert)
		CabSim,     // kdfx distortion
		LoCut,      // kdfx distortion
		HiCut,      // kdfx distortion
		ComboCfg,   // combination blocs, int (0 = A->B, 1 = B->A)
		ModRate,    // combination blocs: modulation LFO rate
		ModDepth,   // combination blocs: modulation depth
		ModFdbk,    // combination blocs: modulation feedback
		FiltType,   // kdfx filter bank, int (0 LP / 1 HP / 2 BP / 3 notch)
		FiltFreq,   // centre / primary frequency (log 16-8372 HZ)
		FiltRes,    // resonance 0-50 DB
		FiltMinFreq, // env-follow at-rest frequency
		FiltSweep,  // env-follow sweep, bipolar ±100%
		FiltAtk,    // env attack rate 0-300 DB/S
		FiltRel,    // env release rate 0-300 DB/S
		FiltClk,    // LFO filter clock, int (0 FIXED / 1 SYSTEM tempo)
		FiltBpm,    // LFO filter BPM 1-255 (fixed clock)
		FiltBeats,  // LFO cycle length 1-16 beats
		FiltShape,  // LFO shape, int SINE/SAW+/SAW-/PULSE/TRI
		FiltPlsw,   // LFO pulse width 0-100%
		FiltSmooth, // LFO smoothing 0-100%
		RmMode,     // ring mod, int (0 L*R / 1 OSC)
		RmO1Lvl,    // ring mod oscillator 1
		RmO1Freq,
		RmO1Shape,  // int SINE/SAW+/SAW-/PULSE/TRI
		RmO1Plsw,   // pulse width 0-100%
		RmO1Smooth, // 0-100%
		RmO2Lvl,    // Sine 2..5: Lvl + Freq each
		RmO2Freq,
		RmO3Lvl,
		RmO3Freq,
		RmO4Lvl,
		RmO4Freq,
		RmO5Lvl,
		RmO5Freq,
		kNumParams
	};
	static_assert(util::to_underlying(Param::kNumParams) <= 64, "dirty bitmask is uint64");

	struct ParamDef {
		Param param;                       // None → "--", column inert
		const char* label;                 // ≤5 chars, fits a 32px column
		bool isInt;
		int32_t intMin;
		int32_t intMax;
		const char* const* intOptions;     // display strings for int params

		// Real-unit display for continuous params, matching the engine's own
		// mapping (read from its setters): shown value = bipolar
		// ? ±(t-0.5)*2*realMax : realMin + t*(realMax-realMin), t = the 0..1 cache
		// value. A null unit means the param has no physical calibration — shown
		// as 0-100%. Curve selects the conversion for the non-linear shapes
		// (compressor ratio/attack/release/sidechain, reverb RT60).
		const char* unit;
		float realMin;
		float realMax;
		uint8_t decimals;
		bool bipolar;
		uint8_t curve;   // 0 linear, 1 bipolar, 2 quadratic, 3 ratio, 4 exponential, 5 RT60
		float expK;      // exponential curve constant

		// Constructor (rather than aggregate DMIs) so the in-class constexpr
		// tables below can brace-initialize with trailing defaults.
		constexpr ParamDef(Param p = Param::None, const char* l = nullptr, bool isInt_ = false, int32_t iMin = 0,
		                   int32_t iMax = 1, const char* const* opts = nullptr, const char* u = nullptr,
		                   float rMin = 0.f, float rMax = 100.f, uint8_t dec = 0, bool bip = false, uint8_t cur = 0,
		                   float ek = 1.5f)
		    : param(p), label(l), isInt(isInt_), intMin(iMin), intMax(iMax), intOptions(opts), unit(u), realMin(rMin),
		      realMax(rMax), decimals(dec), bipolar(bip), curve(cur), expK(ek) {}
	};

	/// Real-unit readout conversion for a ParamDef (t = 0..1 travel).
	static float displayValue(const ParamDef& d, float t) {
		switch (d.curve) {
		case 2: // quadratic (compressor attack/release)
			return d.realMin + t * t * (d.realMax - d.realMin);
		case 3: // ratio: 1:0.1 .. 1:1 .. 1:128, exponential around 1:1
			return (t >= 0.5f) ? std::exp(9.70406052f * (t - 0.5f))
			                   : 1.0f / (1.0f + 9.0f * (1.0f - 2.0f * t));
		case 4: { // exponential (compressor sidechain Hz)
			float e = std::exp(d.expK * t) - 1.0f;
			float eMax = std::exp(d.expK) - 1.0f;
			return d.realMin + (e / eMax) * (d.realMax - d.realMin);
		}
		case 5: { // RT60 seconds, manual range up to Inf (KdfxReverb: g = 0.40 + t x 0.60)
			float g = std::clamp(0.40f + t * 0.60f, 0.05f, 0.999999f);
			if (g >= 0.9995f) {
				return 999.0f; // INF — unity loop gain sustains
			}
			return 0.594f / -std::log(g);
		}
		default:
			return d.bipolar ? (t - 0.5f) * 2.0f * d.realMax : d.realMin + t * (d.realMax - d.realMin);
		}
	}

	/// Inverse of displayValue (real unit → 0..1 travel), for the fine-grained
	/// menu value space. Result is clamped to [0, 1].
	static float tFromReal(const ParamDef& d, float r) {
		float t;
		switch (d.curve) {
		case 2:
			t = std::sqrt((r - d.realMin) / (d.realMax - d.realMin));
			break;
		case 3:
			t = (r >= 1.0f) ? 0.5f + std::log(r) / 9.70406052f
			                : 0.5f - (1.0f / r - 1.0f) / 18.0f;
			break;
		case 4: {
			float frac = (r - d.realMin) / (d.realMax - d.realMin);
			t = std::log(1.0f + frac * (std::exp(d.expK) - 1.0f)) / d.expK;
			break;
		}
		case 6:
			t = std::log(std::max(r, 1.0f) / d.realMin) / std::log(d.realMax / d.realMin);
			break;
		case 5: {
			if (r >= 900.0f) {
				return 1.0f;
			}
			float g = std::exp(-0.594f / std::max(r, 0.05f));
			t = (g - 0.40f) / 0.60f;
			break;
		}
		default:
			t = d.bipolar ? 0.5f + r / (2.0f * d.realMax) : (r - d.realMin) / (d.realMax - d.realMin);
			break;
		}
		return std::clamp(t, 0.0f, 1.0f);
	}

	/// Per-model menu/persistence table, variable length. Index 3 is always LEVEL;
	/// the menu lays params out 4 per page, as many pages as the table needs.
	static constexpr size_t kMaxSlotParams = 16; // menu item pool per slot

	// Display option strings for the int params, and the LEVEL / INGAIN column definitions.
	static constexpr const char* kWaveformOptionStrings[] = {"SINE", "TRI", "S&H"};
	static constexpr const char* kShaperOptionStrings[] = {"SOFT", "TAPE", "HARD", "FOLD"};
	static constexpr const char* kPhaseOptionStrings[] = {"OFF", "ON"};
	static constexpr const char* kCfgOptionStrings[] = {"AMP>MOD", "MOD>AMP"};
	static constexpr const char* kFiltTypeStrings[] = {"LP", "HP", "BP", "NOTCH"};
	static constexpr const char* kRmModeStrings[] = {"L*R", "OSC"};
	static constexpr const char* kRmShapeStrings[] = {"SINE", "SAW+", "SAW-", "PULSE", "TRI"};
	static constexpr const char* kFiltClkStrings[] = {"FIX", "SYS"};
	static constexpr const char* kBusOptionStrings[] = {"BUS 1", "BUS 2", "BUS 3"};

	static constexpr ParamDef LEVEL_DEF() { return {Param::Level, "LEVEL", false, 0, 1, nullptr, "%", 0.f, 100.f, 0, false}; }
	static constexpr ParamDef INGAIN_DEF() { return {Param::InGain, "IN-GN", false, 0, 1, nullptr, "DB", -96.f, 3.f, 1, false}; }
	static constexpr const char* const* kWaveformOptions() { return kWaveformOptionStrings; }
	static constexpr const char* const* kShaperOptions() { return kShaperOptionStrings; }
	static constexpr const char* const* kPhaseOptions() { return kPhaseOptionStrings; }
	static constexpr const char* const* kCfgOptions() { return kCfgOptionStrings; }
	static constexpr const char* const* kFiltTypeOptions() { return kFiltTypeStrings; }
	static constexpr const char* const* kRmModeOptions() { return kRmModeStrings; }
	static constexpr const char* const* kRmShapeOptions() { return kRmShapeStrings; }
	static constexpr const char* const* kFiltClkOptions() { return kFiltClkStrings; }
	static constexpr const char* const* kFiltShapeOptions() { return kRmShapeStrings; }
	static constexpr const char* const* kBusOptions() { return kBusOptionStrings; }

	// Real-unit helpers for the tables below (defaults leave out the trailing fields).
	static constexpr ParamDef hz(Param p, const char* label, float minHz, float maxHz, uint8_t dec = 2) {
		return {p, label, false, 0, 1, nullptr, "HZ", minHz, maxHz, dec, false};
	}
	static constexpr ParamDef ms(Param p, const char* label, float minMs, float maxMs, uint8_t dec = 1) {
		return {p, label, false, 0, 1, nullptr, "MS", minMs, maxMs, dec, false};
	}
	static constexpr ParamDef db(Param p, const char* label, float minDb, float maxDb, bool bipolar = false) {
		return {p, label, false, 0, 1, nullptr, "DB", minDb, maxDb, 1, bipolar};
	}
	/// Plain percent, 0-100 in display units (a null-unit def would read as a 0-1
	/// range in the fine value space — always go through this or an explicit range).
	static constexpr ParamDef logHz(Param p, const char* label, float minHz, float maxHz, uint8_t dec = 0) {
		return {p, label, false, 0, 1, nullptr, "HZ", minHz, maxHz, dec, false, 6, 1.5f};
	}
	static constexpr ParamDef pct(Param p, const char* label) {
		return {p, label, false, 0, 1, nullptr, "%", 0.f, 100.f, 0, false};
	}
	/// Bipolar percent (feedback): t=0.5 ↔ 0%, ends ±maxPercent.
	static constexpr ParamDef bipolarPct(Param p, const char* label, float maxPercent) {
		return {p, label, false, 0, 1, nullptr, "%", 0.f, maxPercent, 0, true};
	}
	/// RT60 seconds, approximate (0.594 = nominal line-set time × 6.91 nepers over
	/// the engine's per-pass decay 0.40..1.0 — baked into the curve conversion;
	/// 999+ renders as INF, the manual's infinite-sustain ceiling).
	/// realMin/realMax are DISPLAY seconds, like every other def.
	static constexpr ParamDef rt60(Param p, const char* label, float minS, float maxS) {
		return {p, label, false, 0, 1, nullptr, "S", minS, maxS, 1, false, 5, 1.5f};
	}

	/// Per-model menu/persistence table, variable length. Index 3 is always LEVEL;
	/// the menu lays params out 4 per page, as many pages as the table needs.
	/// Defined at file scope below (in-class static arrays can't invoke the
	/// ParamDef constructor's default arguments inside the class body).
	static constexpr std::span<const ParamDef> paramDefs(Model m);

	/// Whether a param id holds a plain integer (menu steps and XML round-trip as
	/// ints rather than 0-100%).
	static constexpr bool paramIsInt(Param p) {
		return p == Param::ChorusVoices || p == Param::ChorusWaveform || p == Param::ShaperMode
		       || p == Param::DelayPhase || p == Param::ComboCfg || p == Param::SendBus;
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
		case Param::InGain: return "inGain";
		case Param::SendBus: return "sendBus";
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
		case Param::AirShelf: return "airShelf";
		case Param::DelayPhase: return "phase";
		case Param::CabSim: return "cab";
		case Param::LoCut: return "locut";
		case Param::HiCut: return "hicut";
		case Param::ComboCfg: return "cfg";
		case Param::ModRate: return "modRate";
		case Param::ModDepth: return "modDepth";
		case Param::ModFdbk: return "modFdbk";
		case Param::FiltType: return "filtType";
		case Param::FiltFreq: return "filtFreq";
		case Param::FiltRes: return "filtRes";
		case Param::FiltMinFreq: return "filtMinFreq";
		case Param::FiltSweep: return "filtSweep";
		case Param::FiltAtk: return "filtAtk";
		case Param::FiltRel: return "filtRel";
		case Param::RmMode: return "rmMode";
		case Param::RmO1Lvl: return "rmOsc1Lvl";
		case Param::RmO1Freq: return "rmOsc1Freq";
		case Param::RmO1Shape: return "rmOsc1Shape";
		case Param::RmO1Plsw: return "rmOsc1Plsw";
		case Param::RmO1Smooth: return "rmOsc1Smooth";
		case Param::RmO2Lvl: return "rmOsc2Lvl";
		case Param::RmO2Freq: return "rmOsc2Freq";
		case Param::RmO3Lvl: return "rmOsc3Lvl";
		case Param::RmO3Freq: return "rmOsc3Freq";
		case Param::RmO4Lvl: return "rmOsc4Lvl";
		case Param::RmO4Freq: return "rmOsc4Freq";
		case Param::RmO5Lvl: return "rmOsc5Lvl";
		case Param::RmO5Freq: return "rmOsc5Freq";
		case Param::FiltClk: return "filtClk";
		case Param::FiltBpm: return "filtBpm";
		case Param::FiltBeats: return "filtBeats";
		case Param::FiltShape: return "filtShape";
		case Param::FiltPlsw: return "filtPlsw";
		case Param::FiltSmooth: return "filtSmooth";
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
		if (m != Model::EMPTY && m != Model::GLOBAL_SEND) {
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
				if (def.param == Param::None || def.param == Param::Level || def.param == Param::InGain) {
					continue;
				}
				cache_[util::to_underlying(def.param)] = readEngineParam(replacement, def.param);
			}
		}
		cache_[util::to_underlying(Param::Level)] = 1.0f;
		cache_[util::to_underlying(Param::InGain)] = tFromReal(INGAIN_DEF(), 0.0f);
		if (m == Model::GLOBAL_SEND) {
			cache_[util::to_underlying(Param::SendBus)] = 0.0f;
			cache_[util::to_underlying(Param::Width)] = 0.5f; // Center pan
		}
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
			uint64_t dirty = dirty_;
			dirty_ = 0;
			// Bit position == param id (setParam sets 1u << id) — test each bit
			// explicitly. (A shift-first loop here tested bit id-1 at iteration id,
			// so every edit flushed the NEXT param's cached value: edits seemed dead
			// until a neighbour param's push dragged them in all at once.)
			for (uint32_t id = 1; id < util::to_underlying(Param::kNumParams); id++) {
				if (!(dirty & (1ull << id))) {
					continue;
				}
				if (static_cast<Param>(id) == Param::Level) {
					// LEVEL on send engines is a wet-output scale, applied through the
					// same pan-level input mixWet() already uses (unity = 2^27; the
					// <<5 in mixWet makes wet x panLevel / 2^27). Insert engines blend
					// in process() and need nothing here.
					if (!engine_->isInsert()) {
						float level = std::clamp(cache_[id], 0.0f, 1.0f);
						int32_t pan = static_cast<int32_t>(level * 134217728.0f); // level × 2^27
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
		dirty_ |= (1ull << util::to_underlying(p));
	}

	/// In-place stereo processing. Returns false for passthrough blocs (EMPTY, or an
	/// adapter that has not landed yet) — the router then skips them entirely. Insert
	/// engines (tremolo, auto-pan) transform the buffer directly; send engines consume
	/// a mono input and wet-add into the stereo buffer.
	bool process(StereoSample* buffer, size_t numSamples) {
		if (model_ == Model::GLOBAL_SEND) {
			float inGainDb = displayValue(INGAIN_DEF(), cache_[util::to_underlying(Param::InGain)]);
			float inGainLin = (inGainDb <= -95.0f) ? 0.0f : std::pow(10.0f, inGainDb / 20.0f);
			float level = std::clamp(cache_[util::to_underlying(Param::Level)], 0.0f, 1.0f);
			float totalGain = inGainLin * level;
			if (totalGain > 0.0001f) {
				int32_t busIdx =
				    std::clamp<int32_t>(static_cast<int32_t>(cache_[util::to_underlying(Param::SendBus)]), 0, 2);
				float pan = std::clamp<float>(displayValue(bipolarPct(Param::Width, "PAN", 100.f),
				                                           cache_[util::to_underlying(Param::Width)]),
				                              -100.f, 100.f);
				accumulateMasterSend(busIdx, buffer, numSamples, totalGain, pan);
			}
			return true;
		}
		if (engine_ == nullptr) {
			return false;
		}

		// Input volume attenuation (down to silence) or boost up to +3dB (default 0dB)
		float inGainDb = displayValue(INGAIN_DEF(), cache_[util::to_underlying(Param::InGain)]);
		float inGainLin = (inGainDb <= -95.0f) ? 0.0f : std::pow(10.0f, inGainDb / 20.0f);
		if (inGainLin == 0.0f) {
			for (size_t i = 0; i < numSamples; i++) {
				buffer[i].l = 0;
				buffer[i].r = 0;
			}
		}
		else if (std::fabs(inGainLin - 1.0f) > 0.001f) {
			for (size_t i = 0; i < numSamples; i++) {
				buffer[i].l = static_cast<int32_t>(std::clamp<int64_t>(
				    static_cast<int64_t>(buffer[i].l * inGainLin), -2147483648LL, 2147483647LL));
				buffer[i].r = static_cast<int32_t>(std::clamp<int64_t>(
				    static_cast<int64_t>(buffer[i].r * inGainLin), -2147483648LL, 2147483647LL));
			}
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
		case Param::AirShelf: e->setAirShelf(v); break;
		case Param::DelayPhase: e->setPhaseInvert(v >= 0.5f); break;
		case Param::CabSim: e->setCab(v); break;
		case Param::LoCut: e->setLoCut(v); break;
		case Param::HiCut: e->setHiCut(v); break;
		case Param::ComboCfg: e->setComboCfg(static_cast<int32_t>(v)); break;
		case Param::ModRate: e->setModRate(v); break;
		case Param::ModDepth: e->setModDepth(v); break;
		case Param::ModFdbk: e->setModFdbk(v); break;
		case Param::FiltType: e->setFiltType(static_cast<int32_t>(v)); break;
		case Param::FiltFreq: e->setWidth(v); break;
		case Param::FiltRes: e->setFiltRes(v); break;
		case Param::FiltMinFreq: e->setHPF(v); break;
		case Param::FiltSweep: e->setDiffusion(v); break;
		case Param::FiltAtk: e->setDamping(v); break;
		case Param::FiltRel: e->setDecay(v); break;
		case Param::RmMode: e->setRmMode(static_cast<int32_t>(v)); break;
		case Param::RmO1Lvl: e->setRmOsc(1, 0, v); break;
		case Param::RmO1Freq: e->setRmOsc(1, 1, v); break;
		case Param::RmO1Shape: e->setRmOsc(1, 2, v); break;
		case Param::RmO1Plsw: e->setRmOsc(1, 3, v); break;
		case Param::RmO1Smooth: e->setRmOsc(1, 4, v); break;
		case Param::RmO2Lvl: e->setRmOsc(2, 0, v); break;
		case Param::RmO2Freq: e->setRmOsc(2, 1, v); break;
		case Param::RmO3Lvl: e->setRmOsc(3, 0, v); break;
		case Param::RmO3Freq: e->setRmOsc(3, 1, v); break;
		case Param::RmO4Lvl: e->setRmOsc(4, 0, v); break;
		case Param::RmO4Freq: e->setRmOsc(4, 1, v); break;
		case Param::RmO5Lvl: e->setRmOsc(5, 0, v); break;
		case Param::RmO5Freq: e->setRmOsc(5, 1, v); break;
		case Param::FiltClk: e->setFiltClk(static_cast<int32_t>(v)); break;
		case Param::FiltBpm: e->setFiltBpm(v); break;
		case Param::FiltBeats: e->setFiltBeats(v); break;
		case Param::FiltShape: e->setFiltShape(static_cast<int32_t>(v)); break;
		case Param::FiltPlsw: e->setFiltPlsw(v); break;
		case Param::FiltSmooth: e->setFiltSmooth(v); break;
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
		case Param::AirShelf: return e->getAirShelf();
		case Param::DelayPhase: return e->getPhaseInvert() ? 1.0f : 0.0f;
		case Param::CabSim: return e->getCab();
		case Param::LoCut: return e->getLoCut();
		case Param::HiCut: return e->getHiCut();
		case Param::ComboCfg: return static_cast<float>(e->getComboCfg());
		case Param::ModRate: return e->getModRate();
		case Param::ModDepth: return e->getModDepth();
		case Param::ModFdbk: return e->getModFdbk();
		case Param::FiltType: return static_cast<float>(e->getFiltType());
		case Param::FiltFreq: return e->getWidth();
		case Param::FiltRes: return e->getFiltRes();
		case Param::FiltMinFreq: return e->getHPF();
		case Param::FiltSweep: return e->getDiffusion();
		case Param::FiltAtk: return e->getDamping();
		case Param::FiltRel: return e->getDecay();
		case Param::RmMode: return static_cast<float>(e->getRmMode());
		case Param::RmO1Lvl: return e->getRmOsc(1, 0);
		case Param::RmO1Freq: return e->getRmOsc(1, 1);
		case Param::RmO1Shape: return e->getRmOsc(1, 2);
		case Param::RmO1Plsw: return e->getRmOsc(1, 3);
		case Param::RmO1Smooth: return e->getRmOsc(1, 4);
		case Param::RmO2Lvl: return e->getRmOsc(2, 0);
		case Param::RmO2Freq: return e->getRmOsc(2, 1);
		case Param::RmO3Lvl: return e->getRmOsc(3, 0);
		case Param::RmO3Freq: return e->getRmOsc(3, 1);
		case Param::RmO4Lvl: return e->getRmOsc(4, 0);
		case Param::RmO4Freq: return e->getRmOsc(4, 1);
		case Param::RmO5Lvl: return e->getRmOsc(5, 0);
		case Param::RmO5Freq: return e->getRmOsc(5, 1);
		case Param::FiltClk: return static_cast<float>(e->getFiltClk());
		case Param::FiltBpm: return e->getFiltBpm();
		case Param::FiltBeats: return e->getFiltBeats();
		case Param::FiltShape: return static_cast<float>(e->getFiltShape());
		case Param::FiltPlsw: return e->getFiltPlsw();
		case Param::FiltSmooth: return e->getFiltSmooth();
		default: return 0.f;
		}
	}

	static reverb::Base* constructEngine(Model m) {
		size_t size;
		switch (m) {
		case Model::COMPRESSOR:
			size = sizeof(reverb::CompressorAdapter);
			break;
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
		case Model::KDFX_DIST:
			size = sizeof(reverb::KdfxDistortion);
			break;
		case Model::KDFX_TUBEAMP:
			size = sizeof(reverb::KdfxTubeAmp);
			break;
		case Model::KDFX_LASER:
			size = sizeof(reverb::KdfxLaserVerb);
			break;
		case Model::KDFX_CHO_DLY:
			size = sizeof(reverb::KdfxPair<reverb::PairEngine::Kind::CHORUS, reverb::PairEngine::Kind::DELAY, true>);
			break;
		case Model::KDFX_CHO_REV:
			size = sizeof(reverb::KdfxPair<reverb::PairEngine::Kind::CHORUS, reverb::PairEngine::Kind::REVERB, true>);
			break;
		case Model::KDFX_FLG_DLY:
			size = sizeof(reverb::KdfxPair<reverb::PairEngine::Kind::FLANGER, reverb::PairEngine::Kind::DELAY, false>);
			break;
		case Model::KDFX_FLG_REV:
			size = sizeof(reverb::KdfxPair<reverb::PairEngine::Kind::FLANGER, reverb::PairEngine::Kind::REVERB, true>);
			break;
		case Model::KDFX_FLG_LASER:
			size = sizeof(reverb::KdfxPair<reverb::PairEngine::Kind::FLANGER, reverb::PairEngine::Kind::LASER, true>);
			break;
		case Model::KDFX_SHPR_REV:
			size = sizeof(reverb::KdfxPair<reverb::PairEngine::Kind::TONE, reverb::PairEngine::Kind::REVERB, true>);
			break;
		case Model::KDFX_RESFILT:
			size = sizeof(reverb::KdfxFilter);
			break;
		case Model::KDFX_ENVFILT:
			size = sizeof(reverb::KdfxFilter);
			break;
		case Model::KDFX_LFOFILT:
			size = sizeof(reverb::KdfxFilter);
			break;
		case Model::KDFX_RINGMOD:
			size = sizeof(reverb::KdfxRingMod);
			break;
		case Model::KDFX_QUANT:
			size = sizeof(reverb::KdfxQuantize);
			break;
		case Model::KDFX_QNT_FLG:
			size = sizeof(reverb::KdfxPair<reverb::PairEngine::Kind::QUANT, reverb::PairEngine::Kind::FLANGER, false>);
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
		case Model::COMPRESSOR:
			return new (memory) reverb::CompressorAdapter();
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
		case Model::KDFX_DIST:
			return new (memory) reverb::KdfxDistortion();
		case Model::KDFX_TUBEAMP:
			return new (memory) reverb::KdfxTubeAmp();
		case Model::KDFX_LASER:
			return new (memory) reverb::KdfxLaserVerb();
		case Model::KDFX_CHO_DLY:
			return new (memory) reverb::KdfxPair<reverb::PairEngine::Kind::CHORUS, reverb::PairEngine::Kind::DELAY, true>();
		case Model::KDFX_CHO_REV:
			return new (memory) reverb::KdfxPair<reverb::PairEngine::Kind::CHORUS, reverb::PairEngine::Kind::REVERB, true>();
		case Model::KDFX_FLG_DLY:
			return new (memory) reverb::KdfxPair<reverb::PairEngine::Kind::FLANGER, reverb::PairEngine::Kind::DELAY, false>();
		case Model::KDFX_FLG_REV:
			return new (memory) reverb::KdfxPair<reverb::PairEngine::Kind::FLANGER, reverb::PairEngine::Kind::REVERB, true>();
		case Model::KDFX_FLG_LASER:
			return new (memory) reverb::KdfxPair<reverb::PairEngine::Kind::FLANGER, reverb::PairEngine::Kind::LASER, true>();
		case Model::KDFX_SHPR_REV:
			return new (memory) reverb::KdfxPair<reverb::PairEngine::Kind::TONE, reverb::PairEngine::Kind::REVERB, true>();
		case Model::KDFX_RESFILT:
			return new (memory) reverb::KdfxFilter(reverb::KdfxFilter::Sweep::FIXED);
		case Model::KDFX_ENVFILT:
			return new (memory) reverb::KdfxFilter(reverb::KdfxFilter::Sweep::ENV_FOLLOW);
		case Model::KDFX_LFOFILT:
			return new (memory) reverb::KdfxFilter(reverb::KdfxFilter::Sweep::LFO);
		case Model::KDFX_RINGMOD:
			return new (memory) reverb::KdfxRingMod();
		case Model::KDFX_QUANT:
			return new (memory) reverb::KdfxQuantize();
		case Model::KDFX_QNT_FLG:
			return new (memory) reverb::KdfxPair<reverb::PairEngine::Kind::QUANT, reverb::PairEngine::Kind::FLANGER, false>();
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
	uint64_t dirty_ = 0; // bit per param id — UI writes, audio reads+clears

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

// ---- Per-model param tables, file scope (see the in-class declaration for why) ----

constexpr FxBloc::ParamDef kDefsFreeverb[5] = {
    FxBloc::pct(FxBloc::Param::RoomSize, "SIZE"),
    FxBloc::pct(FxBloc::Param::Damping, "DAMP"),
    FxBloc::pct(FxBloc::Param::Width, "WIDTH"),
    FxBloc::LEVEL_DEF(),
    FxBloc::INGAIN_DEF(),
};
constexpr FxBloc::ParamDef kDefsMutable[6] = {
    FxBloc::pct(FxBloc::Param::RoomSize, "SIZE"),
    FxBloc::pct(FxBloc::Param::Damping, "DAMP"),
    FxBloc::pct(FxBloc::Param::Width, "WIDTH"),
    FxBloc::LEVEL_DEF(),
    FxBloc::INGAIN_DEF(),
    FxBloc::hz(FxBloc::Param::HPF, "HPF", 20.f, 1000.f, 0),
};
constexpr FxBloc::ParamDef kDefsKdfxReverb[10] = {
    {FxBloc::Param::RoomSize, "SIZE", false, 0, 1, nullptr, "M", 3.f, 48.f, 0, false}, // tank scale 1x-3.7x
    FxBloc::rt60(FxBloc::Param::Decay, "DCAY", 0.6f, 999.f),
    FxBloc::hz(FxBloc::Param::Damping, "DAMP", 351.f, 7019.f, 0), // one-pole cutoff, coeff 0.05-1.0
    FxBloc::LEVEL_DEF(),
    FxBloc::INGAIN_DEF(),
    FxBloc::pct(FxBloc::Param::Width, "WIDTH"),
    FxBloc::ms(FxBloc::Param::PreDelay, "PREDL", 0.f, 300.f, 0),
    FxBloc::pct(FxBloc::Param::ERLevel, "ER"),
    {FxBloc::Param::BassDecay, "BASS", false, 0, 1, nullptr, "X", 0.2f, 2.5f, 2, false},
    FxBloc::pct(FxBloc::Param::AirShelf, "AIR"),
};
constexpr FxBloc::ParamDef kDefsKdfxDelay[11] = {
    // Both delay lines and the feedback lead — they are the primary controls.
    FxBloc::ms(FxBloc::Param::RoomSize, "TIM L", 1.f, 1480.f, 0),
    FxBloc::ms(FxBloc::Param::DelayTimeR, "TIM R", 1.f, 1480.f, 0),
    {FxBloc::Param::Decay, "FDBK", false, 0, 1, nullptr, "%", 0.f, 100.f, 0, false},
    FxBloc::LEVEL_DEF(),
    FxBloc::INGAIN_DEF(),
    FxBloc::hz(FxBloc::Param::Damping, "TONE", 351.f, 7019.f, 0),
    FxBloc::pct(FxBloc::Param::Diffusion, "XFEED"),
    FxBloc::db(FxBloc::Param::DelayDrive, "DRV", 0.f, 18.f),
    FxBloc::hz(FxBloc::Param::HPF, "HPF", 20.f, 1000.f, 0),
    FxBloc::pct(FxBloc::Param::Width, "WIDTH"),
    {FxBloc::Param::DelayPhase, "PHASE", true, 0, 1, FxBloc::kPhaseOptions()},
};
constexpr FxBloc::ParamDef kDefsKdfxChorus[10] = {
    FxBloc::hz(FxBloc::Param::RoomSize, "RATE", 0.05f, 15.f),
    FxBloc::ms(FxBloc::Param::Decay, "DEPTH", 0.f, 10.f),
    FxBloc::bipolarPct(FxBloc::Param::Damping, "FDBK", 95.f),
    FxBloc::LEVEL_DEF(),
    FxBloc::INGAIN_DEF(),
    FxBloc::ms(FxBloc::Param::BaseDelay, "BASE", 0.1f, 40.f),
    {FxBloc::Param::ChorusVoices, "VOICE", true, 2, 6},
    {FxBloc::Param::ChorusWaveform, "WAVE", true, 0, 2, FxBloc::kWaveformOptions()},
    {FxBloc::Param::Diffusion, "SPRD", false, 0, 1, nullptr, "DEG", 0.f, 180.f, 0, false},
    FxBloc::hz(FxBloc::Param::HPF, "HPF", 20.f, 1000.f, 0),
};
constexpr FxBloc::ParamDef kDefsKdfxTone[11] = {
    FxBloc::db(FxBloc::Param::RoomSize, "DRIVE", 0.f, 24.f),
    FxBloc::db(FxBloc::Param::BassGain, "BASS", -15.f, 15.f, true),
    FxBloc::db(FxBloc::Param::MidGain, "MID", -15.f, 15.f, true),
    FxBloc::LEVEL_DEF(),
    FxBloc::INGAIN_DEF(),
    {FxBloc::Param::ShaperMode, "SHPR", true, 0, 3, FxBloc::kShaperOptions()},
    FxBloc::hz(FxBloc::Param::BassFreq, "BFREQ", 30.f, 400.f, 0),
    FxBloc::db(FxBloc::Param::TrebleGain, "TRBL", -15.f, 15.f, true),
    FxBloc::pct(FxBloc::Param::Diffusion, "TLVL"),
    FxBloc::hz(FxBloc::Param::MidFreq, "MFREQ", 200.f, 8000.f, 0),
    FxBloc::hz(FxBloc::Param::TrebleFreq, "TFREQ", 1000.f, 16000.f, 0),
};
constexpr FxBloc::ParamDef kDefsKdfxFlanger[8] = {
    FxBloc::hz(FxBloc::Param::RoomSize, "RATE", 0.05f, 8.f),
    FxBloc::ms(FxBloc::Param::Decay, "DEPTH", 0.f, 3.f, 2),
    FxBloc::bipolarPct(FxBloc::Param::Damping, "FDBK", 95.f),
    FxBloc::LEVEL_DEF(),
    FxBloc::INGAIN_DEF(),
    FxBloc::ms(FxBloc::Param::BaseDelay, "BASE", 0.1f, 7.f),
    {FxBloc::Param::Diffusion, "SPRD", false, 0, 1, nullptr, "DEG", 0.f, 180.f, 0, false},
    FxBloc::hz(FxBloc::Param::HPF, "HPF", 20.f, 1000.f, 0),
};
constexpr FxBloc::ParamDef kDefsKdfxPhaser[7] = {
    FxBloc::hz(FxBloc::Param::RoomSize, "RATE", 0.05f, 8.f),
    FxBloc::pct(FxBloc::Param::Decay, "DEPTH"),
    {FxBloc::Param::Damping, "FDBK", false, 0, 1, nullptr, "%", 0.f, 70.f, 0, false},
    FxBloc::LEVEL_DEF(),
    FxBloc::INGAIN_DEF(),
    {FxBloc::Param::Diffusion, "SPRD", false, 0, 1, nullptr, "DEG", 0.f, 180.f, 0, false},
    FxBloc::hz(FxBloc::Param::HPF, "HPF", 20.f, 1000.f, 0),
};
constexpr FxBloc::ParamDef kDefsKdfxTremolo[4] = {
    FxBloc::hz(FxBloc::Param::RoomSize, "RATE", 0.10f, 16.f),
    FxBloc::pct(FxBloc::Param::Decay, "DEPTH"),
    FxBloc::INGAIN_DEF(),
    FxBloc::LEVEL_DEF(),
};
constexpr FxBloc::ParamDef kDefsKdfxAutoPan[4] = {
    FxBloc::hz(FxBloc::Param::RoomSize, "RATE", 0.05f, 10.f),
    FxBloc::pct(FxBloc::Param::Decay, "WIDTH"),
    FxBloc::INGAIN_DEF(),
    FxBloc::LEVEL_DEF(),
};
constexpr FxBloc::ParamDef kDefsKdfxTubeAmp[12] = {
    FxBloc::db(FxBloc::Param::RoomSize, "DRIVE", 0.f, 24.f),
    {FxBloc::Param::ShaperMode, "MODE", true, 0, 3, FxBloc::kShaperOptions()},
    FxBloc::hz(FxBloc::Param::LoCut, "LOCUT", 20.f, 300.f, 0),
    FxBloc::LEVEL_DEF(),
    FxBloc::INGAIN_DEF(),
    FxBloc::pct(FxBloc::Param::CabSim, "CAB"),
    FxBloc::hz(FxBloc::Param::HiCut, "HCUT", 1000.f, 16000.f, 0),
    FxBloc::hz(FxBloc::Param::ModRate, "MRATE", 0.05f, 5.f),
    FxBloc::bipolarPct(FxBloc::Param::ModFdbk, "MFDBK", 95.f),
    FxBloc::ms(FxBloc::Param::ModDepth, "MDEP", 0.f, 10.f),
    {FxBloc::Param::ComboCfg, "CFG", true, 0, 1, FxBloc::kCfgOptions()},
};
constexpr FxBloc::ParamDef kDefsKdfxLaser[8] = {
    // TIME leads (overall delay length = duration); SPAC sets the buzz start pitch
    // / descent, CONT the shape (0=delay, 34%=reverse), FDBK extends (100% sustains).
    FxBloc::ms(FxBloc::Param::RoomSize, "TIME", 0.f, 2000.f, 0),
    FxBloc::LEVEL_DEF(),
    {FxBloc::Param::Width, "SPAC", false, 0, 1, nullptr, "SMP", 0.f, 40.f, 1, false},
    FxBloc::pct(FxBloc::Param::Diffusion, "CONT"),
    {FxBloc::Param::Decay, "FDBK", false, 0, 1, nullptr, "%", 0.f, 100.f, 0, false},
};
// Pair combinations (manual 706-724): primary half's natural ids + the Mod*
// trio for the modulated half; CFG only where the manual's "<>" allows order
// swaps ("+" variants are fixed-order).
constexpr FxBloc::ParamDef kDefsKdfxChoDly[9] = {
    FxBloc::pct(FxBloc::Param::Diffusion, "XFEED"),
    FxBloc::LEVEL_DEF(),
    FxBloc::INGAIN_DEF(),
    FxBloc::ms(FxBloc::Param::RoomSize, "TIME", 1.f, 1480.f, 0),
    {FxBloc::Param::Decay, "FDBK", false, 0, 1, nullptr, "%", 0.f, 100.f, 0, false},
    FxBloc::hz(FxBloc::Param::ModRate, "MRATE", 0.05f, 15.f),
    FxBloc::ms(FxBloc::Param::ModDepth, "MDEP", 0.f, 10.f),
    FxBloc::bipolarPct(FxBloc::Param::ModFdbk, "MFDBK", 95.f),
    {FxBloc::Param::ComboCfg, "CFG", true, 0, 1, FxBloc::kCfgOptions()},
};
constexpr FxBloc::ParamDef kDefsKdfxChoRev[8] = {
    FxBloc::ms(FxBloc::Param::RoomSize, "SIZE", 3.f, 48.f, 0),
    FxBloc::rt60(FxBloc::Param::Decay, "DCAY", 0.6f, 999.f),
    FxBloc::hz(FxBloc::Param::Damping, "DAMP", 351.f, 7019.f, 0),
    FxBloc::LEVEL_DEF(),
    FxBloc::hz(FxBloc::Param::ModRate, "MRATE", 0.05f, 15.f),
    FxBloc::ms(FxBloc::Param::ModDepth, "MDEP", 0.f, 10.f),
    FxBloc::bipolarPct(FxBloc::Param::ModFdbk, "MFDBK", 95.f),
    {FxBloc::Param::ComboCfg, "CFG", true, 0, 1, FxBloc::kCfgOptions()},
};
constexpr FxBloc::ParamDef kDefsKdfxFlgDly[9] = {
    FxBloc::pct(FxBloc::Param::Diffusion, "XFEED"),
    FxBloc::LEVEL_DEF(),
    FxBloc::INGAIN_DEF(),
    FxBloc::ms(FxBloc::Param::RoomSize, "TIME", 1.f, 1480.f, 0),
    {FxBloc::Param::Decay, "FDBK", false, 0, 1, nullptr, "%", 0.f, 100.f, 0, false},
    FxBloc::hz(FxBloc::Param::ModRate, "MRATE", 0.05f, 8.f),
    FxBloc::ms(FxBloc::Param::ModDepth, "MDEP", 0.f, 3.f, 2),
    FxBloc::bipolarPct(FxBloc::Param::ModFdbk, "MFDBK", 95.f),
};
constexpr FxBloc::ParamDef kDefsKdfxFlgRev[8] = {
    FxBloc::ms(FxBloc::Param::RoomSize, "SIZE", 3.f, 48.f, 0),
    FxBloc::rt60(FxBloc::Param::Decay, "DCAY", 0.6f, 999.f),
    FxBloc::hz(FxBloc::Param::Damping, "DAMP", 351.f, 7019.f, 0),
    FxBloc::LEVEL_DEF(),
    FxBloc::hz(FxBloc::Param::ModRate, "MRATE", 0.05f, 8.f),
    FxBloc::ms(FxBloc::Param::ModDepth, "MDEP", 0.f, 3.f, 2),
    FxBloc::bipolarPct(FxBloc::Param::ModFdbk, "MFDBK", 95.f),
    {FxBloc::Param::ComboCfg, "CFG", true, 0, 1, FxBloc::kCfgOptions()},
};
constexpr FxBloc::ParamDef kDefsKdfxFlgLaser[8] = {
    FxBloc::ms(FxBloc::Param::RoomSize, "TIME", 0.f, 2000.f, 0),
    {FxBloc::Param::Decay, "FDBK", false, 0, 1, nullptr, "%", 0.f, 100.f, 0, false},
    {FxBloc::Param::Width, "SPAC", false, 0, 1, nullptr, "SMP", 0.f, 40.f, 1, false},
    FxBloc::LEVEL_DEF(),
    FxBloc::hz(FxBloc::Param::ModRate, "MRATE", 0.05f, 8.f),
    FxBloc::ms(FxBloc::Param::ModDepth, "MDEP", 0.f, 3.f, 2),
    FxBloc::bipolarPct(FxBloc::Param::ModFdbk, "MFDBK", 95.f),
    {FxBloc::Param::ComboCfg, "CFG", true, 0, 1, FxBloc::kCfgOptions()},
};
constexpr FxBloc::ParamDef kDefsKdfxShprRev[9] = {
    FxBloc::db(FxBloc::Param::RoomSize, "DRIVE", 0.f, 24.f),
    {FxBloc::Param::ShaperMode, "SHPR", true, 0, 3, FxBloc::kShaperOptions()},
    FxBloc::db(FxBloc::Param::BassGain, "BASS", -15.f, 15.f, true),
    FxBloc::db(FxBloc::Param::MidGain, "MID", -15.f, 15.f, true),
    FxBloc::db(FxBloc::Param::TrebleGain, "TRBL", -15.f, 15.f, true),
    FxBloc::LEVEL_DEF(),
    FxBloc::ms(FxBloc::Param::ModRate, "SIZE", 3.f, 48.f, 0),
    FxBloc::rt60(FxBloc::Param::ModDepth, "DCAY", 0.6f, 999.f),
    {FxBloc::Param::ComboCfg, "CFG", true, 0, 1, FxBloc::kCfgOptions()},
};
// Resonant filter bank (manual 900/902/903): common TYPE/FREQ/RES; the env
// variant adds at-rest MIN Freq + bipolar SWEEP + dB/sec rates, the LFO variant
// rides the Mod* trio (rate 0.01-10 HZ, depth in octaves).
constexpr FxBloc::ParamDef kDefsKdfxResFilt[5] = {
    {FxBloc::Param::FiltType, "TYPE", true, 0, 3, FxBloc::kFiltTypeOptions()},
    FxBloc::logHz(FxBloc::Param::FiltFreq, "FREQ", 16.f, 8372.f, 0),
    FxBloc::db(FxBloc::Param::FiltRes, "RES", 0.f, 50.f),
    FxBloc::LEVEL_DEF(),
};
constexpr FxBloc::ParamDef kDefsKdfxEnvFilt[8] = {
    {FxBloc::Param::FiltType, "TYPE", true, 0, 3, FxBloc::kFiltTypeOptions()},
    FxBloc::logHz(FxBloc::Param::FiltMinFreq, "MINF", 16.f, 8372.f, 0),
    FxBloc::LEVEL_DEF(),
    FxBloc::db(FxBloc::Param::FiltRes, "RES", 0.f, 50.f),
    {FxBloc::Param::FiltSweep, "SWP", false, 0, 1, nullptr, "%", 0.f, 100.f, 0, true},
    {FxBloc::Param::FiltAtk, "ATK", false, 0, 1, nullptr, "DB/S", 0.f, 300.f, 0, false},
    {FxBloc::Param::FiltRel, "REL", false, 0, 1, nullptr, "DB/S", 0.f, 300.f, 0, false},
};
constexpr FxBloc::ParamDef kDefsKdfxLfoFilt[7] = {
    {FxBloc::Param::FiltType, "TYPE", true, 0, 3, FxBloc::kFiltTypeOptions()},
    FxBloc::logHz(FxBloc::Param::FiltFreq, "FREQ", 16.f, 8372.f, 0),
    FxBloc::LEVEL_DEF(),
    FxBloc::db(FxBloc::Param::FiltRes, "RES", 0.f, 50.f),
    FxBloc::hz(FxBloc::Param::ModRate, "MRATE", 0.01f, 10.f),
    {FxBloc::Param::ModDepth, "MDEP", false, 0, 1, nullptr, "OCT", 0.f, 6.f, 1, false},
};
// Ring Modulator (manual 907): MODE (L*R / OSC); OSC mode = Osc1 (any shape,
// width, smoothing) + four sines, each Lvl 0-100% / Freq 16-25088 HZ log; LEVEL
// = Wet/Dry (in L*R it passes only the LEFT channel dry — engine handles that).
// Quantizer (the 714 Quantize+Flange partner): DYN 0-144 DB (0 = two levels,
// square wave; every 6 DB doubles the levels), HEAD 0-144 DB of headroom above
// the signal, DC Off/-79..0 DB.
constexpr FxBloc::ParamDef kDefsKdfxQuant[4] = {
    FxBloc::db(FxBloc::Param::RoomSize, "DYN", 0.f, 144.f),
    FxBloc::LEVEL_DEF(),
    FxBloc::db(FxBloc::Param::Decay, "HEAD", 0.f, 144.f),
    FxBloc::db(FxBloc::Param::Diffusion, "DC", -79.f, 0.f, true),
};
// Quantize+Flange (manual 714, "+" = fixed order: quantize then flange).
constexpr FxBloc::ParamDef kDefsKdfxQntFlg[10] = {
    FxBloc::db(FxBloc::Param::RoomSize, "DYN", 0.f, 144.f),
    FxBloc::db(FxBloc::Param::Decay, "HEAD", 0.f, 144.f),
    FxBloc::db(FxBloc::Param::Diffusion, "DC", -79.f, 0.f, true),
    FxBloc::LEVEL_DEF(),
    FxBloc::INGAIN_DEF(),
    FxBloc::hz(FxBloc::Param::ModRate, "MRATE", 0.05f, 8.f),
    FxBloc::ms(FxBloc::Param::ModDepth, "MDEP", 0.f, 3.f, 2),
    FxBloc::bipolarPct(FxBloc::Param::ModFdbk, "MFDBK", 95.f),
    FxBloc::ms(FxBloc::Param::BaseDelay, "BASE", 0.1f, 7.f),
};
constexpr FxBloc::ParamDef kDefsKdfxRingMod[16] = {
    {FxBloc::Param::RmMode, "MODE", true, 0, 1, FxBloc::kRmModeOptions()},
    FxBloc::LEVEL_DEF(),
    FxBloc::pct(FxBloc::Param::RmO1Lvl, "O1LVL"),
    FxBloc::logHz(FxBloc::Param::RmO1Freq, "O1FRQ", 16.f, 25088.f, 0),
    {FxBloc::Param::RmO1Shape, "O1SHP", true, 0, 4, FxBloc::kRmShapeOptions()},
    FxBloc::pct(FxBloc::Param::RmO1Plsw, "O1PLS"),
    FxBloc::pct(FxBloc::Param::RmO1Smooth, "O1SMH"),
    FxBloc::pct(FxBloc::Param::RmO2Lvl, "S2LVL"),
    FxBloc::logHz(FxBloc::Param::RmO2Freq, "S2FRQ", 16.f, 25088.f, 0),
    FxBloc::pct(FxBloc::Param::RmO3Lvl, "S3LVL"),
    FxBloc::logHz(FxBloc::Param::RmO3Freq, "S3FRQ", 16.f, 25088.f, 0),
    FxBloc::pct(FxBloc::Param::RmO4Lvl, "S4LVL"),
    FxBloc::logHz(FxBloc::Param::RmO4Freq, "S4FRQ", 16.f, 25088.f, 0),
    FxBloc::pct(FxBloc::Param::RmO5Lvl, "S5LVL"),
    FxBloc::logHz(FxBloc::Param::RmO5Freq, "S5FRQ", 16.f, 25088.f, 0),
};
constexpr FxBloc::ParamDef kDefsKdfxDist[7] = {
    FxBloc::db(FxBloc::Param::RoomSize, "DRIVE", 0.f, 24.f),
    {FxBloc::Param::ShaperMode, "MODE", true, 0, 3, FxBloc::kShaperOptions()},
    FxBloc::hz(FxBloc::Param::LoCut, "LOCUT", 20.f, 300.f, 0),
    FxBloc::LEVEL_DEF(),
    FxBloc::INGAIN_DEF(),
    FxBloc::pct(FxBloc::Param::CabSim, "CAB"),
    FxBloc::hz(FxBloc::Param::HiCut, "HCUT", 1000.f, 16000.f, 0),
};
/// Stock compressor wrapped as a bloc (manual comp/gate family 950-957 stand-in).
/// Param aliases: roomSize=threshold, decay=ratio, damping=attack, diffusion=release,
/// hpf=sidechain HPF, width=wet/dry blend — curves match the engine's setters.
constexpr FxBloc::ParamDef kDefsCompressor[8] = {
    {FxBloc::Param::RoomSize, "THRSH", false, 0, 1, nullptr, "DB", 0.f, -48.f, 1, false},
    {FxBloc::Param::Decay, "RATIO", false, 0, 1, nullptr, "", 0.1f, 128.f, 1, false, 3, 1.5f},
    {FxBloc::Param::Damping, "ATK", false, 0, 1, nullptr, "MS", 0.5f, 100.f, 1, false, 2, 1.5f},
    FxBloc::LEVEL_DEF(),
    FxBloc::INGAIN_DEF(),
    {FxBloc::Param::Diffusion, "REL", false, 0, 1, nullptr, "MS", 10.f, 1000.f, 0, false, 2, 1.5f},
    {FxBloc::Param::HPF, "SIDE", false, 0, 1, nullptr, "HZ", 0.f, 100.f, 0, false, 4, 1.5f},
    {FxBloc::Param::Width, "BLEND", false, 0, 1, nullptr, "%", 0.f, 100.f, 0, false},
};
/// Track-side send into the master chain: BUS picks which master slot's bus the
/// signal feeds; PAN/IN-GN/LEVEL shape it on the way out of the track.
constexpr FxBloc::ParamDef kDefsGlobalSend[4] = {
    {FxBloc::Param::SendBus, "BUS", true, 0, 2, FxBloc::kBusOptions()},
    FxBloc::bipolarPct(FxBloc::Param::Width, "PAN", 100.f),
    FxBloc::INGAIN_DEF(),
    FxBloc::LEVEL_DEF(),
};

constexpr FxBloc::ParamDef kDefsEmpty[1] = {{}};

constexpr std::span<const FxBloc::ParamDef> FxBloc::paramDefs(FxBloc::Model m) {
	switch (m) {
	case Model::COMPRESSOR: return kDefsCompressor;
	case Model::FREEVERB: return kDefsFreeverb;
	case Model::MUTABLE: return kDefsMutable;
	case Model::KDFX_REVERB: return kDefsKdfxReverb;
	case Model::KDFX_DELAY: return kDefsKdfxDelay;
	case Model::KDFX_CHORUS: return kDefsKdfxChorus;
	case Model::KDFX_TONE: return kDefsKdfxTone;
	case Model::KDFX_FLANGER: return kDefsKdfxFlanger;
	case Model::KDFX_PHASER: return kDefsKdfxPhaser;
	case Model::KDFX_TREMOLO: return kDefsKdfxTremolo;
	case Model::KDFX_AUTOPAN: return kDefsKdfxAutoPan;
	case Model::KDFX_DIST: return kDefsKdfxDist;
	case Model::KDFX_TUBEAMP: return kDefsKdfxTubeAmp;
	case Model::KDFX_LASER: return kDefsKdfxLaser;
	case Model::KDFX_CHO_DLY: return kDefsKdfxChoDly;
	case Model::KDFX_CHO_REV: return kDefsKdfxChoRev;
	case Model::KDFX_FLG_DLY: return kDefsKdfxFlgDly;
	case Model::KDFX_FLG_REV: return kDefsKdfxFlgRev;
	case Model::KDFX_FLG_LASER: return kDefsKdfxFlgLaser;
	case Model::KDFX_SHPR_REV: return kDefsKdfxShprRev;
	case Model::KDFX_RESFILT: return kDefsKdfxResFilt;
	case Model::KDFX_ENVFILT: return kDefsKdfxEnvFilt;
	case Model::KDFX_LFOFILT: return kDefsKdfxLfoFilt;
	case Model::KDFX_RINGMOD: return kDefsKdfxRingMod;
	case Model::KDFX_QUANT: return kDefsKdfxQuant;
	case Model::KDFX_QNT_FLG: return kDefsKdfxQntFlg;
	case Model::GLOBAL_SEND: return kDefsGlobalSend;
	case Model::EMPTY:
	default: return kDefsEmpty;
	}
}

} // namespace deluge::dsp
