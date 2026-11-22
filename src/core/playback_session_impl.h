// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors

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
    if (shim != nullptr) {
        shim->close();
    }
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
// Why-Std-Fails: 协调 NVDEC 解码器、双 jthread 拓扑、音频包队列与 WASAPI 主时钟的异步生命周期宿主（类级声明见 playback_session.h；Impl 即宿主本体）。
// Invariant: 声明序即析构逆序——两个 worker 最后声明、最先 join；解码器仅由 video worker 建/用/释；音频解码链与变速尾迹仅由 audio worker 触达；音频包队列元素 <0> 为 seek 纪元（空 data 哨兵包触发 audio worker 单线程自复位）。
struct MediaPlaybackSession::Impl {
    PlaybackOptions options{};
    MediaInfo info{};
    RtxDecoderConfig decoder_config{};
    std::unique_ptr<IDemuxer> demuxer;
    IVideoFrameSink* sink = nullptr;  // 非拥有
    ShimPtr shim;
    DecoderPtr decoder;  // 空持有者：CUDA 当前上下文线程局部，解码器由使用线程创建
#if defined(_WIN32)
    std::unique_ptr<WasapiAudioOutput> audio;
    AudioTrackInfo audio_track{};
#endif

    GopSeekScheduler seek;
    PlaybackClockState clock{nullptr, std::nullopt, Flicks{0}, 1.0};
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
    uint64_t epoch = 0;  // 仅由 video worker 触达（seek/呈现同线程）
    bool front_presented = false;
    bool end_of_stream = false;
    bool decoder_exhausted = false;
    bool decoder_ready = false;  // 由 ctl.mutex 保护；create() 据此同步等待 worker 建好 NVDEC
    double applied_speed = 1.0;
    int64_t last_video_pts90k = std::numeric_limits<int64_t>::min();
    std::chrono::steady_clock::time_point fps_window{};
    uint64_t fps_window_frames = 0;
    uint32_t measured_fps_x100 = 0;
#if defined(_WIN32)
    std::atomic<bool> audio_ready{false};  // audio worker 开链成功后置位，video worker 据此投递
#endif

    std::mutex audio_queue_mutex;
    std::condition_variable_any audio_queue_ready;
    std::condition_variable_any audio_queue_space;
    std::deque<std::pair<uint64_t, Packet>> audio_queue;

    // @rtx-type-justification
    // Category: Concurrency-Invariant
    // Why-Std-Fails: 封装控制面互斥量、条件变量与 seek 世代同步状态，std::tuple 无法表达持锁不变式与唤醒协议。
    // Invariant: 所有字段仅在持有 ctl.mutex 时读写；seek_completed 单调追平 seek_generation 后通过 done 唤醒调用方。
    struct Control {
        std::mutex mutex;
        std::condition_variable_any cv;
        std::condition_variable_any done;
        PlaybackState state = PlaybackState::Idle;
        Request request = Request::None;
        int64_t seek_target = 0;
        uint64_t seek_generation = 0;
        uint64_t seek_completed = 0;
        bool resume_after_seek = true;
        double speed = 1.0;
        Error last_error{ErrorCode::Ok, {}};
    } ctl;

    std::jthread audio_worker;
    std::jthread video_worker;

    Impl(std::unique_ptr<IDemuxer> demuxer_in, const MediaInfo& media, IVideoFrameSink* sink_in,
         PlaybackOptions opts)
        : options(opts), info(media), demuxer(std::move(demuxer_in)), sink(sink_in),
          seek(*demuxer, info.time_base) {}

    void fail(ErrorCode code, const std::string& message, const std::string& detail = {});
    void setClockPaused(bool paused) noexcept;
    Result<void> recreateDecoder();
    void releaseAll() noexcept;
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
