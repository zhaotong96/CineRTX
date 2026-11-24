// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors

#include "hw_transcode_internal.h"
#include "rtx/vram_scheduler.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <format>
#include <limits>
#include <vector>

namespace rtx {

int32_t shimCodec(VideoCodec codec) {
    switch (codec) {
        case VideoCodec::H264: return RTX_CODEC_H264;
        case VideoCodec::HEVC: return RTX_CODEC_HEVC;
        case VideoCodec::AV1:  return RTX_CODEC_AV1;
        default:               return RTX_CODEC_UNKNOWN;
    }
}
int32_t encoderCodec(VideoCodec codec) {
    switch (codec) {
        case VideoCodec::HEVC: return 0;
        case VideoCodec::AV1:  return 1;
        case VideoCodec::H264: return 2;
        default:               return -1;
    }
}
ErrorCode mapShimResult(RtxResult r) {
    switch (r) {
        case RTX_OK: return ErrorCode::Ok;
        case RTX_ERR_INVALID_ARGUMENT: return ErrorCode::InvalidArgument;
        case RTX_ERR_NOT_FOUND: return ErrorCode::NotFound;
        case RTX_ERR_IO: return ErrorCode::Io;
        case RTX_ERR_UNSUPPORTED: case RTX_ERR_NOT_IMPLEMENTED: return ErrorCode::Unsupported;
        case RTX_ERR_DLL_MISSING: return ErrorCode::DllMissing;
        case RTX_ERR_ARCH_UNSUPPORTED: return ErrorCode::ArchUnsupported;
        case RTX_ERR_DRIVER_TOO_OLD: return ErrorCode::DriverTooOld;
        case RTX_ERR_DEVICE_LOST: return ErrorCode::DeviceLost;
        case RTX_ERR_OUT_OF_MEMORY: return ErrorCode::OutOfMemory;
        case RTX_ERR_NGX: return ErrorCode::NgxFailure;
        case RTX_ERR_RESERVED_11: return ErrorCode::Reserved11;
        default: return ErrorCode::Internal;
    }
}
const char* shimResultName(RtxResult r) {
    switch (r) {
        case RTX_OK: return "RTX_OK";
        case RTX_ERR_INVALID_ARGUMENT: return "RTX_ERR_INVALID_ARGUMENT";
        case RTX_ERR_NOT_FOUND: return "RTX_ERR_NOT_FOUND";
        case RTX_ERR_IO: return "RTX_ERR_IO";
        case RTX_ERR_UNSUPPORTED: return "RTX_ERR_UNSUPPORTED";
        case RTX_ERR_DLL_MISSING: return "RTX_ERR_DLL_MISSING";
        case RTX_ERR_ARCH_UNSUPPORTED: return "RTX_ERR_ARCH_UNSUPPORTED";
        case RTX_ERR_DRIVER_TOO_OLD: return "RTX_ERR_DRIVER_TOO_OLD";
        case RTX_ERR_DEVICE_LOST: return "RTX_ERR_DEVICE_LOST";
        case RTX_ERR_OUT_OF_MEMORY: return "RTX_ERR_OUT_OF_MEMORY";
        case RTX_ERR_NGX: return "RTX_ERR_NGX";
        case RTX_ERR_RESERVED_11: return "RTX_ERR_RESERVED_11";
        case RTX_ERR_NOT_IMPLEMENTED: return "RTX_ERR_NOT_IMPLEMENTED";
        case RTX_ERR_INTERNAL: return "RTX_ERR_INTERNAL";
        default: return "RTX_ERR_UNKNOWN";
    }
}
std::string describe(const char* op, RtxResult res, const char* detail) {
    std::string m = std::string(op) + " failed: " + shimResultName(res);
    if (detail && detail[0]) { m += " ("; m += detail; m += ")"; }
    return m;
}
RtxHdrMetadata toShimHdr(const Hdr10Metadata& hdr) {
    RtxHdrMetadata out{};
    out.enabled = hdr.enabled ? 1 : 0;
    const auto [rx, ry, gx, gy, bx, by, wx, wy, max_l, min_l] = hdr.mastering;
    out.red_x = rx; out.red_y = ry; out.green_x = gx; out.green_y = gy;
    out.blue_x = bx; out.blue_y = by; out.white_x = wx; out.white_y = wy;
    out.max_luminance = max_l; out.min_luminance = min_l;
    out.max_cll = hdr.cll.first;
    out.max_fall = hdr.cll.second;
    const auto [cp, ct, cm, cr] = hdr.color;
    out.primaries = static_cast<int32_t>(cp);
    out.transfer = static_cast<int32_t>(ct);
    out.matrix = static_cast<int32_t>(cm);
    out.range = static_cast<int32_t>(cr);
    out.target_nits = hdr.target_nits;
    return out;
}
void applyHdrDefaults(Hdr10Metadata& hdr) {
    if (!hdr.enabled) return;
    const Result<Hdr10Metadata> canonical = SeiSynthesizer::makeMetadata(defaultTrueHdrParameters());
    if (!canonical.has_value()) return;
    const Hdr10Metadata& d = canonical.value();
    auto& [rx, ry, gx, gy, bx, by, wx, wy, max_l, min_l] = hdr.mastering;
    const auto [drx, dry, dgx, dgy, dbx, dby, dwx, dwy, dmax_l, dmin_l] = d.mastering;
    if (max_l == 0) max_l = dmax_l;
    if (min_l == 0) min_l = dmin_l;
    if (rx == 0) {
        rx = drx; ry = dry; gx = dgx; gy = dgy;
        bx = dbx; by = dby; wx = dwx; wy = dwy;
    }
    if (hdr.cll.first == 0) { hdr.cll.first = d.cll.first; hdr.cll.second = d.cll.second; }
    if (hdr.target_nits == 0) hdr.target_nits = d.target_nits;
}
Rational chooseFrameRate(const HwTranscodeOptions& options, const MediaInfo& info) {
    const Rational src_fps = (info.frame_rate.valid() && info.frame_rate.num > 0) ? info.frame_rate : Rational{24, 1};
    if (options.fruc_enabled) {
        if (options.target_fps.valid() && options.target_fps.num > 0) return options.target_fps;
        if (src_fps.toDouble() < 59.9) return Rational{60, 1};
    }
    return src_fps;
}
std::string jsonEscape(const std::string& value) {
    std::string output; output.reserve(value.size());
    for (const char raw : value) {
        const unsigned char c = static_cast<unsigned char>(raw);
        if (c == '"') output += "\"";
        else if (c == '\\') output += "\\\\";
        else if (c == '\n') output += "\\n";
        else if (c == '\r') output += "\\r";
        else if (c == '\t') output += "\\t";
        else if (c < 0x20 || c >= 0x80) { output += std::format("\\u{:04x}", static_cast<unsigned int>(c)); }
        else output += static_cast<char>(c);
    }
    return output;
}
std::string HwTranscodeReport::toJson() const {
    return std::format(
        "{{\n  \"backend\": \"{}\", \"device\": \"{}\", \"path\": \"{}\", \"input_container\": \"{}\", \"output_container\": \"{}\", \"input_codec\": \"{}\", \"output_codec\": \"{}\",\n"
        "  \"input_width\": {}, \"input_height\": {}, \"output_width\": {}, \"output_height\": {}, \"output_bit_depth\": {}, \"packets_read\": {}, \"packets_written\": {}, \"frames_decoded\": {}, \"frames_encoded\": {}, \"bitstream_bytes\": {},\n"
        "  \"decoder_device_to_device_bytes\": {}, \"decoder_device_to_host_bytes\": {}, \"decoder_host_pixel_copies\": {}, \"encoder_host_to_device_bytes\": {}, \"encoder_host_pixel_copies\": {}, \"zero_copy_main_loop\": {},\n"
        "  \"elapsed_ms\": {}, \"main_loop_ms\": {}, \"remaster_preprocess_ms\": {}, \"remaster_ngx_ms\": {}, \"remaster_pack_kernel_ms\": {}, \"remaster_frame_ms\": {}, \"remaster_pack_kernel_max_ms\": {},\n"
        "  \"remaster_pack_launches\": {}, \"vsr_quality\": {}, \"measured_max_cll_nits\": {}, \"measured_max_fall_nits\": {}, \"frames_interpolated\": {}, \"fruc_scene_cuts\": {}, \"fruc_failures\": {}, \"fruc_backend\": \"{}\"\n}}\n",
        jsonEscape(backend), jsonEscape(device), jsonEscape(path), jsonEscape(input_container), jsonEscape(output_container),
        jsonEscape(input_codec), jsonEscape(output_codec), input_width, input_height, output_width, output_height, output_bit_depth,
        packets_read, packets_written, frames_decoded, frames_encoded, bitstream_bytes,
        decoder_device_to_device_bytes, decoder_device_to_host_bytes, decoder_host_pixel_copies,
        encoder_host_to_device_bytes, encoder_host_pixel_copies, zero_copy_main_loop ? "true" : "false",
        elapsed_ms, main_loop_ms, remaster_preprocess_ms, remaster_ngx_ms, remaster_pack_kernel_ms,
        remaster_frame_ms, remaster_pack_kernel_max_ms, remaster_pack_launches, vsr_quality,
        measured_max_cll_nits, measured_max_fall_nits, frames_interpolated, fruc_scene_cuts, fruc_failures, jsonEscape(fruc_backend));
}
Result<std::unique_ptr<NvidiaTranscodeSession>> NvidiaTranscodeSession::create(
    const HwTranscodeOptions& options, bool is_remaster,
    HwTranscodeReport& report, int target_w, int target_h) {
    auto session = std::unique_ptr<NvidiaTranscodeSession>(new NvidiaTranscodeSession());
    auto res = session->configure(options, is_remaster, report, target_w, target_h);
    if (!res.has_value()) {
        return std::unexpected(res.error());
    }
    return session;
}
Result<void> NvidiaTranscodeSession::configure(
    const HwTranscodeOptions& options, bool is_remaster,
    HwTranscodeReport& report, int target_w, int target_h) {
    options_ = options;
    is_remaster_ = is_remaster;
    target_width_ = target_w;
    target_height_ = target_h;

    demuxer_ = makeFfmpegDemuxer();
    auto opened = demuxer_->open(options.input_path);
    if (!opened.has_value()) {
        return std::unexpected(opened.error());
    }
    info_ = opened.value();
    if (info_.width <= 0 || info_.height <= 0) {
        return err(ErrorCode::InvalidArgument, "demuxer reported an invalid video geometry");
    }
    if (shimCodec(info_.codec) == RTX_CODEC_UNKNOWN) {
        return err(ErrorCode::Unsupported, "unsupported input codec: " + info_.video_codec);
    }
    report.input_container = info_.container; report.input_codec = info_.video_codec;
    report.input_width = info_.width; report.input_height = info_.height;
    report.output_width = is_remaster ? target_width_ : info_.width;
    report.output_height = is_remaster ? target_height_ : info_.height;
    report.output_codec = videoCodecName(options.codec);
    const auto ext = options.output_path.extension().string();
    report.output_container = (!ext.empty() && ext.front() == '.') ? ext.substr(1) : ext;
    for (char& c : report.output_container) c = static_cast<char>(c | 0x20);

    std::string shim_error;
    if (is_remaster) {
        if (!bridge_loader_.open(&shim_error)) {
            return err(ErrorCode::DllMissing, "cannot load the NGX bridge", shim_error);
        }
        RtxNgxBridgeCaps caps{};
        const RtxResult caps_res = bridge_loader_.queryCaps(&caps);
        if (caps_res != RTX_OK || caps.vsr_available == 0 || caps.truehdr_available == 0) {
            return err(ErrorCode::NgxFailure, "the RTX Video SDK is not fully available for remaster",
                       caps_res == RTX_OK ? caps.diagnostics : describe("rtx_ngx_bridge_query_caps", caps_res, nullptr));
        }
    }
    if (!shim_.open(&shim_error)) {
        return err(ErrorCode::DllMissing, "cannot load the NVIDIA shim", shim_error);
    }
    if (shim_.probe(&diagnostics_) != RTX_OK || diagnostics_.device.name[0] == '\0') {
        return err(ErrorCode::DllMissing, "no NVIDIA CUDA device is available to rtx_shim", diagnostics_.summary);
    }
    report.device = diagnostics_.device.name;
    {
        const VramScheduler budget_probe = makeVramScheduler(kDefaultDxgiVramBudgetBytes, kVramMinDwellFrames);
        logInfo(std::format("VRAM hysteresis policy armed: budget={} MiB, high82={} MiB, low68={} MiB, dwell={}f",
                            vramDxgiBudgetBytes(budget_probe) / (1024ull * 1024ull),
                            vramHighWatermarkBytes(budget_probe) / (1024ull * 1024ull),
                            vramLowWatermarkBytes(budget_probe) / (1024ull * 1024ull),
                            kVramMinDwellFrames));
    }

    if (options_.codec == VideoCodec::AV1 && !options_.use_cpu_encoder &&
        (diagnostics_.device.compute_major < 8 ||
         (diagnostics_.device.compute_major == 8 && diagnostics_.device.compute_minor < 9))) {
        options_.codec = VideoCodec::HEVC;
        report.output_codec = "hevc";
        logWarn(std::format("GPU (SM {}.{}) lacks hardware AV1 NVENC; automatically falling back to HEVC Main10.",
                            diagnostics_.device.compute_major, diagnostics_.device.compute_minor));
    }
    const int source_depth = info_.bit_depth > 8 ? 10 : 8;
    const int output_depth = 10;
    report.output_bit_depth = output_depth;

    const bool is_portrait_input = (info_.width > 0 && info_.height > 0 && info_.width < info_.height);
    const GeometryTargetMode geom_mode = (options.geometry_mode == 1)
        ? GeometryTargetMode::Pillarbox
        : GeometryTargetMode::AutoDirect;
    bridge_config_.output_offset_x = 0;
    bridge_config_.output_offset_y = 0;
    bridge_config_.scaled_width = 0;
    bridge_config_.scaled_height = 0;
    if (is_remaster && (!options.vsr_enabled || (target_width_ == 0 && target_height_ == 0))) {
        // Native or VSR-disabled: strictly pass through source resolution without upscaling.
        target_width_ = info_.width;
        target_height_ = info_.height;
        report.output_width = target_width_;
        report.output_height = target_height_;
    } else if (is_remaster && is_portrait_input && geom_mode == GeometryTargetMode::Pillarbox) {
        const auto [cw, ch, sw, sh, ox, oy, pl, pr, is_p] =
            calculateSmartGeometry(info_.width, info_.height, geom_mode, target_width_, target_height_);
        (void)pl; (void)pr; (void)is_p;
        if (info_.height > ch) {
            return err(ErrorCode::InvalidArgument,
                       std::format("pillarbox canvas height ({}) is smaller than portrait source height ({}); use AutoDirect or native mode",
                                   ch, info_.height));
        }
        target_width_ = cw;
        target_height_ = ch;
        bridge_config_.output_offset_x = ox;
        bridge_config_.output_offset_y = oy;
        bridge_config_.scaled_width = sw;
        bridge_config_.scaled_height = sh;
        report.output_width = target_width_;
        report.output_height = target_height_;
    } else if (is_remaster && (target_width_ <= 0 || target_height_ <= 0 ||
                               (is_portrait_input && geom_mode == GeometryTargetMode::AutoDirect &&
                                target_width_ > target_height_))) {
        const auto [cw, ch, sw, sh, ox, oy, pl, pr, is_p] =
            calculateSmartGeometry(info_.width, info_.height, geom_mode, target_width_, target_height_);
        (void)sw; (void)sh; (void)ox; (void)oy; (void)pl; (void)pr; (void)is_p;
        target_width_ = cw;
        target_height_ = ch;
        report.output_width = target_width_;
        report.output_height = target_height_;
    }
    const int clamped_nits = std::clamp(options.peak_nits > 0 ? options.peak_nits : 1000, 400, 4000);
    auto hdr_bridge_res = configureHdrAndBridge(report, clamped_nits);
    if (!hdr_bridge_res.has_value()) return hdr_bridge_res;

    auto dec_res = configureDecoder(source_depth);
    if (!dec_res.has_value()) return dec_res;

    output_info_ = info_;
    if (!options_.copy_audio) {
        output_info_.primary_audio_index = -1;
        output_info_.audio_tracks.clear();
        std::erase_if(output_info_.passthrough_streams,
                      [](const PassthroughStreamInfo& s) { const auto& [idx, ct, cid, cn, tb, op] = s; (void)idx; (void)cid; (void)cn; (void)tb; (void)op; return ct == 1; });
    }
    output_info_.time_base = Rational{1, 90000};
    output_info_.codec = options.codec;
    output_info_.video_codec = report.output_codec;
    output_info_.format_name.clear();
    output_info_.annex_b = true;
    output_info_.color = hdr_.color;
    output_info_.bit_depth = output_depth;
    frame_rate_ = chooseFrameRate(options, info_);
    output_info_.frame_rate = frame_rate_;
    return {};
}
Result<void> NvidiaTranscodeSession::configureHdrAndBridge(HwTranscodeReport& /*report*/, int clamped_nits) {
    if (is_remaster_) {
        TrueHdrParameters hdr_params = defaultTrueHdrParameters();
        auto& [hc, hs, hmg, hmax, hmst, hfall] = hdr_params;
        (void)hc; (void)hs; (void)hmg; (void)hmst; (void)hfall;
        hmax = clamped_nits;
        const Result<Hdr10Metadata> canonical = SeiSynthesizer::makeMetadata(hdr_params);
        if (!canonical.has_value()) {
            return std::unexpected(canonical.error());
        }
        hdr_ = canonical.value();
        applyHdrDefaults(hdr_);

        std::memcpy(bridge_config_.luid, diagnostics_.device.luid, sizeof(bridge_config_.luid));
        bridge_config_.luid_node_mask = diagnostics_.device.luid_node_mask;
        bridge_config_.input_width = info_.width;
        bridge_config_.input_height = info_.height;
        bridge_config_.output_width = target_width_;
        bridge_config_.output_height = target_height_;
        bridge_config_.vsr_quality = options_.vsr_enabled ? std::clamp(options_.vsr_quality, 1, 4) : 0;
        bridge_config_.enable_truehdr = 1;
        bridge_config_.contrast = 100;
        bridge_config_.saturation = 100;
        bridge_config_.middle_gray = 50;
        bridge_config_.max_luminance = clamped_nits;
        bridge_config_.project_id = 0x52545831u;

        RtxNgxBridge* raw_bridge = nullptr;
        const RtxResult b_res = bridge_loader_.create(&bridge_config_, &raw_bridge);
        if (b_res != RTX_OK || raw_bridge == nullptr) {
            return err(mapShimResult(b_res), describe("rtx_ngx_bridge_create", b_res, bridge_loader_.lastError(nullptr)));
        }
        bridge_ = BridgeHandle(raw_bridge, [b = &bridge_loader_](RtxNgxBridge* ptr) { if (ptr) b->releaseBridge(ptr); });

        (void)bridge_loader_.previewBuffer(bridge_.get(), 0, &preview_ring_[0], &preview_pitch_);
        (void)bridge_loader_.previewBuffer(bridge_.get(), 1, &preview_ring_[1], &preview_pitch_);
        (void)bridge_loader_.previewBuffer(bridge_.get(), 2, &preview_ring_[2], &preview_pitch_);

        if (!options_.lut_path.empty()) {
            const Result<Lut3D> lut = loadLut3dFromCubeFile(options_.lut_path);
            if (!lut.has_value()) return std::unexpected(lut.error());
            if (!bridge_loader_.hasPhase7Api()) {
                return err(ErrorCode::Unsupported, "the loaded NGX bridge does not support 3D LUT (ABI < 3)");
            }
            const RtxResult lut_res = bridge_loader_.setLut3d(bridge_.get(), lut.value().second.data(), lut.value().first);
            if (lut_res != RTX_OK) {
                return err(mapShimResult(lut_res), describe("rtx_ngx_bridge_set_lut3d", lut_res, nullptr),
                           bridge_loader_.lastError(bridge_.get()));
            }
        }
    } else {
        hdr_ = info_.hdr;
        applyHdrDefaults(hdr_);
    }
    return {};
}
Result<void> NvidiaTranscodeSession::configureDecoder(int source_depth) {
    RtxDecoderConfig decoder_config{};
    decoder_config.codec = shimCodec(info_.codec);
    decoder_config.width = info_.width;
    decoder_config.height = info_.height;
    decoder_config.bit_depth = source_depth;
    decoder_config.output_format = source_depth > 8 ? RTX_PIXEL_P010 : RTX_PIXEL_NV12;
    decoder_config.clock_rate = 90000;
    decoder_config.max_surfaces = 32;
    decoder_config.pool_slots = 16;
    decoder_config.device_index = options_.device_index;

    RtxDecoder* raw_decoder = nullptr;
    const RtxResult d_res = shim_.decoderCreate(&decoder_config, &raw_decoder);
    if (d_res != RTX_OK) {
        return err(mapShimResult(d_res), describe("rtx_shim_decoder_create", d_res, nullptr),
                   shim_.decoderLastError(nullptr));
    }
    decoder_ = DecoderHandle(raw_decoder, [sh = &shim_](RtxDecoder* ptr) { if (ptr) sh->decoderDestroy(ptr); });

    if (options_.seek_start_ms >= 0) {
        const Rational tb = info_.time_base.valid() ? info_.time_base : Rational{1, 90000};
        const int64_t stream_pts = options_.seek_start_ms * tb.den / (1000 * tb.num);
        auto sought = demuxer_->seekToKeyframeBefore(stream_pts);
        if (!sought.has_value()) return sought;
    }
    return {};
}
Result<void> NvidiaTranscodeSession::createEncoder(const RtxFrameDesc& frame, HwTranscodeReport& report) {
    if (options_.use_cpu_encoder) {
        const int enc_w = is_remaster_ ? target_width_ : frame.width;
        const int enc_h = is_remaster_ ? target_height_ : frame.height;
        const int enc_crf = options_.qp > 0 ? options_.qp : 22;
        const int enc_preset = std::clamp(options_.preset, 1, 9);
        auto cpu_enc = CpuVideoEncoder::create(
            options_.codec, enc_w, enc_h, frame_rate_, enc_crf, enc_preset, 0, hdr_);
        if (!cpu_enc.has_value()) {
            return std::unexpected(cpu_enc.error());
        }
        cpu_encoder_ = std::make_unique<CpuVideoEncoder>(std::move(cpu_enc.value()));
        output_info_.bit_depth = is_remaster_ ? 10 : frame.bit_depth;
        output_info_.width = enc_w;
        output_info_.height = enc_h;
        report.output_bit_depth = output_info_.bit_depth;
        return {};
    }
    RtxEncoderConfig config{};
    config.codec = encoderCodec(options_.codec);
    config.width = is_remaster_ ? target_width_ : frame.width;
    config.height = is_remaster_ ? target_height_ : frame.height;
    config.fps_num = static_cast<int32_t>(frame_rate_.num);
    config.fps_den = static_cast<int32_t>(frame_rate_.den);
    config.bitrate_kbps = options_.bitrate_kbps;
    config.preset = std::clamp(options_.preset, 1, 7);
    config.hdr = toShimHdr(hdr_);
    config.input_format = is_remaster_ ? RTX_PIXEL_P010 : frame.format;
    config.gop_length = static_cast<int32_t>(std::max<int64_t>(1, frame_rate_.num > 0 ? frame_rate_.den * 2 : 60));
    config.rate_control = std::clamp(options_.rate_control, 0, 3); // 0=CBR, 1=VBR, 2=QP, 3=CQ
    config.qp = options_.qp;
    config.spatial_aq = (config.rate_control == RTX_RC_CQ || config.rate_control == RTX_RC_VBR) ? 1 : 0;
    config.aq_strength = 8;
    config.device_index = options_.device_index;
    RtxEncoder* raw = nullptr;
    const RtxResult created = shim_.encoderCreate(&config, &raw);
    if (created != RTX_OK || raw == nullptr) {
        return err(mapShimResult(created), describe("rtx_shim_encoder_create", created, nullptr));
    }
    encoder_ = EncoderHandle(raw, [sh = &shim_](RtxEncoder* ptr) { if (ptr) sh->encoderDestroy(ptr); });
    output_info_.bit_depth = is_remaster_ ? 10 : frame.bit_depth;
    output_info_.width = config.width;
    output_info_.height = config.height;
    report.output_bit_depth = output_info_.bit_depth;
    return {};
}
void NvidiaTranscodeSession::releaseDecodedFrame(uint64_t slot) {
    if (decoder_ != nullptr) shim_.decoderReleaseFrame(decoder_.get(), slot);
}
Result<void> NvidiaTranscodeSession::openMuxer() {
    std::lock_guard<std::mutex> lock(muxer_mutex_);
    if (muxer_ != nullptr) return {};
    if (options_.use_cpu_encoder) {
        if (!cpu_encoder_) return err(ErrorCode::Internal, "CPU encoder not initialized");
        auto extra = cpu_encoder_->extradata();
        output_info_.extradata.assign(extra.begin(), extra.end());
        output_info_.annex_b = false;
        muxer_ = makeFfmpegMuxer();
        return muxer_->open(options_.output_path, output_info_, hdr_);
    }
    size_t header_size = 0;
    if (shim_.encoderSequenceHeader(encoder_.get(), nullptr, 0, &header_size) != RTX_OK || header_size == 0)
        return err(ErrorCode::Internal, "NVENC sequence header missing", shim_.encoderLastError(encoder_.get()));
    std::vector<uint8_t> header(header_size);
    if (shim_.encoderSequenceHeader(encoder_.get(), header.data(), header.size(), &header_size) != RTX_OK)
        return err(ErrorCode::Internal, "cannot fetch NVENC header", shim_.encoderLastError(encoder_.get()));
    header.resize(header_size);
    output_info_.annex_b = (options_.codec != VideoCodec::AV1);
    output_info_.extradata = (options_.codec == VideoCodec::AV1)
        ? makeAv1ConfigRecord(header.data(), header.size(), true)
        : (options_.codec == VideoCodec::HEVC ? makeHevcConfigRecord(header.data(), header.size())
                                              : makeAvcConfigRecord(header.data(), header.size()));
    if (output_info_.extradata.empty()) return err(ErrorCode::Internal, "cannot build codec config record");
    muxer_ = makeFfmpegMuxer();
    return muxer_->open(options_.output_path, output_info_, hdr_);
}
Result<void> NvidiaTranscodeSession::writeEncodedPacket(
    const RtxPacketDesc& packet, int64_t fallback_pts, HwTranscodeReport& report,
    const std::vector<uint8_t>& side_data) {
    auto opened = openMuxer();
    if (!opened.has_value()) return opened;
    Packet out;
    if (packet.size != 0) out.data.assign(packet.data, packet.data + packet.size);
    if (!side_data.empty()) out.data.insert(out.data.begin(), side_data.begin(), side_data.end());
    out.pts = packet.pts != std::numeric_limits<int64_t>::min() ? packet.pts : fallback_pts;
    out.dts = packet.dts != std::numeric_limits<int64_t>::min() ? packet.dts : out.pts;
    out.duration = packet.duration;
    out.keyframe = packet.keyframe != 0;
    out.time_base = Rational{1, 90000};
    auto written = muxer_->writePacket(out);
    if (!written.has_value()) return written;
    ++report.packets_written;
    report.bitstream_bytes += packet.size;
    return {};
}
Result<void> NvidiaTranscodeSession::flushEncoder(HwTranscodeReport& report) {
    if (options_.use_cpu_encoder) {
        if (cpu_encoder_ && muxer_) {
            auto flushed = cpu_encoder_->flush();
            if (!flushed.has_value()) return std::unexpected(flushed.error());
            for (const auto& pkt : flushed.value()) {
                auto wr = muxer_->writePacket(pkt);
                if (!wr.has_value()) return wr;
                ++report.packets_written;
                report.bitstream_bytes += pkt.data.size();
            }
        }
        return {};
    }
    if (encoder_ != nullptr) {
        for (int attempt = 0; attempt < 8; ++attempt) {
            RtxPacketDesc packet{};
            const RtxResult flushed = shim_.encoderFlush(encoder_.get(), &packet);
            if (flushed == RTX_ERR_NOT_FOUND) break;
            if (flushed != RTX_OK) {
                return err(mapShimResult(flushed), describe("NvEncEncodePicture(EOS)", flushed,
                                                            shim_.encoderLastError(encoder_.get())));
            }
            if (packet.size != 0) {
                auto res = writeEncodedPacket(packet, packet.pts, report);
                if (!res.has_value()) return res;
            }
        }
    }
    return {};
}
Result<void> NvidiaTranscodeSession::finalize(HwTranscodeReport& report, Clock::time_point started) {
    if (!muxer_) return err(ErrorCode::Internal, "no frame was decoded; output was not created");
    auto fin = muxer_->finalize();
    if (!fin.has_value()) return fin;
    RtxHwStats dec_stats{}, enc_stats{};
    if (shim_.decoderStats(decoder_.get(), &dec_stats) == RTX_OK) {
        report.decoder_device_to_device_bytes = dec_stats.device_to_device_bytes;
        report.decoder_device_to_host_bytes = dec_stats.device_to_host_pixel_bytes;
        report.decoder_host_pixel_copies = dec_stats.host_pixel_copies;
    }
    if (encoder_ && shim_.encoderStats(encoder_.get(), &enc_stats) == RTX_OK) {
        report.encoder_host_to_device_bytes = enc_stats.host_to_device_pixel_bytes;
        report.encoder_host_pixel_copies = enc_stats.host_pixel_copies;
    }
    report.zero_copy_main_loop = (!options_.use_cpu_encoder &&
                                  report.decoder_host_pixel_copies == 0 &&
                                  report.encoder_host_pixel_copies == 0);
    report.elapsed_ms = std::chrono::duration<double, std::milli>(Clock::now() - started).count();
    return {};
}
Result<HwTranscodeReport> runNvidiaRemasterTranscode(const HwTranscodeOptions& options) {
    RemasterPipeline pipeline(options);
    return pipeline.run();
}
} // namespace rtx
// Phase 11.2 (Task 5-1 / C-02): runScrubPreview (the single-frame D2H scrub) is
// removed together with the GUI fake-playback loop; the zero-readback monitor
// path is MediaPlaybackSession + rtx_ngx_bridge_playback_pack + borrow_preview.
