// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
//
// Phase 11.3-S（WO-047 任务 2）：零每会话堆分配多声道（1~8 ch）流式 WSOLA 无变调变速拉伸器。
// 特性：
//   - 动态匹配 1~8 声道（单声道/立体声/5.1/7.1），彻底消除 3+ 声道静音缺陷；
//   - 采用逻辑读指针 ring_head 配合惰性 std::shift_left 紧缩，大幅减少 0.25x 慢放搬移开销；
//   - 静态 32 字节对齐共享汉宁窗 kHannTable + 抛物线子样本互相关插值 + C++23 std::mdspan；
//   - 真正平滑软拐点限幅器 softLimitSample（|x| <= 0.9 线性直通，超阈值 tanh 软膝压缩至 (-1, 1)）。

#include "rtx/audio_mixer_avx2.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>

#if defined(__has_include)
#if __has_include(<mdspan>)
#include <mdspan>
#endif
#endif

namespace rtx {
namespace {

inline constexpr std::size_t kWsolaWindow = 512;
inline constexpr std::size_t kWsolaHalf = kWsolaOverlapFrames;  // 256
inline constexpr std::size_t kWsolaSearch = 128;
inline constexpr double kTwoPi = 6.28318530717958647692;
inline constexpr float kSoftKneeThreshold = 0.9f;
inline constexpr float kSoftKneeHeadroom = 0.1f;

// 静态只读共享周期汉宁窗（一次初始化、全进程共享，零堆分配）。
const std::array<float, kWsolaWindow>& sharedHannTable() noexcept {
    alignas(32) static const std::array<float, kWsolaWindow> kHannTable = []() noexcept {
        std::array<float, kWsolaWindow> table{};
        for (std::size_t i = 0; i < kWsolaWindow; ++i) {
            const double phase = kTwoPi * static_cast<double>(i) / static_cast<double>(kWsolaWindow);
            table[i] = static_cast<float>(0.5 * (1.0 - std::cos(phase)));
        }
        return table;
    }();
    return kHannTable;
}

// 平滑软拐点限幅器：|x| <= 0.9 严格线性无失真，超出后经 tanh 渐近压缩至 (-1.0, 1.0)。
[[nodiscard]] inline float softLimitSample(float x) noexcept {
    const float ax = std::abs(x);
    if (ax <= kSoftKneeThreshold) {
        return x;
    }
    const float excess = (ax - kSoftKneeThreshold) / kSoftKneeHeadroom;
    const float shaped = kSoftKneeThreshold + kSoftKneeHeadroom * std::tanh(excess);
    return std::copysign(shaped, x);
}

[[nodiscard]] float corrAt(const float* buf, std::size_t ch_count,
                           const float* ref_unweighted, std::size_t cand) noexcept {
    float dot = 0.0f;
    float energy = 1e-8f;
    for (std::size_t f = 0; f < kWsolaHalf; ++f) {
        const float a = ref_unweighted[f * ch_count];
        const float b = buf[(cand + f) * ch_count];
        dot += a * b;
        energy += b * b;
    }
    return dot / std::sqrt(energy);
}

// 逐样本归一化互相关 + 抛物线子样本峰值插值，消除 48 kHz / 440 Hz 非整数周期量化漂移。
[[nodiscard]] std::pair<std::size_t, float> findBestOverlapStream(
    const float* buf, std::size_t ch_count, const float* ref_unweighted,
    std::size_t nominal_frame, std::size_t max_frame) noexcept {
    const std::size_t lo = nominal_frame > kWsolaSearch ? (nominal_frame - kWsolaSearch) : 0;
    const std::size_t hi = std::min(nominal_frame + kWsolaSearch, max_frame);
    std::size_t best_pos = nominal_frame;
    float best_score = -1.0e30f;

    for (std::size_t cand = lo; cand <= hi; ++cand) {
        const float score = corrAt(buf, ch_count, ref_unweighted, cand);
        if (score > best_score) {
            best_score = score;
            best_pos = cand;
        }
    }

    float frac = 0.0f;
    if (best_pos > lo && best_pos < hi) {
        const float s_prev = corrAt(buf, ch_count, ref_unweighted, best_pos - 1);
        const float s_next = corrAt(buf, ch_count, ref_unweighted, best_pos + 1);
        const float denom = 2.0f * (s_prev - 2.0f * best_score + s_next);
        if (std::abs(denom) > 1e-7f) {
            frac = std::clamp((s_prev - s_next) / denom, -0.5f, 0.5f);
        }
    }
    if (frac < 0.0f && best_pos > 0) {
        --best_pos;
        frac += 1.0f;
    }
    return {best_pos, frac};
}

// 惰性紧缩：仅在逻辑读头已过半或写空间不足时以 std::shift_left 搬移有效帧。
void compactRingIfNeeded(WsolaStreamState& state, std::size_t ch_count,
                         std::size_t max_ring_frames, bool force_for_write) noexcept {
    if (state.ring_head == 0) {
        return;
    }
    const bool past_half = state.ring_head >= (max_ring_frames / 2);
    const bool write_full = force_for_write && (state.ring_fill >= max_ring_frames);
    if (!past_half && !write_full) {
        return;
    }
    const std::size_t drop_samples = state.ring_head * ch_count;
    const std::size_t fill_samples = state.ring_fill * ch_count;
    std::shift_left(state.analysis_ring.begin(),
                    state.analysis_ring.begin() + static_cast<std::ptrdiff_t>(fill_samples),
                    static_cast<std::ptrdiff_t>(drop_samples));
    state.ring_fill -= state.ring_head;
    state.ring_head = 0;
}

}  // namespace

std::size_t wsolaProcessStream(WsolaStreamState& state,
                               std::span<const float> input,
                               std::span<float> output,
                               float speed_ratio,
                               uint32_t channels) noexcept {
    if (channels == 0 || channels > kWsolaMaxChannels || output.empty()) {
        return 0;
    }
    const std::size_t ch_count = static_cast<std::size_t>(channels);
    const std::size_t max_ring_frames = state.analysis_ring.size() / ch_count;
    const std::size_t max_tail_frames = state.overlap_tail.size() / ch_count;
    if (max_ring_frames < kWsolaWindow + 1 || max_tail_frames < kWsolaHalf) {
        return 0;
    }

    const float clamped_speed = std::clamp(speed_ratio, 0.25f, 2.0f);
    state.last_speed = clamped_speed;

    const std::size_t in_frames = input.size() / ch_count;
    const std::size_t out_cap_frames = output.size() / ch_count;
    if (out_cap_frames == 0) {
        return 0;
    }

    // 1.0x 直通路径：零延迟、平滑软限幅保护。
    const std::size_t avail_init = state.ring_fill - state.ring_head;
    if (std::abs(clamped_speed - 1.0f) < 1e-5f && avail_init == 0 && state.tail_frames == 0) {
        const std::size_t copy_frames = std::min(in_frames, out_cap_frames);
        const std::size_t copy_samples = copy_frames * ch_count;
        for (std::size_t i = 0; i < copy_samples; ++i) {
            output[i] = softLimitSample(input[i]);
        }
        return copy_samples;
    }

    const auto& hann = sharedHannTable();
    const double analysis_hop = static_cast<double>(kWsolaHalf) * static_cast<double>(clamped_speed);

    std::size_t in_consumed = 0;
    std::size_t out_frames = 0;

    while (in_consumed < in_frames || (state.ring_fill - state.ring_head) >= kWsolaWindow + 1) {
        if (in_consumed < in_frames) {
            compactRingIfNeeded(state, ch_count, max_ring_frames, /*force_for_write=*/true);
            if (state.ring_fill < max_ring_frames) {
                const std::size_t can_copy = std::min(in_frames - in_consumed,
                                                      max_ring_frames - state.ring_fill);
                std::copy_n(input.data() + in_consumed * ch_count,
                            can_copy * ch_count,
                            state.analysis_ring.data() + state.ring_fill * ch_count);
                state.ring_fill += can_copy;
                in_consumed += can_copy;
            }
        }

        const std::size_t active_frames = state.ring_fill - state.ring_head;
        if (active_frames < kWsolaWindow + 1 || out_frames + kWsolaHalf > out_cap_frames) {
            break;
        }

        const float* active_ptr = state.analysis_ring.data() + state.ring_head * ch_count;
        const std::size_t max_start = active_frames - kWsolaWindow - 1;
        const std::size_t nominal = std::min(static_cast<std::size_t>(std::max(0.0, state.src_phase)),
                                             max_start);

        const auto [chosen, frac] = (state.tail_frames == kWsolaHalf)
            ? findBestOverlapStream(active_ptr, ch_count, state.overlap_tail.data(), nominal, max_start)
            : std::pair<std::size_t, float>{nominal, 0.0f};
        const float inv_frac = 1.0f - frac;

#if defined(__cpp_lib_mdspan) && __cpp_lib_mdspan >= 202207L
        using ConstAudioView = std::mdspan<const float, std::dextents<std::size_t, 2>>;
        using AudioView = std::mdspan<float, std::dextents<std::size_t, 2>>;
        ConstAudioView ring_view(active_ptr, active_frames, ch_count);
        AudioView tail_view(state.overlap_tail.data(), kWsolaHalf, ch_count);
        AudioView out_view(output.data(), out_cap_frames, ch_count);

        for (std::size_t f = 0; f < kWsolaHalf; ++f) {
            const float w_in = hann[f];
            const float w_out = hann[f + kWsolaHalf];
            for (std::size_t c = 0; c < ch_count; ++c) {
                const float prev = (state.tail_frames == kWsolaHalf) ? (tail_view[f, c] * w_out) : 0.0f;
                const float curr_s = ring_view[chosen + f, c] * inv_frac +
                                     ring_view[chosen + f + 1, c] * frac;
                const float next_s = ring_view[chosen + kWsolaHalf + f, c] * inv_frac +
                                     ring_view[chosen + kWsolaHalf + f + 1, c] * frac;
                out_view[out_frames + f, c] = softLimitSample(prev + curr_s * w_in);
                tail_view[f, c] = next_s;
            }
        }
#else
        for (std::size_t f = 0; f < kWsolaHalf; ++f) {
            const float w_in = hann[f];
            const float w_out = hann[f + kWsolaHalf];
            for (std::size_t c = 0; c < ch_count; ++c) {
                const std::size_t out_idx = (out_frames + f) * ch_count + c;
                const std::size_t tail_idx = f * ch_count + c;
                const float prev = (state.tail_frames == kWsolaHalf)
                    ? (state.overlap_tail[tail_idx] * w_out) : 0.0f;
                const float curr_s = active_ptr[(chosen + f) * ch_count + c] * inv_frac +
                                     active_ptr[(chosen + f + 1) * ch_count + c] * frac;
                const float next_s = active_ptr[(chosen + kWsolaHalf + f) * ch_count + c] * inv_frac +
                                     active_ptr[(chosen + kWsolaHalf + f + 1) * ch_count + c] * frac;
                output[out_idx] = softLimitSample(prev + curr_s * w_in);
                state.overlap_tail[tail_idx] = next_s;
            }
        }
#endif
        state.tail_frames = kWsolaHalf;
        out_frames += kWsolaHalf;
        state.src_phase += analysis_hop;

        // 仅推进逻辑读头 ring_head，避免每次 hop 全量左移搬运。
        if (state.src_phase > static_cast<double>(kWsolaSearch)) {
            const std::size_t drop = std::min(
                static_cast<std::size_t>(state.src_phase - static_cast<double>(kWsolaSearch)),
                state.ring_fill - state.ring_head);
            state.ring_head += drop;
            state.src_phase -= static_cast<double>(drop);
            compactRingIfNeeded(state, ch_count, max_ring_frames, /*force_for_write=*/false);
        }

        const std::size_t remaining = state.ring_fill - state.ring_head;
        if (in_consumed >= in_frames &&
            state.src_phase > static_cast<double>(remaining > kWsolaWindow + 1 ? remaining - kWsolaWindow - 1 : 0)) {
            break;
        }
    }

    return out_frames * ch_count;
}

std::size_t wsolaStretch(std::span<const float> input, std::span<float> output,
                         uint32_t channels, double speed) noexcept {
    if (channels == 0 || channels > kWsolaMaxChannels || input.empty() || output.empty()) {
        return 0;
    }
    WsolaStreamState state{};
    return wsolaProcessStream(state, input, output, static_cast<float>(speed), channels);
}

}  // namespace rtx
