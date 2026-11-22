// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
//
// Phase 11.3-S：零堆分配 AVX2+FMA 音频混音总线与多声道（1~8 ch）流式 WSOLA 状态机接口。
// 彻底拔除 VoicePrep 与 AudioMixerScratchpad 包装类，直接以 std::span 视图、
// 流式状态机 WsolaStreamState 与 noexcept 自由函数交付。

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

namespace rtx {

using AudioVoice = std::pair<std::span<const float>, float /*gain*/>;

void mixAvx2(std::span<const AudioVoice> voices, std::span<float> dst) noexcept;

inline constexpr std::size_t kWsolaMaxChannels = 8;
inline constexpr std::size_t kWsolaRingMaxFrames = 1024;
inline constexpr std::size_t kWsolaOverlapFrames = 256;

// @rtx-type-justification
// Category: NonTrivial-StateMachine
// Why-Std-Fails: 跨缓冲块保持 1~8 声道重叠相加尾迹进位、逻辑读头与 32 字节对齐环形分析窗，std 容器缺乏步长对齐与定容零堆分配环形进位语义。
// Invariant: last_speed > 0.0f; ring_head <= ring_fill <= kWsolaRingMaxFrames; tail_frames <= kWsolaOverlapFrames.
struct WsolaStreamState {
    alignas(32) std::array<float, kWsolaRingMaxFrames * kWsolaMaxChannels> analysis_ring{};
    alignas(32) std::array<float, kWsolaOverlapFrames * kWsolaMaxChannels> overlap_tail{};
    std::size_t ring_head{0};
    std::size_t ring_fill{0};
    std::size_t tail_frames{0};
    double src_phase{0.0};
    float last_speed{1.0f};
};

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
