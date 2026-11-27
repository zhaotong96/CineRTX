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
#include <cmath>
#include <ratio>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <stop_token>
#include <thread>
#include <vector>
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
    Result<void> openDevice();
    void launch();
    void callbackLoop(std::stop_token st) noexcept;
    void pumpDevice() noexcept;
    void publishClock() noexcept;
    void copyFromRing(float* dst, uint64_t read, uint32_t frames) const noexcept;
    void copyToRing(const float* src, uint64_t write, uint32_t frames) noexcept;
    Result<void> reopen();
    void releaseComHandles() noexcept;

    std::atomic<uint64_t> write_cursor_{0};
    uint8_t pad_write_cursor_[64 - sizeof(std::atomic<uint64_t>)]{};
    std::atomic<uint64_t> read_cursor_{0};
    uint8_t pad_read_cursor_[64 - sizeof(std::atomic<uint64_t>)]{};
    std::atomic<int64_t> anchor_{0};
    uint8_t pad_anchor_[64 - sizeof(std::atomic<int64_t>)]{};
    std::atomic<int64_t> published_flicks_{0};
    uint8_t pad_published_flicks_[64 - sizeof(std::atomic<int64_t>)]{};
    std::atomic<int64_t> published_qpc_{0};
    uint8_t pad_published_qpc_[64 - sizeof(std::atomic<int64_t>)]{};
    std::atomic<uint64_t> written_frames_{0};
    uint8_t pad_written_frames_[64 - sizeof(std::atomic<uint64_t>)]{};
    std::atomic<uint64_t> reconnects_{0};
    uint8_t pad_reconnects_[64 - sizeof(std::atomic<uint64_t>)]{};

    uint32_t sample_rate_ = 0;
    uint32_t channels_ = 0;
    uint32_t buffer_frames_ = 0;
    uint32_t ring_frames_ = 0;
    uint64_t clock_freq_ = 0;
    int64_t qpc_freq_ = 1;

    std::vector<float> ring_;
    std::atomic<bool> started_{false};
    std::atomic<bool> invalidated_{false};

    // Opaque Win32/COM handles owned and released in wasapi_audio_output.cpp (zero Windows header bleed)
    void* enumerator_ = nullptr;
    void* device_ = nullptr;
    void* client_ = nullptr;
    void* render_ = nullptr;
    void* clock_ = nullptr;
    void* buffer_event_ = nullptr;
    void* stop_event_ = nullptr;
    std::mutex control_;
    std::jthread worker_;
};

enum class PlaybackState : uint8_t { Idle = 0, Playing = 1, Paused = 2, Seeking = 3, Ended = 4 };

// Phase 12 (Wave 12.2-C): tuple alias (frame_rate, speed, decode_ahead, vram_budget_bytes, device_index, enable_audio).
// Tuple: (fps, speed, prefetch_depth, vram_budget, device_idx, enable_audio, null_sink, audio_gain)
using PlaybackOptions = std::tuple<Rational, double, uint32_t, uint64_t, int, bool, bool, float>;
[[nodiscard]] inline PlaybackOptions defaultPlaybackOptions() noexcept {
    return {Rational{24, 1}, 1.0, 5u, 0ull, 0, true, false, 1.0f};
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


// Wave C-2 (80_ Task C-2): Compile-time rational 23.976 -> 60.000 fps phase & WASAPI meltdown guard (+0 named types).
using Fps23976Duration = std::chrono::duration<int64_t, std::ratio<1001, 24000>>;
using Fps60000Duration = std::chrono::duration<int64_t, std::ratio<1, 60>>;

enum class PlaybackSyncState : uint8_t {
    Normal = 0,
    AudioDrift = 1,
    MeltdownBypass = 2,
};

// Tuple: (base_frame_index, pts_seconds, rife_timestep, is_synthetic, clock_state)
using FractionalPresentationInfo = std::tuple<int64_t, double, float, bool, PlaybackSyncState>;

[[nodiscard]] inline FractionalPresentationInfo computeFractionalFramePhase(
    int64_t target_display_ticks_60hz,
    int64_t video_pts_us,
    int64_t audio_clock_us,
    int64_t current_frame_idx = 0) noexcept {
    const int64_t delta_us = audio_clock_us - video_pts_us;
    const int64_t abs_delta = delta_us < 0 ? -delta_us : delta_us;
    PlaybackSyncState clk_state = PlaybackSyncState::Normal;
    if (abs_delta > 8000) {
        clk_state = PlaybackSyncState::AudioDrift;
        if (abs_delta > 32000) {
            // Severe A/V drift (>32ms): AI meltdown bypass, skip RIFE interpolation and hard-copy source frame.
            return {current_frame_idx, static_cast<double>(video_pts_us) * 1.0e-6,
                    0.0f, false, PlaybackSyncState::MeltdownBypass};
        }
    }
    const double source_frame_exact =
        static_cast<double>(target_display_ticks_60hz) * (24000.0 / 1001.0) / 60.0;
    const int64_t base_frame = static_cast<int64_t>(std::floor(source_frame_exact));
    const float alpha = static_cast<float>(source_frame_exact - static_cast<double>(base_frame));
    return {base_frame, source_frame_exact * (1001.0 / 24000.0), alpha, alpha > 0.001f, clk_state};
}

}  // namespace rtx
