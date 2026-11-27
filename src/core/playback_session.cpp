// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors

#include "playback_session_impl.h"

#include <algorithm>

#include "rtx/flicks.h"
#include "rtx/status.h"

namespace rtx {

namespace {

int32_t shimCodec(VideoCodec codec) noexcept {
    switch (codec) {
        case VideoCodec::H264: return RTX_CODEC_H264;
        case VideoCodec::HEVC: return RTX_CODEC_HEVC;
        case VideoCodec::AV1: return RTX_CODEC_AV1;
        default: return RTX_CODEC_UNKNOWN;
    }
}

template <typename ImplT>
Result<void> dispatchControlRequest(ImplT& impl, Request req, int64_t target,
                                    bool resume, const char* timeout_msg) {
    std::unique_lock<std::mutex> lock(impl.ctl_mutex);
    impl.ctl_request = req;
    if (req == Request::Seek) {
        impl.ctl_seek_target = std::max<int64_t>(0, target);
        impl.ctl_resume_after_seek = (impl.ctl_state == PlaybackState::Playing);
    } else {
        impl.ctl_resume_after_seek = resume;
    }
    impl.ctl_state = PlaybackState::Seeking;
    const uint64_t generation = ++impl.ctl_seek_generation;
    impl.ctl_last_error = Error{ErrorCode::Ok, {}};
    impl.ctl_cv.notify_all();
    if (!impl.ctl_done.wait_for(lock, std::chrono::seconds(10),
                                [&] { return impl.ctl_seek_completed >= generation; })) {
        return err(ErrorCode::Timeout, timeout_msg);
    }
    if (impl.ctl_last_error.code != ErrorCode::Ok) {
        return std::unexpected(impl.ctl_last_error);
    }
    return {};
}

}  // namespace

uint32_t decodeAheadCapacity(int64_t frame_interval_flicks, int64_t decode_cost_flicks,
                             uint64_t vram_used_bytes, uint64_t vram_budget_bytes,
                             double speed) noexcept {
    uint32_t capacity = 4;
    if (frame_interval_flicks > 0 && decode_cost_flicks > 0) {
        const int64_t ratio = (decode_cost_flicks + frame_interval_flicks - 1) / frame_interval_flicks;
        capacity = static_cast<uint32_t>(std::clamp<int64_t>(ratio + 3, 4, 6));
    }
    if (speed > 1.0) capacity = std::min<uint32_t>(6, capacity + 1);
    if (vram_budget_bytes > 0 && vram_used_bytes > vram_budget_bytes * 82 / 100) capacity = 2;
    return capacity;
}

void MediaPlaybackSession::Impl::fail(ErrorCode code, const std::string& message,
                                      const std::string& detail) {
    {
        std::lock_guard<std::mutex> lock(ctl_mutex);
        ctl_last_error = Error{code, message, detail};
    }
    logError("playback: " + message + (detail.empty() ? "" : " [" + detail + "]"));
}

Result<std::unique_ptr<MediaPlaybackSession>> MediaPlaybackSession::create(
    std::unique_ptr<FfmpegDemuxer> demuxer, const MediaInfo& info, VideoFrameSink sink,
    PlaybackOptions options) {
    if (!demuxer || !sink.first) {
        return err(ErrorCode::InvalidArgument, "playback session requires a demuxer and a sink");
    }
    if (info.codec == VideoCodec::Unknown || info.width <= 0 || info.height <= 0) {
        return err(ErrorCode::Unsupported, "playable video stream not found");
    }
    ShimPtr shim(new NvidiaShim());
    std::string error;
    if (!shim->open(&error)) {
        return err(ErrorCode::DllMissing, "rtx_shim unavailable", error);
    }
    RtxDecoderConfig config{};
    config.codec = shimCodec(info.codec);
    config.width = info.width;
    config.height = info.height;
    config.bit_depth = info.bit_depth;
    config.output_format = info.bit_depth > 8 ? RTX_PIXEL_P010 : RTX_PIXEL_NV12;
    config.clock_rate = 90000;
    config.max_surfaces = 32;
    config.pool_slots = 16;
    auto& [opt_fps, opt_speed, opt_ahead, opt_vram, opt_dev, opt_audio, opt_null_sink, opt_gain] = options;
    (void)opt_null_sink; (void)opt_gain;
    (void)opt_vram;
    config.device_index = opt_dev;

    opt_speed = std::clamp(opt_speed, 0.25, 2.0);
    opt_ahead = std::clamp<uint32_t>(opt_ahead, 2, 8);
    auto session = std::unique_ptr<MediaPlaybackSession>(new MediaPlaybackSession());
    session->impl_ = std::make_unique<Impl>(std::move(demuxer), info, std::move(sink), options);
    Impl& impl = *session->impl_;
    impl.decoder_config = config;
    impl.shim = std::move(shim);
    impl.decode_capacity = std::min<uint32_t>(4u, opt_ahead);

    const Rational rate =
        info.frame_rate.valid() && info.frame_rate.num > 0 ? info.frame_rate : opt_fps;
    impl.frame_interval = std::max<int64_t>(
        1, static_cast<int64_t>(kFlicksPerSecond) * rate.den / rate.num);
    impl.drop_tolerance = dropToleranceFlicks(impl.frame_interval);
    impl.applied_speed = opt_speed;
    {
        auto& [audio_clk, anchor_tp, base_flicks, clk_speed] = impl.clock;
        (void)audio_clk; (void)anchor_tp; (void)base_flicks;
        clk_speed = opt_speed;
    }
    {
        std::lock_guard<std::mutex> lock(impl.ctl_mutex);
        impl.ctl_speed = opt_speed;
    }
#if defined(_WIN32)
    if (opt_audio && info.primary_audio_index >= 0) {
        const AudioTrackInfo* track = nullptr;
        for (const AudioTrackInfo& candidate : info.audio_tracks) {
            const auto& [cand_idx, cand_sr, cand_ch, cand_par] = candidate; (void)cand_sr; (void)cand_ch; (void)cand_par; if (cand_idx == info.primary_audio_index) { track = &candidate; break; }
        }
        if (track != nullptr) {
            if (auto audio = WasapiAudioOutput::create(); audio.has_value()) {
                impl.audio_track = *track;
                impl.audio = std::move(audio.value());
                auto& [audio_clk, anchor_tp, base_flicks, clk_speed] = impl.clock;
                (void)anchor_tp; (void)base_flicks; (void)clk_speed;
                audio_clk = [a = impl.audio.get()]() -> std::optional<int64_t> {
                    return a->running() ? std::optional<int64_t>(a->masterFlicks()) : std::nullopt;
                };
                impl.audio->anchor(0);
            }
        }
    }
#endif

    impl.audio_worker = std::jthread([&impl](std::stop_token st) { impl.audioLoop(st); });
    impl.video_worker = std::jthread([&impl](std::stop_token st) { impl.videoLoop(st); });
    {
        std::unique_lock<std::mutex> lock(impl.ctl_mutex);
        impl.ctl_done.wait(lock, [&] { return impl.decoder_ready; });
        if (!impl.decoder) return std::unexpected(impl.ctl_last_error);
    }
    return session;
}

MediaPlaybackSession::~MediaPlaybackSession() = default;

void MediaPlaybackSession::Impl::setClockPaused(bool paused) noexcept {
    auto& [audio_clk, anchor_tp, base_flicks, clk_speed] = clock;
    (void)audio_clk;
    if (paused) {
        if (anchor_tp.has_value()) {
            base_flicks = clockNow(clock);
            anchor_tp.reset();
        }
    } else if (!anchor_tp.has_value()) {
        clockAnchor(clock, base_flicks, clk_speed);
    }
#if defined(_WIN32)
    if (audio) paused ? audio->pause() : audio->resume();
#endif
}

Result<void> MediaPlaybackSession::play() {
    Impl& impl = *impl_;
    std::lock_guard<std::mutex> lock(impl.ctl_mutex);
    if (impl.ctl_state == PlaybackState::Ended) {
        impl.ctl_request = Request::Seek;
        impl.ctl_seek_target = 0;
        impl.ctl_resume_after_seek = true;
        ++impl.ctl_seek_generation;
    }
    impl.ctl_state = PlaybackState::Playing;
    impl.setClockPaused(false);
    impl.ctl_cv.notify_all();
    return {};
}

void MediaPlaybackSession::pause() noexcept {
    Impl& impl = *impl_;
    std::lock_guard<std::mutex> lock(impl.ctl_mutex);
    impl.ctl_state = PlaybackState::Paused;
    impl.setClockPaused(true);
    impl.ctl_cv.notify_all();
}

Result<void> MediaPlaybackSession::seek(int64_t target_flicks) {
    return dispatchControlRequest(*impl_, Request::Seek, target_flicks, true,
                                  "playback seek timed out");
}

Result<void> MediaPlaybackSession::stepFrame(int direction) {
    return dispatchControlRequest(*impl_,
                                  direction < 0 ? Request::StepBackward : Request::StepForward,
                                  0, false, "playback frame step timed out");
}

void MediaPlaybackSession::setSpeed(double speed) noexcept {
    Impl& impl = *impl_;
    std::lock_guard<std::mutex> lock(impl.ctl_mutex);
    impl.ctl_speed = clampPlaybackSpeed(speed);
    impl.ctl_cv.notify_all();
}

PlaybackState MediaPlaybackSession::state() const noexcept {
    std::lock_guard<std::mutex> lock(impl_->ctl_mutex);
    return impl_->ctl_state;
}

int64_t MediaPlaybackSession::playheadFlicks() const noexcept {
    return impl_->playhead.load(std::memory_order_relaxed);
}

TelemetrySnapshot MediaPlaybackSession::telemetry() const noexcept {
    return telemetryRead(impl_->telemetry);
}

bool MediaPlaybackSession::audioActive() const noexcept {
#if defined(_WIN32)
    return impl_->audio != nullptr;
#else
    return false;
#endif
}

}  // namespace rtx
