// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "rtx/io.h"
#include "rtx/status.h"
#include "rtx/types.h"

namespace rtx {

// @rtx-type-justification
// Category: Domain-Model
// Why-Std-Fails: 多字段领域模型：具名字段承载序列化/校验/反射契约，压进 std::tuple 会丢失全部字段语义与默认值契约。
// Invariant: 字段集合与默认值即对外契约（schema/测试逐字段锚定）；只增不删。
struct CpuEncoderConfig {
    VideoCodec codec = VideoCodec::AV1;  // AV1 (libsvtav1) | HEVC (libx265) | H264 (libx264)
    int width = 3840;
    int height = 2160;
    Rational frame_rate{60, 1};
    int crf = 22;                        // 0..51 (SVT-AV1 / x265 / x264 CRF)
    int preset = 5;                      // SVT-AV1 preset (1..12) or x265 preset (0..9)
    int threads = 0;                     // 0 = auto
    Hdr10Metadata hdr{};
};

// @rtx-type-justification
// Category: Polymorphic-Boundary
// Why-Std-Fails: 抽象/跨层虚接口实现：std::function 单方法无法表达虚析构 + 多方法契约，实现与调用方跨层替换必须经此 is-a 边界。
// Invariant: 虚表契约稳定；实现方在构造/析构线程亲和与资源归还顺序上的不变量由接口注释约束。
class CpuVideoEncoder {
public:
    CpuVideoEncoder() = default;
    ~CpuVideoEncoder() noexcept;

    CpuVideoEncoder(const CpuVideoEncoder&) = delete;
    CpuVideoEncoder& operator=(const CpuVideoEncoder&) = delete;
    CpuVideoEncoder(CpuVideoEncoder&&) noexcept;
    CpuVideoEncoder& operator=(CpuVideoEncoder&&) noexcept;

    [[nodiscard]] static Result<CpuVideoEncoder> create(const CpuEncoderConfig& cfg);

// Accepts a host-staged P010 frame, converts/maps to YUV420P10LE via AVX2/YMM,
// attaches HDR10 / HDR10+ side data, and drains encoded packets.
    [[nodiscard]] Result<std::vector<Packet>> encodeP010Frame(
        const uint16_t* y_plane, size_t y_pitch_bytes,
        const uint16_t* uv_plane, size_t uv_pitch_bytes,
        int64_t pts_90k, bool force_keyframe,
        std::span<const uint8_t> hdr10plus_payload = {});

    [[nodiscard]] Result<std::vector<Packet>> flush();
    [[nodiscard]] std::span<const uint8_t> extradata() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace rtx
