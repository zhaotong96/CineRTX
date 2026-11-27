// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
// Phase 12 (Wave 12.2-B2): eliminated all indexed tuple accessors in favor
// of structured bindings and flattened ctl_* control-plane fields.

#include "playback_session_impl.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <thread>
#include <utility>

#include "rtx/flicks.h"
#include "rtx/logger.h"
#include "rtx/rational.h"

namespace rtx {

namespace {

[[nodiscard]] int64_t toClock90k(int64_t pts, Rational time_base) noexcept {
    if (pts == std::numeric_limits<int64_t>::min() || !time_base.valid()) {
        return std::numeric_limits<int64_t>::min();
    }
    return rescaleQ(pts, time_base, Rational{1, 90000});
}

}  // namespace

Result<void> MediaPlaybackSession::Impl::recreateDecoder() {
    RtxDecoder* raw = nullptr;
    const RtxResult created = shim->decoderCreate(&decoder_config, &raw);
    if (created != RTX_OK) {
        return err(ErrorCode::Unsupported, "nvdec decoder creation failed",
                   std::to_string(static_cast<int>(created)));
    }
    decoder = DecoderPtr(raw, [sh = shim.get()](RtxDecoder* d) { sh->decoderDestroy(d); });
    return {};
}

void MediaPlaybackSession::Impl::releaseAll() noexcept {
    while (!decode_queue.empty()) {
        const auto& [frame, deadline_flicks, retire_fence] = decode_queue.front();
        (void)deadline_flicks; (void)retire_fence;
        shim->decoderReleaseFrame(decoder.get(), frame.slot_index);
        decode_queue.pop_front();
    }
    front_presented = false;
}

void MediaPlaybackSession::Impl::retirePresented() noexcept {
    if (!front_presented || decode_queue.empty()) return;
    const auto& [frame, deadline_flicks, retire_fence] = decode_queue.front();
    (void)deadline_flicks;
    if (sinkCompletedFence() < retire_fence) return;
    shim->decoderReleaseFrame(decoder.get(), frame.slot_index);
    decode_queue.pop_front();
    front_presented = false;
}

void MediaPlaybackSession::Impl::drainDecoder() {
    for (;;) {
        if (decode_queue.size() >= decode_capacity || !decoder) return;
        RtxFrameDesc frame{};
        const RtxResult result = shim->decoderReceive(decoder.get(), &frame);
        if (result == RTX_ERR_NOT_FOUND) return;
        if (result != RTX_OK) {
            if (!(decoder_exhausted && result == RTX_ERR_IO)) {
                fail(ErrorCode::Io, "nvdec receive failed", shim->decoderLastError(decoder.get()));
            }
            return;
        }
        ++frames_decoded;
        const int64_t flicks = absoluteTimestampToFlicks(frame.pts, Rational{1, 90000});
        if (!gopSeekAccepting(seek, flicks) || !pushDecodeSlot(decode_queue, decode_capacity, frame, flicks)) {
            shim->decoderReleaseFrame(decoder.get(), frame.slot_index);
            if (decode_queue.size() >= decode_capacity) return;
        }
    }
}

void MediaPlaybackSession::Impl::fillQueue(std::stop_token stop) {
    while (decode_queue.size() < decode_capacity && !end_of_stream && !decoder_exhausted && decoder) {
        if (stop.stop_requested()) return;
        {
            std::lock_guard<std::mutex> lock(ctl_mutex);
            if (ctl_request != Request::None) return;
        }
        Result<Packet> packet = demuxer->readPacket();
        if (!packet.has_value()) {
            if (packet.error().code == ErrorCode::NotFound) {
                end_of_stream = true;
                break;
            }
            fail(packet.error().code, packet.error().message, packet.error().detail);
            return;
        }
        const bool is_audio = info.primary_audio_index >= 0 &&
                              packet.value().stream_index == info.primary_audio_index;
        const bool is_passthrough =
            is_audio ||
            std::any_of(info.passthrough_streams.begin(), info.passthrough_streams.end(),
                        [&](const PassthroughStreamInfo& s) {
                            const auto& [idx, ct, cid, cn, tb, op] = s; (void)ct; (void)cid; (void)cn; (void)tb; (void)op; return idx == packet.value().stream_index;
                        });
        if (is_passthrough) {
#if defined(_WIN32)
            if (is_audio && audio_ready.load(std::memory_order_acquire)) {
                std::unique_lock<std::mutex> alock(audio_queue_mutex);
                audio_queue_space.wait(alock, stop, [&] {
                    return audio_queue.size() < kAudioQueueCapacity || stop.stop_requested();
                });
                if (!stop.stop_requested()) {
                    audio_queue.emplace_back(epoch, std::move(packet.value()));
                    alock.unlock();
                    audio_queue_ready.notify_one();
                }
            }
#endif
            continue;
        }
        const Packet& video = packet.value();
        int64_t pts90k = toClock90k(video.pts, video.time_base);
        if (pts90k == std::numeric_limits<int64_t>::min()) {
            pts90k = last_video_pts90k == std::numeric_limits<int64_t>::min()
                         ? 0
                         : last_video_pts90k + frame_interval * 90000 / static_cast<int64_t>(kFlicksPerSecond);
        }
        last_video_pts90k = pts90k;
        int64_t dts90k = toClock90k(video.dts, video.time_base);
        if (dts90k == std::numeric_limits<int64_t>::min()) dts90k = pts90k;
        const RtxResult submitted =
            shim->decoderSubmit(decoder.get(), video.data.data(), video.data.size(), pts90k, dts90k,
                                video.keyframe ? 1 : 0);
        if (submitted != RTX_OK) {
            fail(ErrorCode::Io, "nvdec submit failed", shim->decoderLastError(decoder.get()));
            return;
        }
        drainDecoder();
    }
    if (end_of_stream && decoder) {
        if (!decoder_exhausted) {
            decoder_exhausted = true;
            shim->decoderFlush(decoder.get());
        }
        drainDecoder();
    }
}

void MediaPlaybackSession::Impl::presentFront() {
    if (decode_queue.empty()) return;
    auto& [frame, deadline_flicks, retire_fence] = decode_queue.front();
    retire_fence = ++retire_sequence;
    Result<void> presented = sink.first(frame, deadline_flicks, retire_fence);
    if (!presented.has_value()) {
        fail(presented.error().code, presented.error().message, presented.error().detail);
        shim->decoderReleaseFrame(decoder.get(), frame.slot_index);
        decode_queue.pop_front();
        return;
    }
    playhead.store(deadline_flicks, std::memory_order_relaxed);
    ++frames_rendered;
    ++fps_window_frames;
    front_presented = true;
    retirePresented();
}

void MediaPlaybackSession::Impl::publishTelemetry() {
    const auto now = std::chrono::steady_clock::now();
    const double window_s = std::chrono::duration<double>(now - fps_window).count();
    if (window_s >= 1.0) {
        measured_fps_x100 = static_cast<uint32_t>(
            std::lround(static_cast<double>(fps_window_frames) * 100.0 / window_s));
        fps_window = now;
        fps_window_frames = 0;
    }
    const int64_t position = playhead.load(std::memory_order_relaxed);
    int64_t audio_flicks = position;
#if defined(_WIN32)
    if (audio && audio->running()) audio_flicks = audio->masterFlicks();
#endif
    TelemetrySnapshot snapshot{};
    telemetrySetU64(snapshot, TelemetrySlot::FramesDecoded, frames_decoded);
    telemetrySetU64(snapshot, TelemetrySlot::FramesRendered, frames_rendered);
    telemetrySetU64(snapshot, TelemetrySlot::FramesDropped, frames_dropped);
    const auto& [o_fps, o_spd, o_ah, o_vram, o_dev, o_aud, o_null_sink, o_gain] = options;
    (void)o_null_sink; (void)o_gain;
    (void)o_fps; (void)o_spd; (void)o_ah; (void)o_dev; (void)o_aud;
    telemetrySetU64(snapshot, TelemetrySlot::VramBudgetBytes, o_vram);
    telemetrySetU64(snapshot, TelemetrySlot::SequenceEpoch, epoch);
    telemetrySetI64(snapshot, TelemetrySlot::PlayheadFlicks, position);
    telemetrySetU32(snapshot, TelemetrySlot::CurrentFpsX100, measured_fps_x100);
    const int64_t av_drift_us = static_cast<int64_t>(
        static_cast<double>(audio_flicks - position) * 1.0e6 / static_cast<double>(kFlicksPerSecond));
    const int64_t abs_drift_us = av_drift_us < 0 ? -av_drift_us : av_drift_us;
    clock_state = (abs_drift_us > 32000) ? PlaybackSyncState::MeltdownBypass
                : (abs_drift_us > 8000)  ? PlaybackSyncState::AudioDrift
                                         : PlaybackSyncState::Normal;
    telemetrySetI64(snapshot, TelemetrySlot::AvDriftUs, av_drift_us);
    telemetryPublish(telemetry, snapshot);
}

void MediaPlaybackSession::Impl::waitFor(std::stop_token stop, int64_t flicks) {
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        Flicks{flicks} / std::max(0.25, applied_speed));
    std::unique_lock<std::mutex> lock(ctl_mutex);
    ctl_cv.wait_for(lock, stop, ns, [&] { return ctl_request != Request::None; });
}

void MediaPlaybackSession::Impl::pumpOnce(std::stop_token stop) {
    {
        std::lock_guard<std::mutex> lock(ctl_mutex);
        if (ctl_last_error.code != ErrorCode::Ok) {
            ctl_state = PlaybackState::Ended;
            return;
        }
        if (ctl_state != PlaybackState::Playing) return;
    }
    retirePresented();
    if (front_presented) {
        std::this_thread::yield();
        publishTelemetry();
        return;
    }
    fillQueue(stop);
    if (decode_queue.empty()) {
        if (end_of_stream && decoder_exhausted) {
            std::lock_guard<std::mutex> lock(ctl_mutex);
            ctl_state = PlaybackState::Ended;
        }
        publishTelemetry();
        return;
    }
    const auto& [frame, deadline_flicks, retire_fence] = decode_queue.front();
    (void)retire_fence;
    const int64_t now = clockNow(clock).count();
    switch (classifyFrameDeadline(deadline_flicks, now, drop_tolerance)) {
        case FrameVerdict::Drop:
            shim->decoderReleaseFrame(decoder.get(), frame.slot_index);
            decode_queue.pop_front();
            ++frames_dropped;
            break;
        case FrameVerdict::Wait:
            waitFor(stop, waitDurationFlicks(deadline_flicks, now));
            break;
        case FrameVerdict::Present:
            presentFront();
            break;
    }
    publishTelemetry();
}

Result<void> MediaPlaybackSession::Impl::performSeek(int64_t target_flicks, std::stop_token stop) {
    if (auto located = gopSeekTo(seek, target_flicks); !located.has_value()) return located;
    resetTemporalBuffers();
    releaseAll();
    if (shim->decoderReset(decoder.get()) != RTX_OK) {
        if (auto rec = recreateDecoder(); !rec.has_value()) return rec;
    } else {
        for (;;) {
            RtxFrameDesc stale{};
            if (shim->decoderReceive(decoder.get(), &stale) != RTX_OK) break;
            shim->decoderReleaseFrame(decoder.get(), stale.slot_index);
        }
    }
    end_of_stream = false;
    decoder_exhausted = false;
    last_video_pts90k = std::numeric_limits<int64_t>::min();
    ++epoch;
    {
        std::lock_guard<std::mutex> lock(audio_queue_mutex);
        audio_queue.clear();
        audio_queue.emplace_back(epoch, Packet{});
    }
    audio_queue_ready.notify_one();
#if defined(_WIN32)
    if (audio) {
        audio->flush();
        audio->anchor(target_flicks);
    }
#endif
    {
        auto& [audio_clk, anchor_tp, base_flicks, clk_speed] = clock;
        (void)audio_clk; (void)clk_speed;
        anchor_tp = std::chrono::steady_clock::now();
        base_flicks = Flicks{target_flicks};
    }
    playhead.store(target_flicks, std::memory_order_relaxed);
    fillQueue(stop);
    if (!decode_queue.empty()) presentFront();
    publishTelemetry();
    return {};
}

void MediaPlaybackSession::Impl::reportSeekDone(bool resume) {
    std::lock_guard<std::mutex> lock(ctl_mutex);
    ctl_seek_completed = ctl_seek_generation;
    ctl_state = resume ? PlaybackState::Playing : PlaybackState::Paused;
    setClockPaused(!resume);
    ctl_done.notify_all();
}

void MediaPlaybackSession::Impl::bootstrapDecoder() {
    if (auto created = recreateDecoder(); !created.has_value()) {
        fail(created.error().code, created.error().message, created.error().detail);
    }
    std::lock_guard<std::mutex> lock(ctl_mutex);
    decoder_ready = true;
    ctl_done.notify_all();
}

void MediaPlaybackSession::Impl::videoLoop(std::stop_token stop) {
    bootstrapDecoder();
    fps_window = std::chrono::steady_clock::now();
    for (;;) {
        Request request = Request::None;
        int64_t target = 0;
        bool resume = true;
        PlaybackState state = PlaybackState::Idle;
        {
            std::unique_lock<std::mutex> lock(ctl_mutex);
            ctl_cv.wait(lock, stop, [&] {
                return ctl_request != Request::None || ctl_state == PlaybackState::Playing;
            });
            if (stop.stop_requested() || !decoder) break;
            request = std::exchange(ctl_request, Request::None);
            target = ctl_seek_target;
            resume = ctl_resume_after_seek;
            state = ctl_state;
            if (ctl_speed != applied_speed) {
                applied_speed = ctl_speed;
#if defined(_WIN32)
                const bool audio_driven = audio != nullptr && audio->running();
#else
                const bool audio_driven = false;
#endif
                auto& [audio_clk, anchor_tp, base_flicks, clk_speed] = clock;
                (void)audio_clk; (void)base_flicks;
                if (anchor_tp.has_value() && !audio_driven) {
                    clockAnchor(clock, clockNow(clock), applied_speed);
                } else {
                    clk_speed = applied_speed;
                }
            }
        }
        if (request == Request::Seek || request == Request::StepBackward) {
            const int64_t destination = request == Request::StepBackward
                                            ? playhead.load(std::memory_order_relaxed) - frame_interval
                                            : target;
            if (auto result = performSeek(std::max<int64_t>(0, destination), stop); !result.has_value()) {
                fail(result.error().code, result.error().message, result.error().detail);
            }
            reportSeekDone(request == Request::Seek && resume);
            continue;
        }
        if (request == Request::StepForward) {
            resetTemporalBuffers();
            retirePresented();
            fillQueue(stop);
            if (!decode_queue.empty() && !front_presented) presentFront();
            publishTelemetry();
            reportSeekDone(false);
            continue;
        }
        if (state != PlaybackState::Playing) continue;
        pumpOnce(stop);
    }
    decoder.reset();
}

}  // namespace rtx
