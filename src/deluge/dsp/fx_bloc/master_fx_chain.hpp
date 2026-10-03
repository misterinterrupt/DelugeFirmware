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
#include <cstdint>
#include <cstring>

namespace deluge::dsp {

class MasterFxChain {
public:
	static constexpr size_t kNumSlots = 3;
	static constexpr size_t kMaxChunk = FxBloc::kMaxChunk;

	enum class Routing : uint8_t {
		SERIES = 0,
		PARALLEL,
		SERIES_PARALLEL,
		PARALLEL_SERIES,
	};

	Routing getRouting() const { return routing_; }
	void setRouting(Routing r) { routing_ = r; }

	FxBloc& slot(size_t i) { return slots_[i]; }
	FxBloc const& slot(size_t i) const { return slots_[i]; }

	/// Audio thread only, at the top of each render block.
	void applyPending() {
		for (auto& s : slots_) {
			s.applyPending();
		}
	}

	/// In-place stereo processing through the configured topology.
	void process(StereoSample* buffer, size_t numSamples) {
		for (size_t done = 0; done < numSamples;) {
			size_t n = std::min(kMaxChunk, numSamples - done);
			processChunk(buffer + done, n);
			done += n;
		}
	}

private:
	void processChunk(StereoSample* buf, size_t n) {
		switch (routing_) {
		case Routing::SERIES:
			slots_[0].process(buf, n);
			slots_[1].process(buf, n);
			slots_[2].process(buf, n);
			break;

		case Routing::PARALLEL:
			runParallel(buf, n, 0, 1, 2);
			break;

		case Routing::SERIES_PARALLEL:
			// 1 -> (2 + 3)
			slots_[0].process(buf, n);
			runParallel(buf, n, 1, 2, -1);
			break;

		case Routing::PARALLEL_SERIES:
			// (1 + 2) -> 3
			runParallel(buf, n, 0, 1, -1);
			slots_[2].process(buf, n);
			break;
		}
	}

	/// Sum of the given branch slots' outputs, each fed the same input snapshot. A -1
	/// branch is skipped. Output overwrites buf: dry plus each branch's contribution.
	void runParallel(StereoSample* buf, size_t n, int32_t a, int32_t b, int32_t c) {
		int32_t branches[3] = {a, b, c};

		// Snapshot the branch input, then restart from dry and add each producing
		// branch's delta (its output minus the dry it was fed).
		std::memcpy(branchScratch_, buf, n * sizeof(StereoSample));
		int32_t producing = 0;
		for (int32_t idx : branches) {
			if (idx < 0) {
				continue;
			}
			std::memcpy(accumScratch_, branchScratch_, n * sizeof(StereoSample));
			if (slots_[idx].process(accumScratch_, n)) {
				producing++;
				for (size_t j = 0; j < n; j++) {
					buf[j].l += accumScratch_[j].l - branchScratch_[j].l;
					buf[j].r += accumScratch_[j].r - branchScratch_[j].r;
				}
			}
		}

		// Unity-mix compensation when several producing branches stack on the dry signal.
		if (producing > 1) {
			int32_t gain = ONE_Q31 / (producing + 1); // branches + dry
			for (size_t j = 0; j < n; j++) {
				buf[j].l = multiply_32x32_rshift32_rounded(buf[j].l, gain) << 1;
				buf[j].r = multiply_32x32_rshift32_rounded(buf[j].r, gain) << 1;
			}
		}
	}

	Routing routing_ = Routing::SERIES;
	FxBloc slots_[kNumSlots]{};
	StereoSample branchScratch_[kMaxChunk]{};
	StereoSample accumScratch_[kMaxChunk]{};
};

} // namespace deluge::dsp
