/*
 * FxBloc — one effect-slot wrapper for the modular master/track FX chains.
 *
 * A bloc wraps any of the selectable effect engines behind a uniform in-place stereo
 * process(). EMPTY is a transparent passthrough that costs nothing (no CPU, no memory
 * activity). Reverb-class blocs reuse the existing dsp::Reverb variant wrapper, which
 * already implements deferred, render-boundary model/param application — model switches
 * never mutate an engine mid-render.
 *
 * Engine instances are intended to be statically placed in SDRAM via PLACE_SDRAM_BSS
 * at the chain level (the ~715KB variant union makes internal RAM placement untenable
 * for multiple slots; 64MB of external SDRAM is managed by the general allocator and
 * linker sections — see src/definitions.h).
 */

#pragma once

#include "dsp/reverb/reverb.hpp"
#include "dsp/stereo_sample.h"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <span>

namespace deluge::dsp {

class FxBloc {
public:
	// COMPRESSOR / FILTER adapters (wrapping RMSFeedbackCompressor and the song filter
	// set) land with the integration step; until then selecting them behaves as EMPTY.
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
	};

	static constexpr size_t kMaxChunk = 64;

	Model getModel() const { return model_; }
	bool isEmpty() const { return model_ == Model::EMPTY; }

	/// UI thread. Records the selection; the engine is only reconfigured at the next
	/// render boundary via applyPending() so an in-flight render never sees a torn state.
	void setModel(Model m) {
		if (m == model_ && !pending_) {
			return;
		}
		pendingModel_ = m;
		pending_ = true;
	}

	/// Audio thread only, at the top of each render block.
	void applyPending() {
		if (pending_) {
			pending_ = false;
			Model m = pendingModel_;
			if (m != Model::EMPTY && m != Model::COMPRESSOR && m != Model::FILTER) {
				engine_.setModel(reverbModelFor(m));
			}
			model_ = m;
		}
		// Must run every boundary, not just on model changes: menus write bloc params
		// through reverbEngine() at any time, and those deferred writes (plus the
		// setModel above) are only serviced by the engine's own applyPending().
		engine_.applyPending();
	}

	/// In-place stereo processing. Returns false when this bloc is a passthrough
	/// (EMPTY, or an adapter that has not landed yet) — the router then skips it.
	bool process(StereoSample* buffer, size_t numSamples) {
		if (model_ == Model::EMPTY || model_ == Model::COMPRESSOR || model_ == Model::FILTER) {
			return false;
		}
		// Reverb-class engines consume a mono input and add their wet output into the
		// stereo buffer (send-style wet-add). Downmix in bounded chunks through a scratch
		// that lives in the (SDRAM-placed) chain object, keeping it out of the stack.
		for (size_t done = 0; done < numSamples;) {
			size_t n = std::min(kMaxChunk, numSamples - done);
			StereoSample* buf = buffer + done;
			for (size_t i = 0; i < n; i++) {
				monoScratch_[i] = (static_cast<int64_t>(buf[i].l) + buf[i].r) >> 1;
			}
			engine_.process(std::span<int32_t>{monoScratch_, n}, std::span<StereoSample>{buf, n});
			done += n;
		}
		return true;
	}

	/// Direct access for parameter plumbing (menus write through here; the engine's own
	/// deferred setters keep it render-safe).
	dsp::Reverb& reverbEngine() { return engine_; }

private:
	static dsp::Reverb::Model reverbModelFor(Model m) {
		switch (m) {
		case Model::FREEVERB:
			return dsp::Reverb::Model::FREEVERB;
		case Model::MUTABLE:
			return dsp::Reverb::Model::MUTABLE;
		case Model::KDFX_REVERB:
			return dsp::Reverb::Model::KDFX_REVERB;
		case Model::KDFX_DELAY:
			return dsp::Reverb::Model::KDFX_DELAY;
		case Model::KDFX_CHORUS:
			return dsp::Reverb::Model::KDFX_CHORUS;
		case Model::KDFX_TONE:
			return dsp::Reverb::Model::KDFX_TONE;
		default:
			return dsp::Reverb::Model::FREEVERB;
		}
	}

	Model model_ = Model::EMPTY;
	bool pending_ = false;
	Model pendingModel_ = Model::EMPTY;

	dsp::Reverb engine_{};
	int32_t monoScratch_[kMaxChunk]{};
};

} // namespace deluge::dsp
