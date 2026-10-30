// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
//
// Multi-track audio pipeline contract (W9-3).

#include "test_framework.h"

#include <cmath>
#include <cstdint>
#include <vector>

#include "audio_mixer_avx2.h"

using namespace rtx;

namespace {

// Deterministic pseudo-audio: a sum of two low-frequency tones plus a slow
// envelope.  Values stay inside [-0.4, 0.4] so the mixdown never clips and the
// AVX2 / scalar comparison is not dominated by saturation.
std::vector<float> makeTone(size_t samples, double frequency, double phase, double amplitude) {
    std::vector<float> data(samples);
    for (size_t index = 0; index < samples; ++index) {
        const double t = static_cast<double>(index) / 48000.0;
        const double envelope = 0.75 + 0.25 * std::sin(2.0 * 3.14159265358979 * 0.5 * t);
        data[index] = static_cast<float>(amplitude * envelope *
                                         std::sin(2.0 * 3.14159265358979 * frequency * t + phase));
    }
    return data;
}

}  // namespace

RTX_TEST(audio_mix_avx2_matches_scalar) {
    constexpr size_t kSamples = kAudioSliceSamples;
    const std::vector<float> a = makeTone(kSamples, 220.0, 0.0, 0.40);
    const std::vector<float> b = makeTone(kSamples, 311.0, 1.1, 0.35);
    const std::vector<float> c = makeTone(kSamples, 97.0, 2.3, 0.30);

    std::vector<float> vectorised(kSamples, 0.0f);
    std::vector<float> scalar(kSamples, 0.0f);

    const float* tracks[3] = {a.data(), b.data(), c.data()};
    const float gains[3] = {0.9f, 0.6f, 0.4f};
    mixTracks(tracks, gains, 3, vectorised.data(), kSamples);
    mixTracksScalar(tracks, gains, 3, scalar.data(), kSamples);

    // FMA folds the multiply and the add into one rounding step, so the two
    // paths can differ in the last bit; 1e-6 is far below the 16-bit LSB.
    for (size_t index = 0; index < kSamples; ++index) {
        RTX_CHECK_NEAR(vectorised[index], scalar[index], 1e-6);
    }

    // The two-track convenience form must agree with the general one.
    std::vector<float> pair(kSamples, 0.0f);
    mixTracksAvx2(a.data(), b.data(), 0.9f, 0.6f, pair.data(), kSamples);
    const float* pair_tracks[2] = {a.data(), b.data()};
    const float pair_gains[2] = {0.9f, 0.6f};
    std::vector<float> pair_reference(kSamples, 0.0f);
    mixTracksScalar(pair_tracks, pair_gains, 2, pair_reference.data(), kSamples);
    for (size_t index = 0; index < kSamples; ++index) {
        RTX_CHECK_NEAR(pair[index], pair_reference[index], 1e-6);
    }

    // A null track list yields silence rather than a crash.
    std::vector<float> silence(kSamples, 1.0f);
    mixTracks(nullptr, nullptr, 0, silence.data(), kSamples);
    RTX_CHECK_NEAR(silence[0], 0.0, 1e-9);
    RTX_CHECK_NEAR(silence[kSamples - 1], 0.0, 1e-9);
}

RTX_TEST(audio_mix_gain_and_clip) {
    constexpr size_t kSamples = 32;
    std::vector<float> samples(kSamples);
    for (size_t index = 0; index < kSamples; ++index) {
        samples[index] = (index % 2 == 0) ? 0.8f : -0.8f;
    }
    // Unity gain must not alter the signal.
    std::vector<float> unity = samples;
    applyGainAndClip(unity.data(), kSamples, 1.0f);
    for (size_t index = 0; index < kSamples; ++index) {
        RTX_CHECK_NEAR(unity[index], samples[index], 1e-7);
    }
    // A 4x gain must clip to exactly +/-1.0, never wrap.
    std::vector<float> boosted = samples;
    applyGainAndClip(boosted.data(), kSamples, 4.0f);
    for (size_t index = 0; index < kSamples; ++index) {
        RTX_CHECK_NEAR(boosted[index], (index % 2 == 0) ? 1.0 : -1.0, 1e-7);
    }

    // The mixer clips the summed bus: two full-scale tracks cannot exceed 1.0.
    std::vector<float> full(kSamples, 0.9f);
    const float* tracks[2] = {full.data(), full.data()};
    const float gains[2] = {1.0f, 1.0f};
    std::vector<float> mixed(kSamples, 0.0f);
    mixTracks(tracks, gains, 2, mixed.data(), kSamples);
    for (size_t index = 0; index < kSamples; ++index) {
        RTX_CHECK(mixed[index] <= 1.0f);
        RTX_CHECK(mixed[index] >= -1.0f);
        RTX_CHECK_NEAR(mixed[index], 1.0, 1e-6);
    }
    // A null destination is a no-op, not a fault.
    applyGainAndClip(nullptr, kSamples, 2.0f);
}

RTX_TEST(audio_spsc_ring_slice) {
    AudioSliceRing ring(4);
    RTX_REQUIRE(ring.valid());
    RTX_CHECK_EQ(ring.capacity(), static_cast<size_t>(4));
    RTX_CHECK_EQ(ring.available(), static_cast<size_t>(0));
    RTX_CHECK(ring.peekReadSlice() == nullptr);
    RTX_CHECK(ring.acquireWriteSlice() != nullptr);

    // Fill the ring to capacity (the ring keeps one slot free only if the
    // implementation chooses to; this one tracks read/write indices directly).
    for (size_t index = 0; index < 4; ++index) {
        float* slice = ring.acquireWriteSlice();
        RTX_REQUIRE(slice != nullptr);
        for (size_t sample = 0; sample < kAudioSliceSamples; ++sample) {
            slice[sample] = static_cast<float>(index) + static_cast<float>(sample) * 1e-4f;
        }
        ring.publishWriteSlice();
    }
    RTX_CHECK_EQ(ring.available(), static_cast<size_t>(4));
    // Full: the producer must be refused rather than overwriting unread data.
    RTX_CHECK(ring.acquireWriteSlice() == nullptr);

    const float* first = ring.peekReadSlice();
    RTX_REQUIRE(first != nullptr);
    RTX_CHECK_NEAR(first[0], 0.0, 1e-6);
    ring.releaseReadSlice();
    RTX_CHECK_EQ(ring.available(), static_cast<size_t>(3));

    const float* second = ring.peekReadSlice();
    RTX_REQUIRE(second != nullptr);
    RTX_CHECK_NEAR(second[0], 1.0, 1e-6);
    ring.releaseReadSlice();
    ring.releaseReadSlice();
    ring.releaseReadSlice();
    RTX_CHECK_EQ(ring.available(), static_cast<size_t>(0));
    RTX_CHECK(ring.peekReadSlice() == nullptr);
}

RTX_TEST(audio_pll_convergence) {
    AvSyncPll pll;
    constexpr double kSliceUs = static_cast<double>(kAudioSliceSamples) * 1e6 / 48000.0;
    double offset_us = 80000.0;  // the video clock starts 80 ms ahead

    // Warm up: the correction must saturate at +/-0.05 % and pull the error
    // down monotonically while it is clamped.  A positive offset means the
    // video clock is ahead, so a ratio > 1 makes the audio catch up and the
    // offset shrinks.
    double previous = offset_us;
    for (int step = 0; step < 1000; ++step) {
        pll.update(static_cast<int64_t>(offset_us));
        RTX_CHECK(pll.resampleRatio() <= 1.0005 + 1e-12);
        RTX_CHECK(pll.resampleRatio() >= 0.9995 - 1e-12);
        offset_us -= (pll.resampleRatio() - 1.0) * kSliceUs;
        RTX_CHECK(offset_us <= previous + 1e-6);
        previous = offset_us;
    }
    RTX_CHECK(offset_us < 80000.0);

    // Full convergence: 80 ms at 16.7 us per slice is ~4800 slices, plus the
    // geometric tail below the 500 us clamp boundary.
    for (int step = 0; step < 6000; ++step) {
        pll.update(static_cast<int64_t>(offset_us));
        offset_us -= (pll.resampleRatio() - 1.0) * kSliceUs;
    }
    RTX_CHECK(std::fabs(offset_us) < 200.0);
    RTX_CHECK(pll.locked());
    RTX_CHECK_NEAR(pll.resampleRatio(), 1.0, 1e-3);

    // A negative offset (audio ahead) drives the ratio below 1.0.
    AvSyncPll reverse;
    reverse.update(-60000);
    RTX_CHECK(reverse.resampleRatio() < 1.0);
    RTX_CHECK(reverse.resampleRatio() >= 0.9995);

    // reset() returns the loop to the nominal ratio.
    reverse.reset();
    RTX_CHECK_NEAR(reverse.resampleRatio(), 1.0, 1e-12);
    RTX_CHECK(!reverse.locked());
}
