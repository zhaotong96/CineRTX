// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
//
// Phase 11.3-S：AVX2+FMA 混音总线与流式 WSOLA 零分配及基频保真单元测试。
// 严格遵守红线：严禁全局替换 operator new/delete；通过栈定容数组 std::array<AudioVoice, 32>、
// 静态 noexcept 契约与 Win32 Debug CRT 局部作用域 _CrtSetAllocHook 实现零全局污染验证。

#include "test_framework.h"

#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <span>
#include <vector>

#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
#endif

#include "rtx/audio_mixer_avx2.h"

using namespace rtx;

namespace {

static_assert(noexcept(mixAvx2(std::span<const AudioVoice>{}, std::span<float>{})),
              "mixAvx2 must be strictly noexcept for real-time audio threads");
static_assert(noexcept(wsolaProcessStream(std::declval<WsolaStreamState&>(),
                                          std::span<const float>{}, std::span<float>{}, 1.0f, 2)),
              "wsolaProcessStream must be strictly noexcept for real-time audio threads");
static_assert(noexcept(wsolaStretch(std::span<const float>{}, std::span<float>{}, 2, 1.0)),
              "wsolaStretch must be strictly noexcept for real-time audio threads");

#if defined(_MSC_VER) && defined(_DEBUG)
std::atomic<int> g_scoped_heap_allocs{0};

int __cdecl scopedAllocHook(int alloc_type, void*, std::size_t, int, long,
                            const unsigned char*, int) {
    if (alloc_type == _HOOK_ALLOC || alloc_type == _HOOK_REALLOC) {
        g_scoped_heap_allocs.fetch_add(1, std::memory_order_relaxed);
    }
    return 1;
}
#endif

// 通过正向过零点线性插值测量单声道信号基频（Hz），用于验证 WSOLA 变速不变调精度。
double measureFundamentalHz(const std::vector<float>& interleaved, std::size_t channels,
                            double sample_rate_hz) {
    const std::size_t total_frames = interleaved.size() / channels;
    const std::size_t skip = std::min<std::size_t>(total_frames / 5, 2048);
    const std::size_t end = total_frames - skip;
    double first_cross = -1.0;
    double last_cross = -1.0;
    std::size_t cycles = 0;

    for (std::size_t f = skip + 1; f < end; ++f) {
        const float prev = interleaved[(f - 1) * channels];
        const float curr = interleaved[f * channels];
        if (prev <= 0.0f && curr > 0.0f) {
            const double frac = static_cast<double>(-prev) / static_cast<double>(curr - prev);
            const double exact_pos = static_cast<double>(f - 1) + frac;
            if (first_cross < 0.0) {
                first_cross = exact_pos;
            } else {
                last_cross = exact_pos;
                ++cycles;
            }
        }
    }
    if (cycles == 0 || last_cross <= first_cross) return 0.0;
    return static_cast<double>(cycles) * sample_rate_hz / (last_cross - first_cross);
}

}  // namespace

RTX_TEST(audio_mixer_avx2_fma_and_zero_alloc_hot_loop) {
    constexpr std::size_t kSamples = 131;  // 非 8 倍数，同时覆盖 AVX2 主循环与标量尾迹
    std::array<float, kSamples> src_a{};
    std::array<float, kSamples> src_b{};
    std::array<float, kSamples> dst{};

    for (std::size_t i = 0; i < kSamples; ++i) {
        src_a[i] = static_cast<float>(i % 17) * 0.125f - 1.0f;
        src_b[i] = static_cast<float>((i * 3) % 13) * 0.25f - 1.5f;
    }

    std::array<AudioVoice, 32> voice_slots{};
    voice_slots[0] = {std::span<const float>(src_a), 0.75f};
    voice_slots[1] = {std::span<const float>(src_b), 0.25f};
    const std::span<const AudioVoice> active_voices(voice_slots.data(), 2);

#if defined(_MSC_VER) && defined(_DEBUG)
    g_scoped_heap_allocs.store(0, std::memory_order_relaxed);
    const _CRT_ALLOC_HOOK prev_hook = _CrtSetAllocHook(&scopedAllocHook);
#endif
    for (int iter = 0; iter < 64; ++iter) {
        mixAvx2(active_voices, dst);
    }
#if defined(_MSC_VER) && defined(_DEBUG)
    _CrtSetAllocHook(prev_hook);
    const int allocs = g_scoped_heap_allocs.load(std::memory_order_relaxed);
    std::printf("             [ALLOC-HOOK] avx2 hot-loop allocations: %d\n", allocs);
    RTX_CHECK_EQ(allocs, 0);
#endif

    for (std::size_t i = 0; i < kSamples; ++i) {
        const float expected = src_a[i] * 0.75f + src_b[i] * 0.25f;
        RTX_CHECK_NEAR(dst[i], expected, 1e-5f);
    }

    // Empty voices must zero dst cleanly
    mixAvx2({}, dst);
    for (float sample : dst) {
        RTX_CHECK_NEAR(sample, 0.0f, 1e-7f);
    }
}

RTX_TEST(audio_wsola_stretch_mdspan_and_zero_alloc) {
    constexpr uint32_t kChannels = 2;
    constexpr std::size_t kInFrames = 2048;
    constexpr std::size_t kOutCapFrames = 4096;

    std::array<float, kInFrames * kChannels> input{};
    std::array<float, kOutCapFrames * kChannels> output{};
    for (std::size_t f = 0; f < kInFrames; ++f) {
        const float s = 0.5f * std::sin(2.0f * 3.14159265f * 220.0f *
                                        static_cast<float>(f) / 48000.0f);
        input[f * kChannels + 0] = s;
        input[f * kChannels + 1] = -s;
    }

    // Prime static Hann table once before allocation audit
    const std::size_t warmup = wsolaStretch(input, output, kChannels, 1.5);
    RTX_REQUIRE(warmup > 0);

#if defined(_MSC_VER) && defined(_DEBUG)
    g_scoped_heap_allocs.store(0, std::memory_order_relaxed);
    const _CRT_ALLOC_HOOK prev_hook = _CrtSetAllocHook(&scopedAllocHook);
#endif
    const std::size_t written_fast = wsolaStretch(input, output, kChannels, 1.5);
    const std::size_t written_slow = wsolaStretch(input, output, kChannels, 0.75);
    const std::size_t written_unit = wsolaStretch(input, output, kChannels, 1.0);
#if defined(_MSC_VER) && defined(_DEBUG)
    _CrtSetAllocHook(prev_hook);
    const int allocs = g_scoped_heap_allocs.load(std::memory_order_relaxed);
    std::printf("             [ALLOC-HOOK] wsola stretch allocations: %d\n", allocs);
    RTX_CHECK_EQ(allocs, 0);
#endif

    RTX_CHECK(written_fast > 0);
    RTX_CHECK(written_slow > written_fast);
    RTX_CHECK_EQ(written_unit, input.size());
    for (std::size_t i = 0; i < written_unit; ++i) {
        RTX_CHECK(std::isfinite(output[i]));
        RTX_CHECK_NEAR(output[i], input[i], 1e-6f);
    }
}

RTX_TEST(audio_wsola_stream_pitch_drift_under_0_05_percent) {
    constexpr double kSampleRate = 48000.0;
    constexpr double kTargetHz = 440.0;
    constexpr uint32_t kChannels = 2;
    constexpr std::size_t kChunkFrames = 512;
    constexpr std::size_t kTotalInFrames = 24000;  // 0.5 s @ 48 kHz

    std::vector<float> input(kTotalInFrames * kChannels, 0.0f);
    for (std::size_t f = 0; f < kTotalInFrames; ++f) {
        const float s = 0.6f * static_cast<float>(
            std::sin(2.0 * 3.14159265358979323846 * kTargetHz * static_cast<double>(f) / kSampleRate));
        input[f * kChannels + 0] = s;
        input[f * kChannels + 1] = s;
    }

    for (float speed : {0.25f, 0.5f, 1.5f, 2.0f}) {
        WsolaStreamState state{};
        std::vector<float> accumulated;
        accumulated.reserve(static_cast<std::size_t>(static_cast<double>(input.size()) / speed) + 4096);
        std::array<float, 4096 * kChannels> out_chunk{};

        for (std::size_t offset = 0; offset < kTotalInFrames; offset += kChunkFrames) {
            const std::size_t frames = std::min(kChunkFrames, kTotalInFrames - offset);
            const std::span<const float> in_span(input.data() + offset * kChannels, frames * kChannels);
            const std::size_t produced = wsolaProcessStream(state, in_span, out_chunk, speed, kChannels);
            for (std::size_t i = 0; i < produced; ++i) {
                RTX_CHECK(out_chunk[i] >= -1.0f && out_chunk[i] <= 1.0f);
            }
            accumulated.insert(accumulated.end(), out_chunk.begin(),
                               out_chunk.begin() + static_cast<std::ptrdiff_t>(produced));
        }

        RTX_REQUIRE(accumulated.size() > 4096 * kChannels);
        const double measured_hz = measureFundamentalHz(accumulated, kChannels, kSampleRate);
        const double drift_pct = std::abs(measured_hz - kTargetHz) / kTargetHz * 100.0;
        RTX_CHECK(drift_pct < 0.05);
    }
}

RTX_TEST(audio_wsola_multichannel_5_1_and_7_1_soft_limiter) {
    for (uint32_t ch : {6u, 8u}) {
        constexpr std::size_t kInFrames = 2048;
        constexpr std::size_t kOutCapFrames = 4096;
        std::vector<float> input(kInFrames * ch, 0.0f);
        std::vector<float> output(kOutCapFrames * ch, 0.0f);
        for (std::size_t f = 0; f < kInFrames; ++f) {
            const float s = 1.6f * std::sin(2.0f * 3.14159265f * 440.0f *
                                            static_cast<float>(f) / 48000.0f);
            for (uint32_t c = 0; c < ch; ++c) {
                input[f * ch + c] = (c % 2 == 0) ? s : -s;
            }
        }
        WsolaStreamState state{};
        const std::size_t written = wsolaProcessStream(state, input, output, 0.5f, ch);
        RTX_REQUIRE(written > 0);
        for (std::size_t i = 0; i < written; ++i) {
            RTX_CHECK(std::isfinite(output[i]));
            RTX_CHECK(output[i] > -1.0f && output[i] < 1.0f);
        }
    }
}
