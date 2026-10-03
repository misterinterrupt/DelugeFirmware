/*
 * KDFX TubeAmp<>MD>Chor (manual algorithms 729-732, 3 PAU).
 *
 * The TubeAmp chains: a tube-style distortion feeding a modulated delay /
 * chorus, in one bloc. Insert engine — the amp transforms the stereo signal in
 * place; the modulated delay wet-adds from a mono downmix of whatever it is
 * fed (post-amp in AMP->MOD order, or the dry signal plus its own wet in
 * MOD->AMP order, where the amp then shapes the wet too — that is what the
 * order config means). CFG swaps the order, exactly like the manual's A->B cfg.
 *
 * Param routing: DRIVE/MODE/LOCUT/CAB/HCUT go to the amp; the MRATE/MDEPTH/
 * MFDBK trio goes to the modulated delay (its base delay is fixed longer than
 * a plain chorus — ~20 ms, the MD character).
 */

#pragma once

#include "definitions_cxx.hpp"
#include "dsp/reverb/base.hpp"
#include "dsp/reverb/kdfx/chorus.hpp"
#include "dsp/reverb/kdfx/distortion.hpp"
#include "dsp/reverb/kdfx/kdfx_util.hpp"
#include <algorithm>
#include <cstdint>
#include <span>

namespace deluge::dsp::reverb {

class KdfxTubeAmp : public Base {
public:
	KdfxTubeAmp() {
		// The modulated delay: longer base than a chorus (~20 ms), modest depth,
		// and — the hard-won rule — a non-zero wet pan, or the whole mod half is
		// silently passthrough.
		mod_.setBaseDelay(0.6f);
		mod_.setDecay(0.2f);
		mod_.setDamping(0.5f);
		mod_.setPanLevels(kdfx::unityWetPan(), kdfx::unityWetPan());
	}
	~KdfxTubeAmp() override = default;

	[[nodiscard]] bool isInsert() const override { return true; }

	// Never called for insert engines (FxBloc::process branches on isInsert()).
	void process(std::span<int32_t> input, std::span<StereoSample> output) override {}

	void processInsert(std::span<StereoSample> buffer) override {
		if (cfg_ == 0) {
			// AMP -> MOD: shape first, then modulate a wet copy of the shaped signal.
			dist_.processInsert(buffer);
			runModDelay(buffer);
		}
		else {
			// MOD -> AMP: wet-add the modulated dry, then shape everything.
			runModDelay(buffer);
			dist_.processInsert(buffer);
		}
	}

	// --- amp params ---
	void setRoomSize(float v) override { dist_.setRoomSize(v); }
	[[nodiscard]] float getRoomSize() const override { return dist_.getRoomSize(); }
	void setShaperMode(int32_t v) override { dist_.setShaperMode(v); }
	[[nodiscard]] int32_t getShaperMode() const override { return dist_.getShaperMode(); }
	void setLoCut(float v) override { dist_.setLoCut(v); }
	[[nodiscard]] float getLoCut() const override { return dist_.getLoCut(); }
	void setCab(float v) override { dist_.setCab(v); }
	[[nodiscard]] float getCab() const override { return dist_.getCab(); }
	void setHiCut(float v) override { dist_.setHiCut(v); }
	[[nodiscard]] float getHiCut() const override { return dist_.getHiCut(); }

	// --- modulated delay params ---
	void setModRate(float v) override { mod_.setRoomSize(v); }
	[[nodiscard]] float getModRate() const override { return mod_.getRoomSize(); }
	void setModDepth(float v) override { mod_.setDecay(v); }
	[[nodiscard]] float getModDepth() const override { return mod_.getDecay(); }
	void setModFdbk(float v) override { mod_.setDamping(v); }
	[[nodiscard]] float getModFdbk() const override { return mod_.getDamping(); }

	void setComboCfg(int32_t v) override { cfg_ = (v != 0) ? 1 : 0; }
	[[nodiscard]] int32_t getComboCfg() const override { return cfg_; }

private:
	void runModDelay(std::span<StereoSample> buffer) {
		for (size_t done = 0; done < buffer.size(); done += kMaxChunk) {
			size_t n = std::min(kMaxChunk, buffer.size() - done);
			StereoSample* buf = buffer.data() + done;
			for (size_t i = 0; i < n; i++) {
				monoScratch_[i] = (static_cast<int64_t>(buf[i].l) + buf[i].r) >> 1;
			}
			mod_.process(std::span<int32_t>{monoScratch_, n}, std::span<StereoSample>{buf, n});
		}
	}

	static constexpr size_t kMaxChunk = 64;

	KdfxDistortion dist_{};
	KdfxChorus mod_{};
	int32_t cfg_ = 0;

	int32_t monoScratch_[kMaxChunk]{};
};

} // namespace deluge::dsp::reverb
