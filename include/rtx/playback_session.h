// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
//
// Track 11C：实时播放平面（Phase 11.3-R Std-First 版）。
//
// 分层单向：Demuxer -> 有界 std::deque 预读 -> Flicks 自由函数时钟 ->
// 帧截止自由函数 -> IVideoFrameSink。本平面与离线 RemasterPipeline 严格分离：
// 后者优化吞吐，本平面的契约是呈现截止时间——过期视频帧直接丢弃，音频硬件
// 时钟绝不等待任何 GPU 推理。
//
// 线程拓扑（零跨线程音频锁）：video_worker（std::jthread）独占解复用器与
// NVDEC；audio_worker（std::jthread）独占 FFmpeg 解码链与变速尾迹，seek 经
// 携带递增 seek_epoch 的空包哨兵由 audio_worker 在单线程内自行复位；唯一保留
// 的互斥量只服务控制面唤醒与音频包队列背压。

#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <span>
#include <tuple>
#include <utility>

#include "rtx/io.h"
#include "rtx/status.h"
#include "rtx/telemetry_bus.h"
#include "rtx/time_model.h"
#include "shim/rtx_shim_abi.h"

namespace rtx {

// @rtx-type-justification
// Category: Polymorphic-Boundary
// Why-Std-Fails: std::function 无法表达「虚析构 + present + completedFence」的双方法虚接口；无头验证 Sink、DComp 视口与测试替身都必须经此抽象跨层替换。
// Invariant: present() 由会话 video_worker 线程调用；completedFence() 返回已完成 GPU 工作的最大退役令牌（同步 Sink 恒 UINT64_MAX = 立即退役），会话据此决定解码槽位何时归还 NVDEC 表面池。
class IVideoFrameSink {
public:
    virtual ~IVideoFrameSink() = default;
    virtual Result<void> present(const RtxFrameDesc& frame, int64_t deadline_flicks,
                                 uint64_t retire_fence) = 0;
    [[nodiscard]] virtual uint64_t completedFence() const noexcept { return UINT64_MAX; }
};

// @rtx-type-justification
// Category: Polymorphic-Boundary
// Why-Std-Fails: WasapiAudioOutput 与测试替身必须以同一虚接口注入时钟状态元组；std::function 双方法无法表达多态生命周期与 is-a 关系。
// Invariant: masterFlicks() 是音频硬件采样位置换算的权威时间线；running() 为 false 时软件时钟退化为 steady_clock 外推。
class IAudioMasterClock {
public:
    virtual ~IAudioMasterClock() = default;
    [[nodiscard]] virtual int64_t masterFlicks() const noexcept = 0;
    [[nodiscard]] virtual bool running() const noexcept = 0;
};

// Phase 11.3-R：压缩为 4 元组，以 anchor_tp == std::nullopt 编码暂停态，
// 消除 6 元组中易误用的 std::get<3/5> 魔法索引。
// 成员语义：{audio_master, anchor_steady_tp (nullopt = paused), base_flicks, speed}
using PlaybackClockState = std::tuple<IAudioMasterClock*,
                                      std::optional<std::chrono::steady_clock::time_point>,
                                      Flicks,
                                      double>;

[[nodiscard]] constexpr double clampPlaybackSpeed(double speed) noexcept {
    return speed > 0.0 ? std::clamp(speed, 0.25, 2.0) : 0.25;
}
inline void clockAnchor(PlaybackClockState& clock, Flicks target_flicks, double target_speed) noexcept {
    auto& [audio, anchor_tp, base_flicks, speed] = clock;
    (void)audio;
    anchor_tp = std::chrono::steady_clock::now();
    base_flicks = target_flicks;
    speed = target_speed;
}
[[nodiscard]] inline Flicks clockNow(const PlaybackClockState& clock) noexcept {
    const auto& [audio, anchor_tp, base_flicks, speed] = clock;
    if (audio != nullptr && audio->running()) {
        return Flicks{audio->masterFlicks()};
    }
    if (!anchor_tp.has_value()) {
        return base_flicks;
    }
    const auto elapsed = std::chrono::duration_cast<Flicks>(
        std::chrono::steady_clock::now() - *anchor_tp);
    const double scaled = static_cast<double>(elapsed.count()) * speed;
    return base_flicks + Flicks{static_cast<int64_t>(scaled)};
}
enum class FrameVerdict : uint8_t { Present = 0, Wait = 1, Drop = 2 };

[[nodiscard]] constexpr FrameVerdict classifyFrameDeadline(int64_t frame_flicks,
                                                           int64_t now_flicks,
                                                           int64_t tolerance_flicks) noexcept {
    if (now_flicks > frame_flicks + tolerance_flicks) {
        return FrameVerdict::Drop;
    }
    if (now_flicks < frame_flicks) {
        return FrameVerdict::Wait;
    }
    return FrameVerdict::Present;
}
[[nodiscard]] constexpr int64_t dropToleranceFlicks(int64_t frame_interval_flicks) noexcept {
    return frame_interval_flicks > 0 ? std::max<int64_t>(1, frame_interval_flicks / 2) : 1;
}
[[nodiscard]] constexpr int64_t waitDurationFlicks(int64_t frame_flicks,
                                                   int64_t now_flicks) noexcept {
    return frame_flicks > now_flicks ? frame_flicks - now_flicks : 0;
}
[[nodiscard]] uint32_t decodeAheadCapacity(int64_t frame_interval_flicks,
                                           int64_t decode_cost_flicks,
                                           uint64_t vram_used_bytes,
                                           uint64_t vram_budget_bytes,
                                           double speed) noexcept;

using DecodeAheadSlot = std::tuple<RtxFrameDesc, int64_t, uint64_t>;

inline constexpr uint32_t kDecodeAheadMaxSlots = 16;

[[nodiscard]] constexpr uint32_t clampDecodeAheadCapacity(uint32_t requested,
                                                          uint32_t max_slots) noexcept {
    return std::clamp(requested, 1u, max_slots == 0 ? 1u : max_slots);
}
inline bool pushDecodeSlot(std::deque<DecodeAheadSlot>& queue, uint32_t capacity,
                           const RtxFrameDesc& frame, int64_t deadline_flicks) noexcept {
    if (queue.size() >= capacity) {
        return false;
    }
    queue.emplace_back(frame, deadline_flicks, 0ULL);
    return true;
}
// @rtx-type-justification
// Category: NonTrivial-StateMachine
// Why-Std-Fails: preroll 生命周期（seek 下发 -> 拒帧 -> 命中即终态 -> cancel）跨帧维护目标点与跳过计数，std::tuple 无法表达状态迁移不变式。
// Invariant: 未命中 target 之前的帧一律被 accepting() 拒绝（只解码不呈现），命中后立即退出 preroll 态。
class GopSeekScheduler {
public:
    GopSeekScheduler(IDemuxer& demuxer, Rational video_time_base) noexcept;

    Result<void> seekTo(int64_t target_flicks);

    [[nodiscard]] bool accepting(int64_t frame_flicks) noexcept;

    void cancel() noexcept;
    [[nodiscard]] bool active() const noexcept { return active_; }
    [[nodiscard]] int64_t target() const noexcept { return target_; }
    [[nodiscard]] uint64_t prerollSkipped() const noexcept { return skipped_; }

private:
    IDemuxer* demuxer_;  // 非拥有
    Rational time_base_;
    int64_t target_ = 0;
    uint64_t skipped_ = 0;
    bool active_ = false;
};

// @rtx-type-justification
// Category: NonTrivial-StateMachine
// Why-Std-Fails: 协调 COM 设备对象、事件句柄、环形缓冲与回调 jthread 的异步生命周期与设备热拔插重连状态机。
// Invariant: 回调线程四条红线（零 I/O、零堆分配、零长锁、零同步 Fence Wait）；析构时先 join 回调线程再释放 COM 接口。
class WasapiAudioOutput final : public IAudioMasterClock {
public:
    static Result<std::unique_ptr<WasapiAudioOutput>> create();
    ~WasapiAudioOutput() override;

    WasapiAudioOutput(const WasapiAudioOutput&) = delete;
    WasapiAudioOutput& operator=(const WasapiAudioOutput&) = delete;

    Result<uint32_t> write(std::span<const float> interleaved);

    void anchor(int64_t base_flicks) noexcept;
    void pause() noexcept;
    void resume() noexcept;
    void flush() noexcept;

    [[nodiscard]] int64_t masterFlicks() const noexcept override;
    [[nodiscard]] bool running() const noexcept override;
    [[nodiscard]] uint32_t sampleRate() const noexcept;
    [[nodiscard]] uint32_t channels() const noexcept;
    [[nodiscard]] uint64_t writtenFrames() const noexcept;
    [[nodiscard]] uint64_t reconnects() const noexcept;

private:
    WasapiAudioOutput() = default;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

enum class PlaybackState : uint8_t { Idle = 0, Playing = 1, Paused = 2, Seeking = 3, Ended = 4 };

// @rtx-type-justification
// Category: Domain-Model
// Why-Std-Fails: 具名语义字段（帧率、倍速、预读、VRAM 预算、设备索引、音频开关）承载播放会话配置契约与默认值。
// Invariant: speed ∈ [0.25, 2.0]（create() 钳位）；decode_ahead ∈ [2, 8]。
struct PlaybackOptions {
    Rational frame_rate{24, 1};
    double speed = 1.0;              // 0.25x – 2.0x
    uint32_t decode_ahead = 5;       // 初始 4–6
    uint64_t vram_budget_bytes = 0;  // 0 = 不设预算
    int device_index = 0;
    bool enable_audio = true;
};

// @rtx-type-justification
// Category: NonTrivial-StateMachine
// Why-Std-Fails: 协调 NVDEC 解码器、双 jthread 线程拓扑、音频包队列与 WASAPI 主时钟的 PIMPL 播放会话宿主。
// Invariant: 解码器只由 video_worker 建/用/释（cuCtxSetCurrent 线程亲和）；析构顺序严格保证两 worker 先 join 再释放共享状态。
class MediaPlaybackSession {
public:
    static Result<std::unique_ptr<MediaPlaybackSession>> create(std::unique_ptr<IDemuxer> demuxer,
                                                                const MediaInfo& info,
                                                                IVideoFrameSink* sink,
                                                                PlaybackOptions options);
    ~MediaPlaybackSession();

    MediaPlaybackSession(const MediaPlaybackSession&) = delete;
    MediaPlaybackSession& operator=(const MediaPlaybackSession&) = delete;

    Result<void> play();
    void pause() noexcept;
    Result<void> seek(int64_t target_flicks);
    Result<void> stepFrame(int direction);
    void setSpeed(double speed) noexcept;

    [[nodiscard]] PlaybackState state() const noexcept;
    [[nodiscard]] int64_t playheadFlicks() const noexcept;
    [[nodiscard]] TelemetrySnapshot telemetry() const noexcept;
    [[nodiscard]] bool audioActive() const noexcept;

private:
    MediaPlaybackSession() = default;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace rtx
