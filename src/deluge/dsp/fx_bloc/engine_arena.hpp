/*
 * Copyright (c) 2014-2023 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */
#pragma once

#include "memory/general_memory_allocator.h"
#include <cstdint>

namespace deluge::dsp::fx_arena {

/// Dedicated memory arena for FX-bloc engines.
///
/// Engine picks allocate hundreds of KB (reverb ~715KB, delay 512KB, pairs up to
/// ~750KB). Routing those through GeneralMemoryAllocator means walking its free
/// list and, when the external region can't fit the block, stealing from the
/// sample cache cluster by cluster — a multi-millisecond stall on the UI thread
/// that underruns the audio buffer (device symptom: audible lag when picking a
/// bloc). This arena is carved from the general allocator ONCE at boot, while
/// the cache is still empty, and afterwards serves engine-sized requests from
/// a tiny sorted free list: first-fit over at most a handful of blocks, no
/// cache eviction, ever.
///
/// Threading: alloc/free are UI-thread only by construction — FxBloc constructs
/// engines on the UI thread (setModel) and retires/frees them on the UI thread
/// too (reclaimRetired / destructor); the audio thread only swaps pointers.
///
/// If the arena is exhausted, callers fall back to GeneralMemoryAllocator —
/// same behaviour as before this arena existed.
inline constexpr size_t kArenaBytes = 6 << 20; // ~8 reverb-class engines concurrently
inline constexpr size_t kAlignment = 8;
inline constexpr size_t kHeaderSize = 8; // size prefix on every allocation
inline constexpr int kMaxFreeBlocks = 128;

struct FreeBlock {
	uintptr_t start;
	size_t size;
};

inline void* base_ = nullptr;
inline size_t size_ = 0;
inline FreeBlock free_[kMaxFreeBlocks];
inline int numFree_ = 0;

/// Carve the arena once (idempotent). Tries progressively smaller sizes so a
/// full SDRAM degrades to a smaller arena instead of none.
inline bool init() {
	if (base_ != nullptr) {
		return true;
	}
	for (size_t want : {kArenaBytes, size_t(4) << 20, size_t(2) << 20, size_t(1) << 20}) {
		void* p = GeneralMemoryAllocator::get().allocLowSpeed(want);
		if (p != nullptr) {
			// Keep the payload base aligned regardless of what the allocator handed back
			uintptr_t raw = (uintptr_t)p;
			uintptr_t aligned = (raw + kAlignment - 1) & ~(uintptr_t)(kAlignment - 1);
			base_ = (void*)aligned;
			size_ = want - (aligned - raw);
			free_[0] = {aligned, size_};
			numFree_ = 1;
			return true;
		}
	}
	return false;
}

[[nodiscard]] inline bool owns(const void* p) {
	uintptr_t a = (uintptr_t)p;
	uintptr_t b = (uintptr_t)base_;
	return a >= b && a < b + size_;
}

[[nodiscard]] inline void* alloc(size_t n) {
	if (base_ == nullptr) {
		if (!init()) {
			return nullptr;
		}
	}
	size_t payload = (n + kAlignment - 1) & ~(size_t)(kAlignment - 1);
	size_t need = payload + kHeaderSize;
	for (int i = 0; i < numFree_; i++) {
		if (free_[i].size < need) {
			continue;
		}
		uintptr_t start = free_[i].start;
		size_t blockSize = free_[i].size;
		size_t* header = (size_t*)start;
		if (blockSize - need < kHeaderSize + kAlignment) {
			// Too small to split — hand out the whole block
			free_[i] = free_[numFree_ - 1];
			numFree_--;
			*header = blockSize;
		}
		else {
			free_[i].start += need;
			free_[i].size -= need;
			*header = need;
		}
		return (void*)(start + kHeaderSize);
	}
	return nullptr;
}

inline void free(void* p) {
	if (p == nullptr || !owns(p)) {
		return;
	}
	uintptr_t start = (uintptr_t)p - kHeaderSize;
	size_t size = *((size_t*)start);

	// Insert into the address-sorted free list, coalescing with adjacent blocks
	int insertAt = 0;
	while (insertAt < numFree_ && free_[insertAt].start < start) {
		insertAt++;
	}

	bool mergePrev = insertAt > 0 && (free_[insertAt - 1].start + free_[insertAt - 1].size == start);
	bool mergeNext = insertAt < numFree_ && (start + size == free_[insertAt].start);

	if (mergePrev && mergeNext) {
		free_[insertAt - 1].size += size + free_[insertAt].size;
		for (int i = insertAt; i < numFree_ - 1; i++) {
			free_[i] = free_[i + 1];
		}
		numFree_--;
	}
	else if (mergePrev) {
		free_[insertAt - 1].size += size;
	}
	else if (mergeNext) {
		free_[insertAt] = {start, size + free_[insertAt].size};
		// stays at the same index — still address-sorted
	}
	else if (insertAt < numFree_) {
		if (numFree_ >= kMaxFreeBlocks) {
			// Should be impossible with coalescing (engines are few); leak rather
			// than corrupt — the space comes back when the neighbouring blocks free.
			return;
		}
		for (int i = numFree_; i > insertAt; i--) {
			free_[i] = free_[i - 1];
		}
		free_[insertAt] = {start, size};
		numFree_++;
	}
	else {
		if (numFree_ >= kMaxFreeBlocks) {
			return;
		}
		free_[numFree_++] = {start, size};
	}
}

} // namespace deluge::dsp::fx_arena
