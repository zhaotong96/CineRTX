// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
#include "rtx/hw_transcode.h"
#include "rtx/geometry.h"
#include "bitstream.h"
#include "rtx/hdr.h"
#include "rtx/lut3d.h"
#include "rtx/ngx_remaster.h"
#include "rtx/nvidia_shim.h"
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <format>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace rtx {
namespace {
using Clock = std::chrono::steady_clock;

int32_t shimCodec(VideoCodec codec) {
    switch (codec) {
        case VideoCodec::H264:
            return RTX_CODEC_H264;
        case VideoCodec::HEVC:
            return RTX_CODEC_HEVC;
        case VideoCodec::AV1:
            return RTX_CODEC_AV1;
        default:
            return RTX_CODEC_UNKNOWN;
    }
}

int32_t encoderCodec(VideoCodec codec) {
    switch (codec) {
        case VideoCodec::HEVC:
            return 0;
        case VideoCodec::AV1:
            return 1;
        case VideoCodec::H264:
            return 2;
        default:
            return -1;
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
    out.red_x = hdr.mastering.red_x;
    out.red_y = hdr.mastering.red_y;
    out.green_x = hdr.mastering.green_x;
    out.green_y = hdr.mastering.green_y;
    out.blue_x = hdr.mastering.blue_x;
    out.blue_y = hdr.mastering.blue_y;
    out.white_x = hdr.mastering.white_x;
    out.white_y = hdr.mastering.white_y;
    out.max_luminance = hdr.mastering.max_luminance;
    out.min_luminance = hdr.mastering.min_luminance;
    out.max_cll = hdr.cll.max_cll;
    out.max_fall = hdr.cll.max_fall;
    out.primaries = static_cast<int32_t>(hdr.color.primaries);
    out.transfer = static_cast<int32_t>(hdr.color.transfer);
    out.matrix = static_cast<int32_t>(hdr.color.matrix);
    out.range = static_cast<int32_t>(hdr.color.range);
    out.target_nits = hdr.target_nits;
    return out;
}
void applyHdrDefaults(Hdr10Metadata& hdr) {
    if (!hdr.enabled) return;
    const Result<Hdr10Metadata> canonical = SeiSynthesizer::makeMetadata(TrueHdrParameters{});
    if (!canonical.has_value()) return;
    const Hdr10Metadata& d = canonical.value();
    if (hdr.mastering.max_luminance == 0) hdr.mastering.max_luminance = d.mastering.max_luminance;
    if (hdr.mastering.min_luminance == 0) hdr.mastering.min_luminance = d.mastering.min_luminance;
    if (hdr.mastering.red_x == 0) {
        hdr.mastering.red_x = d.mastering.red_x; hdr.mastering.red_y = d.mastering.red_y;
        hdr.mastering.green_x = d.mastering.green_x; hdr.mastering.green_y = d.mastering.green_y;
        hdr.mastering.blue_x = d.mastering.blue_x; hdr.mastering.blue_y = d.mastering.blue_y;
        hdr.mastering.white_x = d.mastering.white_x; hdr.mastering.white_y = d.mastering.white_y;
    }
    if (hdr.cll.max_cll == 0) { hdr.cll.max_cll = d.cll.max_cll; hdr.cll.max_fall = d.cll.max_fall; }
    if (hdr.target_nits == 0) hdr.target_nits = d.target_nits;
}
Rational chooseFrameRate(const HwTranscodeOptions& options, const MediaInfo& info) {
    const Rational src_fps = (info.frame_rate.valid() && info.frame_rate.num > 0) ? info.frame_rate : Rational{24, 1};
    if (options.fps.valid() && options.fps.num > 0) return options.fps;
    if (options.fruc_enabled) {
        if (options.target_fps.valid() && options.target_fps.num > 0) return options.target_fps;
        if (src_fps.toDouble() < 59.9) return Rational{60, 1};
    }
    return src_fps;
}
int chooseOutputDepth(const HwTranscodeOptions& options, const MediaInfo& info) {
    if (options.bit_depth == 8 || options.bit_depth == 10) return options.bit_depth;
    return info.bit_depth > 8 ? 10 : 8;
}

struct DecoderDeleter {
    const NvidiaShim* shim = nullptr;
    void operator()(RtxDecoder* d) const { if (shim && d) shim->decoderDestroy(d); }
};
struct EncoderDeleter {
    const NvidiaShim* shim = nullptr;
    void operator()(RtxEncoder* e) const { if (shim && e) shim->encoderDestroy(e); }
};
struct BridgeDeleter {
    const NgxRemasterBridge* loader = nullptr;
    void operator()(RtxNgxBridge* b) const { if (loader && b) loader->releaseBridge(b); }
};

using DecoderHandle = std::unique_ptr<RtxDecoder, DecoderDeleter>;
using EncoderHandle = std::unique_ptr<RtxEncoder, EncoderDeleter>;
using BridgeHandle = std::unique_ptr<RtxNgxBridge, BridgeDeleter>;

std::string jsonEscape(const std::string& value) {
    std::string output; output.reserve(value.size());
    for (const char raw : value) {
        const unsigned char c = static_cast<unsigned char>(raw);
        if (c == '"') output += "\"";
        else if (c == '\\') output += "\\\\";
        else if (c == '\n') output += "\\n";
        else if (c == '\r') output += "\\r";
        else if (c == '\t') output += "\\t";
        else if (c < 0x20 || c >= 0x80) { char b[8] = {}; std::snprintf(b, sizeof(b), "\\u%04x", c); output += b; }
        else output += static_cast<char>(c);
    }
    return output;
}
}  // namespace

std::string HwTranscodeReport::toJson() const {
    return std::format(
        "{{\n  \"backend\": \"{}\", \"device\": \"{}\", \"path\": \"{}\", \"input_container\": \"{}\", \"output_container\": \"{}\", \"input_codec\": \"{}\", \"output_codec\": \"{}\",\n"
        "  \"input_width\": {}, \"input_height\": {}, \"output_width\": {}, \"output_height\": {}, \"output_bit_depth\": {}, \"packets_read\": {}, \"packets_written\": {}, \"frames_decoded\": {}, \"frames_encoded\": {}, \"bitstream_bytes\": {},\n"
        "  \"decoder_device_to_device_bytes\": {}, \"decoder_device_to_host_bytes\": {}, \"decoder_host_pixel_copies\": {}, \"encoder_host_to_device_bytes\": {}, \"encoder_host_pixel_copies\": {}, \"zero_copy_main_loop\": {},\n"
        "  \"elapsed_ms\": {}, \"main_loop_ms\": {}, \"remaster_preprocess_ms\": {}, \"remaster_ngx_ms\": {}, \"remaster_pack_kernel_ms\": {}, \"remaster_frame_ms\": {}, \"remaster_pack_kernel_max_ms\": {},\n"
        "  \"remaster_pack_launches\": {}, \"vsr_quality\": {}, \"measured_max_cll_nits\": {}, \"measured_max_fall_nits\": {}, \"frames_interpolated\": {}, \"fruc_scene_cuts\": {}, \"fruc_backend\": \"{}\"\n}}\n",
        jsonEscape(backend), jsonEscape(device), jsonEscape(path), jsonEscape(input_container), jsonEscape(output_container),
        jsonEscape(input_codec), jsonEscape(output_codec), input_width, input_height, output_width, output_height, output_bit_depth,
        packets_read, packets_written, frames_decoded, frames_encoded, bitstream_bytes,
        decoder_device_to_device_bytes, decoder_device_to_host_bytes, decoder_host_pixel_copies,
        encoder_host_to_device_bytes, encoder_host_pixel_copies, zero_copy_main_loop ? "true" : "false",
        elapsed_ms, main_loop_ms, remaster_preprocess_ms, remaster_ngx_ms, remaster_pack_kernel_ms,
        remaster_frame_ms, remaster_pack_kernel_max_ms, remaster_pack_launches, vsr_quality,
        measured_max_cll_nits, measured_max_fall_nits, frames_interpolated, fruc_scene_cuts, jsonEscape(fruc_backend));
}

// ---------------------------------------------------------------------------
// NvidiaTranscodeSession: Unified hardware transcode session
// ---------------------------------------------------------------------------

class NvidiaTranscodeSession {
public:
    static Result<std::unique_ptr<NvidiaTranscodeSession>> create(
        const HwTranscodeOptions& options, bool is_remaster,
        HwTranscodeReport& report, int target_w = 0, int target_h = 0) {
        auto session = std::unique_ptr<NvidiaTranscodeSession>(new NvidiaTranscodeSession());
        auto res = session->configure(options, is_remaster, report, target_w, target_h);
        if (!res.has_value()) {
            return std::unexpected(res.error());
        }
        return session;
    }

    ~NvidiaTranscodeSession() {
        muxer_.reset();
        encoder_.reset();
        decoder_.reset();
        bridge_.reset();
        demuxer_.reset();
        shim_.close();
        bridge_loader_.close();
    }

    NvidiaTranscodeSession(const NvidiaTranscodeSession&) = delete;
    NvidiaTranscodeSession& operator=(const NvidiaTranscodeSession&) = delete;

private:
    NvidiaTranscodeSession() = default;

    Result<void> configure(const HwTranscodeOptions& options, bool is_remaster,
                           HwTranscodeReport& report, int target_w = 0, int target_h = 0) {
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

        // Bridge must be queried before shim.probe() calls cuInit(0) to preserve DXGI anti-TDR
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

        if (options_.codec == VideoCodec::AV1 &&
            (diagnostics_.device.compute_major < 8 ||
             (diagnostics_.device.compute_major == 8 && diagnostics_.device.compute_minor < 9))) {
            options_.codec = VideoCodec::HEVC;
            report.output_codec = "hevc";
            std::cerr << "[WARN] GPU (SM " << diagnostics_.device.compute_major << "."
                      << diagnostics_.device.compute_minor
                      << ") lacks hardware AV1 NVENC; automatically falling back to HEVC Main10.\n";
        }

        const int source_depth = info_.bit_depth > 8 ? 10 : 8;
        int output_depth = 10;
        if (!is_remaster) {
            output_depth = chooseOutputDepth(options, info_);
            if (output_depth != source_depth) {
                return err(ErrorCode::Unsupported, "stage-1 zero-copy transcode preserves the source bit depth",
                           "source bit depth is " + std::to_string(source_depth) + ", requested " + std::to_string(output_depth));
            }
            if (output_depth > 8 && options.codec == VideoCodec::H264) {
                return err(ErrorCode::Unsupported, "H.264 NVENC does not support 10-bit encoding");
            }
        }
        report.output_bit_depth = output_depth;

        const bool is_portrait_input = (info_.width > 0 && info_.height > 0 && info_.width < info_.height);
        const bool is_default_landscape_4k = (target_width_ == 3840 && target_height_ == 2160);
        const GeometryTargetMode geom_mode = (options.geometry_mode == 1)
            ? GeometryTargetMode::Pillarbox
            : GeometryTargetMode::AutoDirect;
        if (is_remaster && (!options.vsr_enabled)) {
            // Phase 6.1 微瑕 3: an explicitly disabled VSR must never upscale.
            target_width_ = info_.width;
            target_height_ = info_.height;
            report.output_width = target_width_;
            report.output_height = target_height_;
        } else if (is_remaster && (target_width_ <= 0 || target_height_ <= 0 ||
                                   (is_portrait_input && is_default_landscape_4k))) {
            auto geom = calculateSmartGeometry(info_.width, info_.height, geom_mode, target_width_, target_height_);
            target_width_ = geom.canvas_width;
            target_height_ = geom.canvas_height;
            report.output_width = target_width_;
            report.output_height = target_height_;
        }

        const bool enable_truehdr = options.hdr;
        // Phase 6.1 微瑕 1: bind the GUI/CLI peak-nits option end to end.
        const int clamped_nits = std::clamp(options.peak_nits > 0 ? options.peak_nits : 1000, 400, 4000);
        if (is_remaster) {
            if (enable_truehdr) {
                TrueHdrParameters hdr_params{};
                hdr_params.max_luminance = clamped_nits;
                const Result<Hdr10Metadata> canonical = SeiSynthesizer::makeMetadata(hdr_params);
                if (!canonical.has_value()) {
                    return std::unexpected(canonical.error());
                }
                hdr_ = canonical.value();
                applyHdrDefaults(hdr_);
            } else {
                hdr_.enabled = false;
                hdr_.color.primaries = ColorPrimaries::BT709;
                hdr_.color.transfer = TransferFunction::BT709;
                hdr_.color.matrix = MatrixCoefficients::BT709;
                hdr_.color.range = ColorRange::Limited;
            }

            RtxNgxBridgeConfig bridge_config{};
            std::memcpy(bridge_config.luid, diagnostics_.device.luid, sizeof(bridge_config.luid));
            bridge_config.luid_node_mask = diagnostics_.device.luid_node_mask;
            bridge_config.input_width = info_.width;
            bridge_config.input_height = info_.height;
            bridge_config.output_width = target_width_;
            bridge_config.output_height = target_height_;
            // Phase 6.1 微瑕 3: vsr_quality 0 tells the bridge VSR is disabled
            // even when the output geometry would otherwise request an upscale.
            bridge_config.vsr_quality = options.vsr_enabled ? std::clamp(options.vsr_quality, 1, 4) : 0;
            bridge_config.enable_truehdr = enable_truehdr ? 1 : 0;
            bridge_config.contrast = 100;
            bridge_config.saturation = 100;
            bridge_config.middle_gray = 50;
            bridge_config.max_luminance = clamped_nits;
            bridge_config.project_id = 0x52545831u;
            bridge_config_ = bridge_config;  // Phase 8: kept for the TDR rebuild

            RtxNgxBridge* raw_bridge = nullptr;
            const RtxResult b_res = bridge_loader_.create(&bridge_config, &raw_bridge);
            if (b_res != RTX_OK || raw_bridge == nullptr) {
                return err(mapShimResult(b_res), describe("rtx_ngx_bridge_create", b_res, nullptr),
                           bridge_loader_.lastError(nullptr));
            }
            bridge_ = BridgeHandle(raw_bridge, BridgeDeleter{&bridge_loader_});

            // Phase 7 (Task 7-B1): upload the .cube 3D LUT into the bridge's
            // CUDA texture before the first remaster.
            if (!options_.lut_path.empty()) {
                const Result<Lut3D> lut = Lut3D::loadFromCubeFile(options_.lut_path);
                if (!lut.has_value()) {
                    return std::unexpected(lut.error());
                }
                if (!bridge_loader_.hasPhase7Api()) {
                    return err(ErrorCode::Unsupported,
                               "the loaded NGX bridge does not support 3D LUT (ABI < 3); "
                               "rebuild the bridge");
                }
                const RtxResult lut_res = bridge_loader_.setLut3d(
                    bridge_.get(), lut.value().half4Data().data(), lut.value().gridSize());
                if (lut_res != RTX_OK) {
                    return err(mapShimResult(lut_res),
                               describe("rtx_ngx_bridge_set_lut3d", lut_res, nullptr),
                               bridge_loader_.lastError(bridge_.get()));
                }
            }

            // Phase 7 realtime monitoring: bind the bridge-owned double
            // 960x540 R10G10B10A2 preview ring when the caller consumes it.
            if (options_.preview) {
                if (!bridge_loader_.hasPhase7Api()) {
                    return err(ErrorCode::Unsupported,
                               "the loaded NGX bridge does not support the realtime "
                               "preview path (ABI < 3); rebuild the bridge");
                }
                for (uint32_t slot = 0; slot < 2; ++slot) {
                    uint64_t pointer = 0;
                    uint32_t pitch = 0;
                    const RtxResult p_res = bridge_loader_.previewBuffer(bridge_.get(), slot,
                                                                         &pointer, &pitch);
                    if (p_res != RTX_OK || pointer == 0 || pitch == 0) {
                        return err(mapShimResult(p_res),
                                   describe("rtx_ngx_bridge_preview_buffer", p_res, nullptr),
                                   bridge_loader_.lastError(bridge_.get()));
                    }
                    preview_ring_[slot] = pointer;
                    preview_pitch_ = pitch;
                }
            }
        } else {
            if (options.hdr) {
                hdr_ = info_.hdr;
                if (!hdr_.enabled && info_.color.transfer != TransferFunction::BT709) {
                    hdr_.enabled = true;
                    hdr_.color = info_.color;
                }
                applyHdrDefaults(hdr_);
            }
        }

        RtxDecoderConfig decoder_config{};
        decoder_config.codec = shimCodec(info_.codec);
        decoder_config.width = info_.width;
        decoder_config.height = info_.height;
        decoder_config.bit_depth = source_depth;
        decoder_config.output_format = source_depth > 8 ? RTX_PIXEL_P010 : RTX_PIXEL_NV12;
        decoder_config.clock_rate = 90000;
        decoder_config.max_surfaces = 32;
        decoder_config.pool_slots = 16;
        // Phase 8 (Task 8-A1): explicit multi-GPU binding.
        decoder_config.device_index = options_.device_index;
        decoder_config_ = decoder_config;  // Phase 8: kept for the TDR rebuild

        RtxDecoder* raw_decoder = nullptr;
        const RtxResult d_res = shim_.decoderCreate(&decoder_config, &raw_decoder);
        if (d_res != RTX_OK) {
            return err(mapShimResult(d_res), describe("rtx_shim_decoder_create", d_res, nullptr),
                       shim_.decoderLastError(nullptr));
        }
        decoder_ = DecoderHandle(raw_decoder, DecoderDeleter{&shim_});

        output_info_ = info_;
        // Phase 8 (Task 8-A2): GOP-segmented transcodes land on a key frame
        // before the first packet is read.
        if (options_.seek_start_ms >= 0) {
            const Rational tb = info_.time_base.valid() ? info_.time_base : Rational{1, 90000};
            const int64_t stream_pts =
                options_.seek_start_ms * tb.den / (1000 * tb.num);
            auto sought = demuxer_->seekToKeyframeBefore(stream_pts);
            if (!sought.has_value()) {
                return sought;
            }
        }
        // Phase 6.1 微瑕 2: an explicit copy_audio=false must strip every audio
        // stream from the output description, not just stop forwarding packets.
        // Phase 7 keeps the subtitle passthrough intact.
        if (!options_.copy_audio) {
            output_info_.primary_audio_index = -1;
            output_info_.audio_tracks.clear();
            std::erase_if(output_info_.passthrough_streams,
                          [](const PassthroughStreamInfo& s) { return s.codec_type == 1; });
        }
        output_info_.time_base = Rational{1, 90000};
        output_info_.codec = options.codec;
        output_info_.video_codec = report.output_codec;
        output_info_.format_name.clear();
        output_info_.annex_b = true;
        output_info_.color = hdr_.color;
        output_info_.bit_depth = output_depth;
        frame_rate_ = chooseFrameRate(options, info_);
        return {};
    }

public:
    Result<void> createEncoder(const RtxFrameDesc& frame, HwTranscodeReport& report) {
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
        config.rate_control = std::clamp(options_.rate_control, 0, 2);
        config.qp = options_.qp; config.device_index = options_.device_index;
        RtxEncoder* raw = nullptr;
        const RtxResult created = shim_.encoderCreate(&config, &raw);
        if (created != RTX_OK || raw == nullptr) return err(mapShimResult(created), describe("rtx_shim_encoder_create", created, nullptr));
        encoder_ = EncoderHandle(raw, EncoderDeleter{&shim_});
        output_info_.bit_depth = is_remaster_ ? 10 : frame.bit_depth;
        output_info_.width = config.width; output_info_.height = config.height;
        report.output_bit_depth = output_info_.bit_depth;
        return {};
    }

    void releaseDecodedFrame(uint64_t slot) {
        if (decoder_ != nullptr) shim_.decoderReleaseFrame(decoder_.get(), slot);
    }

    Result<void> openMuxer() {
        std::lock_guard<std::mutex> lock(muxer_mutex_);
        if (muxer_ != nullptr) return {};
        size_t header_size = 0;
        if (shim_.encoderSequenceHeader(encoder_.get(), nullptr, 0, &header_size) != RTX_OK || header_size == 0)
            return err(ErrorCode::Internal, "NVENC sequence header missing", shim_.encoderLastError(encoder_.get()));
        std::vector<uint8_t> header(header_size);
        if (shim_.encoderSequenceHeader(encoder_.get(), header.data(), header.size(), &header_size) != RTX_OK)
            return err(ErrorCode::Internal, "cannot fetch NVENC header", shim_.encoderLastError(encoder_.get()));
        header.resize(header_size);
        output_info_.annex_b = (options_.codec != VideoCodec::AV1);
        output_info_.extradata = (options_.codec == VideoCodec::AV1)
            ? makeAv1ConfigRecord(header.data(), header.size(), options_.hdr)
            : (options_.codec == VideoCodec::HEVC ? makeHevcConfigRecord(header.data(), header.size())
                                                  : makeAvcConfigRecord(header.data(), header.size()));
        if (output_info_.extradata.empty()) return err(ErrorCode::Internal, "cannot build codec config record");
        muxer_ = makeFfmpegMuxer();
        return muxer_->open(options_.output_path, output_info_, hdr_);
    }

    Result<void> writeEncodedPacket(const RtxPacketDesc& packet, int64_t fallback_pts, HwTranscodeReport& report,
                                    const std::vector<uint8_t>& side_data = {}) {
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

    Result<void> flushEncoder(HwTranscodeReport& report) {
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

    Result<void> finalize(HwTranscodeReport& report, Clock::time_point started) {
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
        report.zero_copy_main_loop = (report.decoder_host_pixel_copies == 0 && report.encoder_host_pixel_copies == 0);
        report.elapsed_ms = std::chrono::duration<double, std::milli>(Clock::now() - started).count();
        return {};
    }

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
    bool isAudioPacket(const Packet& p) const noexcept { return info_.primary_audio_index >= 0 && p.stream_index == info_.primary_audio_index; }
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
    RtxResult downloadPreviewInto(void* dst, size_t bytes) const { return bridge_loader_.downloadPreview(bridge_.get(), dst, bytes); }
    int audioStreamIndex() const noexcept { return info_.primary_audio_index; }
    bool isMuxerOpen() const noexcept { return muxer_ != nullptr; }
    Result<void> writeAudioPacket(const Packet& packet) {
        if (!muxer_) return err(ErrorCode::State, "muxer is not open");
        return muxer_->writeAudioPacket(packet);
    }
    const MediaInfo& outputInfo() const { return output_info_; }
    const Rational& frameRate() const { return frame_rate_; }
    const Hdr10Metadata& hdr() const { return hdr_; }

private:
    HwTranscodeOptions options_{};
    bool is_remaster_ = false;
    int target_width_ = 0, target_height_ = 0;
    MediaInfo info_{}, output_info_{};
    Rational frame_rate_{30, 1};
    Hdr10Metadata hdr_{};
    RtxDiagnostics diagnostics_{};
    std::mutex muxer_mutex_;
    int measured_max_cll_nits_ = 0, measured_max_fall_nits_ = 0;
    uint64_t preview_ring_[2] = {0, 0};
    uint32_t preview_pitch_ = 0;
    RtxNgxBridgeConfig bridge_config_{};
    RtxDecoderConfig decoder_config_{};
    NgxRemasterBridge bridge_loader_;
    BridgeHandle bridge_{nullptr, BridgeDeleter{nullptr}};
    NvidiaShim shim_;
    DecoderHandle decoder_{nullptr, DecoderDeleter{nullptr}};
    EncoderHandle encoder_{nullptr, EncoderDeleter{nullptr}};
    std::unique_ptr<IDemuxer> demuxer_;
    std::unique_ptr<IMuxer> muxer_;
};

Result<HwTranscodeReport> runNvidiaRemasterTranscode(const HwTranscodeOptions& options) {
    if (options.input_path.empty() || options.output_path.empty()) {
        return err(ErrorCode::InvalidArgument, "remaster transcode requires input and output paths");
    }
    if (options.codec != VideoCodec::HEVC && options.codec != VideoCodec::AV1 && options.codec != VideoCodec::H264) {
        return err(ErrorCode::Unsupported, "the VSR-remaster path supports AV1, HEVC and H.264 output");
    }
    if (options.hdr && options.codec == VideoCodec::H264) {
        return err(ErrorCode::InvalidArgument,
                   "TrueHDR requires 10-bit encoding (AV1 or HEVC); H.264 only supports 8-bit SDR");
    }
    if (options.vsr_quality < 1 || options.vsr_quality > 4) {
        return err(ErrorCode::InvalidArgument, "vsr_quality must be in 1..4");
    }

    const Clock::time_point started = Clock::now();
    HwTranscodeReport report;
    report.path = "vsr-remaster";
    report.vsr_quality = options.vsr_quality;

    auto session_res = NvidiaTranscodeSession::create(options, /*is_remaster=*/true, report, options.target_width, options.target_height);
    if (!session_res.has_value()) {
        return std::unexpected(session_res.error());
    }
    auto& session = *session_res.value();

    const int target_width = session.targetWidth();
    const int target_height = session.targetHeight();
    if ((target_width & 1) != 0 || (target_height & 1) != 0) {
        return err(ErrorCode::InvalidArgument, "target geometry must be even");
    }

    const MediaInfo& info = session.info();
    if (target_width < info.width || target_height < info.height) {
        return err(ErrorCode::InvalidArgument, "remaster target geometry must not be smaller than the source");
    }

    const Rational& frame_rate = session.frameRate();
    const Rational clock_time_base{1, 90000};
    int64_t estimated_total_frames = 0;
    if (info.duration_ms > 0 && frame_rate.num > 0 && frame_rate.den > 0) {
        estimated_total_frames = info.duration_ms * frame_rate.num /
                                 (1000 * static_cast<int64_t>(frame_rate.den));
    }

        std::mutex progress_mutex;
    auto reportProgress = [&]() {
        if (!options.progress) return;
        std::lock_guard<std::mutex> lock(progress_mutex);
        options.progress(HwTranscodeProgress{
            .frames_decoded = report.frames_decoded, .frames_encoded = report.frames_encoded,
            .total_frames = estimated_total_frames,
            .elapsed_ms = std::chrono::duration<double, std::milli>(Clock::now() - started).count(),
        });
    };
    auto cancelled = [&]() { return options.cancel != nullptr && options.cancel->load(std::memory_order_relaxed); };
    const int input_full_range = (info.color.range == ColorRange::Full) ? 1 : 0;
    const int input_matrix = (info.color.matrix == MatrixCoefficients::BT2020NC) ? 1 : 0;
RtxNgxRemasterStats previous_stats{};
    double pack_max_ms = 0.0;
    // Phase 7: whole-film measured luminance statistics + preview staging.
    uint32_t accumulated_max_cll_ = 0;
    uint32_t accumulated_max_fall_ = 0;
    uint32_t preview_slot_ = 0;
    std::vector<uint8_t> preview_host_;
    if (session.previewReady()) {
        preview_host_.assign(static_cast<size_t>(session.previewPitch()) * 540u, 0);
    }
    std::optional<Clock::time_point> last_preview_download_;

    struct PendingEncode {
        RtxNgxRemasterOutput remaster_out{};
        int64_t pts = 0;
        // Phase 7 (Task 7-B2): per-frame HDR10+ dynamic metadata prefix
        // (empty when disabled).
        std::vector<uint8_t> hdr10plus_prefix;
    };
    std::deque<PendingEncode> encode_queue;
    std::mutex pipeline_mutex;
    std::condition_variable queue_ready;
    std::condition_variable slot_free;
    bool producer_done = false;
    bool consumer_failed = false;
    Result<void> consumer_error;
    uint64_t remasters_issued = 0;
    uint64_t encodes_completed = 0;

    auto consumerStopped = [&]() {
        return consumer_failed || cancelled();
    };

    std::thread encode_thread([&]() {
        for (;;) {
            PendingEncode item;
            {
                std::unique_lock<std::mutex> lock(pipeline_mutex);
                queue_ready.wait(lock, [&] {
                    return !encode_queue.empty() || producer_done || consumerStopped();
                });
                if (consumerStopped()) return;
                if (encode_queue.empty()) {
                    if (producer_done) return;
                    continue;
                }
                item = encode_queue.front();
                encode_queue.pop_front();
            }

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
            const RtxResult encoded = session.shim().encoderEncode(session.encoder(), &encode_frame, &packet);
            if (encoded != RTX_OK) {
                std::lock_guard<std::mutex> lock(pipeline_mutex);
                consumer_failed = true;
                consumer_error = err(mapShimResult(encoded), describe("NvEncEncodePicture", encoded,
                                                                     session.shim().encoderLastError(session.encoder())));
                slot_free.notify_all();
                return;
            }

            auto written = session.writeEncodedPacket(packet, item.pts, report,
                                                      item.hdr10plus_prefix);
            if (!written.has_value()) {
                std::lock_guard<std::mutex> lock(pipeline_mutex);
                consumer_failed = true;
                consumer_error = written;
                slot_free.notify_all();
                return;
            }


            {
                std::lock_guard<std::mutex> done_lock(pipeline_mutex);
                ++encodes_completed;
                ++report.frames_encoded;
            }
            slot_free.notify_all();
            reportProgress();
        }
    });

    auto processFrame = [&](const RtxFrameDesc& frame, int64_t pts_override = -1,
                            bool release_slot = true) -> Result<void> {
        const int64_t effective_pts = (pts_override >= 0) ? pts_override : frame.pts;

        {
            // Phase 7 (Task 7-B3): the 3-slot P010 ring lets the GPU pipeline
            // run one frame further ahead of NVENC than the old 2-slot cap.
            std::unique_lock<std::mutex> lock(pipeline_mutex);
            slot_free.wait(lock, [&] {
                return remasters_issued - encodes_completed < 3 || consumerStopped();
            });
            if (consumerStopped()) {
                return consumer_failed ? consumer_error : err(ErrorCode::State, "transcode cancelled");
            }
        }
        RtxNgxRemasterInput remaster_in{};
        remaster_in.y_plane = frame.planes[0];
        remaster_in.uv_plane = frame.planes[1];
        remaster_in.y_pitch = frame.pitches[0];
        remaster_in.uv_pitch = frame.pitches[1];
        remaster_in.format = frame.format;
        remaster_in.full_range = input_full_range;
        remaster_in.matrix = input_matrix;
        // Phase 7 realtime monitoring: alternate the double preview ring and
        // publish the GUI's A/B split position for this frame.
        bool preview_this_frame = false;
        if (options.preview && session.previewReady()) {
            remaster_in.preview_rgba10_ptr = session.previewDevicePointer(preview_slot_ & 1u);
            remaster_in.preview_pitch = session.previewPitch();
            remaster_in.split_x_norm =
                options.split_ratio != nullptr
                    ? std::clamp(options.split_ratio->load(std::memory_order_relaxed), 0.0f, 1.0f)
                    : 0.5f;
            preview_slot_ ^= 1u;
            preview_this_frame = true;
        }
        RtxNgxRemasterOutput remaster_out{};
        RtxNgxRemasterStats frame_stats{};
        const RtxResult remastered = session.bridgeLoader().remaster(
            session.bridge(), &remaster_in, &remaster_out, &frame_stats);
        if (remastered != RTX_OK) {
            if (release_slot) {
                (void)session.releaseDecodedFrame(frame.slot_index);
            }
            return err(mapShimResult(remastered), describe("rtx_ngx_bridge_remaster", remastered, nullptr),
                       session.bridgeLoader().lastError(session.bridge()));
        }
        const double pack_delta =
            static_cast<double>(frame_stats.pack_kernel_ms - previous_stats.pack_kernel_ms);
        previous_stats = frame_stats;
        if (pack_delta > pack_max_ms) {
            pack_max_ms = pack_delta;
        }

        // Phase 7 (Task 7-B3): accumulate the real GPU-reduced frame luma
        // statistics; the running maximum feeds the measured MaxCLL/MaxFALL.
        accumulated_max_cll_ = std::max<uint32_t>(accumulated_max_cll_, remaster_out.frame_max_nits);
        accumulated_max_fall_ = std::max<uint32_t>(accumulated_max_fall_, remaster_out.frame_avg_nits);

        // Phase 7 realtime monitoring: stage the finished preview slot into
        // host memory at most 15 times per second (throttled by wall clock).
        if (preview_this_frame) {
            const auto now = Clock::now();
            const bool due = !last_preview_download_.has_value() ||
                             std::chrono::duration<double>(now - *last_preview_download_).count() >=
                                 (1.0 / 15.0);
            if (due && session.downloadPreviewInto(preview_host_.data(), preview_host_.size()) ==
                           RTX_OK) {
                last_preview_download_ = now;
                options.preview(preview_host_.data(), session.previewPitch());
            }
        }

        if (release_slot) {
            (void)session.releaseDecodedFrame(frame.slot_index);
        }

        // Phase 7 (Task 7-B2): synthesise the SMPTE ST 2094-40 payload for
        // this frame from its real statistics.
        std::vector<uint8_t> hdr10plus_prefix;
        if (options.hdr10plus) {
            Hdr10PlusFrameStats frame_hdr10plus{};
            frame_hdr10plus.maxscl_r_nits = std::max<uint32_t>(1u, remaster_out.frame_max_nits);
            frame_hdr10plus.maxscl_g_nits = std::max<uint32_t>(1u, remaster_out.frame_max_nits);
            frame_hdr10plus.maxscl_b_nits = std::max<uint32_t>(1u, remaster_out.frame_max_nits);
            frame_hdr10plus.average_maxrgb_nits = std::max<uint32_t>(1u, remaster_out.frame_avg_nits);
            const float ratio =
                remaster_out.frame_max_nits > 0
                    ? static_cast<float>(remaster_out.frame_avg_nits) /
                          static_cast<float>(remaster_out.frame_max_nits)
                    : 0.5f;
            frame_hdr10plus.knee_point_x = 500;
            frame_hdr10plus.knee_point_y = static_cast<uint16_t>(
                std::clamp(static_cast<int>(ratio * 1000.0f), 1, 1000));
            hdr10plus_prefix = (options.codec == VideoCodec::HEVC)
                                   ? SeiSynthesizer::hevcHdr10PlusPrefixSei(frame_hdr10plus)
                                   : SeiSynthesizer::av1Hdr10PlusMetadataObu(frame_hdr10plus);
        }

        // Phase 7 (Task 7-B3): the encoder and the muxer now open on the very
        // first remastered frame, which drains audio_packet_queue immediately
        // (no more pre-first-video-packet buffering).
                if (session.encoder() == nullptr) {
            session.setMeasuredCll(accumulated_max_cll_, accumulated_max_fall_);
            auto created = session.createEncoder(frame, report);
            if (!created.has_value()) return created;
            auto mux = session.openMuxer();
            if (!mux.has_value()) return mux;
        }
        {
            std::lock_guard<std::mutex> lock(pipeline_mutex);
            encode_queue.push_back(PendingEncode{remaster_out, effective_pts, std::move(hdr10plus_prefix)});
            ++remasters_issued;
        }
        queue_ready.notify_all();
        return {};
    };

    auto finishPipeline = [&]() -> Result<void> {
        { std::lock_guard<std::mutex> lock(pipeline_mutex); producer_done = true; }
        queue_ready.notify_all();
        encode_thread.join();
        return consumer_failed ? consumer_error : Result<void>{};
    };

    const bool fruc_enabled = options.fruc_enabled;
    const uint64_t fruc_out_buffer = fruc_enabled ? session.bridgeLoader().getInterpBuffer(
        session.bridge(), static_cast<uint32_t>(session.info().width), static_cast<uint32_t>(session.info().height)) : 0;
    if (fruc_enabled && fruc_out_buffer != 0) report.fruc_backend = "nvof";

    bool has_prev_frame = false;
    RtxFrameDesc prev_frame{};
    int64_t prev_pts_flicks = 0, next_out_pts_flicks = -1;
    constexpr int64_t kFlicksPerTick90k = 7840;
    const Rational out_frame_rate = session.frameRate();
    const int64_t delta_out_flicks = (out_frame_rate.num > 0 && out_frame_rate.den > 0)
        ? (705600000LL * out_frame_rate.den / out_frame_rate.num) : 11760000LL;

    auto releasePrevLease = [&]() {
        if (has_prev_frame) { (void)session.releaseDecodedFrame(prev_frame.slot_index); has_prev_frame = false; }
    };

    auto handleDecodedFrame = [&](const RtxFrameDesc& frame) -> Result<void> {
        if (!fruc_enabled || fruc_out_buffer == 0) {
            return processFrame(frame, -1, /*release_slot=*/true);
        }

        if (!has_prev_frame) {
            prev_frame = frame;
            prev_pts_flicks = frame.pts * kFlicksPerTick90k;
            next_out_pts_flicks = prev_pts_flicks;
            has_prev_frame = true;
            return Result<void>{};
        }

        const int64_t curr_pts_flicks = frame.pts * kFlicksPerTick90k;
        if (curr_pts_flicks <= prev_pts_flicks) {
            (void)session.releaseDecodedFrame(prev_frame.slot_index);
            prev_frame = frame;
            prev_pts_flicks = curr_pts_flicks;
            return Result<void>{};
        }

        const int64_t window_duration = curr_pts_flicks - prev_pts_flicks;
        while (next_out_pts_flicks < curr_pts_flicks) {
            if (cancelled()) {
                return err(ErrorCode::State, "transcode cancelled");
            }
            const float alpha = static_cast<float>(next_out_pts_flicks - prev_pts_flicks) /
                                static_cast<float>(window_duration);
            const int64_t out_pts_90k = (next_out_pts_flicks + (kFlicksPerTick90k / 2)) / kFlicksPerTick90k;

            if (alpha <= 0.0001f) {
                auto res = processFrame(prev_frame, out_pts_90k, /*release_slot=*/false);
                if (!res.has_value()) return res;
            } else if (alpha >= 0.9999f) {
                auto res = processFrame(frame, out_pts_90k, /*release_slot=*/false);
                if (!res.has_value()) return res;
            } else {
                const uint32_t pitch = prev_frame.pitches[0];
                const RtxResult status = session.bridgeLoader().interpolateNv12(
                    session.bridge(),
                    prev_frame.planes[0],
                    frame.planes[0],
                    pitch,
                    alpha,
                    fruc_out_buffer);
                if (status == RTX_OK) {
                    ++report.frames_interpolated;
                    RtxFrameDesc interp_frame = prev_frame;
                    interp_frame.planes[0] = fruc_out_buffer;
                    interp_frame.planes[1] = fruc_out_buffer + static_cast<uint64_t>(pitch) * prev_frame.height;
                    interp_frame.pts = out_pts_90k;
                    interp_frame.slot_index = 0xFFFFFFFF;
                    auto res = processFrame(interp_frame, out_pts_90k, /*release_slot=*/false);
                    if (!res.has_value()) return res;
                } else {
                    auto res = processFrame(prev_frame, out_pts_90k, /*release_slot=*/false);
                    if (!res.has_value()) return res;
                }
            }
            next_out_pts_flicks += delta_out_flicks;
        }

        (void)session.releaseDecodedFrame(prev_frame.slot_index);
        prev_frame = frame;
        prev_pts_flicks = curr_pts_flicks;
        has_prev_frame = true;
        return Result<void>{};
    };

    auto drainEndOfStream = [&]() -> Result<void> {
        if (fruc_enabled && fruc_out_buffer != 0 && has_prev_frame) {
            if (next_out_pts_flicks <= prev_pts_flicks) {
                const int64_t out_pts_90k = (next_out_pts_flicks + (kFlicksPerTick90k / 2)) / kFlicksPerTick90k;
                auto res = processFrame(prev_frame, out_pts_90k, /*release_slot=*/false);
                if (!res.has_value()) {
                    releasePrevLease();
                    return res;
                }
                next_out_pts_flicks += delta_out_flicks;
            }
            releasePrevLease();
        }
        return Result<void>{};
    };

    auto drainFrames = [&](bool end_of_stream) -> Result<void> {
        for (;;) {
            if (cancelled()) {
                return err(ErrorCode::State, "transcode cancelled");
            }
            if (options.frame_limit > 0 && report.frames_decoded >= options.frame_limit) {
                return {};
            }
            RtxFrameDesc frame{};
            const RtxResult received = session.shim().decoderReceive(session.decoder(), &frame);
            if (received == RTX_OK) {
                auto processed = handleDecodedFrame(frame);
                if (!processed.has_value()) {
                    return processed;
                }
                ++report.frames_decoded;
                reportProgress();
                continue;
            }
            if (received == RTX_ERR_NOT_FOUND) {
                return {};
            }
            if (received == RTX_ERR_IO && end_of_stream) {
                return {};
            }
            return err(mapShimResult(received),
                       describe("rtx_shim_decoder_receive", received,
                                session.shim().decoderLastError(session.decoder())));
        }
    };

    std::deque<Packet> audio_packet_queue;
    std::mutex audio_queue_mutex;

    const Clock::time_point main_loop_started = Clock::now();
    int64_t packet_pts = 0;
    const int64_t frame_ticks =
        frame_rate.num > 0 && frame_rate.den > 0
            ? std::max<int64_t>(1, static_cast<int64_t>(90000) * frame_rate.den / frame_rate.num)
            : 3000;
    const Rational input_time_base = info.time_base.valid() ? info.time_base : clock_time_base;

    Result<void> pipeline_status;
    for (;;) {
        if (cancelled()) {
            pipeline_status = err(ErrorCode::State, "transcode cancelled");
            break;
        }
        if (options.frame_limit > 0 && report.frames_decoded >= options.frame_limit) {
            break;
        }
        Result<Packet> packet = session.demuxer()->readPacket();
        if (!packet.has_value()) {
            if (packet.error().code == ErrorCode::NotFound) {
                break;
            }
            pipeline_status = std::unexpected(packet.error());
            break;
        }
        ++report.packets_read;
        Packet& input = packet.value();
        if (session.isPassthroughPacket(input)) {
            if (session.isAudioPacket(input) && !session.copyAudioEnabled()) {
                continue;  // Phase 6.1 微瑕 2: audio passthrough explicitly disabled.
            }
            if (session.isMuxerOpen()) {
                std::deque<Packet> to_flush;
                {
                    std::lock_guard<std::mutex> lk(audio_queue_mutex);
                    to_flush.swap(audio_packet_queue);
                }
                for (const auto& ap : to_flush) {
                    auto a_res = session.writeAudioPacket(ap);
                    if (!a_res.has_value()) {
                        pipeline_status = a_res;
                        break;
                    }
                    ++report.packets_written;
                }
                if (!pipeline_status.has_value()) break;
                auto a_res = session.writeAudioPacket(input);
                if (!a_res.has_value()) {
                    pipeline_status = a_res;
                    break;
                }
                ++report.packets_written;
            } else {
                std::lock_guard<std::mutex> lk(audio_queue_mutex);
                audio_packet_queue.push_back(std::move(input));
            }
            continue;
        }
        int64_t pts = input.pts;
        if (pts == std::numeric_limits<int64_t>::min()) {
            pts = packet_pts;
        } else {
            pts = rescaleQ(pts, input_time_base, clock_time_base);
        }
        int64_t dts = input.dts;
        if (dts == std::numeric_limits<int64_t>::min()) {
            dts = pts;
        } else {
            dts = rescaleQ(dts, input_time_base, clock_time_base);
        }
        const RtxResult submitted =
            session.shim().decoderSubmit(session.decoder(), input.data.data(), input.data.size(), pts,
                                         dts, input.keyframe ? 1 : 0);
        if (submitted != RTX_OK) {
            pipeline_status = err(mapShimResult(submitted),
                                  describe("cuvidParseVideoData", submitted, session.shim().decoderLastError(session.decoder())));
            break;
        }
        auto drained = drainFrames(false);
        if (!drained.has_value()) {
            pipeline_status = drained;
            break;
        }
        packet_pts += frame_ticks;
    }

    if (pipeline_status.has_value()) {
        const RtxResult flushed_decoder = session.shim().decoderFlush(session.decoder());
        if (flushed_decoder != RTX_OK) {
            pipeline_status = err(mapShimResult(flushed_decoder),
                                  describe("cuvidParseVideoData(EOS)", flushed_decoder,
                                           session.shim().decoderLastError(session.decoder())));
        }
    }
    if (pipeline_status.has_value()) {
        auto drained = drainFrames(true);
        if (!drained.has_value()) {
            pipeline_status = drained;
        }
    }
    if (pipeline_status.has_value()) {
        auto eos_res = drainEndOfStream();
        if (!eos_res.has_value()) {
            pipeline_status = eos_res;
        }
    }

    auto finished = finishPipeline();
    releasePrevLease();
    if (!pipeline_status.has_value()) {
        return std::unexpected(pipeline_status.error());
    }
    if (!finished.has_value()) {
        return std::unexpected(finished.error());
    }

    report.main_loop_ms =
        std::chrono::duration<double, std::milli>(Clock::now() - main_loop_started).count();

    auto flushed_encoder = session.flushEncoder(report);
    if (!flushed_encoder.has_value()) {
        return std::unexpected(flushed_encoder.error());
    }

    if (session.isMuxerOpen()) {
        std::deque<Packet> to_flush;
        {
            std::lock_guard<std::mutex> lk(audio_queue_mutex);
            to_flush.swap(audio_packet_queue);
        }
        for (const auto& ap : to_flush) {
            auto a_res = session.writeAudioPacket(ap);
            if (!a_res.has_value()) {
                return std::unexpected(a_res.error());
            }
            ++report.packets_written;
        }
    }

    auto finalized = session.finalize(report, started);
    if (!finalized.has_value()) {
        return std::unexpected(finalized.error());
    }

    if (report.frames_encoded > 0) {
        const double n = static_cast<double>(report.frames_encoded);
        report.remaster_preprocess_ms = previous_stats.preprocess_ms / n;
        report.remaster_ngx_ms = previous_stats.ngx_ms / n;
        report.remaster_pack_kernel_ms = previous_stats.pack_kernel_ms / n;
        report.remaster_frame_ms = report.main_loop_ms / n;
        report.remaster_pack_kernel_max_ms = pack_max_ms;
        report.remaster_pack_launches = previous_stats.pack_launches;
    }
    // Phase 7 (Task 7-B3): surface the whole-film measured HDR10 statistics.
    report.measured_max_cll_nits = session.measuredMaxCllNits();
    report.measured_max_fall_nits = session.measuredMaxFallNits();

    return report;
}

// ---------------------------------------------------------------------------
// Phase 7 (Task 7-C2): timeline scrubbing preview.  One full decode ->
// remaster cycle at an arbitrary seek point, published through the preview
// callback.  No encoder and no output file are involved.
// ---------------------------------------------------------------------------

Result<void> runScrubPreview(const HwTranscodeOptions& options, int64_t seek_pts_ms) {
    if (!options.preview) return err(ErrorCode::InvalidArgument, "scrub preview requires a preview callback");
    if (options.input_path.empty()) return err(ErrorCode::InvalidArgument, "scrub preview requires an input path");

    HwTranscodeReport report;
    auto session_res = NvidiaTranscodeSession::create(options, true, report, options.target_width, options.target_height);
    if (!session_res.has_value()) return std::unexpected(session_res.error());
    auto& session = *session_res.value();
    if (!session.previewReady()) return err(ErrorCode::Unsupported, "NGX bridge preview unsupported");

    const MediaInfo& info = session.info();
    Rational tb = (info.time_base.valid() && info.time_base.num > 0) ? info.time_base : Rational{1, 90000};
    int64_t s_pts = seek_pts_ms <= 0 ? 0 : seek_pts_ms * tb.den / (1000 * tb.num);
    auto sought = session.demuxer()->seekToKeyframeBefore(s_pts);
    if (!sought.has_value()) return sought;

    bool ready = false;
    RtxFrameDesc frame{};
    for (int p = 0; !ready && p < 60; ++p) {
        auto pkt = session.demuxer()->readPacket();
        if (!pkt.has_value()) break;
        if (session.isPassthroughPacket(pkt.value())) continue;
        const Packet& inp = pkt.value();
        int64_t pts = inp.pts == std::numeric_limits<int64_t>::min() ? 0 : inp.pts;
        int64_t dts = inp.dts == std::numeric_limits<int64_t>::min() ? 0 : inp.dts;
        if (session.shim().decoderSubmit(session.decoder(), inp.data.data(), inp.data.size(), pts, dts, inp.keyframe ? 1 : 0) != RTX_OK) break;
        if (session.shim().decoderReceive(session.decoder(), &frame) == RTX_OK) ready = true;
    }
    if (!ready) {
        session.shim().decoderFlush(session.decoder());
        if (session.shim().decoderReceive(session.decoder(), &frame) == RTX_OK) ready = true;
    }
    if (!ready) return err(ErrorCode::NotFound, "no scrub frame decoded");

    RtxNgxRemasterInput in{
        .y_plane = frame.planes[0], .uv_plane = frame.planes[1],
        .y_pitch = frame.pitches[0], .uv_pitch = frame.pitches[1],
        .format = frame.format, .full_range = (info.color.range == ColorRange::Full) ? 1 : 0,
        .matrix = (info.color.matrix == MatrixCoefficients::BT2020NC) ? 1 : 0,
        .preview_rgba10_ptr = session.previewDevicePointer(0),
        .preview_pitch = session.previewPitch(),
        .split_x_norm = options.split_ratio ? std::clamp(options.split_ratio->load(std::memory_order_relaxed), 0.0f, 1.0f) : 0.5f,
    };
    RtxNgxRemasterOutput out{};
    RtxNgxRemasterStats stats{};
    RtxResult r = session.bridgeLoader().remaster(session.bridge(), &in, &out, &stats);
    session.releaseDecodedFrame(frame.slot_index);
    if (r != RTX_OK) return err(mapShimResult(r), "rtx_ngx_bridge_remaster failed");

    std::vector<uint8_t> host(static_cast<size_t>(session.previewPitch()) * 540u, 0);
    if (session.downloadPreviewInto(host.data(), host.size()) != RTX_OK) return err(ErrorCode::Internal, "preview download failed");
    options.preview(host.data(), session.previewPitch());
    return {};
}
}  // namespace rtx
