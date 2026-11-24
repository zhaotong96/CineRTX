// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
//
// Track 11C：实时播放平面（Phase 12 Wave 12.2-B2 Std-First 版）。
// 物理拔除 IVideoFrameSink / IAudioMasterClock / IDemuxer 虚基类，
// 改用 std::function / std::tuple 与具体 FfmpegDemuxer。

#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
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

// Video frame sink callbacks: (present(frame, deadline_flicks, retire_fence), completedFence()).
// If completedFence is empty, frames retire immediately (UINT64_MAX).
using VideoFrameSink = std::pair<
    std::function<Result<void>(const RtxFrameDesc&, int64_t, uint64_t)>,
    std::function<uint64_t()>>;

// Audio master clock callback: returns hardware masterFlicks() when running, std::nullopt otherwise.
using AudioMasterClockFn = std::function<std::optional<int64_t>()>;

// 成员语义：{audio_master_fn, anchor_steady_tp (nullopt = paused), base_flicks, speed}
using PlaybackClockState = std::tuple<AudioMasterClockFn,
                                      std::optional<std::chrono::steady_clock::time_point>,
                                      Flicks,
                                      double>;

[[nodiscard]] constexpr double clampPlaybackSpeed(double speed) noexcept {
    return speed > 0.0 ? std::clamp(speed, 0.25, 2.0) : 0.25;
}

inline void clockAnchor(PlaybackClockState& clock, Flicks target_flicks, double target_speed) noexcept {
    auto& [audio_fn, anchor_tp, base_flicks, speed] = clock;
    (void)audio_fn;
    anchor_tp = std::chrono::steady_clock::now();
    base_flicks = target_flicks;
    speed = target_speed;
}

[[nodiscard]] inline Flicks clockNow(const PlaybackClockState& clock) noexcept {
    const auto& [audio_fn, anchor_tp, base_flicks, speed] = clock;
    if (audio_fn) {
        if (const auto master = audio_fn(); master.has_value()) {
            return Flicks{*master};
        }
    }
    if (!anchor_tp.has_value()) {
        return base_flicks;
    }
    const auto elapsed = std::chrono::duration_cast<Flicks>(
        std::chrono::steady_clock::now() - *anchor_tp);
    return base_flicks + Flicks{static_cast<int64_t>(static_cast<double>(elapsed.count()) * speed)};
}

enum class FrameVerdict : uint8_t { Present = 0, Wait = 1, Drop = 2 };

[[nodiscard]] constexpr FrameVerdict classifyFrameDeadline(int64_t frame_flicks,
                                                           int64_t now_flicks,
                                                           int64_t tolerance_flicks) noexcept {
    if (now_flicks > frame_flicks + tolerance_flicks) return FrameVerdict::Drop;
    if (now_flicks < frame_flicks) return FrameVerdict::Wait;
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
    if (queue.size() >= capacity) return false;
    queue.emplace_back(frame, deadline_flicks, 0ULL);
    return true;
}

// Phase 12 (Wave 12.2-C): tuple state (seek_fn, time_base, target, skipped, active) + free functions.
using GopSeekScheduler = std::tuple<std::function<Result<void>(int64_t)>, Rational, int64_t, uint64_t, bool>;

[[nodiscard]] GopSeekScheduler makeGopSeekScheduler(FfmpegDemuxer& demuxer, Rational video_time_base);
[[nodiscard]] GopSeekScheduler makeGopSeekScheduler(std::function<Result<void>(int64_t)> seek_fn,
                                                    Rational video_time_base) noexcept;
[[nodiscard]] Result<void> gopSeekTo(GopSeekScheduler& sched, int64_t target_flicks);
[[nodiscard]] bool gopSeekAccepting(GopSeekScheduler& sched, int64_t frame_flicks) noexcept;
void gopSeekCancel(GopSeekScheduler& sched) noexcept;
[[nodiscard]] inline bool gopSeekActive(const GopSeekScheduler& sched) noexcept {
    const auto& [fn, tb, tgt, skip, act] = sched;
    (void)fn; (void)tb; (void)tgt; (void)skip;
    return act;
}
[[nodiscard]] inline int64_t gopSeekTarget(const GopSeekScheduler& sched) noexcept {
    const auto& [fn, tb, tgt, skip, act] = sched;
    (void)fn; (void)tb; (void)skip; (void)act;
    return tgt;
}
[[nodiscard]] inline uint64_t gopSeekPrerollSkipped(const GopSeekScheduler& sched) noexcept {
    const auto& [fn, tb, tgt, skip, act] = sched;
    (void)fn; (void)tb; (void)tgt; (void)act;
    return skip;
}

// @rtx-type-justification
// Category: NonTrivial-StateMachine
// Why-Std-Fails: 协调 COM 设备对象、事件句柄、环形缓冲与回调 jthread 的异步生命周期与设备热拔插重连状态机。
// Invariant: 回调线程四条红线（零 I/O、零堆分配、零长锁、零同步 Fence Wait）；析构时先 join 回调线程再释放 COM 接口。
class WasapiAudioOutput final {
public:
    static Result<std::unique_ptr<WasapiAudioOutput>> create();
    ~WasapiAudioOutput();

    WasapiAudioOutput(const WasapiAudioOutput&) = delete;
    WasapiAudioOutput& operator=(const WasapiAudioOutput&) = delete;

    Result<uint32_t> write(std::span<const float> interleaved);

    void anchor(int64_t base_flicks) noexcept;
    void pause() noexcept;
    void resume() noexcept;
    void flush() noexcept;

    [[nodiscard]] int64_t masterFlicks() const noexcept;
    [[nodiscard]] bool running() const noexcept;
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

// Phase 12 (Wave 12.2-C): tuple alias (frame_rate, speed, decode_ahead, vram_budget_bytes, device_index, enable_audio).
using PlaybackOptions = std::tuple<Rational, double, uint32_t, uint64_t, int, bool>;
[[nodiscard]] inline PlaybackOptions defaultPlaybackOptions() noexcept {
    return {Rational{24, 1}, 1.0, 5u, 0ull, 0, true};
}

// @rtx-type-justification
// Category: NonTrivial-StateMachine
// Why-Std-Fails: 协调 NVDEC 解码器、双 jthread 线程拓扑、音频包队列与 WASAPI 主时钟的 PIMPL 播放会话宿主。
// Invariant: 解码器只由 video_worker 建/用/释（cuCtxSetCurrent 线程亲和）；析构顺序严格保证两 worker 先 join 再释放共享状态。
class MediaPlaybackSession {
public:
    static Result<std::unique_ptr<MediaPlaybackSession>> create(std::unique_ptr<FfmpegDemuxer> demuxer,
                                                                const MediaInfo& info,
                                                                VideoFrameSink sink,
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
