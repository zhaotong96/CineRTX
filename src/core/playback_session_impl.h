// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
// Phase 12 (Wave 12.2-B2): eliminated nested struct Control
// by flattening control-plane primitives directly into MediaPlaybackSession::Impl.

#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

#include "rtx/flicks.h"
#include "rtx/logger.h"
#include "rtx/nvidia_shim.h"
#include "rtx/playback_session.h"
#include "rtx/raii_owners.h"
#include "rtx/rational.h"

namespace rtx {

inline constexpr size_t kAudioQueueCapacity = 64;

inline void closeNvidiaShim(NvidiaShim* shim) noexcept {
    if (shim != nullptr) shim->close();
}
using ShimPtr = std::unique_ptr<NvidiaShim, FnDeleter<&closeNvidiaShim>>;
#if defined(__cpp_lib_move_only_function) && __cpp_lib_move_only_function >= 202110L
using DecoderPtr = std::unique_ptr<RtxDecoder, std::move_only_function<void(RtxDecoder*)>>;
#else
using DecoderPtr = std::unique_ptr<RtxDecoder, std::function<void(RtxDecoder*)>>;
#endif

enum class Request : uint8_t { None = 0, Seek = 1, StepForward = 2, StepBackward = 3 };

// @rtx-type-justification
// Category: NonTrivial-StateMachine
// Why-Std-Fails: 协调 NVDEC 解码器、双 jthread 拓扑、音频包队列、控制面条件变量与 WASAPI 主时钟的异步生命周期宿主。
// Invariant: 声明序即析构逆序——两个 worker 最后声明、最先 join；解码器仅由 video worker 建/用/释；ctl_* 字段仅在持 ctl_mutex 时读写。
struct MediaPlaybackSession::Impl {
    PlaybackOptions options{};
    MediaInfo info{};
    RtxDecoderConfig decoder_config{};
    std::unique_ptr<FfmpegDemuxer> demuxer;
    VideoFrameSink sink{};
    ShimPtr shim;
    DecoderPtr decoder;
#if defined(_WIN32)
    std::unique_ptr<WasapiAudioOutput> audio;
    AudioTrackInfo audio_track{};
#endif

    GopSeekScheduler seek;
    PlaybackClockState clock{AudioMasterClockFn{}, std::nullopt, Flicks{0}, 1.0};
    AtomicTelemetryBus telemetry;
    int64_t frame_interval = static_cast<int64_t>(kFlicksPerSecond) / 24;
    int64_t drop_tolerance = dropToleranceFlicks(frame_interval);
    uint32_t decode_capacity = 4;
    std::deque<DecodeAheadSlot> decode_queue;
    std::atomic<int64_t> playhead{0};
    uint64_t retire_sequence = 0;
    uint64_t frames_decoded = 0;
    uint64_t frames_rendered = 0;
    uint64_t frames_dropped = 0;
    uint64_t epoch = 0;
    bool front_presented = false;
    bool end_of_stream = false;
    bool decoder_exhausted = false;
    bool decoder_ready = false;
    double applied_speed = 1.0;
    int64_t last_video_pts90k = std::numeric_limits<int64_t>::min();
    std::chrono::steady_clock::time_point fps_window{};
    uint64_t fps_window_frames = 0;
    uint32_t measured_fps_x100 = 0;
    PlaybackSyncState clock_state = PlaybackSyncState::Normal;
    bool temporal_prev_valid = false;
#if defined(_WIN32)
    std::atomic<bool> audio_ready{false};
#endif

    std::mutex audio_queue_mutex;
    std::condition_variable_any audio_queue_ready;
    std::condition_variable_any audio_queue_space;
    std::deque<std::pair<uint64_t, Packet>> audio_queue;

    // Flattened control-plane state (all guarded by ctl_mutex).
    std::mutex ctl_mutex;
    std::condition_variable_any ctl_cv;
    std::condition_variable_any ctl_done;
    PlaybackState ctl_state = PlaybackState::Idle;
    Request ctl_request = Request::None;
    int64_t ctl_seek_target = 0;
    uint64_t ctl_seek_generation = 0;
    uint64_t ctl_seek_completed = 0;
    bool ctl_resume_after_seek = true;
    double ctl_speed = 1.0;
    Error ctl_last_error{ErrorCode::Ok, {}};

    std::jthread audio_worker;
    std::jthread video_worker;

    Impl(std::unique_ptr<FfmpegDemuxer> demuxer_in, const MediaInfo& media, VideoFrameSink sink_in,
         PlaybackOptions opts)
        : options(opts), info(media), demuxer(std::move(demuxer_in)), sink(std::move(sink_in)),
          seek(makeGopSeekScheduler(*demuxer, info.time_base)) {}

    [[nodiscard]] uint64_t sinkCompletedFence() const noexcept {
        return sink.second ? sink.second() : UINT64_MAX;
    }

    void fail(ErrorCode code, const std::string& message, const std::string& detail = {});
    void setClockPaused(bool paused) noexcept;
    Result<void> recreateDecoder();
    void releaseAll() noexcept;
    void resetTemporalBuffers() noexcept {
        temporal_prev_valid = false;
        clock_state = PlaybackSyncState::Normal;
    }
    void retirePresented() noexcept;
    void drainDecoder();
    void fillQueue(std::stop_token stop);
    void presentFront();
    void publishTelemetry();
    void waitFor(std::stop_token stop, int64_t flicks);
    void pumpOnce(std::stop_token stop);
    Result<void> performSeek(int64_t target_flicks, std::stop_token stop);
    void reportSeekDone(bool resume);
    void bootstrapDecoder();
    void videoLoop(std::stop_token stop);
    void audioLoop(std::stop_token stop);
};

}  // namespace rtx
