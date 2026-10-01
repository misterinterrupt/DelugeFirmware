/*
 * KDFX-style fixed-point utilities (Q31) shared by the kdfx effect models.
 *
 * Builds on the engine's existing Q31 primitives (util/fixedpoint.h:
 * multiply_32x32_rshift32_rounded, add_saturation; util/functions.h) rather
 * than introducing a new fixed-point convention. All audio is int32_t with
 * 0x80000000 = -1.0 and 0x7FFFFFFF ≈ +1.0.
 */

#pragma once

#include "deluge/util/fixedpoint.h"
#include <array>
#include <cstdint>
#include <limits>

namespace deluge::dsp::reverb::kdfx {

/// Collapse a 64-bit matrix accumulator back to Q31 with saturation.
/// Sums of several near-full-scale lines exceed 32 bits; wrapping would
/// produce loud clicks, so clamp instead (the "fixed-point trap").
constexpr inline q31_t qcollapse(int64_t sum) {
	if (sum > std::numeric_limits<int32_t>::max()) {
		return std::numeric_limits<int32_t>::max();
	}
	if (sum < std::numeric_limits<int32_t>::min()) {
		return std::numeric_limits<int32_t>::min();
	}
	return static_cast<q31_t>(sum);
}

/// Sum N lines and apply the Householder scale of 2/N in one step.
/// For N=4: y_i = x_i - (sum >> 1). The >>1 is exact, so the matrix is
/// energy-preserving up to the deliberate decay/damping applied per loop.
constexpr inline q31_t householder4(q31_t x, int64_t sum) {
	return qcollapse(static_cast<int64_t>(x) - (sum >> 1));
}

/// Triangular (TPDF) dither, ±1 LSB. Injected into delay-network feedback
/// to keep quantization from settling into idle limit cycles (the faint
/// permanent tone integer reverbs can hang on).
inline q31_t tpdf_dither(uint32_t& rngState) {
	// xorshift32: cheap, no multiplier
	rngState ^= rngState << 13;
	rngState ^= rngState >> 17;
	rngState ^= rngState << 5;
	// two independent bits → triangular distribution over {-1, 0, +1}
	return static_cast<q31_t>(static_cast<int32_t>(rngState & 1u) - static_cast<int32_t>((rngState >> 1) & 1u));
}

constexpr inline uint32_t ditherSeed(uint32_t seed) { return seed ? seed : 0x9E3779B9u; }

/// Smooth 1-pole lowpass damping: y += (x - y) * coeff, Q31 multiply.
/// Unlike a `>> shift` trap this sweeps continuously across the damping
/// knob (KDFX "HF Damp"), rather than snapping between fs/2^n octaves.
constexpr inline void smoothDamp(q31_t& y, q31_t x, q31_t dampCoeffQ31) {
	y += multiply_32x32_rshift32_rounded(x - y, dampCoeffQ31);
}

/// One-pole damping with an "air" blend: out = lp + air * (x - lp).
/// KDFX loop filters often act as a high-shelf rather than a hard LP so the
/// absolute top of the tail keeps a hint of sparkle. air = 0 → pure LP,
/// air = ONE_Q31 → bypass. Implemented as the LP state plus a partial
/// reintroduction of the removed band.
constexpr inline void shelfDamp(q31_t& y, q31_t& yAir, q31_t x, q31_t dampCoeffQ31, q31_t air) {
	smoothDamp(y, x, dampCoeffQ31);
	// track the removed HF band separately so the shelf doesn't compound
	smoothDamp(yAir, x - y, dampCoeffQ31);
}

/// Single-order allpass diffuser with its own small delay buffer.
/// Cascaded at the FDN input (KDFX "In Diff"), delays ~100-300 samples,
/// coefficients 0.5 or 0.618: smears transients before the network so 4
/// lines sound like an instant bloom instead of a flutter echo.
template <size_t N> struct AllPass {
	std::array<q31_t, N> buf{};
	size_t pos = 0;

	constexpr q31_t process(q31_t input, q31_t coeff) {
		q31_t delayed = buf[pos];
		q31_t out = add_saturation(delayed, multiply_32x32_rshift32_rounded(coeff, input - delayed));
		buf[pos] = input;
		pos = (pos + 1) % N;
		return out;
	}
	void clear() { buf.fill(0); }
};

/// Read `back` samples behind writePos from a circular buffer.
/// Positive offsets only — callers must guarantee 1 <= back < BufSize
/// (the FDN reverb's reads are all length-derived, so they qualify).
/// For signed/modulated offsets use circReadSigned below.
template <size_t BufSize>
constexpr inline q31_t circRead(const std::array<q31_t, BufSize>& buf, size_t writePos, size_t back) {
	size_t p = writePos + BufSize - back;
	if (p >= BufSize) {
		p -= BufSize;
	}
	return buf[p];
}

/// Safe circular buffer read supporting signed offsets.
template <size_t BufSize>
inline q31_t circReadSigned(const std::array<q31_t, BufSize>& buf, size_t writePos, int32_t back) {
    // Wrap 'back' safely into [0, BufSize - 1]
    int32_t p = static_cast<int32_t>(writePos) - back;
    while (p < 0) {
        p += static_cast<int32_t>(BufSize);
    }
    while (p >= static_cast<int32_t>(BufSize)) {
        p -= static_cast<int32_t>(BufSize);
    }
    return buf[static_cast<size_t>(p)];
}

/// 4-point, 3rd-order Catmull-Rom / Hermite interpolation in Q31.
/// idx: integer sample delay (distance back from writePos); frac: Q31
/// [0, ONE_Q31) representing [0.0, 1.0). As frac rises the interpolated
/// point moves from delay `idx` toward delay `idx + 1` (time-correct
/// direction). Intermediates are full int64: the c2/c3 coefficients reach
/// ±6.0 and cannot exist in a q31_t; per-step Horner products stay in
/// range and each >>31 renormalizes.
template <size_t BufSize>
inline q31_t hermite4(const std::array<q31_t, BufSize>& buf, size_t writePos, int32_t idx, q31_t frac) {
    // 4 sample taps surrounding the fractional point:
    // y0 = idx - 1 (newest), y1 = idx (floor), y2 = idx + 1 (ceil), y3 = idx + 2 (oldest)
    const int64_t y0 = circReadSigned(buf, writePos, idx - 1);
    const int64_t y1 = circReadSigned(buf, writePos, idx);
    const int64_t y2 = circReadSigned(buf, writePos, idx + 1);
    const int64_t y3 = circReadSigned(buf, writePos, idx + 2);

    // Catmull-Rom coefficients in 64-bit:
    //   c0 = y1
    //   c1 = 0.5 * (y2 - y0)
    //   c2 = y0 - 2.5 * y1 + 2.0 * y2 - 0.5 * y3
    //   c3 = 0.5 * (y3 - y0) + 1.5 * (y1 - y2)
    const int64_t c0 = y1;
    const int64_t c1 = (y2 - y0) >> 1;
    const int64_t c2 = y0 - ((5 * y1) >> 1) + (y2 << 1) - (y3 >> 1);
    const int64_t c3 = ((y3 - y0) >> 1) + ((3 * (y1 - y2)) >> 1);

    // Horner evaluation: y = (((c3 * f) + c2) * f + c1) * f + c0
    const int64_t f = static_cast<int64_t>(frac);

    int64_t acc = (c3 * f) >> 31;
    acc = ((acc + c2) * f) >> 31;
    acc = ((acc + c1) * f) >> 31;
    acc = acc + c0;

    // Saturate to valid Q31 boundaries
    if (acc > ONE_Q31) {
        return ONE_Q31;
    }
    if (acc < -ONE_Q31) {
        return -ONE_Q31;
    }
    return static_cast<q31_t>(acc);
}

/// Clamp the polynomial shaper's output. At |x| > 1 the x³ term dominates
/// and drives the polynomial toward -inf; hard clamp before that wraps.
constexpr inline q31_t shaperClamp(int64_t y) { return qcollapse(y); }

/// Saturating mix into a channel (used for output frames)
constexpr inline void mixInto(int32_t& channel, q31_t tap) { channel = add_saturation(channel, tap); }

} // namespace deluge::dsp::reverb::kdfx
