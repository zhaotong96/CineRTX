// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors

#include "hw_transcode_internal.h"
#include "rtx/logger.h"
#include <cstdlib>
#include <cstring>
#include <format>
#include <utility>

namespace rtx {

RemasterPipeline::RemasterPipeline(const HwTranscodeOptions& options)
    : options_(options) {
    started_ = Clock::now();
    report_.path = "vsr-remaster";
    report_.vsr_quality = options.vsr_quality;
}

RemasterPipeline::~RemasterPipeline() {
    releasePrevLease();
    if (encode_thread_.joinable()) {
        {
            std::lock_guard<std::mutex> lock(pipeline_mutex_);
            producer_done_ = true;
        }
        queue_ready_.notify_all();
        encode_thread_.join();
    }
    if (rife_active_ && session_ && session_->bridgeLoader().hasRifeApi()) {
        session_->bridgeLoader().destroyRife(session_->bridge());
        rife_active_ = false;
    }
    for (int i = 0; i < 2; ++i) {
        if (anchor_p010_[i] != 0 && cuda_.mem_free) {
            cuda_.mem_free(anchor_p010_[i]);
            anchor_p010_[i] = 0;
        }
        if (pinned_host_buf_[i] != nullptr) {
            if (cuda_.mem_free_host) {
                cuda_.mem_free_host(pinned_host_buf_[i]);
            } else {
                std::free(pinned_host_buf_[i]);
            }
            pinned_host_buf_[i] = nullptr;
        }
    }
}

bool RemasterPipeline::consumerStopped() const noexcept {
    const bool cancelled = (options_.cancel != nullptr && options_.cancel->load(std::memory_order_relaxed));
    return consumer_failed_ || cancelled;
}

void RemasterPipeline::reportProgress() {
    if (!options_.progress) return;
    options_.progress(HwTranscodeProgress{
        .frames_decoded = report_.frames_decoded,
        .frames_encoded = report_.frames_encoded,
        .total_frames = estimated_total_frames_,
        .elapsed_ms = std::chrono::duration<double, std::milli>(Clock::now() - started_).count(),
    });
}

Result<void> RemasterPipeline::initDecodeAndDemuxStage() {
    if (options_.input_path.empty() || options_.output_path.empty()) {
        return err(ErrorCode::InvalidArgument, "remaster transcode requires input and output paths");
    }
    if (options_.codec != VideoCodec::HEVC && options_.codec != VideoCodec::AV1) {
        return err(ErrorCode::InvalidArgument, "the 10-bit HDR10 VSR-remaster pipeline supports AV1 and HEVC Main10 output only");
    }
    if (options_.vsr_quality < 1 || options_.vsr_quality > 4) {
        return err(ErrorCode::InvalidArgument, "vsr_quality must be in 1..4");
    }

    auto session_res = NvidiaTranscodeSession::create(options_, true, report_, options_.target_width, options_.target_height);
    if (!session_res.has_value()) {
        return std::unexpected(session_res.error());
    }
    session_ = std::move(session_res.value());

    const int target_w = session_->targetWidth();
    const int target_h = session_->targetHeight();
    if ((target_w & 1) != 0 || (target_h & 1) != 0) {
        return err(ErrorCode::InvalidArgument, "target geometry must be even");
    }
    const MediaInfo& info = session_->info();
    if (target_w < info.width || target_h < info.height) {
        return err(ErrorCode::InvalidArgument, "remaster target geometry must not be smaller than the source");
    }

    const Rational& frame_rate = session_->frameRate();
    if (info.duration_ms > 0 && frame_rate.num > 0 && frame_rate.den > 0) {
        estimated_total_frames_ = info.duration_ms * frame_rate.num / (1000 * static_cast<int64_t>(frame_rate.den));
    }
    cuda_.init();
    if (session_->previewReady()) {
        preview_host_.assign(static_cast<size_t>(session_->previewPitch()) * 540u, 0);
    }
    return {};
}

Result<void> RemasterPipeline::initRemasterAndFgStage() {
    segmenter_.reset();
    const bool fruc_enabled = options_.fruc_enabled;
    const bool rife_requested = fruc_enabled && !options_.rife_engine_path.empty();

    if (rife_requested) {
        if (!session_->bridgeLoader().hasRifeApi()) {
            return err(ErrorCode::Unsupported, "loaded rtx_ngx_bridge.dll does not export the Phase 8 RIFE API (ABI < 4)");
        }
        const uint32_t in_w = static_cast<uint32_t>(session_->info().width);
        const uint32_t in_h = static_cast<uint32_t>(session_->info().height);
        const std::string engine_path_str = options_.rife_engine_path.string();
        const int32_t r_res = session_->bridgeLoader().initRife(session_->bridge(), engine_path_str.c_str(), in_w, in_h);
        if (r_res != RTX_OK) {
            return err(mapShimResult(static_cast<RtxResult>(r_res)),
                       describe("rtx_ngx_bridge_init_rife", static_cast<RtxResult>(r_res),
                                session_->bridgeLoader().lastError(session_->bridge())));
        }
        rife_active_ = true;
        report_.fruc_backend = "rife-trt";
        fruc_out_buffer_ = session_->bridgeLoader().getInterpBuffer(session_->bridge(), in_w, in_h);
        logInfo(std::format("FRUC Route B active: TensorRT RIFE ({})", engine_path_str));
    } else if (fruc_enabled) {
        report_.fruc_backend = "nvof";
        fruc_out_buffer_ = session_->bridgeLoader().getInterpBuffer(
            session_->bridge(), static_cast<uint32_t>(session_->info().width), static_cast<uint32_t>(session_->info().height));
        logInfo("FRUC Route A active: NVOFA hardware optical flow");
    } else {
        logInfo("FRUC disabled: preserving source frame rate");
    }

    const Rational& out_fps = session_->frameRate();
    delta_out_flicks_ = (out_fps.num > 0 && out_fps.den > 0)
        ? (705600000LL * out_fps.den / out_fps.num)
        : 11760000LL;

    const int target_w = session_->targetWidth();
    const int target_h = session_->targetHeight();
    anchor_pitch_ = static_cast<uint32_t>(target_w * 2);
    const size_t frame_bytes = static_cast<size_t>(target_w) * target_h * 3;

    if (options_.vsr_before_fg && cuda_.loaded && cuda_.mem_alloc) {
        cuda_.mem_alloc(&anchor_p010_[0], frame_bytes);
        cuda_.mem_alloc(&anchor_p010_[1], frame_bytes);
    }

    if (options_.use_cpu_encoder) {
        pinned_buf_bytes_ = frame_bytes;
        for (int i = 0; i < 2; ++i) {
            if (cuda_.loaded && cuda_.mem_alloc_host) {
                cuda_.mem_alloc_host(&pinned_host_buf_[i], frame_bytes);
            } else {
                pinned_host_buf_[i] = std::malloc(frame_bytes);
            }
        }
    }
    return {};
}

Result<void> RemasterPipeline::initEncoderAndMuxerStage() {
    encode_thread_ = std::thread([this]() {
        for (;;) {
            PendingEncodeItem item;
            {
                std::unique_lock<std::mutex> lock(pipeline_mutex_);
                queue_ready_.wait(lock, [this] {
                    return !encode_queue_.empty() || producer_done_ || consumerStopped();
                });
                if (consumerStopped()) return;
                if (encode_queue_.empty()) {
                    if (producer_done_) return;
                    continue;
                }
                item = std::move(encode_queue_.front());
                encode_queue_.pop_front();
            }

            if (options_.use_cpu_encoder) {
                const int target_w = session_->targetWidth();
                const int target_h = session_->targetHeight();
                const size_t y_bytes = static_cast<size_t>(target_w) * 2 * target_h;
                const size_t uv_bytes = static_cast<size_t>(target_w) * 2 * (target_h / 2);
                uint8_t* dst = static_cast<uint8_t*>(pinned_host_buf_[pinned_slot_ & 1u]);
                pinned_slot_ ^= 1u;

                if (cuda_.loaded && cuda_.memcpy_dtoh) {
                    cuda_.memcpy_dtoh(dst, item.remaster_out.y_plane, y_bytes);
                    cuda_.memcpy_dtoh(dst + y_bytes, item.remaster_out.uv_plane, uv_bytes);
                } else {
                    std::memcpy(dst, reinterpret_cast<const void*>(item.remaster_out.y_plane), y_bytes);
                    std::memcpy(dst + y_bytes, reinterpret_cast<const void*>(item.remaster_out.uv_plane), uv_bytes);
                }

                auto pkts = session_->cpuEncoder()->encodeP010Frame(
                    reinterpret_cast<const uint16_t*>(dst), target_w * 2,
                    reinterpret_cast<const uint16_t*>(dst + y_bytes), target_w * 2,
                    item.pts, item.force_keyframe, item.hdr10plus_prefix);
                if (!pkts.has_value()) {
                    std::lock_guard<std::mutex> lock(pipeline_mutex_);
                    consumer_failed_ = true;
                    consumer_error_ = std::unexpected(pkts.error());
                    slot_free_.notify_all();
                    return;
                }
                for (const auto& pkt : pkts.value()) {
                    auto wr = session_->muxer()->writePacket(pkt);
                    if (!wr.has_value()) {
                        std::lock_guard<std::mutex> lock(pipeline_mutex_);
                        consumer_failed_ = true;
                        consumer_error_ = wr;
                        slot_free_.notify_all();
                        return;
                    }
                    ++report_.packets_written;
                    report_.bitstream_bytes += pkt.data.size();
                }
            } else {
                RtxPacketDesc packet{};
                RtxFrameDesc encode_frame{};
                encode_frame.width = item.remaster_out.width;
                encode_frame.height = item.remaster_out.height;
                encode_frame.format = item.remaster_out.format;
                encode_frame.planes[0] = item.remaster_out.y_plane;
                encode_frame.planes[1] = item.remaster_out.uv_plane;
                encode_frame.pitches[0] = item.remaster_out.y_pitch;
                encode_frame.pitches[1] = item.remaster_out.uv_pitch;
                encode_frame.bit_depth = 10;
                encode_frame.pts = item.pts;
                encode_frame.slot_index = 0;
                const RtxResult encoded = session_->shim().encoderEncode(session_->encoder(), &encode_frame, &packet);
                if (encoded != RTX_OK) {
                    std::lock_guard<std::mutex> lock(pipeline_mutex_);
                    consumer_failed_ = true;
                    consumer_error_ = err(mapShimResult(encoded), describe("NvEncEncodePicture", encoded,
                                                                         session_->shim().encoderLastError(session_->encoder())));
                    slot_free_.notify_all();
                    return;
                }
                auto written = session_->writeEncodedPacket(packet, item.pts, report_, item.hdr10plus_prefix);
                if (!written.has_value()) {
                    std::lock_guard<std::mutex> lock(pipeline_mutex_);
                    consumer_failed_ = true;
                    consumer_error_ = written;
                    slot_free_.notify_all();
                    return;
                }
            }

            {
                std::lock_guard<std::mutex> done_lock(pipeline_mutex_);
                ++encodes_completed_;
                ++report_.frames_encoded;
            }
            slot_free_.notify_all();
            reportProgress();
        }
    });
    return {};
}

Result<void> RemasterPipeline::queueEncodeItem(const RtxNgxRemasterOutput& out, int64_t pts,
                                              const std::vector<uint8_t>& hdr10plus, bool force_key) {
    if (session_->encoder() == nullptr && session_->cpuEncoder() == nullptr) {
        session_->setMeasuredCll(accumulated_max_cll_, accumulated_max_fall_);
        RtxFrameDesc fdesc{};
        fdesc.width = out.width; fdesc.height = out.height; fdesc.format = out.format; fdesc.bit_depth = 10;
        auto created = session_->createEncoder(fdesc, report_);
        if (!created.has_value()) return created;
        auto mux = session_->openMuxer();
        if (!mux.has_value()) return mux;
    }

    {
        std::lock_guard<std::mutex> lock(pipeline_mutex_);
        encode_queue_.push_back(PendingEncodeItem{
            .remaster_out = out,
            .pts = pts,
            .hdr10plus_prefix = hdr10plus,
            .force_keyframe = force_key,
            .pinned_slot = pinned_slot_,
        });
        ++remasters_issued_;
    }
    queue_ready_.notify_all();
    return {};
}

void RemasterPipeline::releasePrevLease() noexcept {
    if (has_prev_frame_ && session_) {
        session_->releaseDecodedFrame(prev_frame_.slot_index);
        has_prev_frame_ = false;
    }
}

Result<void> RemasterPipeline::flushBufferedAudioPackets() {
    if (!session_ || !session_->isMuxerOpen()) return {};
    std::deque<Packet> to_flush;
    {
        std::lock_guard<std::mutex> lk(audio_mtx_);
        to_flush.swap(audio_queue_);
    }
    for (const auto& ap : to_flush) {
        auto a_res = session_->writeAudioPacket(ap);
        if (!a_res.has_value()) return a_res;
        ++report_.packets_written;
    }
    return {};
}

Result<void> RemasterPipeline::remasterAndEnqueueFrame(
    const RtxFrameDesc& frame, int64_t pts_override, bool release_slot, bool force_reset_temporal) {
    const int64_t effective_pts = (pts_override >= 0) ? pts_override : frame.pts;

    // 关键修复：必须在调用 bridgeLoader().remaster() 覆写 p010_ring[i % 3] 之前等待空闲槽位！
    {
        std::unique_lock<std::mutex> lock(pipeline_mutex_);
        slot_free_.wait(lock, [this] {
            return remasters_issued_ - encodes_completed_ < 3 || consumerStopped();
        });
        if (consumerStopped()) {
            if (release_slot && frame.slot_index != 0xFFFFFFFFu) {
                session_->releaseDecodedFrame(frame.slot_index);
            }
            return consumer_failed_ ? consumer_error_ : err(ErrorCode::State, "transcode cancelled");
        }
    }

    const MediaInfo& info = session_->info();
    const bool reset_temporal = (frame_index_ == 0) || force_reset_temporal || last_was_cut_;
    last_was_cut_ = false;

    RtxNgxRemasterInput in{};
    in.y_plane = frame.planes[0];
    in.uv_plane = frame.planes[1];
    in.y_pitch = frame.pitches[0];
    in.uv_pitch = frame.pitches[1];
    in.format = frame.format;
    in.full_range = (info.color.range == ColorRange::Full) ? 1 : 0;
    in.matrix = (info.color.matrix == MatrixCoefficients::BT2020NC) ? 1 : 0;
    in.reset_temporal = reset_temporal ? 1 : 0;

    bool preview_this_frame = false;
    if (options_.preview && session_->previewReady()) {
        in.preview_rgba10_ptr = session_->previewDevicePointer(preview_slot_ & 1u);
        in.preview_pitch = session_->previewPitch();
        in.split_x_norm = options_.split_ratio != nullptr
            ? std::clamp(options_.split_ratio->load(std::memory_order_relaxed), 0.0f, 1.0f) : 0.5f;
        preview_slot_ ^= 1u;
        preview_this_frame = true;
    }

    RtxNgxRemasterOutput out{};
    RtxNgxRemasterStats stats{};
    const RtxResult r = session_->bridgeLoader().remaster(session_->bridge(), &in, &out, &stats);
    if (release_slot && frame.slot_index != 0xFFFFFFFFu) {
        session_->releaseDecodedFrame(frame.slot_index);
    }
    if (r != RTX_OK) {
        return err(mapShimResult(r), describe("rtx_ngx_bridge_remaster", r, nullptr),
                   session_->bridgeLoader().lastError(session_->bridge()));
    }

    const double pack_delta = static_cast<double>(stats.pack_kernel_ms - previous_stats_.pack_kernel_ms);
    previous_stats_ = stats;
    if (pack_delta > pack_max_ms_) pack_max_ms_ = pack_delta;
    accumulated_max_cll_ = std::max<uint32_t>(accumulated_max_cll_, out.frame_max_nits);
    accumulated_max_fall_ = std::max<uint32_t>(accumulated_max_fall_, out.frame_avg_nits);

    if (preview_this_frame) {
        const auto now = Clock::now();
        const bool due = !last_preview_download_.has_value() ||
                         std::chrono::duration<double>(now - *last_preview_download_).count() >= (1.0 / 15.0);
        if (due && session_->downloadPreviewInto(preview_host_.data(), preview_host_.size()) == RTX_OK) {
            last_preview_download_ = now;
            options_.preview(preview_host_.data(), session_->previewPitch());
        }
    }

    std::vector<uint8_t> hdr10plus;
    if (options_.hdr10plus) {
        Hdr10PlusFrameStats fstats{};
        fstats.maxscl_r_nits = std::max<uint32_t>(1u, out.frame_max_nits);
        fstats.maxscl_g_nits = std::max<uint32_t>(1u, out.frame_max_nits);
        fstats.maxscl_b_nits = std::max<uint32_t>(1u, out.frame_max_nits);
        fstats.average_maxrgb_nits = std::max<uint32_t>(1u, out.frame_avg_nits);
        const float ratio = out.frame_max_nits > 0
            ? static_cast<float>(out.frame_avg_nits) / static_cast<float>(out.frame_max_nits) : 0.5f;
        fstats.knee_point_x = 500;
        fstats.knee_point_y = static_cast<uint16_t>(std::clamp(static_cast<int>(ratio * 1000.0f), 1, 1000));
        hdr10plus = (options_.codec == VideoCodec::HEVC)
            ? SeiSynthesizer::hevcHdr10PlusPrefixSei(fstats)
            : SeiSynthesizer::av1Hdr10PlusMetadataObu(fstats);
    }

    auto q_res = queueEncodeItem(out, effective_pts, hdr10plus, reset_temporal);
    if (!q_res.has_value()) return q_res;
    ++frame_index_;
    return {};
}

Result<void> RemasterPipeline::synthesizeAndRemasterWindow(
    const RtxFrameDesc& prev_f, const RtxFrameDesc& curr_f, int64_t out_pts_90k, float alpha) {
    if (alpha <= 0.0001f) {
        return remasterAndEnqueueFrame(prev_f, out_pts_90k, /*release_slot=*/false);
    }
    if (alpha >= 0.9999f) {
        return remasterAndEnqueueFrame(curr_f, out_pts_90k, /*release_slot=*/false);
    }

    const uint32_t pitch = prev_f.pitches[0];
    const int input_full_range = (session_->info().color.range == ColorRange::Full) ? 1 : 0;

    if (rife_active_) {
        int32_t scene_cut = 0;
        const int32_t status = session_->bridgeLoader().interpolateRife(
            session_->bridge(), prev_f.planes[0], curr_f.planes[0], pitch,
            alpha, fruc_out_buffer_, input_full_range, &scene_cut);
        if (status == RTX_OK) {
            if (scene_cut != 0) {
                ++report_.fruc_scene_cuts;
                last_was_cut_ = true;
                const RtxFrameDesc& fb = (alpha < 0.5f) ? prev_f : curr_f;
                return remasterAndEnqueueFrame(fb, out_pts_90k, /*release_slot=*/false, /*force_reset_temporal=*/true);
            }
            ++report_.frames_interpolated;
            RtxFrameDesc interp_frame = prev_f;
            interp_frame.planes[0] = fruc_out_buffer_;
            interp_frame.planes[1] = fruc_out_buffer_ + static_cast<uint64_t>(pitch) * prev_f.height;
            interp_frame.pts = out_pts_90k;
            interp_frame.slot_index = 0xFFFFFFFFu;
            return remasterAndEnqueueFrame(interp_frame, out_pts_90k, /*release_slot=*/false);
        }
        ++report_.fruc_failures;
        if (fruc_error_logs_ < 5) {
            ++fruc_error_logs_;
            logError(std::format("[FRUC-ERROR] RIFE interpolation failed (code={}): {}",
                                 status, session_->bridgeLoader().lastError(session_->bridge())));
        }
        const RtxFrameDesc& fb = (alpha < 0.5f) ? prev_f : curr_f;
        return remasterAndEnqueueFrame(fb, out_pts_90k, /*release_slot=*/false);
    }

    const RtxResult status = session_->bridgeLoader().interpolateNv12(
        session_->bridge(), prev_f.planes[0], curr_f.planes[0], pitch, alpha, fruc_out_buffer_);
    if (status == RTX_OK) {
        ++report_.frames_interpolated;
        RtxFrameDesc interp_frame = prev_f;
        interp_frame.planes[0] = fruc_out_buffer_;
        interp_frame.planes[1] = fruc_out_buffer_ + static_cast<uint64_t>(pitch) * prev_f.height;
        interp_frame.pts = out_pts_90k;
        interp_frame.slot_index = 0xFFFFFFFFu;
        return remasterAndEnqueueFrame(interp_frame, out_pts_90k, /*release_slot=*/false);
    }
    ++report_.fruc_failures;
    return remasterAndEnqueueFrame(prev_f, out_pts_90k, /*release_slot=*/false);
}

Result<void> RemasterPipeline::handleDecodedFrame(const RtxFrameDesc& frame) {
    constexpr int64_t kFlicksPerTick90k = 7840;
    if (!options_.fruc_enabled || fruc_out_buffer_ == 0) {
        return remasterAndEnqueueFrame(frame, -1, /*release_slot=*/true);
    }
    if (!has_prev_frame_) {
        prev_frame_ = frame;
        prev_pts_flicks_ = frame.pts * kFlicksPerTick90k;
        next_out_pts_flicks_ = prev_pts_flicks_;
        has_prev_frame_ = true;
        return {};
    }
    const int64_t curr_pts_flicks = frame.pts * kFlicksPerTick90k;
    if (curr_pts_flicks <= prev_pts_flicks_) {
        session_->releaseDecodedFrame(prev_frame_.slot_index);
        prev_frame_ = frame;
        prev_pts_flicks_ = curr_pts_flicks;
        return {};
    }
    const int64_t window_duration = curr_pts_flicks - prev_pts_flicks_;
    while (next_out_pts_flicks_ < curr_pts_flicks) {
        if (consumerStopped()) {
            return err(ErrorCode::State, "transcode cancelled");
        }
        const float alpha = static_cast<float>(next_out_pts_flicks_ - prev_pts_flicks_) /
                            static_cast<float>(window_duration);
        const int64_t out_pts_90k = (next_out_pts_flicks_ + (kFlicksPerTick90k / 2)) / kFlicksPerTick90k;
        auto res = synthesizeAndRemasterWindow(prev_frame_, frame, out_pts_90k, alpha);
        if (!res.has_value()) return res;
        next_out_pts_flicks_ += delta_out_flicks_;
    }
    session_->releaseDecodedFrame(prev_frame_.slot_index);
    prev_frame_ = frame;
    prev_pts_flicks_ = curr_pts_flicks;
    has_prev_frame_ = true;
    return {};
}

Result<void> RemasterPipeline::drainEndOfStream() {
    constexpr int64_t kFlicksPerTick90k = 7840;
    if (options_.fruc_enabled && fruc_out_buffer_ != 0 && has_prev_frame_) {
        if (next_out_pts_flicks_ <= prev_pts_flicks_) {
            const int64_t out_pts_90k = (next_out_pts_flicks_ + (kFlicksPerTick90k / 2)) / kFlicksPerTick90k;
            auto res = remasterAndEnqueueFrame(prev_frame_, out_pts_90k, /*release_slot=*/false);
            if (!res.has_value()) {
                releasePrevLease();
                return res;
            }
            next_out_pts_flicks_ += delta_out_flicks_;
        }
        releasePrevLease();
    }
    return {};
}

Result<void> RemasterPipeline::flushAndFinalizePipeline() {
    {
        std::lock_guard<std::mutex> lock(pipeline_mutex_);
        producer_done_ = true;
    }
    queue_ready_.notify_all();
    if (encode_thread_.joinable()) {
        encode_thread_.join();
    }
    releasePrevLease();
    if (consumer_failed_) {
        return consumer_error_;
    }

    auto flushed_enc = session_->flushEncoder(report_);
    if (!flushed_enc.has_value()) return flushed_enc;

    auto flushed_aud = flushBufferedAudioPackets();
    if (!flushed_aud.has_value()) return flushed_aud;

    if (rife_active_ && report_.fruc_failures > 0 && report_.frames_interpolated == 0) {
        (void)session_->finalize(report_, started_);
        return err(ErrorCode::Internal,
                   std::format("RIFE frame interpolation failed on all {} intermediate frame(s): {}",
                               report_.fruc_failures,
                               session_->bridgeLoader().lastError(session_->bridge())));
    }

    auto finalized = session_->finalize(report_, started_);
    if (!finalized.has_value()) return finalized;

    if (report_.frames_encoded > 0) {
        const double n = static_cast<double>(report_.frames_encoded);
        report_.remaster_preprocess_ms = previous_stats_.preprocess_ms / n;
        report_.remaster_ngx_ms = previous_stats_.ngx_ms / n;
        report_.remaster_pack_kernel_ms = previous_stats_.pack_kernel_ms / n;
        report_.remaster_frame_ms = report_.main_loop_ms / n;
        report_.remaster_pack_kernel_max_ms = pack_max_ms_;
        report_.remaster_pack_launches = previous_stats_.pack_launches;
    }
    report_.measured_max_cll_nits = session_->measuredMaxCllNits();
    report_.measured_max_fall_nits = session_->measuredMaxFallNits();
    return {};
}

Result<HwTranscodeReport> RemasterPipeline::run() {
    auto d_res = initDecodeAndDemuxStage();
    if (!d_res.has_value()) return std::unexpected(d_res.error());

    auto r_res = initRemasterAndFgStage();
    if (!r_res.has_value()) return std::unexpected(r_res.error());

    auto e_res = initEncoderAndMuxerStage();
    if (!e_res.has_value()) return std::unexpected(e_res.error());

    const Clock::time_point loop_start = Clock::now();
    int64_t packet_pts = 0;
    const Rational& frame_rate = session_->frameRate();
    const int64_t frame_ticks = (frame_rate.num > 0 && frame_rate.den > 0)
        ? std::max<int64_t>(1, static_cast<int64_t>(90000) * frame_rate.den / frame_rate.num) : 3000;
    const Rational clock_tb{1, 90000};
    const Rational input_tb = session_->info().time_base.valid() ? session_->info().time_base : clock_tb;

    Result<void> pipe_status;
    for (;;) {
        if (consumerStopped()) {
            pipe_status = err(ErrorCode::State, "transcode cancelled");
            break;
        }
        if (options_.frame_limit > 0 && report_.frames_decoded >= options_.frame_limit) break;

        auto pkt_res = session_->demuxer()->readPacket();
        if (!pkt_res.has_value()) {
            if (pkt_res.error().code == ErrorCode::NotFound) break;
            pipe_status = std::unexpected(pkt_res.error());
            break;
        }
        ++report_.packets_read;
        Packet& pkt = pkt_res.value();
        if (session_->isPassthroughPacket(pkt)) {
            if (session_->isAudioPacket(pkt) && !session_->copyAudioEnabled()) continue;
            if (session_->isMuxerOpen()) {
                auto a_res = flushBufferedAudioPackets();
                if (!a_res.has_value()) {
                    pipe_status = a_res;
                    break;
                }
                auto w_res = session_->writeAudioPacket(pkt);
                if (!w_res.has_value()) {
                    pipe_status = w_res;
                    break;
                }
                ++report_.packets_written;
            } else {
                std::lock_guard<std::mutex> lk(audio_mtx_);
                audio_queue_.push_back(std::move(pkt));
            }
            continue;
        }

        int64_t pts = pkt.pts == std::numeric_limits<int64_t>::min() ? packet_pts : rescaleQ(pkt.pts, input_tb, clock_tb);
        int64_t dts = pkt.dts == std::numeric_limits<int64_t>::min() ? pts : rescaleQ(pkt.dts, input_tb, clock_tb);
        RtxResult subm = session_->shim().decoderSubmit(session_->decoder(), pkt.data.data(), pkt.data.size(), pts, dts, pkt.keyframe ? 1 : 0);
        if (subm != RTX_OK) {
            pipe_status = err(mapShimResult(subm), describe("cuvidParseVideoData", subm, session_->shim().decoderLastError(session_->decoder())));
            break;
        }

        for (;;) {
            if (consumerStopped()) { pipe_status = err(ErrorCode::State, "transcode cancelled"); break; }
            if (options_.frame_limit > 0 && report_.frames_decoded >= options_.frame_limit) break;
            RtxFrameDesc fdesc{};
            RtxResult rec = session_->shim().decoderReceive(session_->decoder(), &fdesc);
            if (rec == RTX_OK) {
                Result<void> proc = handleDecodedFrame(fdesc);
                if (!proc.has_value()) { pipe_status = proc; break; }
                ++report_.frames_decoded;
                reportProgress();
                continue;
            }
            if (rec == RTX_ERR_NOT_FOUND) break;
            pipe_status = err(mapShimResult(rec), describe("rtx_shim_decoder_receive", rec, session_->shim().decoderLastError(session_->decoder())));
            break;
        }
        if (!pipe_status.has_value()) break;
        packet_pts += frame_ticks;
    }

    if (pipe_status.has_value()) {
        session_->shim().decoderFlush(session_->decoder());
        for (;;) {
            RtxFrameDesc fdesc{};
            RtxResult rec = session_->shim().decoderReceive(session_->decoder(), &fdesc);
            if (rec != RTX_OK) break;
            Result<void> proc = handleDecodedFrame(fdesc);
            if (!proc.has_value()) { pipe_status = proc; break; }
            ++report_.frames_decoded;
            reportProgress();
        }
    }

    if (pipe_status.has_value()) {
        pipe_status = drainEndOfStream();
    }

    report_.main_loop_ms = std::chrono::duration<double, std::milli>(Clock::now() - loop_start).count();
    auto fin = flushAndFinalizePipeline();
    if (!pipe_status.has_value()) return std::unexpected(pipe_status.error());
    if (!fin.has_value()) return std::unexpected(fin.error());
    return report_;
}

} // namespace rtx
