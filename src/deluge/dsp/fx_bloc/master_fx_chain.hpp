/*
 * MasterFxChain — the three-slot master effect chain with selectable routing topology.
 *
 * Slots hold FxBlocs (any engine, EMPTY = transparent passthrough). The routing
 * "algorithm" is the user-selectable topology over the three slots:
 *
 *   SERIES           1 -> 2 -> 3                        (default; maps to today's chain)
 *   PARALLEL         (1 + 2 + 3) summed against dry
 *   SERIES_PARALLEL  1 -> (2 + 3)
 *   PARALLEL_SERIES  (1 + 2) -> 3
 *
 * Branch summing is done against a snapshot of the branch input so wet-add blocs
 * (reverb class) and future transform blocs (compressor/filter) both behave correctly.
 * All scratch lives inside the (SDRAM-placed) chain object; processing runs in bounded
 * chunks so the scratch stays fixed-size.
 */

#pragma once

#include "dsp/fx_bloc/fx_bloc.hpp"
#include "util/fixedpoint.h"
#include <algorithm>
#include <cstdint>
#include <cstring>

namespace deluge::dsp {

class FxChain {
public:
	static constexpr size_t kNumSlots = 3;
	static constexpr size_t kMaxChunk = FxBloc::kMaxChunk;
	static constexpr size_t kMaxSendBufferSize = 128;

	enum class Routing : uint8_t {
		SERIES = 0,
		PARALLEL,
		SERIES_PARALLEL,
		PARALLEL_SERIES,
	};

	Routing getRouting() const { return routing_; }
	void setRouting(Routing r) { routing_ = r; }

	/// True when any slot holds an engine, or when audio has been sent to a master bus.
	[[nodiscard]] bool isActive() const {
		return !slots_[0].isEmpty() || !slots_[1].isEmpty() || !slots_[2].isEmpty() || hasSendAudio_[0]
		       || hasSendAudio_[1] || hasSendAudio_[2];
	}

	void clear() {
		routing_ = Routing::SERIES;
		for (auto& s : slots_) {
			s.setModel(FxBloc::Model::EMPTY);
		}
		for (size_t s = 0; s < kNumSlots; s++) {
			hasSendAudio_[s] = false;
		}
	}

	void clearSendBusses(size_t numSamples) {
		size_t n = std::min(kMaxSendBufferSize, numSamples);
		for (size_t s = 0; s < kNumSlots; s++) {
			std::memset(sendBus_[s], 0, n * sizeof(StereoSample));
			hasSendAudio_[s] = false;
		}
	}

	void accumulateSend(size_t busIdx, const StereoSample* src, size_t numSamples, float gain, float panPercent) {
		if (busIdx >= kNumSlots) {
			return;
		}
		size_t n = std::min(kMaxSendBufferSize, numSamples);
		hasSendAudio_[busIdx] = true;
		float panL = (panPercent <= 0.0f) ? 1.0f : (100.0f - panPercent) / 100.0f;
		float panR = (panPercent >= 0.0f) ? 1.0f : (100.0f + panPercent) / 100.0f;
		float gainL = gain * panL;
		float gainR = gain * panR;

		for (size_t i = 0; i < n; i++) {
			int64_t addedL = static_cast<int64_t>(src[i].l * gainL);
			int64_t addedR = static_cast<int64_t>(src[i].r * gainR);
			sendBus_[busIdx][i].l = static_cast<int32_t>(std::clamp<int64_t>(
			    static_cast<int64_t>(sendBus_[busIdx][i].l) + addedL, -2147483648LL, 2147483647LL));
			sendBus_[busIdx][i].r = static_cast<int32_t>(std::clamp<int64_t>(
			    static_cast<int64_t>(sendBus_[busIdx][i].r) + addedR, -2147483648LL, 2147483647LL));
		}
	}

	void cloneFrom(const FxChain& other) {
		routing_ = other.routing_;
		for (size_t s = 0; s < kNumSlots; s++) {
			auto model = other.slots_[s].getModel();
			slots_[s].setModel(model);
			for (const auto& def : FxBloc::paramDefs(model)) {
				if (def.param != FxBloc::Param::None) {
					slots_[s].setParam(def.param, other.slots_[s].getParam(def.param));
				}
			}
		}
	}

	FxBloc& slot(size_t i) { return slots_[i]; }
	FxBloc const& slot(size_t i) const { return slots_[i]; }

	/// Audio thread only, at the top of each render block.
	void applyPending(int32_t const* sourceValues = nullptr) {
		for (auto& s : slots_) {
			s.applyPending(sourceValues);
		}
	}

	/// Longest wet tail any slot still owes, in samples (0 = none). The
	/// render-skip logic holds the owning render open for exactly this long.
	[[nodiscard]] uint32_t tailSamples() const {
		uint32_t t = 0;
		for (const auto& s : slots_) {
			t = std::max(t, s.tailSamples());
		}
		return t;
	}

	/// FX shutdown policy, in dBFS, measured on this chain's output:
	///   > 0 dB   - voices rule: processing ends with the voices (previous default)
	///   -96..0   - RMS gate: processing holds until the chain's output falls
	///              below the threshold for a hold time, then stops
	///   <= -96   - always on: the chain processes every block while engaged
	/// The peak is tracked per render block by the owning render path.
	float fxRmsCutoffDb = 6.0f;
	float lastBlockPeak = 0.0f;        // linear, 1.0 = 0 dBFS
	uint32_t belowThresholdSince = 0;  // audioSampleTimer when first seen below

	void noteBlockPeak(float peakLinear) { lastBlockPeak = peakLinear; }

	[[nodiscard]] bool rmsGateOpen(uint32_t now) const {
		if (fxRmsCutoffDb <= -96.0f) {
			return true; // always-on floor
		}
		float threshold = std::pow(10.0f, fxRmsCutoffDb / 20.0f);
		return lastBlockPeak >= threshold;
	}

	/// Panic-kill every slot's wet tail (short fade, then silent-dry).
	/// Safe from the UI thread: only bloc scalars are touched; the audio
	/// thread performs the fade on its next blocks.
	void killTails() {
		for (auto& s : slots_) {
			s.killTail();
		}
	}

	/// In-place stereo processing through the configured topology.
	void process(StereoSample* buffer, size_t numSamples) {
		for (size_t done = 0; done < numSamples;) {
			size_t n = std::min(kMaxChunk, numSamples - done);
			processChunk(buffer + done, n, done);
			done += n;
		}
	}

private:
	void processChunk(StereoSample* buf, size_t n, size_t offset) {
		switch (routing_) {
		case Routing::SERIES:
			for (size_t j = 0; j < n; j++) {
				buf[j].l = add_saturation(buf[j].l, sendBus_[0][offset + j].l);
				buf[j].r = add_saturation(buf[j].r, sendBus_[0][offset + j].r);
			}
			slots_[0].process(buf, n);
			for (size_t j = 0; j < n; j++) {
				buf[j].l = add_saturation(buf[j].l, sendBus_[1][offset + j].l);
				buf[j].r = add_saturation(buf[j].r, sendBus_[1][offset + j].r);
			}
			slots_[1].process(buf, n);
			for (size_t j = 0; j < n; j++) {
				buf[j].l = add_saturation(buf[j].l, sendBus_[2][offset + j].l);
				buf[j].r = add_saturation(buf[j].r, sendBus_[2][offset + j].r);
			}
			slots_[2].process(buf, n);
			break;

		case Routing::PARALLEL:
			runParallel(buf, n, offset, 0, 1, 2);
			break;

		case Routing::SERIES_PARALLEL:
			// 1 -> (2 + 3)
			for (size_t j = 0; j < n; j++) {
				buf[j].l = add_saturation(buf[j].l, sendBus_[0][offset + j].l);
				buf[j].r = add_saturation(buf[j].r, sendBus_[0][offset + j].r);
			}
			slots_[0].process(buf, n);
			runParallel(buf, n, offset, 1, 2, -1);
			break;

		case Routing::PARALLEL_SERIES:
			// (1 + 2) -> 3
			runParallel(buf, n, offset, 0, 1, -1);
			for (size_t j = 0; j < n; j++) {
				buf[j].l = add_saturation(buf[j].l, sendBus_[2][offset + j].l);
				buf[j].r = add_saturation(buf[j].r, sendBus_[2][offset + j].r);
			}
			slots_[2].process(buf, n);
			break;
		}
	}

	/// Sum of the given branch slots' outputs, each fed the same input snapshot. A -1
	/// branch is skipped. Output overwrites buf: dry plus each branch's contribution.
	void runParallel(StereoSample* buf, size_t n, size_t offset, int32_t a, int32_t b, int32_t c) {
		int32_t branches[3] = {a, b, c};

		// Snapshot the branch input, then accumulate each producing branch's delta
		// (output minus dry input) in 64-bit precision to prevent 32-bit integer overflow.
		std::memcpy(branchScratch_, buf, n * sizeof(StereoSample));
		int64_t sumL[kMaxChunk];
		int64_t sumR[kMaxChunk];
		for (size_t j = 0; j < n; j++) {
			sumL[j] = branchScratch_[j].l;
			sumR[j] = branchScratch_[j].r;
		}

		for (int32_t idx : branches) {
			if (idx < 0) {
				continue;
			}
			for (size_t j = 0; j < n; j++) {
				accumScratch_[j].l = add_saturation(branchScratch_[j].l, sendBus_[idx][offset + j].l);
				accumScratch_[j].r = add_saturation(branchScratch_[j].r, sendBus_[idx][offset + j].r);
			}
			if (slots_[idx].process(accumScratch_, n)) {
				for (size_t j = 0; j < n; j++) {
					int64_t deltaL = static_cast<int64_t>(accumScratch_[j].l) - branchScratch_[j].l;
					int64_t deltaR = static_cast<int64_t>(accumScratch_[j].r) - branchScratch_[j].r;
					sumL[j] += deltaL;
					sumR[j] += deltaR;
				}
			}
			else {
				for (size_t j = 0; j < n; j++) {
					sumL[j] += sendBus_[idx][offset + j].l;
					sumR[j] += sendBus_[idx][offset + j].r;
				}
			}
		}

		for (size_t j = 0; j < n; j++) {
			buf[j].l = static_cast<int32_t>(std::clamp<int64_t>(sumL[j], -2147483648LL, 2147483647LL));
			buf[j].r = static_cast<int32_t>(std::clamp<int64_t>(sumR[j], -2147483648LL, 2147483647LL));
		}
	}

	Routing routing_ = Routing::SERIES;
	FxBloc slots_[kNumSlots]{};
	StereoSample branchScratch_[kMaxChunk]{};
	StereoSample accumScratch_[kMaxChunk]{};
	StereoSample sendBus_[kNumSlots][kMaxSendBufferSize]{};
	bool hasSendAudio_[kNumSlots]{};
};

using MasterFxChain = FxChain;

FxChain* getGlobalMasterFxChain();

inline void accumulateMasterSend(size_t busIdx, const StereoSample* src, size_t numSamples, float gain, float panPercent) {
	auto* chain = getGlobalMasterFxChain();
	if (chain != nullptr) {
		chain->accumulateSend(busIdx, src, numSamples, gain, panPercent);
	}
}

} // namespace deluge::dsp
