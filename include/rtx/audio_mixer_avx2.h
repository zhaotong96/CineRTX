// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
//
// Phase 11.3-S / Wave 12.2-D：零堆分配 AVX2+FMA 音频混音总线与多声道（1~8 ch）流式 WSOLA 状态机接口。
// 以 std::span 视图、标准 std::tuple 状态别名 WsolaStreamState 与 noexcept 自由函数交付。

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <tuple>
#include <utility>

namespace rtx {

using AudioVoice = std::pair<std::span<const float>, float /*gain*/>;

void mixAvx2(std::span<const AudioVoice> voices, std::span<float> dst) noexcept;

inline constexpr std::size_t kWsolaMaxChannels = 8;
inline constexpr std::size_t kWsolaRingMaxFrames = 1024;
inline constexpr std::size_t kWsolaOverlapFrames = 256;

// Phase 12 (Wave 12.2-D): standard tuple alias replaces WsolaStreamState wrapper.
// Layout: (analysis_ring, overlap_tail, ring_head, ring_fill, tail_frames, src_phase, last_speed).
using WsolaStreamState = std::tuple<
    std::array<float, kWsolaRingMaxFrames * kWsolaMaxChannels>,
    std::array<float, kWsolaOverlapFrames * kWsolaMaxChannels>,
    std::size_t,
    std::size_t,
    std::size_t,
    double,
    float>;

// 流式无变调时间伸缩（支持 1~8 声道跨缓冲块零堆分配连续推流，默认立体声 2 声道）。
// 返回写入 output 的交错浮点样本总数（frames * channels）。
[[nodiscard]] std::size_t wsolaProcessStream(WsolaStreamState& state,
                                             std::span<const float> input,
                                             std::span<float> output,
                                             float speed_ratio,
                                             uint32_t channels = 2) noexcept;

// 一次性整块无堆分配 WSOLA 变速拉伸（内部委托静态共享汉宁窗与 C++23 std::mdspan）。
[[nodiscard]] std::size_t wsolaStretch(std::span<const float> input, std::span<float> output,
                                       uint32_t channels, double speed) noexcept;

}  // namespace rtx
