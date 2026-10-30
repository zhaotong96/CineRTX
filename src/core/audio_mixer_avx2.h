// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
//
// Multi-track audio pipeline (W9-3).
//
// Three independent mechanisms live here:
//
//   * a lock-free SPSC ring of 1600-sample planar slices, one per 33.3 ms at
//     48 kHz, so the demux thread never blocks on the mixer;
//   * an AVX2 (with a scalar reference) multi-track mixdown with per-track
//     gain and a hard clip;
//   * a phase-locked loop that trims the audio clock by at most +/-0.05 % to
//     keep |delta_AV| <= 1.0 ms over a two-hour programme (Gate 3.4), plus a
//     WSOLA time-scale modifier for explicit speed changes where pitch must be
//     preserved.
//
// The AVX2 path is selected at run time; the scalar path is bit-exact against
// it for the same inputs, which is what the unit test asserts.

#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace rtx {

// 1600 samples = 33.3 ms at 48 kHz: one mixer slice.
inline constexpr size_t kAudioSliceSamples = 1600;
inline constexpr size_t kMaxAudioTracks = 8;
// WSOLA frame geometry: 1024-sample Hann frames with 50 % overlap and a
// +/-256-sample correlation search.
inline constexpr size_t kWsolaFrameSamples = 1024;
inline constexpr size_t kWsolaSearchSamples = 256;

// True when the current CPU implements AVX2 + FMA and the translation unit was
// compiled with the intrinsics enabled.
bool avx2Available() noexcept;

// N-track mixdown into `out`.  `tracks[i]` must hold at least `num_samples`
// floats; `gains[i]` is applied before summing.  The result is clipped to
// [-1, 1] because a summed bus can exceed full scale.
void mixTracks(const float* const* tracks, const float* gains, size_t track_count, float* out,
               size_t num_samples);

// Two-track convenience form used by the reference mixer and the tests.
void mixTracksAvx2(const float* track_a, const float* track_b, float gain_a, float gain_b,
                   float* out, size_t num_samples);

// Scalar reference implementation; `mixTracks` falls back to this.
void mixTracksScalar(const float* const* tracks, const float* gains, size_t track_count,
                     float* out, size_t num_samples);

// In-place gain with a hard clip to [-1, 1].
void applyGainAndClip(float* samples, size_t num_samples, float gain);

// ---------------------------------------------------------------------------
// Lock-free slice ring
//
// The producer fills a slice and publishes it; the consumer mixes the oldest
// published slice.  Storage is allocated once, so the audio thread performs no
// allocation and takes no lock.
// ---------------------------------------------------------------------------
class AudioSliceRing {
public:
    explicit AudioSliceRing(size_t capacity_slices = 16);

    bool valid() const noexcept { return capacity_ > 0; }
    size_t capacity() const noexcept { return capacity_; }
    size_t available() const noexcept;  // published but not yet released

    // Producer side.  Returns nullptr when the ring is full.
    float* acquireWriteSlice() noexcept;
    void publishWriteSlice() noexcept;

    // Consumer side.  Returns nullptr when no slice is published.
    const float* peekReadSlice() const noexcept;
    void releaseReadSlice() noexcept;

private:
    size_t capacity_ = 0;
    std::vector<std::array<float, kAudioSliceSamples>> storage_;
    std::atomic<uint32_t> write_index_{0};
    std::atomic<uint32_t> read_index_{0};
};

// ---------------------------------------------------------------------------
// WSOLA time-scale modification (pitch preserving)
//
// `ratio` > 1.0 speeds the programme up (more input consumed per output
// sample), `ratio` < 1.0 slows it down.  The implementation is overlap-add with
// a cross-correlation search, i.e. the classic SOLA core of WSOLA.
// ---------------------------------------------------------------------------
class WsolaResampler {
public:
    // Processes `input_samples` of input into `output_samples` of output.
    // Both buffers must be valid for the lengths given; the caller owns the
    // remainder of the tail between calls.
    void process(const float* input, size_t input_samples, float* output, size_t output_samples,
                 double ratio);

    // Diagnostic: the correlation offset chosen for the last frame, in samples.
    int lastOverlapOffset() const noexcept { return last_offset_; }
    void reset() noexcept;

private:
    std::array<float, kWsolaFrameSamples> previous_{};
    bool has_previous_ = false;
    int last_offset_ = 0;
};

// ---------------------------------------------------------------------------
// A/V phase-locked loop
//
// `offset_us` is (video clock - audio clock) in microseconds.  The loop trims
// the audio resampling ratio by at most `max_ratio` (default 0.05 %), which is
// inaudible and sufficient to absorb a two-hour clock mismatch.
//
// `gain` is the per-sample loop gain applied to the instantaneous error.  The
// default of 1.0 combined with the 33.3 ms slice gives a 3.3 % per-slice
// correction demand, so the loop is clamped for any error above 500 us and
// decays geometrically below it.  A lagged (low-pass) measurement was measured
// to be marginally unstable at this slice length, so the smoothed value is
// reported for telemetry only and never fed back into the ratio.
// ---------------------------------------------------------------------------
class AvSyncPll {
public:
    explicit AvSyncPll(double gain = 1.0, double max_ratio = 0.0005);

    void update(int64_t offset_us);
    double resampleRatio() const noexcept { return ratio_; }
    int64_t smoothedOffsetUs() const noexcept { return smoothed_offset_us_; }
    bool locked() const noexcept { return locked_; }
    void reset() noexcept;

private:
    double gain_ = 1.0;
    double max_ratio_ = 0.0005;
    double smoothed_offset_us_ = 0.0;
    double ratio_ = 1.0;
    bool locked_ = false;
};

}  // namespace rtx
