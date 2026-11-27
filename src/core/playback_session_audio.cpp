// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors

#include "playback_session_impl.h"

#include <algorithm>
#include <array>
#include <thread>
#include <utility>

#include "rtx/audio_mixer_avx2.h"
#include "rtx/flicks.h"
#include "rtx/status.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <libavutil/mathematics.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
}
namespace rtx {
namespace {

using AudioCodecPtr = std::unique_ptr<AVCodecContext, FnDeleter<&avcodec_free_context>>;
using SwrPtr = std::unique_ptr<SwrContext, FnDeleter<&swr_free>>;
using FramePtr = std::unique_ptr<AVFrame, FnDeleter<&av_frame_free>>;
using PacketPtr = std::unique_ptr<AVPacket, FnDeleter<&av_packet_free>>;

Result<void> openAudioDecode(const AudioTrackInfo& track, uint32_t out_rate, uint32_t out_channels,
                             AudioCodecPtr& ctx, SwrPtr& swr, FramePtr& frame, PacketPtr& packet) {
    const auto& [t_idx, t_sr, t_ch, t_par] = track;
    (void)t_idx; (void)t_sr; (void)t_ch;
    if (!t_par) {
        return err(ErrorCode::InvalidArgument, "audio track has no codec parameters");
    }
    const auto* parameters = static_cast<const AVCodecParameters*>(t_par.get());
    const AVCodec* codec = avcodec_find_decoder(parameters->codec_id);
    if (codec == nullptr) {
        return err(ErrorCode::Unsupported, "no software audio decoder",
                   avcodec_get_name(parameters->codec_id));
    }
    AVCodecContext* raw_ctx = avcodec_alloc_context3(codec);
    if (raw_ctx == nullptr) {
        return err(ErrorCode::OutOfMemory, "audio context allocation failed");
    }
    ctx.reset(raw_ctx);
    if (avcodec_parameters_to_context(ctx.get(), parameters) < 0 ||
        avcodec_open2(ctx.get(), codec, nullptr) < 0) {
        return err(ErrorCode::Unsupported, "audio decoder open failed");
    }
    frame.reset(av_frame_alloc());
    packet.reset(av_packet_alloc());
    if (frame == nullptr || packet == nullptr) {
        return err(ErrorCode::OutOfMemory, "audio frame allocation failed");
    }
    AVChannelLayout out_layout{};
    av_channel_layout_default(&out_layout, static_cast<int>(out_channels));
    SwrContext* raw_swr = nullptr;
    const int status = swr_alloc_set_opts2(&raw_swr, &out_layout, AV_SAMPLE_FMT_FLT,
                                           static_cast<int>(out_rate), &ctx->ch_layout,
                                           ctx->sample_fmt, ctx->sample_rate, 0, nullptr);
    av_channel_layout_uninit(&out_layout);
    if (status < 0 || raw_swr == nullptr || swr_init(raw_swr) < 0) {
        return err(ErrorCode::Unsupported, "audio resampler init failed");
    }
    swr.reset(raw_swr);
    return {};
}
Result<void> drainAudioDecode(AVCodecContext* ctx, SwrContext* swr, AVFrame* frame,
                              AVPacket* packet, const Packet& source, uint32_t out_rate,
                              uint32_t out_channels, std::vector<float>& out,
                              std::vector<float>& interleaved) {
    av_packet_unref(packet);
    packet->data = const_cast<uint8_t*>(source.data.data());
    packet->size = static_cast<int>(source.data.size());
    packet->pts = source.pts;
    packet->dts = source.dts;
    packet->duration = source.duration;
    const int sent = avcodec_send_packet(ctx, packet);
    if (sent < 0 && sent != AVERROR(EAGAIN)) {
        return err(ErrorCode::Io, "audio send_packet failed", std::to_string(sent));
    }
    for (;;) {
        const int status = avcodec_receive_frame(ctx, frame);
        if (status == AVERROR(EAGAIN) || status == AVERROR_EOF) {
            return {};
        }
        if (status < 0) {
            return err(ErrorCode::Io, "audio receive_frame failed", std::to_string(status));
        }
        const int64_t delay = swr_get_delay(swr, ctx->sample_rate);
        const int capacity = static_cast<int>(
            av_rescale_rnd(delay + frame->nb_samples, out_rate, ctx->sample_rate, AV_ROUND_UP));
        if (capacity <= 0) {
            av_frame_unref(frame);
            continue;
        }
        const size_t needed = static_cast<size_t>(capacity) * out_channels;
        if (interleaved.size() < needed) {
            interleaved.resize(needed);
        }
        uint8_t* out_planes[1] = {reinterpret_cast<uint8_t*>(interleaved.data())};
        const int converted = swr_convert(swr, out_planes, capacity,
                                          const_cast<const uint8_t**>(frame->extended_data),
                                          frame->nb_samples);
        av_frame_unref(frame);
        if (converted < 0) {
            return err(ErrorCode::Io, "swr_convert failed", std::to_string(converted));
        }
        out.insert(out.end(), interleaved.begin(),
                   interleaved.begin() + static_cast<ptrdiff_t>(converted) * out_channels);
    }
}
}  // namespace

void MediaPlaybackSession::Impl::audioLoop(std::stop_token stop) {
#if defined(_WIN32)
    if (!audio) {
        return;
    }
    const uint32_t channels = audio->channels();
    const uint32_t out_rate = audio->sampleRate();
    AudioCodecPtr ctx;
    SwrPtr swr;
    FramePtr frame;
    PacketPtr packet;
    if (!openAudioDecode(audio_track, out_rate, channels, ctx, swr, frame, packet).has_value()) {
        return;
    }
    audio_ready.store(true, std::memory_order_release);
    WsolaStreamState wsola_state{};
    double applied = 1.0;
    uint64_t seen_epoch = 0;
    std::vector<float> decoded;
    std::vector<float> interleaved;
    std::vector<float> stretched;
    std::vector<float> feed;
    for (;;) {
        if (stop.stop_requested()) {
            return;
        }
        double requested = 1.0;
        {
            std::lock_guard<std::mutex> lock(ctl_mutex);
            requested = ctl_speed;
        }
        if (requested != applied) {
            applied = requested;
        }
        std::pair<uint64_t, Packet> entry;
        {
            std::unique_lock<std::mutex> qlock(audio_queue_mutex);
            audio_queue_ready.wait_for(qlock, stop, std::chrono::milliseconds(20),
                                       [&] { return !audio_queue.empty(); });
            if (audio_queue.empty()) continue;
            entry = std::move(audio_queue.front());
            audio_queue.pop_front();
            qlock.unlock();
            audio_queue_space.notify_one();
        }
        if (entry.first != seen_epoch) {
            seen_epoch = entry.first;
            avcodec_flush_buffers(ctx.get());
            if (swr) {
                swr_close(swr.get());
                swr_init(swr.get());
            }
            wsola_state = WsolaStreamState{};
            audio->flush();  // 丢弃复位前已写入设备的陈旧尾迹
        }
        if (entry.second.data.empty()) {
            continue;  // 哨兵包：复位即目的，不解码
        }
        decoded.clear();
        if (!drainAudioDecode(ctx.get(), swr.get(), frame.get(), packet.get(), entry.second,
                              out_rate, channels, decoded, interleaved)
                 .has_value() ||
            decoded.empty()) {
            continue;
        }
        const std::size_t max_out_samples =
            std::max<std::size_t>(decoded.size() * 4 + static_cast<std::size_t>(channels) * 1024,
                                  static_cast<std::size_t>(channels) * 4096);
        if (stretched.size() < max_out_samples) {
            stretched.resize(max_out_samples);
        }
        const std::size_t produced = wsolaProcessStream(
            wsola_state, decoded, stretched, static_cast<float>(applied), channels);
        feed.resize(produced);
        if (produced > 0) {
            const float audio_gain = std::get<7>(options);
            if (audio_gain <= 0.0f) {
                std::fill(feed.begin(), feed.end(), 0.0f);
            } else {
                const std::array<AudioVoice, 1> voices{{
                    {std::span<const float>(stretched.data(), produced), audio_gain},
                }};
                mixAvx2(voices, feed);
            }
        }
        size_t offset = 0;
        while (offset < feed.size()) {
            if (stop.stop_requested()) {
                return;
            }
            const size_t chunk = std::min<size_t>(feed.size() - offset,
                                                  static_cast<size_t>(channels) * 4096);
            Result<uint32_t> written =
                audio->write(std::span<const float>(feed.data() + offset, chunk));
            if (!written.has_value()) {
                break;
            }
            if (written.value() == 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }
            offset += static_cast<size_t>(written.value()) * channels;
        }
    }
#else
    (void)stop;
#endif
}
}  // namespace rtx
