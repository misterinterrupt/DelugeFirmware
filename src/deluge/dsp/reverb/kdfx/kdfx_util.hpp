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
#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>

namespace deluge::dsp::reverb::kdfx {

/// Exact normalized Q31 multiply: y = (a * b) in Q31.
/// Since smmulr/multiply_32x32_rshift32_rounded shifts by 32,
/// shifting left by 1 restores full Q31 normalization.
constexpr inline q31_t q31_mul(q31_t a, q31_t b) {
	return multiply_32x32_rshift32_rounded(a, b) << 1;
}

/// Collapse a 64-bit matrix accumulator back to Q31 with saturation.
constexpr inline q31_t qcollapse(int64_t sum) {
	if (sum > std::numeric_limits<int32_t>::max()) {
		return std::numeric_limits<int32_t>::max();
	}
	if (sum < std::numeric_limits<int32_t>::min()) {
		return std::numeric_limits<int32_t>::min();
	}
	return static_cast<q31_t>(sum);
}

/// Accumulate sum and collapse to Q31 with saturation.
constexpr inline q31_t qsum64(int64_t acc, int32_t val) {
	return qcollapse(acc + val);
}

/// Sum N lines and apply the Householder scale of 2/N in one step.
/// For N=4: y_i = x_i - (sum >> 1). The >>1 is exact, so the matrix is
/// energy-preserving up to the deliberate decay/damping applied per loop.
constexpr inline q31_t householder4(q31_t x, int64_t sum) {
	return qcollapse(static_cast<int64_t>(x) - (sum >> 1));
}

/// Triangular (TPDF) dither, ±1 LSB.
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
constexpr inline void smoothDamp(q31_t& y, q31_t x, q31_t dampCoeffQ31) {
	y += q31_mul(x - y, dampCoeffQ31);
}

/// One-pole damping with an "air" blend: out = lp + air * (x - lp).
constexpr inline void shelfDamp(q31_t& y, q31_t& yAir, q31_t x, q31_t dampCoeffQ31, q31_t air) {
	smoothDamp(y, x, dampCoeffQ31);
	smoothDamp(yAir, x - y, dampCoeffQ31);
}

/// Canonical single-order allpass diffuser with delay buffer.
/// Transfer function: H(z) = (z^-N - g) / (1 - g * z^-N)
/// Preserves 100% signal energy with pure phase dispersion.
template <size_t N> struct AllPass {
	std::array<q31_t, N> buf{};
	size_t pos = 0;

	constexpr q31_t process(q31_t input, q31_t coeff) {
		q31_t delayed = buf[pos];
		q31_t g_delayed = q31_mul(coeff, delayed);
		q31_t w = add_saturation(input, g_delayed);
		buf[pos] = w;
		q31_t g_w = q31_mul(coeff, w);
		pos = (pos + 1) % N;
		return add_saturation(delayed, -g_w);
	}
	void clear() { buf.fill(0); }
};

/// Read `back` samples behind writePos from a circular buffer.
template <size_t BufSize>
constexpr inline q31_t circRead(const std::array<q31_t, BufSize>& buf, size_t writePos, size_t back) {
	size_t p = writePos + BufSize - back;
	if (p >= BufSize) {
		p -= BufSize;
	}
	return buf[p];
}

/// Circular read for a delay line whose buffer is left uninitialized on construction.
///
/// The big KDFX engine buffers (0.5 MB for the delay) are deliberately not zero-initialized:
/// a variant emplace clearing them inside the audio task would overrun the SSI TX buffer and
/// glitch output on every model switch. Instead every read distance is capped to the engine's
/// age (frames completed since construction), so we only touch positions the write cursor has
/// already filled. Reads that would reach further back — the first lap, for long delays —
/// return silence, which is exactly what a cleared line would have given.
template <size_t BufSize>
constexpr inline q31_t circReadCapped(const std::array<q31_t, BufSize>& buf, size_t writePos, size_t back,
                                      uint32_t age) {
	if (back > age) {
		return 0;
	}
	// back == 0 stays a plain read of the write position (a zero pre-delay must pass the
	// just-written sample through).
	return circRead<BufSize>(buf, writePos, back);
}

/// Safe circular buffer read supporting signed offsets.
template <size_t BufSize>
inline q31_t circReadSigned(const std::array<q31_t, BufSize>& buf, size_t writePos, int32_t back) {
	int32_t p = static_cast<int32_t>(writePos) - back;
	while (p < 0) {
		p += static_cast<int32_t>(BufSize);
	}
	while (p >= static_cast<int32_t>(BufSize)) {
		p -= static_cast<int32_t>(BufSize);
	}
	return buf[static_cast<size_t>(p)];
}

/// Clamp the polynomial shaper's output.
constexpr inline q31_t shaperClamp(int64_t y) { return qcollapse(y); }

/// Saturating mix into output channel with pan/send volume scaling.
/// getPanLeft()/Right() max out at 0x20000000 (0.25 in Q31 scale);
/// << 5 provides +12 dB wet makeup headroom so max send is deeply wet and
/// halfway send is rich and immediately audible.
constexpr inline void mixWet(int32_t& channel, q31_t wet, int32_t panLevel) {
	int32_t scaled = multiply_32x32_rshift32_rounded(wet, panLevel) << 5;
	channel = add_saturation(channel, scaled);
}

/// Pan level that makes mixWet() a unity wet-add: mixWet computes
/// wet x panLevel / 2^27 ((wet x pan) >> 32, then << 5), so unity is 2^27.
constexpr inline int32_t unityWetPan() {
	return 0x08000000;
}

/// Parabolic bipolar LFO over a full Q31 phase turn — the same approximation
/// the chorus engine uses for its sine waveform. At phase 0 the output is at a
/// negative extreme, so treat it as a phase-shifted sine; tremolo/pan gain
/// mappings only care that it is smooth and periodic.
inline q31_t parabolicLfo(uint32_t phase) {
	int32_t x = static_cast<int32_t>(phase) - 0x40000000;
	int64_t absX = std::abs(static_cast<int64_t>(x));
	int64_t oneMinusAbsX = 0x7FFFFFFF - absX;
	int64_t y = (static_cast<int64_t>(x) * oneMinusAbsX) >> 29;
	return qcollapse(y);
}

/// The KDFX oscillator bank's waveforms (manual: Sine, Saw+, Saw-, Pulse, Tri).
/// plsw is the pulse width fraction (0.5 = square, only affects Pulse); smooth
/// pulls Saw toward Tri and Pulse toward Sine ("removes the higher harmonics").
/// Phase is a full turn in 32 bits; output is -1..1.
inline float oscWave(int32_t shape, uint32_t phase, float plsw, float smooth) {
	float p = static_cast<float>(phase) / 4294967296.0f; // 0..1
	// Sine via the shared parabolic approximation, zero-crossing at phase 0.
	float sine = static_cast<float>(parabolicLfo(phase + 0x40000000u)) / 2147483648.0f;
	float w;
	switch (shape) {
	case 1: { // Saw+ : rising 0..1 -> -1..1
		float saw = 2.0f * p - 1.0f;
		float tri = 1.0f - 4.0f * std::fabs(p - 0.5f);
		w = saw + (tri - saw) * smooth;
		break;
	}
	case 2: { // Saw- : falling
		float saw = 1.0f - 2.0f * p;
		float tri = 1.0f - 4.0f * std::fabs(p - 0.5f);
		w = saw + (tri - saw) * smooth;
		break;
	}
	case 3: { // Pulse (width % of period; 50% = square), smooth -> toward sine
		float pw = std::clamp(plsw, 0.01f, 0.99f);
		float pulse = (p < pw) ? 1.0f : -1.0f;
		w = pulse + (sine - pulse) * smooth;
		break;
	}
	case 4: // Tri
		w = 1.0f - 4.0f * std::fabs(p - 0.5f);
		break;
	case 5: { // Expon (ringmod Osc1 only): convex exponential rise from 0 to a
		// narrow sharp peak at the cycle end, wrapping straight back to 0 —
		// "a waveform with narrow, sharp peaks which seems to rise exponentially
		// from 0" (reference FXAlg #907).
		constexpr float k = 6.9078f; // e^k - 1 = 999: three decades of rise
		w = 2.0f * ((std::exp(k * p) - 1.0f) / (std::exp(k) - 1.0f)) - 1.0f;
		break;
	}
	case 0:
	default: // Sine
		w = sine;
		break;
	}
	return std::clamp(w, -1.0f, 1.0f);
}

} // namespace deluge::dsp::reverb::kdfx
