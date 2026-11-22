// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
//
// Phase 11.3-R：零堆分配 AVX2+FMA 音频混音总线。
// 直接消费 std::span<const AudioVoice> 与 std::span<float>，热循环绝对零堆分配、
// 零空指针冗余判断，在 AVX2+FMA 目标上执行单指令融合乘加 _mm256_fmadd_ps。

#include "rtx/audio_mixer_avx2.h"

#include <algorithm>
#include <cstddef>

#if defined(__AVX2__) && (defined(_MSC_VER) || defined(__FMA__))
#include <immintrin.h>
#endif

namespace rtx {

void mixAvx2(std::span<const AudioVoice> voices, std::span<float> dst) noexcept {
    std::fill(dst.begin(), dst.end(), 0.0f);
    if (dst.empty() || voices.empty()) {
        return;
    }

    for (const auto& [samples, gain] : voices) {
        if (samples.empty() || gain == 0.0f) {
            continue;
        }
        const std::size_t n = std::min(samples.size(), dst.size());
        const float* s = samples.data();
        float* d = dst.data();
        std::size_t i = 0;

#if defined(__AVX2__) && (defined(_MSC_VER) || defined(__FMA__))
        const __m256 vgain = _mm256_set1_ps(gain);
        for (; i + 8 <= n; i += 8) {
            const __m256 acc = _mm256_loadu_ps(d + i);
            const __m256 src = _mm256_loadu_ps(s + i);
            _mm256_storeu_ps(d + i, _mm256_fmadd_ps(src, vgain, acc));
        }
#endif
        for (; i < n; ++i) {
            d[i] += s[i] * gain;
        }
    }
}

}  // namespace rtx
