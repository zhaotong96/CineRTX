// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors

#include "hw_transcode_internal.h"
#include "rtx/audio_mixer_avx2.h"
#include "rtx/vram_scheduler.h"
#include "rtx/logger.h"
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace rtx {

namespace {

void writeWebuiPreviewBmpIfDue(NgxRemasterBridge& loader, RtxNgxBridge* bridge, uint32_t pitch) {
#if defined(_WIN32)
    if (bridge == nullptr || pitch < 960u * 4u) return;
    static std::atomic<int64_t> last_dump_ms{0};
    const int64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    int64_t prev_ms = last_dump_ms.load(std::memory_order_relaxed);
    if (now_ms - prev_ms < 66 || !last_dump_ms.compare_exchange_strong(prev_ms, now_ms)) {
        return;
    }

    constexpr uint32_t kW = 960u;
    constexpr uint32_t kH = 540u;
    constexpr uint32_t kRowBytes = kW * 3u;
    constexpr uint32_t kPixelBytes = kRowBytes * kH;
    constexpr uint32_t kTotalBytes = 54u + kPixelBytes;

    std::vector<uint8_t> raw(static_cast<size_t>(pitch) * kH);
    if (loader.downloadPreview(bridge, raw.data(), raw.size()) != RTX_OK) {
        return;
    }

    std::vector<uint8_t> bmp(kTotalBytes, 0);
    bmp[0] = 'B';
    bmp[1] = 'M';
    std::memcpy(&bmp[2], &kTotalBytes, 4);
    const uint32_t offset = 54u;
    std::memcpy(&bmp[10], &offset, 4);
    const uint32_t dib_size = 40u;
    std::memcpy(&bmp[14], &dib_size, 4);
    const int32_t w_i32 = static_cast<int32_t>(kW);
    const int32_t h_i32 = static_cast<int32_t>(kH);
    std::memcpy(&bmp[18], &w_i32, 4);
    std::memcpy(&bmp[22], &h_i32, 4);
    const uint16_t planes = 1u;
    const uint16_t bpp = 24u;
    std::memcpy(&bmp[26], &planes, 2);
    std::memcpy(&bmp[28], &bpp, 2);
    std::memcpy(&bmp[34], &kPixelBytes, 4);

    for (uint32_t y = 0; y < kH; ++y) {
        const uint32_t* src = reinterpret_cast<const uint32_t*>(
            raw.data() + static_cast<size_t>(kH - 1u - y) * pitch);
        uint8_t* dst = bmp.data() + 54u + static_cast<size_t>(y) * kRowBytes;
        for (uint32_t x = 0; x < kW; ++x) {
            const uint32_t px = src[x];
            dst[x * 3u + 0u] = static_cast<uint8_t>(((px >> 20) & 0x3FFu) >> 2);  // B
            dst[x * 3u + 1u] = static_cast<uint8_t>(((px >> 10) & 0x3FFu) >> 2);  // G
            dst[x * 3u + 2u] = static_cast<uint8_t>((px & 0x3FFu) >> 2);          // R
        }
    }

    wchar_t tmp_dir[MAX_PATH]{};
    const DWORD dir_len = GetTempPathW(MAX_PATH, tmp_dir);
    if (dir_len == 0 || dir_len >= MAX_PATH) return;
    const std::filesystem::path dir_path(tmp_dir);
    const std::filesystem::path tmp_path = dir_path / L"cinertx_latest_preview.bmp.tmp";
    const std::filesystem::path dst_path = dir_path / L"cinertx_latest_preview.bmp";
    {
        std::ofstream out(tmp_path, std::ios::binary | std::ios::trunc);
        if (!out) return;
        out.write(reinterpret_cast<const char*>(bmp.data()), static_cast<std::streamsize>(bmp.size()));
        if (!out) return;
    }
    (void)MoveFileExW(tmp_path.c_str(), dst_path.c_str(), MOVEFILE_REPLACE_EXISTING);
#else
    (void)loader; (void)bridge; (void)pitch;
#endif
}

}  // namespace

RemasterPipeline::RemasterPipeline(const HwTranscodeOptions& options)
    : options_(options) {
    started_ = Clock::now();
    report_.path = "vsr-remaster";
    report_.vsr_quality = options.vsr_quality;
}
RemasterPipeline::~RemasterPipeline() {
    if (encode_worker_.joinable()) {
        encode_worker_.join();
    }
    NgxRemasterBridge::clearActivePreviewBridge(&session_->bridgeLoader());
// Phase 11.2 (C-02)：流水线销毁即注销活动预览源；GUI 的下一次借用将安全
// 空转（borrowActivePreview 返回 false），共享环随桥析构由 interop 回收。
    releasePrevLease();
}
bool RemasterPipeline::consumerStopped() const noexcept {
    const bool cancelled = (options_.cancel != nullptr && options_.cancel->load(std::memory_order_relaxed));
    return consumer_failed_ || cancelled;
}
void RemasterPipeline::reportProgress() {
    if (!options_.progress) return;
    options_.progress(HwTranscodeProgress{
        report_.frames_decoded,
        report_.frames_encoded,
        estimated_total_frames_,
        std::chrono::duration<double, std::milli>(Clock::now() - started_).count(),
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
        return err(ErrorCode::InvalidArgument,
                   std::format("remaster target geometry ({}x{}) must not be smaller than the source ({}x{})",
                               target_w, target_h, info.width, info.height));
    }
    const Rational& frame_rate = session_->frameRate();
    if (info.duration_ms > 0 && frame_rate.num > 0 && frame_rate.den > 0) {
        estimated_total_frames_ = info.duration_ms * frame_rate.num / (1000 * static_cast<int64_t>(frame_rate.den));
    }
    cuda_drv::init();
    NgxRemasterBridge::publishActivePreviewBridge(session_->bridgeLoader(), session_->bridge());
    return {};
}
Result<void> RemasterPipeline::initRemasterAndFgStage() {
    resetShotSegmenter(segmenter_);
    const bool fruc_enabled = options_.fruc_enabled;
// Phase 11.2 (C-02)：把本流水线的桥注册为监看消费者眼中的活动预览源；
// 槽位空闲标志随发布清零，跨作业互不残留。
    const bool rife_requested = fruc_enabled && !options_.rife_engine_path.empty();

    if (rife_requested) {
        if (!session_->bridgeLoader().hasRifeApi()) {
            return err(ErrorCode::Unsupported, "loaded rtx_ngx_bridge.dll does not export the Phase 8 RIFE API (ABI < 4)");
        }
        // Phase 11.4-F (PKG-G4): Dual-topology adaptive routing.
        // Spatial upscale (e.g. 1080p -> 4K) forces true fg-first (native 1080p RIFE scale=1.0 + VSR 2x).
        // Same-resolution 4K -> 4K enables physical scale=0.5f RIFE (1080p half-res flow + bilinear 4K).
        // Phase 13 (Wave 2-A Task 3 + Wave 4-B Task 1): Honor user pipeline order (vsr_before_fg)
        // without silent fg-first override, and apply adaptive infer_scale for 4K/ultrawide workloads.
        // Doc 57 (Task 1): synthesizeAndRemasterWindow always feeds decoded native NV12 surfaces
        // into RIFE before VSR remaster; lock RIFE init geometry strictly to native info.width x info.height.
        const bool use_fg_first = true;
        const uint32_t rife_w = static_cast<uint32_t>(session_->info().width);
        const uint32_t rife_h = static_cast<uint32_t>(session_->info().height);
        if (rife_w == 0u || rife_h == 0u || (rife_w & 1u) != 0u || (rife_h & 1u) != 0u) {
            return err(ErrorCode::InvalidArgument,
                       std::format("RIFE NV12 geometry guard failed: invalid surface dimensions ({}x{})",
                                   rife_w, rife_h));
        }
        const float rife_scale =
            (static_cast<uint64_t>(rife_w) * static_cast<uint64_t>(rife_h) >= 2560ULL * 1440ULL ||
             rife_w >= 2560u || rife_h >= 2160u) ? 0.5f : 1.0f;
        const std::string engine_path_str = options_.rife_engine_path.string();
        RtxRifeInitConfig rife_cfg{};
        rife_cfg.struct_size = sizeof(RtxRifeInitConfig);
        rife_cfg.width = rife_w;
        rife_cfg.height = rife_h;
        rife_cfg.scale = rife_scale;
        rife_cfg.align_mod = 128u;
        rife_cfg.reflect_padding = 1;
        uint64_t trt_peak_bytes = 0;
        const int32_t r_res = session_->bridgeLoader().initRifeEx(
            session_->bridge(), engine_path_str.c_str(), &rife_cfg, &trt_peak_bytes);
        if (r_res != RTX_OK) {
            const std::string diag = describe("rtx_ngx_bridge_init_rife_ex", static_cast<RtxResult>(r_res),
                                              session_->bridgeLoader().lastError(session_->bridge()));
            logError(std::format("[RIFE Error] Failed to initialize TensorRT engine ({}, {}x{}, target={}fps): {} — aborting without NVOFA fallback",
                                 engine_path_str, rife_w, rife_h, options_.target_fps.num, diag));
            return err(mapShimResult(static_cast<RtxResult>(r_res)), diag);
        }
        rife_destroy_guard_ = decltype(rife_destroy_guard_)(
            session_->bridge(), [this](void* bridge) {
                if (session_ && session_->bridgeLoader().hasRifeApi()) {
                    session_->bridgeLoader().destroyRife(static_cast<RtxNgxBridge*>(bridge));
                }
            });
        report_.fruc_backend = (rife_scale <= 0.5f) ? "rife-trt-scale05" : "rife-trt";
        fruc_out_buffer_ = session_->bridgeLoader().getInterpBuffer(session_->bridge(), rife_w, rife_h);
        vramSetTrtPeakAllocationBytes(vram_scheduler_, trt_peak_bytes);
        vramSetCodecRefPoolBytes(vram_scheduler_, static_cast<uint64_t>(rife_w) * rife_h * 3ull * 8ull);
        logInfo(std::format("[Pipeline] Active Route: {} | Target: {} fps | Native: {}x{} | Engine: {}",
                            report_.fruc_backend, options_.target_fps.num, rife_w, rife_h, engine_path_str));
        logInfo(std::format("FRUC Route B active: TensorRT RIFE ({}, {}x{}, scale={:.1f}, topology={}, A=128, reflect101, trt_peak={} MiB, vram_budget={} MiB)",
                            engine_path_str, rife_w, rife_h, rife_scale,
                            use_fg_first ? "fg-first" : "4k-scale05",
                            trt_peak_bytes / (1024ull * 1024ull),
                            vramDxgiBudgetBytes(vram_scheduler_) / (1024ull * 1024ull)));
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
        ? (static_cast<int64_t>((kFlicksPerSecond * static_cast<uint64_t>(out_fps.den)) / static_cast<uint64_t>(out_fps.num)))
        : static_cast<int64_t>(kFlicksPerSecond / 60ULL);

    const int target_w = session_->targetWidth();
    const int target_h = session_->targetHeight();
    const size_t frame_bytes = static_cast<size_t>(target_w) * target_h * 3;
    if (options_.use_cpu_encoder) {
        pinned_buf_bytes_ = frame_bytes;
        const bool pinned = cuda_drv::loaded && cuda_drv::mem_alloc_host && cuda_drv::mem_free_host;
        for (auto& buf : pinned_host_buf_) {
            void* p = nullptr;
            if (pinned) {
                (void)cuda_drv::mem_alloc_host(&p, frame_bytes);
            } else {
                p = std::malloc(frame_bytes);
            }
            if (p == nullptr) {
                return err(ErrorCode::OutOfMemory,
                           "cannot allocate the CPU-encoder P010 staging buffer");
            }
            buf = pinned ? wrapCudaPinnedHost(p, frame_bytes, cuda_drv::mem_free_host)
                         : wrapSystemHeapHost(p, frame_bytes);
        }
    }
    return {};
}
Result<void> RemasterPipeline::initEncoderAndMuxerStage() {
    encode_worker_ = std::jthread([this](std::stop_token st) { encodeWorkerBody(st); });
    return {};
}
void RemasterPipeline::encodeWorkerBody(std::stop_token stop) {
    for (;;) {
        PendingEncodeItem item;
        {
            std::unique_lock<std::mutex> lock(pipeline_mutex_);
            queue_ready_.wait(lock, stop, [this] {
                return !encode_queue_.empty() || producer_done_ || consumerStopped();
            });
            if (stop.stop_requested()) return;
            if (consumerStopped()) return;
            if (encode_queue_.empty()) {
                if (producer_done_) return;
                continue;
            }
            item = std::move(encode_queue_.front());
            encode_queue_.pop_front();
        }
        auto& [remaster_out, item_pts, hdr10plus_prefix, force_keyframe, item_pinned_slot] = item;
        (void)item_pinned_slot;
        if (options_.use_cpu_encoder) {
            const int target_w = session_->targetWidth();
            const int target_h = session_->targetHeight();
            const size_t y_bytes = static_cast<size_t>(target_w) * 2 * target_h;
            const size_t uv_bytes = static_cast<size_t>(target_w) * 2 * (target_h / 2);
            uint8_t* dst = static_cast<uint8_t*>(pinned_host_buf_[pinned_slot_ & 1u].first.get());
            pinned_slot_ ^= 1u;

                if (cuda_drv::loaded && cuda_drv::memcpy_dtoh) {
                    cuda_drv::memcpy_dtoh(dst, remaster_out.y_plane, y_bytes);
                    cuda_drv::memcpy_dtoh(dst + y_bytes, remaster_out.uv_plane, uv_bytes);
                } else {
                    std::memcpy(dst, reinterpret_cast<const void*>(remaster_out.y_plane), y_bytes);
                    std::memcpy(dst + y_bytes, reinterpret_cast<const void*>(remaster_out.uv_plane), uv_bytes);
                }
                auto pkts = session_->cpuEncoder()->encodeP010Frame(
                    reinterpret_cast<const uint16_t*>(dst), target_w * 2,
                    reinterpret_cast<const uint16_t*>(dst + y_bytes), target_w * 2,
                    item_pts, force_keyframe, hdr10plus_prefix);
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
                encode_frame.width = remaster_out.width;
                encode_frame.height = remaster_out.height;
                encode_frame.format = remaster_out.format;
                encode_frame.planes[0] = remaster_out.y_plane;
                encode_frame.planes[1] = remaster_out.uv_plane;
                encode_frame.pitches[0] = remaster_out.y_pitch;
                encode_frame.pitches[1] = remaster_out.uv_pitch;
                encode_frame.bit_depth = 10;
                encode_frame.pts = item_pts;
                encode_frame.slot_index = 0;
                encode_frame.keyframe = force_keyframe ? 1 : 0;
                const RtxResult encoded = session_->shim().encoderEncode(session_->encoder(), &encode_frame, &packet);
                if (encoded != RTX_OK) {
                    std::lock_guard<std::mutex> lock(pipeline_mutex_);
                    consumer_failed_ = true;
                    consumer_error_ = err(mapShimResult(encoded), describe("NvEncEncodePicture", encoded,
                                                                         session_->shim().encoderLastError(session_->encoder())));
                    slot_free_.notify_all();
                    return;
                }
                auto written = session_->writeEncodedPacket(packet, item_pts, report_, hdr10plus_prefix);
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
        encode_queue_.emplace_back(out, pts, hdr10plus, force_key, pinned_slot_);
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
// 关键修复：必须在调用 bridgeLoader().remaster() 覆写 p010_ring[i % 3] 之前等待空闲槽位！
    in.y_pitch = frame.pitches[0];
    in.uv_pitch = frame.pitches[1];
    in.format = frame.format;
    const auto [icp, ict, icm, icr] = info.color;
    (void)icp; (void)ict;
    in.full_range = (icr == ColorRange::Full) ? 1 : 0;
    in.matrix = (icm == MatrixCoefficients::BT2020NC) ? 1 : 0;
    in.reset_temporal = reset_temporal ? 1 : 0;

    bool preview_this_frame = false;
    if (options_.preview && session_->previewReady()) {
        uint32_t acquired = 0;
        if (session_->bridgeLoader().acquirePreviewSlot(&acquired)) {
            in.preview_rgba10_ptr = session_->previewDevicePointer(acquired);
            in.preview_pitch = session_->previewPitch();
            in.split_x_norm = options_.split_ratio != nullptr
                ? std::clamp(options_.split_ratio->load(std::memory_order_relaxed), 0.0f, 1.0f) : 0.5f;
            preview_slot_ = acquired;
            preview_this_frame = true;
        } else {
            in.preview_rgba10_ptr = 0;
            in.preview_pitch = 0;
            in.split_x_norm = -1.0f;
        }
    }
    RtxNgxRemasterOutput out{};
    RtxNgxRemasterStats stats{};
    const RtxResult r = session_->bridgeLoader().remaster(session_->bridge(), &in, &out, &stats);
    if (release_slot && frame.slot_index != 0xFFFFFFFFu) {
// Phase 11.2 (C-02): 防撕裂防阻塞调度。写入槽位前先经注册表探测原子世代
// 标志，三槽均忙（视口尚未读完）则置 preview_rgba10_ptr = 0 跳过本次预览
// 拷贝，绝不阻塞转码流水线。
        session_->releaseDecodedFrame(frame.slot_index);
    }
    if (r != RTX_OK) {
        if (preview_this_frame) {
            (*session_->bridgeLoader().previewSlotFlagsArray())[preview_slot_].store(0, std::memory_order_release);
        }
        return err(mapShimResult(r), describe("rtx_ngx_bridge_remaster", r, nullptr),
                   session_->bridgeLoader().lastError(session_->bridge()));
    }
    const double pack_delta = static_cast<double>(stats.pack_kernel_ms - previous_stats_.pack_kernel_ms);
    previous_stats_ = stats;
    if (pack_delta > pack_max_ms_) pack_max_ms_ = pack_delta;
    accumulated_max_cll_ = std::max<uint32_t>(accumulated_max_cll_, out.frame_max_nits);
    accumulated_max_fall_ = std::max<uint32_t>(accumulated_max_fall_, out.frame_avg_nits);

    if (preview_this_frame) {
        session_->bridgeLoader().publishPreviewSlot(preview_slot_);
        writeWebuiPreviewBmpIfDue(session_->bridgeLoader(), session_->bridge(), session_->previewPitch());
    }
    std::vector<uint8_t> hdr10plus;
    if (options_.hdr10plus) {
        const float ratio = out.frame_max_nits > 0
            ? static_cast<float>(out.frame_avg_nits) / static_cast<float>(out.frame_max_nits) : 0.5f;
        const uint32_t max_nits = std::max<uint32_t>(1u, out.frame_max_nits);
        const uint32_t avg_nits = std::max<uint32_t>(1u, out.frame_avg_nits);
        const Hdr10PlusFrameStats fstats{
            max_nits, max_nits, max_nits, avg_nits, uint16_t{500},
            static_cast<uint16_t>(std::clamp(static_cast<int>(ratio * 1000.0f), 1, 1000))};
        hdr10plus = (options_.codec == VideoCodec::HEVC)
            ? SeiSynthesizer::hevcHdr10PlusPrefixSei(fstats)
            : SeiSynthesizer::av1Hdr10PlusMetadataObu(fstats);
    }
    auto q_res = queueEncodeItem(out, effective_pts, hdr10plus, reset_temporal);
    if (!q_res.has_value()) return q_res;
    ++frame_index_;
    return {};
}
// rtx_ngx_bridge_remaster 是同步调用（pack 流收尾已同步）：槽位缓冲对
// 视口的 D3D12 队列完全可见，直接发布给 GUI 30 Hz 消费者。槽位空闲标志
// 由视口的 Fence 退役回调（PreviewSlotTicket）归还，全流程 D2H == 0 B。
Result<void> RemasterPipeline::synthesizeAndRemasterWindow(
    const RtxFrameDesc& prev_f, const RtxFrameDesc& curr_f, int64_t out_pts_90k, float alpha) {
    if (alpha <= 0.0001f) {
        return remasterAndEnqueueFrame(prev_f, out_pts_90k, /*release_slot=*/false);
    }
    if (alpha >= 0.9999f) {
        return remasterAndEnqueueFrame(curr_f, out_pts_90k, /*release_slot=*/false);
    }
    const uint32_t pitch = prev_f.pitches[0];
    const auto [fcp, fct, fcm, fcr] = session_->info().color; (void)fcp; (void)fct; (void)fcm;
    const int input_full_range = (fcr == ColorRange::Full) ? 1 : 0;

    if (rife_destroy_guard_) {
        if (prev_f.width <= 0 || prev_f.height <= 0 ||
            prev_f.width != session_->info().width || prev_f.height != session_->info().height ||
            pitch < static_cast<uint32_t>(prev_f.width) ||
            prev_f.planes[0] == 0 || curr_f.planes[0] == 0 || fruc_out_buffer_ == 0) {
            ++report_.fruc_failures;
            return err(ErrorCode::InvalidArgument, "RIFE geometry/pitch mismatch before interpolation");
        }
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
    // Phase 13 (Wave 2-B): Real pixel SAD + black-fade scene-cut bypass runs inside
    // rtx_ngx_bridge_interpolate_nv12 on the actual GPU NV12 Y-plane.
    const RtxResult status = session_->bridgeLoader().interpolateNv12(
        session_->bridge(), prev_f.planes[0], curr_f.planes[0], pitch, alpha, fruc_out_buffer_);
    if (status == RTX_OK) {
        if (session_->bridgeLoader().consumeNvofSceneCut(session_->bridge()) != 0) {
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
    return remasterAndEnqueueFrame(prev_f, out_pts_90k, /*release_slot=*/false);
}
Result<void> RemasterPipeline::handleDecodedFrame(const RtxFrameDesc& frame) {
    {
        const uint64_t working_set =
            static_cast<uint64_t>(session_->targetWidth()) *
            static_cast<uint64_t>(session_->targetHeight()) * 16ull;
        if (vramObserveFrame(vram_scheduler_, 0ull, working_set)) {
            logWarn(std::format("[VRAM-SCHED] Tier transitioned to {} (pressure={:.1f}%)",
                                pipelineTierName(vramCurrentTier(vram_scheduler_)),
                                vramLastPressureRatio(vram_scheduler_) * 100.0));
        }
    }
    constexpr int64_t kFlicksPerTick90k = static_cast<int64_t>(kFlicksPerSecond / 90000ULL);
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
    constexpr int64_t kFlicksPerTick90k = static_cast<int64_t>(kFlicksPerSecond / 90000ULL);
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
    encode_worker_.join();
    releasePrevLease();
    if (consumer_failed_) {
        return consumer_error_;
    }
    auto flushed_enc = session_->flushEncoder(report_);
    if (!flushed_enc.has_value()) return flushed_enc;

    auto flushed_aud = flushBufferedAudioPackets();
    if (!flushed_aud.has_value()) return flushed_aud;

    if (rife_destroy_guard_ && report_.fruc_failures > 0 && report_.frames_interpolated == 0) {
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

Result<void> RemasterPipeline::runRenderGraph(std::span<const RenderGraphSegment> segments,
                                              std::span<const AudioGraphSegment> /*audio_segments*/,
                                              std::stop_token stop_tok) {
    if (segments.empty()) {
        return err(ErrorCode::InvalidArgument, "render graph: empty segment list");
    }
    const auto& [first_path, f_sin, f_sout, f_tin, f_tout, f_hc, f_fk] = segments.front();
    (void)f_sin; (void)f_sout; (void)f_tin; (void)f_tout; (void)f_hc; (void)f_fk;
    if (options_.input_path.empty()) {
        options_.input_path = std::filesystem::path(first_path);
    }

    auto d_res = initDecodeAndDemuxStage();
    if (!d_res.has_value()) return d_res;
    auto r_res = initRemasterAndFgStage();
    if (!r_res.has_value()) return r_res;
    auto e_res = initEncoderAndMuxerStage();
    if (!e_res.has_value()) return e_res;

    using Ticks90k = std::chrono::duration<int64_t, std::ratio<1, 90000>>;
    const Rational clock_tb{1, 90000};
    const Clock::time_point loop_start = Clock::now();

    std::string active_source = first_path;
    std::unique_ptr<FfmpegDemuxer> owned_demux;
    FfmpegDemuxer* active_demux = session_->demuxer();
    MediaInfo active_info = session_->info();

    auto destroyExtraDecoder = [sh = &session_->shim()](RtxDecoder* d) {
        if (d != nullptr) sh->decoderDestroy(d);
    };
    std::unique_ptr<RtxDecoder, decltype(destroyExtraDecoder)> owned_decoder(nullptr, destroyExtraDecoder);
    RtxDecoder* active_decoder = session_->decoder();

    int64_t last_emitted_pts_90k = -1;
    Result<void> pipe_status;

    for (std::size_t seg_idx = 0; seg_idx < segments.size(); ++seg_idx) {
        const auto& [seg_source_path, seg_src_in, seg_src_out, seg_tl_in, seg_tl_out, seg_hard_cut, seg_force_kf] = segments[seg_idx];
        (void)seg_tl_out; (void)seg_hard_cut;
        if (stop_tok.stop_requested() || consumerStopped()) {
            pipe_status = err(ErrorCode::State, "transcode cancelled");
            break;
        }
        if (seg_source_path != active_source) {
            auto next_demux = makeFfmpegDemuxer();
            if (!next_demux) {
                pipe_status = err(ErrorCode::Internal, "render graph: failed to allocate segment demuxer");
                break;
            }
            auto opened = next_demux->open(std::filesystem::path(seg_source_path));
            if (!opened.has_value()) {
                pipe_status = std::unexpected(opened.error());
                break;
            }
            RtxDecoderConfig dec_cfg{};
            dec_cfg.codec = shimCodec(opened->codec);
            dec_cfg.width = opened->width;
            dec_cfg.height = opened->height;
            dec_cfg.bit_depth = opened->bit_depth;
            dec_cfg.output_format = opened->bit_depth > 8 ? RTX_PIXEL_P010 : RTX_PIXEL_NV12;
            dec_cfg.clock_rate = 90000;
            dec_cfg.max_surfaces = 32;
            dec_cfg.pool_slots = 16;
            dec_cfg.device_index = options_.device_index;
            RtxDecoder* raw_dec = nullptr;
            const RtxResult created = session_->shim().decoderCreate(&dec_cfg, &raw_dec);
            if (created != RTX_OK || raw_dec == nullptr) {
                pipe_status = err(mapShimResult(created),
                                  describe("rtx_shim_decoder_create(segment)", created, nullptr));
                break;
            }
            owned_demux = std::move(next_demux);
            active_demux = owned_demux.get();
            active_info = *opened;
            owned_decoder.reset(raw_dec);
            active_decoder = owned_decoder.get();
            active_source = seg_source_path;
        } else if (seg_idx > 0) {
            (void)session_->shim().decoderReset(active_decoder);
        }

        const Rational input_tb = active_info.time_base.valid() ? active_info.time_base : clock_tb;
        if (seg_src_in > 0) {
            const int64_t seek_pts = rescaleQ(seg_src_in, kTimeBaseFlicks, input_tb);
            (void)active_demux->seekToKeyframeBefore(seek_pts);
        }

        const Rational& fps = active_info.frame_rate.valid() && active_info.frame_rate.num > 0
                                  ? active_info.frame_rate
                                  : session_->frameRate();
        const int64_t frame_ticks = (fps.num > 0 && fps.den > 0)
            ? std::max<int64_t>(1, static_cast<int64_t>(90000) * fps.den / fps.num) : 3000;
        int64_t fallback_pts = rescaleQ(seg_src_in, kTimeBaseFlicks, clock_tb);
        bool first_frame_of_segment = true;
        bool seg_done = false;

        auto emitFrame = [&](const RtxFrameDesc& fdesc) -> Result<void> {
            const int64_t frame_src_flicks = absoluteTimestampToFlicks(fdesc.pts, clock_tb);
            if (frame_src_flicks < seg_src_in) {
                session_->shim().decoderReleaseFrame(active_decoder, fdesc.slot_index);
                return {};
            }
            if (frame_src_flicks >= seg_src_out) {
                session_->shim().decoderReleaseFrame(active_decoder, fdesc.slot_index);
                seg_done = true;
                return {};
            }
            const int64_t frame_flicks_offset = frame_src_flicks - seg_src_in;
            int64_t pts_90k = std::chrono::duration_cast<Ticks90k>(
                Flicks{seg_tl_in + frame_flicks_offset}).count();
            if (last_emitted_pts_90k >= 0 && pts_90k <= last_emitted_pts_90k) {
                pts_90k = last_emitted_pts_90k + 1;
            }
            last_emitted_pts_90k = pts_90k;

            // 段首帧强制重置 VSR 时域历史并在硬切点强制注入 IDR 关键帧（C-05 闭环）。
            const bool reset_temporal = first_frame_of_segment;
            if (first_frame_of_segment && seg_force_kf) {
                last_was_cut_ = true;
            }
            first_frame_of_segment = false;

            const bool release_via_session = (active_decoder == session_->decoder());
            Result<void> res = remasterAndEnqueueFrame(fdesc, pts_90k, release_via_session, reset_temporal);
            if (!release_via_session && fdesc.slot_index != 0xFFFFFFFFu) {
                session_->shim().decoderReleaseFrame(active_decoder, fdesc.slot_index);
            }
            ++report_.frames_decoded;
            reportProgress();
            return res;
        };

        while (!seg_done) {
            if (stop_tok.stop_requested() || consumerStopped()) {
                pipe_status = err(ErrorCode::State, "transcode cancelled");
                break;
            }
            auto pkt_res = active_demux->readPacket();
            if (!pkt_res.has_value()) {
                if (pkt_res.error().code == ErrorCode::NotFound) break;
                pipe_status = std::unexpected(pkt_res.error());
                break;
            }
            ++report_.packets_read;
            const Packet& pkt = pkt_res.value();
            if (active_info.primary_audio_index >= 0 && pkt.stream_index == active_info.primary_audio_index) {
                continue;
            }
            const int64_t pts = pkt.pts == std::numeric_limits<int64_t>::min()
                                    ? fallback_pts
                                    : rescaleQ(pkt.pts, input_tb, clock_tb);
            const int64_t dts = pkt.dts == std::numeric_limits<int64_t>::min()
                                    ? pts
                                    : rescaleQ(pkt.dts, input_tb, clock_tb);
            const RtxResult subm = session_->shim().decoderSubmit(
                active_decoder, pkt.data.data(), pkt.data.size(), pts, dts, pkt.keyframe ? 1 : 0);
            if (subm != RTX_OK) {
                pipe_status = err(mapShimResult(subm),
                                  describe("cuvidParseVideoData(segment)", subm,
                                           session_->shim().decoderLastError(active_decoder)));
                break;
            }
            for (;;) {
                RtxFrameDesc fdesc{};
                const RtxResult rec = session_->shim().decoderReceive(active_decoder, &fdesc);
                if (rec == RTX_OK) {
                    if (auto emitted = emitFrame(fdesc); !emitted.has_value()) {
                        pipe_status = emitted;
                        break;
                    }
                    if (seg_done) break;
                    continue;
                }
                if (rec == RTX_ERR_NOT_FOUND) break;
                pipe_status = err(mapShimResult(rec),
                                  describe("rtx_shim_decoder_receive(segment)", rec,
                                           session_->shim().decoderLastError(active_decoder)));
                break;
            }
            if (!pipe_status.has_value()) break;
            fallback_pts += frame_ticks;
        }
        if (!pipe_status.has_value()) break;

        if (!seg_done) {
            session_->shim().decoderFlush(active_decoder);
            for (;;) {
                RtxFrameDesc fdesc{};
                const RtxResult rec = session_->shim().decoderReceive(active_decoder, &fdesc);
                if (rec != RTX_OK) break;
                if (auto emitted = emitFrame(fdesc); !emitted.has_value()) {
                    pipe_status = emitted;
                    break;
                }
                if (seg_done) break;
            }
        }
        if (!pipe_status.has_value()) break;
    }

    report_.main_loop_ms = std::chrono::duration<double, std::milli>(Clock::now() - loop_start).count();
    auto fin = flushAndFinalizePipeline();
    if (!pipe_status.has_value()) return pipe_status;
    return fin;
}

Result<HwTranscodeReport> runNvidiaRemasterRenderGraph(
    const HwTranscodeOptions& options,
    std::span<const RenderGraphSegment> segments,
    std::span<const AudioGraphSegment> audio_segments,
    std::stop_token stop_tok) {
    RemasterPipeline pipeline(options);
    auto res = pipeline.runRenderGraph(segments, audio_segments, stop_tok);
    if (!res.has_value()) {
        return std::unexpected(res.error());
    }
    return pipeline.report();
}

} // namespace rtx
