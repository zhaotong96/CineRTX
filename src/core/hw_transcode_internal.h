// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
#pragma once

#include "rtx/hw_transcode.h"
#include "rtx/cpu_video_encoder.h"
#include "rtx/geometry.h"
#include "bitstream.h"
#include "rtx/hdr.h"
#include "rtx/logger.h"
#include "rtx/lut3d.h"
#include "rtx/ngx_remaster.h"
#include "rtx/nvidia_shim.h"
#include "rtx/shot_segmenter.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace rtx {

using Clock = std::chrono::steady_clock;

int32_t shimCodec(VideoCodec codec);
int32_t encoderCodec(VideoCodec codec);
ErrorCode mapShimResult(RtxResult r);
const char* shimResultName(RtxResult r);
std::string describe(const char* op, RtxResult res, const char* detail);
RtxHdrMetadata toShimHdr(const Hdr10Metadata& hdr);
void applyHdrDefaults(Hdr10Metadata& hdr);
Rational chooseFrameRate(const HwTranscodeOptions& options, const MediaInfo& info);
std::string jsonEscape(const std::string& value);

struct CudaDriverApi {
    int (*mem_alloc)(unsigned long long*, size_t) = nullptr;
    int (*mem_free)(unsigned long long) = nullptr;
    int (*mem_alloc_host)(void**, size_t) = nullptr;
    int (*mem_free_host)(void*) = nullptr;
    int (*memcpy_htod)(unsigned long long, const void*, size_t) = nullptr;
    int (*memcpy_dtoh)(void*, unsigned long long, size_t) = nullptr;
    int (*memcpy_dtoh_async)(void*, unsigned long long, size_t, void*) = nullptr;
    int (*memcpy_dtod_async)(unsigned long long, unsigned long long, size_t, void*) = nullptr;
    int (*memset_d32_async)(unsigned long long, unsigned int, size_t, void*) = nullptr;
    int (*stream_sync)(void*) = nullptr;
    bool loaded = false;

    bool init() {
        if (loaded) return true;
#if defined(_WIN32)
        HMODULE h = LoadLibraryExW(L"nvcuda.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!h) return false;
        mem_alloc = reinterpret_cast<decltype(mem_alloc)>(GetProcAddress(h, "cuMemAlloc_v2"));
        if (!mem_alloc) mem_alloc = reinterpret_cast<decltype(mem_alloc)>(GetProcAddress(h, "cuMemAlloc"));
        mem_free = reinterpret_cast<decltype(mem_free)>(GetProcAddress(h, "cuMemFree_v2"));
        if (!mem_free) mem_free = reinterpret_cast<decltype(mem_free)>(GetProcAddress(h, "cuMemFree"));
        mem_alloc_host = reinterpret_cast<decltype(mem_alloc_host)>(GetProcAddress(h, "cuMemAllocHost_v2"));
        if (!mem_alloc_host) mem_alloc_host = reinterpret_cast<decltype(mem_alloc_host)>(GetProcAddress(h, "cuMemAllocHost"));
        mem_free_host = reinterpret_cast<decltype(mem_free_host)>(GetProcAddress(h, "cuMemFreeHost_v2"));
        if (!mem_free_host) mem_free_host = reinterpret_cast<decltype(mem_free_host)>(GetProcAddress(h, "cuMemFreeHost"));
        memcpy_htod = reinterpret_cast<decltype(memcpy_htod)>(GetProcAddress(h, "cuMemcpyHtoD_v2"));
        if (!memcpy_htod) memcpy_htod = reinterpret_cast<decltype(memcpy_htod)>(GetProcAddress(h, "cuMemcpyHtoD"));
        memcpy_dtoh = reinterpret_cast<decltype(memcpy_dtoh)>(GetProcAddress(h, "cuMemcpyDtoH_v2"));
        if (!memcpy_dtoh) memcpy_dtoh = reinterpret_cast<decltype(memcpy_dtoh)>(GetProcAddress(h, "cuMemcpyDtoH"));
        memcpy_dtoh_async = reinterpret_cast<decltype(memcpy_dtoh_async)>(GetProcAddress(h, "cuMemcpyDtoHAsync_v2"));
        if (!memcpy_dtoh_async) memcpy_dtoh_async = reinterpret_cast<decltype(memcpy_dtoh_async)>(GetProcAddress(h, "cuMemcpyDtoHAsync"));
        memcpy_dtod_async = reinterpret_cast<decltype(memcpy_dtod_async)>(GetProcAddress(h, "cuMemcpyDtoDAsync_v2"));
        if (!memcpy_dtod_async) memcpy_dtod_async = reinterpret_cast<decltype(memcpy_dtod_async)>(GetProcAddress(h, "cuMemcpyDtoDAsync"));
        memset_d32_async = reinterpret_cast<decltype(memset_d32_async)>(GetProcAddress(h, "cuMemsetD32Async"));
        stream_sync = reinterpret_cast<decltype(stream_sync)>(GetProcAddress(h, "cuStreamSynchronize"));
        loaded = (mem_alloc && mem_free && memcpy_dtoh && memcpy_dtod_async && stream_sync);
        return loaded;
#else
        return false;
#endif
    }
};

class NvidiaTranscodeSession {
public:
    static Result<std::unique_ptr<NvidiaTranscodeSession>> create(
        const HwTranscodeOptions& options, bool is_remaster,
        HwTranscodeReport& report, int target_w = 0, int target_h = 0);

    ~NvidiaTranscodeSession();

    NvidiaTranscodeSession(const NvidiaTranscodeSession&) = delete;
    NvidiaTranscodeSession& operator=(const NvidiaTranscodeSession&) = delete;

    Result<void> createEncoder(const RtxFrameDesc& frame, HwTranscodeReport& report);
    void releaseDecodedFrame(uint64_t slot);
    Result<void> openMuxer();
    Result<void> writeEncodedPacket(const RtxPacketDesc& packet, int64_t fallback_pts,
                                    HwTranscodeReport& report,
                                    const std::vector<uint8_t>& side_data = {});
    Result<void> flushEncoder(HwTranscodeReport& report);
    Result<void> finalize(HwTranscodeReport& report, Clock::time_point started);

    NvidiaShim& shim() { return shim_; }
    NgxRemasterBridge& bridgeLoader() { return bridge_loader_; }
    RtxNgxBridge* bridge() const { return bridge_.get(); }
    RtxDecoder* decoder() const { return decoder_.get(); }
    RtxEncoder* encoder() const { return encoder_.get(); }
    IDemuxer* demuxer() const { return demuxer_.get(); }
    IMuxer* muxer() const { return muxer_.get(); }
    const MediaInfo& info() const { return info_; }
    int targetWidth() const noexcept { return target_width_; }
    int targetHeight() const noexcept { return target_height_; }
    bool isAudioPacket(const Packet& p) const noexcept {
        return info_.primary_audio_index >= 0 && p.stream_index == info_.primary_audio_index;
    }
    bool copyAudioEnabled() const noexcept { return options_.copy_audio; }
    bool isPassthroughPacket(const Packet& p) const noexcept {
        return isAudioPacket(p) || std::any_of(info_.passthrough_streams.begin(), info_.passthrough_streams.end(),
                                               [&p](const PassthroughStreamInfo& s) { return s.stream_index == p.stream_index; });
    }
    void setMeasuredCll(uint32_t max_cll_nits, uint32_t max_fall_nits) {
        if (max_cll_nits > 0) hdr_.cll.max_cll = static_cast<uint16_t>(std::min<uint32_t>(max_cll_nits, 65535));
        if (max_fall_nits > 0) hdr_.cll.max_fall = static_cast<uint16_t>(std::min<uint32_t>(max_fall_nits, 65535));
        measured_max_cll_nits_ = static_cast<int>(max_cll_nits);
        measured_max_fall_nits_ = static_cast<int>(max_fall_nits);
    }
    int measuredMaxCllNits() const noexcept { return measured_max_cll_nits_; }
    int measuredMaxFallNits() const noexcept { return measured_max_fall_nits_; }
    bool previewReady() const noexcept { return preview_ring_[0] != 0 && preview_ring_[1] != 0; }
    uint64_t previewDevicePointer(uint32_t slot) const noexcept { return slot < 2 ? preview_ring_[slot] : 0; }
    uint32_t previewPitch() const noexcept { return preview_pitch_; }
    RtxResult downloadPreviewInto(void* dst, size_t bytes) const {
        return bridge_loader_.downloadPreview(bridge_.get(), dst, bytes);
    }
    int audioStreamIndex() const noexcept { return info_.primary_audio_index; }
    bool isMuxerOpen() const noexcept { return muxer_ != nullptr; }
    Result<void> writeAudioPacket(const Packet& packet) {
        if (!muxer_) return err(ErrorCode::State, "muxer is not open");
        return muxer_->writePacket(packet);
    }
    const MediaInfo& outputInfo() const { return output_info_; }
    const Rational& frameRate() const { return frame_rate_; }
    const Hdr10Metadata& hdr() const { return hdr_; }
    CpuVideoEncoder* cpuEncoder() const noexcept { return cpu_encoder_.get(); }
    bool useCpuEncoder() const noexcept { return options_.use_cpu_encoder; }

private:
    NvidiaTranscodeSession() = default;
    Result<void> configure(const HwTranscodeOptions& options, bool is_remaster,
                           HwTranscodeReport& report, int target_w = 0, int target_h = 0);
    Result<void> configureHdrAndBridge(HwTranscodeReport& report, int clamped_nits);
    Result<void> configureDecoder(int source_depth);

    struct DecoderDeleter {
        NvidiaShim* shim = nullptr;
        void operator()(RtxDecoder* p) const { if (shim && p) shim->decoderDestroy(p); }
    };
    struct EncoderDeleter {
        NvidiaShim* shim = nullptr;
        void operator()(RtxEncoder* p) const { if (shim && p) shim->encoderDestroy(p); }
    };
    struct BridgeDeleter {
        NgxRemasterBridge* bridge = nullptr;
        void operator()(RtxNgxBridge* p) const { if (bridge && p) bridge->releaseBridge(p); }
    };

    using DecoderHandle = std::unique_ptr<RtxDecoder, DecoderDeleter>;
    using EncoderHandle = std::unique_ptr<RtxEncoder, EncoderDeleter>;
    using BridgeHandle = std::unique_ptr<RtxNgxBridge, BridgeDeleter>;

    HwTranscodeOptions options_{};
    bool is_remaster_ = false;
    int target_width_ = 0;
    int target_height_ = 0;
    NvidiaShim shim_;
    NgxRemasterBridge bridge_loader_;
    RtxDiagnostics diagnostics_{};
    MediaInfo info_{};
    MediaInfo output_info_{};
    Rational frame_rate_{24, 1};
    Hdr10Metadata hdr_{};
    std::unique_ptr<IDemuxer> demuxer_;
    DecoderHandle decoder_;
    EncoderHandle encoder_;
    std::unique_ptr<CpuVideoEncoder> cpu_encoder_;
    BridgeHandle bridge_;
    std::unique_ptr<IMuxer> muxer_;
    std::mutex muxer_mutex_;
    RtxNgxBridgeConfig bridge_config_{};
    uint64_t preview_ring_[2] = {0, 0};
    uint32_t preview_pitch_ = 0;
    int measured_max_cll_nits_ = 0;
    int measured_max_fall_nits_ = 0;
};

struct PendingEncodeItem {
    RtxNgxRemasterOutput remaster_out{};
    int64_t pts = 0;
    std::vector<uint8_t> hdr10plus_prefix;
    bool force_keyframe = false;
    uint32_t pinned_slot = 0;
};

class RemasterPipeline {
public:
    explicit RemasterPipeline(const HwTranscodeOptions& options);
    ~RemasterPipeline();

    Result<HwTranscodeReport> run();

    Result<void> initDecodeAndDemuxStage();
    Result<void> initRemasterAndFgStage();
    Result<void> initEncoderAndMuxerStage();
    Result<void> handleDecodedFrame(const RtxFrameDesc& frame);
    Result<void> synthesizeAndRemasterWindow(const RtxFrameDesc& prev_f, const RtxFrameDesc& curr_f,
                                             int64_t out_pts_90k, float alpha);
    Result<void> remasterAndEnqueueFrame(const RtxFrameDesc& frame, int64_t pts_override,
                                         bool release_slot, bool force_reset_temporal = false);
    Result<void> drainEndOfStream();
    Result<void> flushAndFinalizePipeline();

private:
    HwTranscodeOptions options_;
    HwTranscodeReport report_;
    Clock::time_point started_;
    std::unique_ptr<NvidiaTranscodeSession> session_;
    CudaDriverApi cuda_;

    ShotGroupSegmenter segmenter_;
    uint64_t frame_index_ = 0;
    bool last_was_cut_ = false;

    bool has_prev_frame_ = false;
    RtxFrameDesc prev_frame_{};
    int64_t prev_pts_flicks_ = 0;
    int64_t next_out_pts_flicks_ = -1;
    int64_t delta_out_flicks_ = 11760000LL;
    int fruc_error_logs_ = 0;

    uint64_t anchor_p010_[2] = {0, 0};
    uint32_t anchor_slot_ = 0;
    bool anchor_has_prev_ = false;
    int64_t prev_anchor_pts_ = 0;
    uint32_t anchor_pitch_ = 0;

    void* pinned_host_buf_[2] = {nullptr, nullptr};
    size_t pinned_buf_bytes_ = 0;
    uint32_t pinned_slot_ = 0;

    std::deque<PendingEncodeItem> encode_queue_;
    std::deque<Packet> audio_queue_;
    std::mutex audio_mtx_;
    std::mutex pipeline_mutex_;
    std::condition_variable queue_ready_;
    std::condition_variable slot_free_;
    bool producer_done_ = false;
    bool consumer_failed_ = false;
    Result<void> consumer_error_;
    uint64_t remasters_issued_ = 0;
    uint64_t encodes_completed_ = 0;
    std::thread encode_thread_;

    bool rife_active_ = false;
    uint64_t fruc_out_buffer_ = 0;
    RtxNgxRemasterStats previous_stats_{};
    double pack_max_ms_ = 0.0;
    uint32_t accumulated_max_cll_ = 0;
    uint32_t accumulated_max_fall_ = 0;
    uint32_t preview_slot_ = 0;
    std::vector<uint8_t> preview_host_;
    std::optional<Clock::time_point> last_preview_download_;
    int64_t estimated_total_frames_ = 0;

    void releasePrevLease() noexcept;
    bool consumerStopped() const noexcept;
    void reportProgress();
    Result<void> flushBufferedAudioPackets();
    Result<void> queueEncodeItem(const RtxNgxRemasterOutput& out, int64_t pts,
                                const std::vector<uint8_t>& hdr10plus, bool force_key);
};

} // namespace rtx
