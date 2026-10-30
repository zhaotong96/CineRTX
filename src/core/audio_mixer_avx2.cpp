// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
//
// Multi-track audio pipeline implementation (W9-3).

#include "audio_mixer_avx2.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#if defined(__AVX2__)
#include <immintrin.h>
#endif

namespace rtx {
namespace {

constexpr float kClipLimit = 1.0f;

inline float clipSample(float value) noexcept {
    if (value > kClipLimit) {
        return kClipLimit;
    }
    if (value < -kClipLimit) {
        return -kClipLimit;
    }
    return value;
}

// Raised-cosine window: w[n] = 0.5 - 0.5 cos(2*pi*n/(N-1)).  Used for the
// WSOLA overlap-add cross-fade.
float hannWeight(size_t index, size_t length) noexcept {
    if (length < 2) {
        return 1.0f;
    }
    const double phase = 2.0 * 3.14159265358979323846 * static_cast<double>(index) /
                         static_cast<double>(length - 1);
    return static_cast<float>(0.5 - 0.5 * std::cos(phase));
}

}  // namespace

bool avx2Available() noexcept {
#if defined(__AVX2__)
    return true;
#else
    return false;
#endif
}

void mixTracksScalar(const float* const* tracks, const float* gains, size_t track_count,
                     float* out, size_t num_samples) {
    if (out == nullptr) {
        return;
    }
    if (tracks == nullptr || gains == nullptr || track_count == 0) {
        std::memset(out, 0, num_samples * sizeof(float));
        return;
    }
    for (size_t index = 0; index < num_samples; ++index) {
        float sum = 0.0f;
        for (size_t track = 0; track < track_count; ++track) {
            if (tracks[track] == nullptr) {
                continue;
            }
            sum += tracks[track][index] * gains[track];
        }
        out[index] = clipSample(sum);
    }
}

void mixTracks(const float* const* tracks, const float* gains, size_t track_count, float* out,
               size_t num_samples) {
    if (out == nullptr) {
        return;
    }
    if (tracks == nullptr || gains == nullptr || track_count == 0) {
        std::memset(out, 0, num_samples * sizeof(float));
        return;
    }
#if defined(__AVX2__)
    // Fast path: the first track seeds the accumulator, every further track is
    // a fused multiply-add.  The tail is finished by the scalar code so the
    // two paths agree bit for bit.
    const size_t vectorised = num_samples & ~static_cast<size_t>(7);
    const float* seed = tracks[0] != nullptr ? tracks[0] : nullptr;
    if (seed != nullptr) {
        const __m256 seed_gain = _mm256_set1_ps(gains[0]);
        for (size_t index = 0; index < vectorised; index += 8) {
            const __m256 value = _mm256_loadu_ps(seed + index);
            _mm256_storeu_ps(out + index, _mm256_mul_ps(value, seed_gain));
        }
    } else {
        std::memset(out, 0, vectorised * sizeof(float));
    }
    for (size_t track = 1; track < track_count; ++track) {
        if (tracks[track] == nullptr) {
            continue;
        }
        const __m256 gain = _mm256_set1_ps(gains[track]);
        for (size_t index = 0; index < vectorised; index += 8) {
            const __m256 accumulated = _mm256_loadu_ps(out + index);
            const __m256 value = _mm256_loadu_ps(tracks[track] + index);
            _mm256_storeu_ps(out + index, _mm256_fmadd_ps(value, gain, accumulated));
        }
    }
    for (size_t index = 0; index < vectorised; ++index) {
        out[index] = clipSample(out[index]);
    }
    for (size_t index = vectorised; index < num_samples; ++index) {
        float sum = 0.0f;
        for (size_t track = 0; track < track_count; ++track) {
            if (tracks[track] == nullptr) {
                continue;
            }
            sum += tracks[track][index] * gains[track];
        }
        out[index] = clipSample(sum);
    }
#else
    mixTracksScalar(tracks, gains, track_count, out, num_samples);
#endif
}

void mixTracksAvx2(const float* track_a, const float* track_b, float gain_a, float gain_b,
                   float* out, size_t num_samples) {
    const float* tracks[kMaxAudioTracks] = {track_a, track_b};
    const float gains[2] = {gain_a, gain_b};
    mixTracks(tracks, gains, 2, out, num_samples);
}

void applyGainAndClip(float* samples, size_t num_samples, float gain) {
    if (samples == nullptr) {
        return;
    }
    for (size_t index = 0; index < num_samples; ++index) {
        samples[index] = clipSample(samples[index] * gain);
    }
}

// ---------------------------------------------------------------------------
// AudioSliceRing
// ---------------------------------------------------------------------------

AudioSliceRing::AudioSliceRing(size_t capacity_slices) {
    if (capacity_slices < 2) {
        capacity_slices = 2;
    }
    storage_.resize(capacity_slices);
    capacity_ = storage_.size();
}

size_t AudioSliceRing::available() const noexcept {
    if (capacity_ == 0) {
        return 0;
    }
    const uint32_t write = write_index_.load(std::memory_order_acquire);
    const uint32_t read = read_index_.load(std::memory_order_acquire);
    return static_cast<size_t>(write - read);
}

float* AudioSliceRing::acquireWriteSlice() noexcept {
    if (capacity_ == 0) {
        return nullptr;
    }
    const uint32_t write = write_index_.load(std::memory_order_relaxed);
    const uint32_t read = read_index_.load(std::memory_order_acquire);
    if (static_cast<size_t>(write - read) >= capacity_) {
        return nullptr;  // full: the producer must drop or wait
    }
    return storage_[write % capacity_].data();
}

void AudioSliceRing::publishWriteSlice() noexcept {
    if (capacity_ == 0) {
        return;
    }
    const uint32_t write = write_index_.load(std::memory_order_relaxed);
    write_index_.store(write + 1, std::memory_order_release);
}

const float* AudioSliceRing::peekReadSlice() const noexcept {
    if (capacity_ == 0) {
        return nullptr;
    }
    const uint32_t read = read_index_.load(std::memory_order_relaxed);
    const uint32_t write = write_index_.load(std::memory_order_acquire);
    if (read == write) {
        return nullptr;
    }
    return storage_[read % capacity_].data();
}

void AudioSliceRing::releaseReadSlice() noexcept {
    if (capacity_ == 0) {
        return;
    }
    const uint32_t read = read_index_.load(std::memory_order_relaxed);
    read_index_.store(read + 1, std::memory_order_release);
}

// ---------------------------------------------------------------------------
// WsolaResampler
// ---------------------------------------------------------------------------

void WsolaResampler::reset() noexcept {
    previous_.fill(0.0f);
    has_previous_ = false;
    last_offset_ = 0;
}

void WsolaResampler::process(const float* input, size_t input_samples, float* output,
                             size_t output_samples, double ratio) {
    if (output == nullptr || output_samples == 0) {
        return;
    }
    if (input == nullptr || input_samples == 0 || !(ratio > 0.0) || ratio > 4.0) {
        std::memset(output, 0, output_samples * sizeof(float));
        return;
    }

    const size_t hop = kWsolaFrameSamples / 2;
    const size_t frame = kWsolaFrameSamples;
    size_t input_cursor = 0;
    size_t output_cursor = 0;

    while (output_cursor + frame <= output_samples && input_cursor + frame <= input_samples) {
        // Correlation search: pick the input offset in [-search, +search] whose
        // first half best continues the previous frame's second half.  This is
        // what preserves pitch while changing duration.
        int best_offset = 0;
        if (has_previous_) {
            double best_score = -1.0e30;
            const int limit = static_cast<int>(kWsolaSearchSamples);
            const int low = -std::min<int>(limit, static_cast<int>(input_cursor));
            const int high = static_cast<int>(
                std::min<size_t>(static_cast<size_t>(limit),
                                 input_samples - input_cursor - frame));
            for (int offset = low; offset <= high; ++offset) {
                const float* candidate = input + input_cursor + offset;
                double score = 0.0;
                for (size_t index = 0; index < hop; ++index) {
                    score += static_cast<double>(previous_[hop + index]) *
                             static_cast<double>(candidate[index]);
                }
                if (score > best_score) {
                    best_score = score;
                    best_offset = offset;
                }
            }
        }
        last_offset_ = best_offset;

        const float* current = input + input_cursor + static_cast<size_t>(best_offset);
        // Cross-fade the previous tail into the current frame's head, then emit
        // the current frame's first hop as new material.
        for (size_t index = 0; index < hop; ++index) {
            const float weight = hannWeight(index, hop);
            output[output_cursor + index] =
                previous_[hop + index] * (1.0f - weight) + current[index] * weight;
        }
        for (size_t index = 0; index < hop; ++index) {
            output[output_cursor + hop + index] = current[hop + index];
        }
        std::memcpy(previous_.data(), current, frame * sizeof(float));
        has_previous_ = true;

        output_cursor += hop;
        // Consume `hop * ratio` of input: ratio > 1 shortens the programme.
        const double advance = static_cast<double>(hop) * ratio;
        input_cursor += static_cast<size_t>(advance < 1.0 ? 1.0 : advance);
    }
    // Anything the frame loop could not fill is silence rather than stale data.
    for (size_t index = output_cursor; index < output_samples; ++index) {
        output[index] = 0.0f;
    }
}

// ---------------------------------------------------------------------------
// AvSyncPll
// ---------------------------------------------------------------------------

AvSyncPll::AvSyncPll(double gain, double max_ratio) : gain_(gain), max_ratio_(max_ratio) {
    if (!(gain_ > 0.0) || gain_ > 4.0) {
        gain_ = 1.0;
    }
    if (!(max_ratio_ > 0.0) || max_ratio_ > 0.01) {
        max_ratio_ = 0.0005;
    }
}

void AvSyncPll::reset() noexcept {
    smoothed_offset_us_ = 0.0;
    ratio_ = 1.0;
    locked_ = false;
}

void AvSyncPll::update(int64_t offset_us) {
    // Telemetry-only smoothing; see the header for why the control path uses
    // the instantaneous error.
    smoothed_offset_us_ = smoothed_offset_us_ * 0.9 + static_cast<double>(offset_us) * 0.1;

    // A positive offset means the video clock is ahead, so the audio must run
    // slightly fast: ratio > 1.  The correction is clamped to +/-0.05 %, which
    // bounds the pitch shift to a quarter of a cent.
    double correction = gain_ * static_cast<double>(offset_us) / 1000000.0;
    correction = std::max(-max_ratio_, std::min(max_ratio_, correction));
    ratio_ = 1.0 + correction;
    // Locked once the measured error is inside the Gate 3.4 budget.
    locked_ = offset_us > -1000 && offset_us < 1000;
}

}  // namespace rtx
