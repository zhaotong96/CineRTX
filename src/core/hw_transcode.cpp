// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors

#include "rtx/hw_transcode.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <limits>
#include <memory>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>

#include "bitstream.h"
#include "rtx/hdr.h"
#include "rtx/ngx_remaster.h"
#include "rtx/nvidia_shim.h"

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
        case VideoCodec::H264:
            return 2;
        default:
            return -1;
    }
}

ErrorCode mapShimResult(RtxResult result) {
    switch (result) {
        case RTX_OK:
            return ErrorCode::Ok;
        case RTX_ERR_INVALID_ARGUMENT:
            return ErrorCode::InvalidArgument;
        case RTX_ERR_NOT_FOUND:
            return ErrorCode::NotFound;
        case RTX_ERR_IO:
            return ErrorCode::Io;
        case RTX_ERR_UNSUPPORTED:
            return ErrorCode::Unsupported;
        case RTX_ERR_DLL_MISSING:
            return ErrorCode::DllMissing;
        case RTX_ERR_ARCH_UNSUPPORTED:
            return ErrorCode::ArchUnsupported;
        case RTX_ERR_DRIVER_TOO_OLD:
            return ErrorCode::DriverTooOld;
        case RTX_ERR_DEVICE_LOST:
            return ErrorCode::DeviceLost;
        case RTX_ERR_OUT_OF_MEMORY:
            return ErrorCode::OutOfMemory;
        case RTX_ERR_NGX:
            return ErrorCode::NgxFailure;
        case RTX_ERR_TENSORRT_VERSION:
            return ErrorCode::TensorRtVersion;
        case RTX_ERR_NOT_IMPLEMENTED:
            return ErrorCode::Unsupported;
        case RTX_ERR_INTERNAL:
            return ErrorCode::Internal;
    }
    return ErrorCode::Internal;
}

const char* shimResultName(RtxResult result) {
    switch (result) {
        case RTX_OK:
            return "RTX_OK";
        case RTX_ERR_INVALID_ARGUMENT:
            return "RTX_ERR_INVALID_ARGUMENT";
        case RTX_ERR_NOT_FOUND:
            return "RTX_ERR_NOT_FOUND";
        case RTX_ERR_IO:
            return "RTX_ERR_IO";
        case RTX_ERR_UNSUPPORTED:
            return "RTX_ERR_UNSUPPORTED";
        case RTX_ERR_DLL_MISSING:
            return "RTX_ERR_DLL_MISSING";
        case RTX_ERR_ARCH_UNSUPPORTED:
            return "RTX_ERR_ARCH_UNSUPPORTED";
        case RTX_ERR_DRIVER_TOO_OLD:
            return "RTX_ERR_DRIVER_TOO_OLD";
        case RTX_ERR_DEVICE_LOST:
            return "RTX_ERR_DEVICE_LOST";
        case RTX_ERR_OUT_OF_MEMORY:
            return "RTX_ERR_OUT_OF_MEMORY";
        case RTX_ERR_NGX:
            return "RTX_ERR_NGX";
        case RTX_ERR_TENSORRT_VERSION:
            return "RTX_ERR_TENSORRT_VERSION";
        case RTX_ERR_NOT_IMPLEMENTED:
            return "RTX_ERR_NOT_IMPLEMENTED";
        case RTX_ERR_INTERNAL:
            return "RTX_ERR_INTERNAL";
    }
    return "RTX_ERR_UNKNOWN";
}

std::string describe(const char* operation, RtxResult result, const char* detail) {
    std::string message = operation;
    message += " failed: ";
    message += shimResultName(result);
    if (detail != nullptr && detail[0] != '\0') {
        message += " (";
        message += detail;
        message += ')';
    }
    return message;
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
    if (!hdr.enabled) {
        return;
    }
    // Defaults come from the single source of truth in SeiSynthesizer: DCI-P3
    // D65 primaries, 1000 cd/m^2 peak, MaxCLL 1000 and MaxFALL 300.  Keeping a
    // private copy of these numbers here is what produced the BT.2020 /
    // MaxFALL=400 drift the acceptance review flagged.
    const Result<Hdr10Metadata> canonical =
        SeiSynthesizer::makeMetadata(TrueHdrParameters{});
    if (!canonical.ok()) {
        return;
    }
    const Hdr10Metadata& defaults = canonical.value();
    if (hdr.mastering.max_luminance == 0) {
        hdr.mastering.max_luminance = defaults.mastering.max_luminance;
    }
    if (hdr.mastering.min_luminance == 0) {
        hdr.mastering.min_luminance = defaults.mastering.min_luminance;
    }
    if (hdr.mastering.red_x == 0) {
        hdr.mastering.red_x = defaults.mastering.red_x;
        hdr.mastering.red_y = defaults.mastering.red_y;
        hdr.mastering.green_x = defaults.mastering.green_x;
        hdr.mastering.green_y = defaults.mastering.green_y;
        hdr.mastering.blue_x = defaults.mastering.blue_x;
        hdr.mastering.blue_y = defaults.mastering.blue_y;
        hdr.mastering.white_x = defaults.mastering.white_x;
        hdr.mastering.white_y = defaults.mastering.white_y;
    }
    if (hdr.cll.max_cll == 0) {
        hdr.cll.max_cll = defaults.cll.max_cll;
        hdr.cll.max_fall = defaults.cll.max_fall;
    }
    if (hdr.target_nits == 0) {
        hdr.target_nits = defaults.target_nits;
    }
}

Rational chooseFrameRate(const HwTranscodeOptions& options, const MediaInfo& info) {
    if (options.fps.valid() && options.fps.num > 0) {
        return options.fps;
    }
    if (info.frame_rate.valid() && info.frame_rate.num > 0 && info.frame_rate.den > 0) {
        return info.frame_rate;
    }
    return Rational{30, 1};
}

int chooseOutputDepth(const HwTranscodeOptions& options, const MediaInfo& info) {
    if (options.bit_depth == 8 || options.bit_depth == 10) {
        return options.bit_depth;
    }
    return info.bit_depth > 8 ? 10 : 8;
}

struct DecoderDeleter {
    const NvidiaShim* shim = nullptr;
    void operator()(RtxDecoder* decoder) const {
        if (shim != nullptr && decoder != nullptr) {
            shim->decoderDestroy(decoder);
        }
    }
};

struct EncoderDeleter {
    const NvidiaShim* shim = nullptr;
    void operator()(RtxEncoder* encoder) const {
        if (shim != nullptr && encoder != nullptr) {
            shim->encoderDestroy(encoder);
        }
    }
};

using DecoderHandle = std::unique_ptr<RtxDecoder, DecoderDeleter>;
using EncoderHandle = std::unique_ptr<RtxEncoder, EncoderDeleter>;

std::string jsonEscape(const std::string& value) {
    std::string output;
    output.reserve(value.size());
    for (const char raw : value) {
        const unsigned char c = static_cast<unsigned char>(raw);
        switch (c) {
            case '"':
                output += "\\\"";
                break;
            case '\\':
                output += "\\\\";
                break;
            case '\n':
                output += "\\n";
                break;
            case '\r':
                output += "\\r";
                break;
            case '\t':
                output += "\\t";
                break;
            default:
                if (c < 0x20 || c >= 0x80) {
                    char buffer[8] = {};
                    std::snprintf(buffer, sizeof(buffer), "\\u%04x", c);
                    output += buffer;
                } else {
                    output += static_cast<char>(c);
                }
                break;
        }
    }
    return output;
}

}  // namespace

std::string HwTranscodeReport::toJson() const {
    std::ostringstream out;
    out << "{\n";
    out << "  \"backend\": \"" << jsonEscape(backend) << "\",\n";
    out << "  \"device\": \"" << jsonEscape(device) << "\",\n";
    out << "  \"path\": \"" << jsonEscape(path) << "\",\n";
    out << "  \"input_container\": \"" << jsonEscape(input_container) << "\",\n";
    out << "  \"output_container\": \"" << jsonEscape(output_container) << "\",\n";
    out << "  \"input_codec\": \"" << jsonEscape(input_codec) << "\",\n";
    out << "  \"output_codec\": \"" << jsonEscape(output_codec) << "\",\n";
    out << "  \"input_width\": " << input_width << ",\n";
    out << "  \"input_height\": " << input_height << ",\n";
    out << "  \"output_width\": " << output_width << ",\n";
    out << "  \"output_height\": " << output_height << ",\n";
    out << "  \"output_bit_depth\": " << output_bit_depth << ",\n";
    out << "  \"packets_read\": " << packets_read << ",\n";
    out << "  \"packets_written\": " << packets_written << ",\n";
    out << "  \"frames_decoded\": " << frames_decoded << ",\n";
    out << "  \"frames_encoded\": " << frames_encoded << ",\n";
    out << "  \"bitstream_bytes\": " << bitstream_bytes << ",\n";
    out << "  \"decoder_device_to_device_bytes\": " << decoder_device_to_device_bytes << ",\n";
    out << "  \"decoder_device_to_host_bytes\": " << decoder_device_to_host_bytes << ",\n";
    out << "  \"decoder_host_pixel_copies\": " << decoder_host_pixel_copies << ",\n";
    out << "  \"encoder_host_to_device_bytes\": " << encoder_host_to_device_bytes << ",\n";
    out << "  \"encoder_host_pixel_copies\": " << encoder_host_pixel_copies << ",\n";
    out << "  \"zero_copy_main_loop\": " << (zero_copy_main_loop ? "true" : "false") << ",\n";
    out << "  \"elapsed_ms\": " << elapsed_ms << ",\n";
    out << "  \"main_loop_ms\": " << main_loop_ms << ",\n";
    out << "  \"remaster_preprocess_ms\": " << remaster_preprocess_ms << ",\n";
    out << "  \"remaster_ngx_ms\": " << remaster_ngx_ms << ",\n";
    out << "  \"remaster_pack_kernel_ms\": " << remaster_pack_kernel_ms << ",\n";
    out << "  \"remaster_frame_ms\": " << remaster_frame_ms << ",\n";
    out << "  \"remaster_pack_kernel_max_ms\": " << remaster_pack_kernel_max_ms << ",\n";
    out << "  \"remaster_pack_launches\": " << remaster_pack_launches << ",\n";
    out << "  \"vsr_quality\": " << vsr_quality << "\n";
    out << "}\n";
    return out.str();
}

Result<HwTranscodeReport> runNvidiaContainerTranscode(const HwTranscodeOptions& options) {
    if (options.input_path.empty() || options.output_path.empty()) {
        return Result<HwTranscodeReport>::fail(ErrorCode::InvalidArgument,
                                               "container transcode requires input and output paths");
    }
    if (options.codec != VideoCodec::HEVC && options.codec != VideoCodec::H264) {
        return Result<HwTranscodeReport>::fail(
            ErrorCode::Unsupported, "stage-1 NVENC supports HEVC and H.264 output");
    }
    if (!ffmpegAvailable()) {
        return Result<HwTranscodeReport>::fail(
            ErrorCode::FfmpegMissing, "this build was compiled without FFmpeg",
            "reconfigure with -DRTX_FFMPEG_ROOT=<prefix>");
    }

    std::unique_ptr<IDemuxer> demuxer = makeFfmpegDemuxer();
    Result<MediaInfo> opened = demuxer->open(options.input_path);
    if (!opened.ok()) {
        return Result<HwTranscodeReport>::fromStatus(opened.status());
    }
    const MediaInfo info = opened.value();
    if (info.width <= 0 || info.height <= 0) {
        return Result<HwTranscodeReport>::fail(ErrorCode::InvalidArgument,
                                               "demuxer reported an invalid video geometry");
    }
    if (shimCodec(info.codec) == RTX_CODEC_UNKNOWN) {
        return Result<HwTranscodeReport>::fail(
            ErrorCode::Unsupported, "unsupported input codec: " + info.video_codec);
    }

    const Clock::time_point started = Clock::now();
    HwTranscodeReport report;
    report.path = "container";
    report.input_container = info.container;
    report.input_codec = info.video_codec;
    report.input_width = info.width;
    report.input_height = info.height;
    report.output_width = info.width;
    report.output_height = info.height;
    report.output_codec = options.codec == VideoCodec::HEVC ? "hevc" : "h264";
    {
        const size_t dot = options.output_path.find_last_of('.');
        if (dot != std::string::npos && dot + 1 < options.output_path.size()) {
            report.output_container = options.output_path.substr(dot + 1);
            std::transform(report.output_container.begin(), report.output_container.end(),
                           report.output_container.begin(),
                           [](unsigned char c) { return static_cast<char>(c | 0x20); });
        }
    }

    NvidiaShim shim;
    std::string shim_error;
    if (!shim.open(&shim_error)) {
        return Result<HwTranscodeReport>::fail(ErrorCode::DllMissing,
                                               "cannot load the NVIDIA shim", shim_error);
    }
    RtxDiagnostics diagnostics{};
    if (shim.probe(&diagnostics) != RTX_OK || diagnostics.device.name[0] == '\0') {
        return Result<HwTranscodeReport>::fail(
            ErrorCode::DllMissing, "no NVIDIA CUDA device is available to rtx_shim",
            diagnostics.summary);
    }
    report.device = diagnostics.device.name;

    // The decoder output depth drives the NVENC session: NVDEC can convert a
    // 10-bit stream down to NV12, but the zero-copy stage-1 path cannot
    // synthesise 10-bit samples from an 8-bit surface.
    const int source_depth = info.bit_depth > 8 ? 10 : 8;
    const int output_depth = chooseOutputDepth(options, info);
    if (output_depth != source_depth) {
        // NVDEC produces the stream's native depth; the stage-1 zero-copy path
        // has no depth-conversion kernel, so the NVENC session must match the
        // decoder surface exactly.
        return Result<HwTranscodeReport>::fail(
            ErrorCode::Unsupported,
            "stage-1 zero-copy transcode preserves the source bit depth",
            "source bit depth is " + std::to_string(source_depth) + ", requested " +
                std::to_string(output_depth));
    }
    if (output_depth > 8 && options.codec == VideoCodec::H264) {
        return Result<HwTranscodeReport>::fail(
            ErrorCode::Unsupported, "H.264 NVENC does not support 10-bit encoding");
    }
    report.output_bit_depth = output_depth;

    RtxDecoderConfig decoder_config{};
    decoder_config.codec = shimCodec(info.codec);
    decoder_config.width = info.width;
    decoder_config.height = info.height;
    decoder_config.bit_depth = output_depth;
    decoder_config.output_format =
        output_depth > 8 ? RTX_PIXEL_P010 : RTX_PIXEL_NV12;
    decoder_config.clock_rate = 90000;
    decoder_config.max_surfaces = 32;
    decoder_config.pool_slots = 16;

    RtxDecoder* raw_decoder = nullptr;
    RtxResult result = shim.decoderCreate(&decoder_config, &raw_decoder);
    if (result != RTX_OK) {
        return Result<HwTranscodeReport>::fail(
            mapShimResult(result), describe("rtx_shim_decoder_create", result, nullptr),
            shim.decoderLastError(nullptr));
    }
    DecoderHandle decoder(raw_decoder, DecoderDeleter{&shim});

    Hdr10Metadata hdr{};
    if (options.hdr) {
        hdr = info.hdr;
        if (!hdr.enabled && info.color.transfer != TransferFunction::BT709) {
            hdr.enabled = true;
            hdr.color = info.color;
        }
        applyHdrDefaults(hdr);
        // Stage 1 is a pass-through: an SDR source stays SDR.  HDR10 metadata
        // is propagated only when the source carries it (or declares a PQ/HLG
        // transfer), because stage 1 performs no inverse tone mapping.
    }

    EncoderHandle encoder(nullptr, EncoderDeleter{&shim});
    std::unique_ptr<IMuxer> muxer;
    MediaInfo output_info = info;
    output_info.codec = options.codec;
    output_info.video_codec = report.output_codec;
    output_info.format_name.clear();
    output_info.annex_b = true;
    if (hdr.enabled) {
        // The container must describe the same colour space as the bitstream
        // VUI/SEI; otherwise the mdcv box and the NAL metadata disagree.
        output_info.color = hdr.color;
        output_info.bit_depth = output_depth;
    }
    // SDR pass-through still needs the source matrix/range in the NVENC VUI;
    // the shim otherwise defaults to BT.709 and the bitstream would disagree
    // with the container flags copied from the demuxer.
    hdr.color = info.color;
    // The CUVID parser clock and therefore every NVENC input timestamp are in
    // the 90 kHz clock declared in RtxDecoderConfig::clock_rate.
    const Rational clock_time_base{1, 90000};
    output_info.time_base = clock_time_base;
    output_info.frame_rate = chooseFrameRate(options, info);
    output_info.bit_depth = output_depth;
    output_info.hdr = hdr;
    if (options.bitrate_kbps > 0) {
        output_info.bitrate = options.bitrate_kbps * 1000;
    } else if (output_info.bitrate <= 0) {
        output_info.bitrate =
            static_cast<int>(std::max<int64_t>(500000, static_cast<int64_t>(info.width) *
                                                              info.height * 3));
    }

    const Rational frame_rate = output_info.frame_rate;
    int64_t estimated_total_frames = 0;
    if (info.duration_ms > 0 && frame_rate.num > 0 && frame_rate.den > 0) {
        estimated_total_frames = info.duration_ms * frame_rate.num /
                                 (1000 * static_cast<int64_t>(frame_rate.den));
    }
    auto reportProgress = [&]() {
        if (!options.progress) {
            return;
        }
        HwTranscodeProgress progress;
        progress.frames_decoded = report.frames_decoded;
        progress.frames_encoded = report.frames_encoded;
        progress.total_frames = estimated_total_frames;
        progress.elapsed_ms =
            std::chrono::duration<double, std::milli>(Clock::now() - started).count();
        options.progress(progress);
    };
    auto cancelled = [&]() {
        return options.cancel != nullptr && options.cancel->load(std::memory_order_relaxed);
    };
    auto createEncoder = [&](const RtxFrameDesc& frame) -> Status {
        RtxEncoderConfig config{};
        config.codec = encoderCodec(options.codec);
        config.width = frame.width;
        config.height = frame.height;
        config.fps_num = static_cast<int32_t>(frame_rate.num);
        config.fps_den = static_cast<int32_t>(frame_rate.den);
        config.bitrate_kbps = options.bitrate_kbps;
        config.preset = std::clamp(options.preset, 1, 7);
        config.level = 0;
        config.tier = 0;
        config.hdr = toShimHdr(hdr);
        config.input_format = frame.format;
        config.gop_length = static_cast<int32_t>(std::max<int64_t>(
            1, frame_rate.num > 0 ? frame_rate.den * 2 : 60));
        config.rate_control = std::clamp(options.rate_control, 0, 2);
        config.qp = options.qp;

        RtxEncoder* raw = nullptr;
        const RtxResult created = shim.encoderCreate(&config, &raw);
        if (created != RTX_OK || raw == nullptr) {
            return Status::fail(mapShimResult(created),
                                describe("rtx_shim_encoder_create", created, nullptr));
        }
        encoder.reset(raw);
        report.output_bit_depth = frame.bit_depth;
        output_info.bit_depth = frame.bit_depth;
        // Trust the decoded geometry over the container metadata: NVDEC crops
        // macroblock padding (1088 -> 1080) and the muxer must declare exactly
        // what NVENC encoded.
        output_info.width = frame.width;
        output_info.height = frame.height;
        report.output_width = frame.width;
        report.output_height = frame.height;
        return Status::success();
    };

    auto openMuxer = [&]() -> Status {
        if (muxer != nullptr) {
            return Status::success();
        }
        size_t header_size = 0;
        RtxResult header_result =
            shim.encoderSequenceHeader(encoder.get(), nullptr, 0, &header_size);
        if (header_result != RTX_OK || header_size == 0) {
            return Status::fail(ErrorCode::Internal,
                                "NVENC did not provide a sequence header",
                                shim.encoderLastError(encoder.get()));
        }
        std::vector<uint8_t> sequence_header(header_size);
        header_result = shim.encoderSequenceHeader(encoder.get(), sequence_header.data(),
                                                   sequence_header.size(), &header_size);
        if (header_result != RTX_OK) {
            return Status::fail(ErrorCode::Internal, "cannot fetch the NVENC sequence header",
                                shim.encoderLastError(encoder.get()));
        }
        sequence_header.resize(header_size);
        output_info.extradata =
            options.codec == VideoCodec::HEVC
                ? makeHevcConfigRecord(sequence_header.data(), sequence_header.size())
                : makeAvcConfigRecord(sequence_header.data(), sequence_header.size());
        if (output_info.extradata.empty()) {
            return Status::fail(ErrorCode::Internal,
                                "cannot build the ISO-BMFF codec configuration record");
        }
        muxer = makeFfmpegMuxer();
        return muxer->open(options.output_path, output_info, hdr);
    };

    int64_t fallback_pts = 0;
    const int64_t frame_ticks =
        frame_rate.num > 0 && frame_rate.den > 0
            ? std::max<int64_t>(1, static_cast<int64_t>(90000) * frame_rate.den / frame_rate.num)
            : 3000;
    auto processFrame = [&](const RtxFrameDesc& frame) -> Status {
        if (encoder == nullptr) {
            const Status created = createEncoder(frame);
            if (!created.ok()) {
                (void)shim.decoderReleaseFrame(decoder.get(), frame.slot_index);
                return created;
            }
        }
        RtxPacketDesc packet{};
        const RtxResult encoded = shim.encoderEncode(encoder.get(), &frame, &packet);
        if (encoded != RTX_OK) {
            (void)shim.decoderReleaseFrame(decoder.get(), frame.slot_index);
            return Status::fail(mapShimResult(encoded), describe("NvEncEncodePicture", encoded,
                                                                shim.encoderLastError(
                                                                    encoder.get())));
        }
        (void)shim.decoderReleaseFrame(decoder.get(), frame.slot_index);

        const Status opened_mux = openMuxer();
        if (!opened_mux.ok()) {
            return opened_mux;
        }
        Packet out;
        if (packet.size != 0) {
            out.data.assign(packet.data, packet.data + packet.size);
        }
        out.pts = packet.pts != 0 ? packet.pts : fallback_pts;
        out.dts = packet.dts;
        out.duration = packet.duration;
        out.keyframe = packet.keyframe != 0;
        out.time_base = output_info.time_base;
        const Status written = muxer->writePacket(out);
        if (!written.ok()) {
            return written;
        }
        ++report.frames_encoded;
        ++report.packets_written;
        report.bitstream_bytes += packet.size;
        fallback_pts += frame_ticks;
        return Status::success();
    };

    auto drainFrames = [&](bool end_of_stream) -> Status {
        for (;;) {
            if (cancelled()) {
                return Status::fail(ErrorCode::State, "transcode cancelled");
            }
            // --frames N: once the limit is reached, return remaining decode
            // slots without encoding so the count is exact.
            if (options.frame_limit > 0 && report.frames_decoded >= options.frame_limit) {
                return Status::success();
            }
            RtxFrameDesc frame{};
            const RtxResult received = shim.decoderReceive(decoder.get(), &frame);
            if (received == RTX_OK) {
                const Status processed = processFrame(frame);
                if (!processed.ok()) {
                    return processed;
                }
                ++report.frames_decoded;
                reportProgress();
                continue;
            }
            if (received == RTX_ERR_NOT_FOUND) {
                return Status::success();
            }
            if (received == RTX_ERR_IO && end_of_stream) {
                return Status::success();
            }
            return Status::fail(mapShimResult(received),
                                describe("rtx_shim_decoder_receive", received,
                                         shim.decoderLastError(decoder.get())));
        }
    };

    int64_t packet_pts = 0;
    const Rational input_time_base =
        info.time_base.valid() ? info.time_base : clock_time_base;
    for (;;) {
        if (cancelled()) {
            return Result<HwTranscodeReport>::fail(ErrorCode::State,
                                                   "transcode cancelled");
        }
        if (options.frame_limit > 0 && report.frames_decoded >= options.frame_limit) {
            break;
        }
        Result<Packet> packet = demuxer->readPacket();
        if (!packet.ok()) {
            if (packet.code() == ErrorCode::NotFound) {
                break;
            }
            return Result<HwTranscodeReport>::fromStatus(packet.status());
        }
        ++report.packets_read;
        Packet& input = packet.value();
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
            shim.decoderSubmit(decoder.get(), input.data.data(), input.data.size(), pts,
                               dts, input.keyframe ? 1 : 0);
        if (submitted != RTX_OK) {
            return Result<HwTranscodeReport>::fail(
                mapShimResult(submitted),
                describe("cuvidParseVideoData", submitted, shim.decoderLastError(decoder.get())));
        }
        const Status drained = drainFrames(false);
        if (!drained.ok()) {
            return Result<HwTranscodeReport>::fromStatus(drained);
        }
        packet_pts += frame_ticks;
    }

    const RtxResult flushed_decoder = shim.decoderFlush(decoder.get());
    if (flushed_decoder != RTX_OK) {
        return Result<HwTranscodeReport>::fail(
            mapShimResult(flushed_decoder),
            describe("cuvidParseVideoData(EOS)", flushed_decoder,
                     shim.decoderLastError(decoder.get())));
    }
    Status drained = drainFrames(true);
    if (!drained.ok()) {
        return Result<HwTranscodeReport>::fromStatus(drained);
    }

    if (encoder != nullptr) {
        for (int attempt = 0; attempt < 8; ++attempt) {
            RtxPacketDesc packet{};
            const RtxResult flushed = shim.encoderFlush(encoder.get(), &packet);
            if (flushed == RTX_ERR_NOT_FOUND) {
                break;
            }
            if (flushed != RTX_OK) {
                return Result<HwTranscodeReport>::fail(
                    mapShimResult(flushed),
                    describe("NvEncEncodePicture(EOS)", flushed,
                             shim.encoderLastError(encoder.get())));
            }
            if (packet.size != 0) {
                const Status opened_mux = openMuxer();
                if (!opened_mux.ok()) {
                    return Result<HwTranscodeReport>::fromStatus(opened_mux);
                }
                Packet out;
                out.data.assign(packet.data, packet.data + packet.size);
                out.pts = packet.pts;
                out.dts = packet.dts;
                out.duration = packet.duration;
                out.keyframe = packet.keyframe != 0;
                out.time_base = output_info.time_base;
                const Status written = muxer->writePacket(out);
                if (!written.ok()) {
                    return Result<HwTranscodeReport>::fromStatus(written);
                }
                ++report.packets_written;
                report.bitstream_bytes += packet.size;
            }
        }
    }

    if (muxer == nullptr) {
        return Result<HwTranscodeReport>::fail(ErrorCode::Internal,
                                               "no frame was decoded; output was not created");
    }
    const Status finalized = muxer->finalize();
    if (!finalized.ok()) {
        return Result<HwTranscodeReport>::fromStatus(finalized);
    }

    RtxHwStats decoder_stats{};
    if (shim.decoderStats(decoder.get(), &decoder_stats) == RTX_OK) {
        report.decoder_device_to_device_bytes = decoder_stats.device_to_device_bytes;
        report.decoder_device_to_host_bytes = decoder_stats.device_to_host_pixel_bytes;
        report.decoder_host_pixel_copies = decoder_stats.host_pixel_copies;
    }
    if (encoder != nullptr) {
        RtxHwStats encoder_stats{};
        if (shim.encoderStats(encoder.get(), &encoder_stats) == RTX_OK) {
            report.encoder_host_to_device_bytes = encoder_stats.host_to_device_pixel_bytes;
            report.encoder_host_pixel_copies = encoder_stats.host_pixel_copies;
        }
    }
    report.zero_copy_main_loop =
        report.decoder_host_pixel_copies == 0 && report.encoder_host_pixel_copies == 0 &&
        report.decoder_device_to_host_bytes == 0 && report.encoder_host_to_device_bytes == 0;
    report.elapsed_ms =
        std::chrono::duration<double, std::milli>(Clock::now() - started).count();
    return Result<HwTranscodeReport>::ok(std::move(report));
}

Result<HwTranscodeReport> runNvidiaRemasterTranscode(const HwTranscodeOptions& options) {
    if (options.input_path.empty() || options.output_path.empty()) {
        return Result<HwTranscodeReport>::fail(ErrorCode::InvalidArgument,
                                               "remaster transcode requires input and output paths");
    }
    if (options.codec != VideoCodec::HEVC) {
        return Result<HwTranscodeReport>::fail(
            ErrorCode::Unsupported, "the VSR-remaster path outputs 10-bit HEVC Main10 only");
    }
    if (options.vsr_quality < 1 || options.vsr_quality > 4) {
        return Result<HwTranscodeReport>::fail(ErrorCode::InvalidArgument,
                                               "vsr_quality must be in 1..4");
    }
    if (!ffmpegAvailable()) {
        return Result<HwTranscodeReport>::fail(
            ErrorCode::FfmpegMissing, "this build was compiled without FFmpeg",
            "reconfigure with -DRTX_FFMPEG_ROOT=<prefix>");
    }

    std::unique_ptr<IDemuxer> demuxer = makeFfmpegDemuxer();
    Result<MediaInfo> opened = demuxer->open(options.input_path);
    if (!opened.ok()) {
        return Result<HwTranscodeReport>::fromStatus(opened.status());
    }
    const MediaInfo info = opened.value();
    if (info.width <= 0 || info.height <= 0) {
        return Result<HwTranscodeReport>::fail(ErrorCode::InvalidArgument,
                                               "demuxer reported an invalid video geometry");
    }
    if (shimCodec(info.codec) == RTX_CODEC_UNKNOWN) {
        return Result<HwTranscodeReport>::fail(
            ErrorCode::Unsupported, "unsupported input codec: " + info.video_codec);
    }
    const int target_width = options.target_width > 0 ? options.target_width : 3840;
    const int target_height = options.target_height > 0 ? options.target_height : 2160;
    if ((target_width & 1) != 0 || (target_height & 1) != 0) {
        return Result<HwTranscodeReport>::fail(ErrorCode::InvalidArgument,
                                               "target geometry must be even");
    }
    if (target_width < info.width || target_height < info.height) {
        return Result<HwTranscodeReport>::fail(
            ErrorCode::InvalidArgument,
            "remaster target geometry must not be smaller than the source");
    }

    const Clock::time_point started = Clock::now();
    HwTranscodeReport report;
    report.path = "vsr-remaster";
    report.input_container = info.container;
    report.input_codec = info.video_codec;
    report.input_width = info.width;
    report.input_height = info.height;
    report.output_width = target_width;
    report.output_height = target_height;
    report.output_bit_depth = 10;
    report.output_codec = "hevc";
    report.vsr_quality = options.vsr_quality;
    {
        const size_t dot = options.output_path.find_last_of('.');
        if (dot != std::string::npos && dot + 1 < options.output_path.size()) {
            report.output_container = options.output_path.substr(dot + 1);
            std::transform(report.output_container.begin(), report.output_container.end(),
                           report.output_container.begin(),
                           [](unsigned char c) { return static_cast<char>(c | 0x20); });
        }
    }

    // CRITICAL: Load and query the NGX bridge BEFORE shim.probe() calls cuInit(0).
    // cuInit(0) initializes internal DXGI state in the NVIDIA driver, which causes
    // a subsequent DXGIDeclareAdapterRemovalSupport() call to fail with
    // DXGI_ERROR_INVALID_CALL and silently disables anti-TDR mechanism 4.
    std::string shim_error;
    NgxRemasterBridge bridge_loader;
    if (!bridge_loader.open(&shim_error)) {
        return Result<HwTranscodeReport>::fail(ErrorCode::DllMissing,
                                               "cannot load the NGX bridge", shim_error);
    }
    RtxNgxBridgeCaps caps{};
    const RtxResult caps_result = bridge_loader.queryCaps(&caps);
    if (caps_result != RTX_OK || caps.vsr_available == 0 || caps.truehdr_available == 0) {
        return Result<HwTranscodeReport>::fail(
            ErrorCode::NgxFailure, "the RTX Video SDK is not fully available for remaster",
            caps_result == RTX_OK ? caps.diagnostics
                                  : describe("rtx_ngx_bridge_query_caps", caps_result, nullptr));
    }

    NvidiaShim shim;
    if (!shim.open(&shim_error)) {
        return Result<HwTranscodeReport>::fail(ErrorCode::DllMissing,
                                               "cannot load the NVIDIA shim", shim_error);
    }
    RtxDiagnostics diagnostics{};
    if (shim.probe(&diagnostics) != RTX_OK || diagnostics.device.name[0] == '\0') {
        return Result<HwTranscodeReport>::fail(
            ErrorCode::DllMissing, "no NVIDIA CUDA device is available to rtx_shim",
            diagnostics.summary);
    }
    report.device = diagnostics.device.name;

    // HDR10 metadata comes from the single source of truth (SeiSynthesizer):
    // DCI-P3 D65 mastering, 1000 cd/m^2 peak, MaxCLL 1000 / MaxFALL 300, and
    // the BT.2020 primaries + PQ transfer + BT.2020 NCL matrix the pack v2
    // kernel actually produced.  The NVENC VUI, the SEI and the container
    // boxes all read this one object.
    const Result<Hdr10Metadata> canonical = SeiSynthesizer::makeMetadata(TrueHdrParameters{});
    if (!canonical.ok()) {
        return Result<HwTranscodeReport>::fromStatus(canonical.status());
    }
    Hdr10Metadata hdr = canonical.value();
    applyHdrDefaults(hdr);

    // NVDEC emits the source's native depth; the preprocess kernel accepts
    // both NV12 (8-bit SDR, the Gate 2.1 input) and P010 (10-bit source).
    const int source_depth = info.bit_depth > 8 ? 10 : 8;
    RtxDecoderConfig decoder_config{};
    decoder_config.codec = shimCodec(info.codec);
    decoder_config.width = info.width;
    decoder_config.height = info.height;
    decoder_config.bit_depth = source_depth;
    decoder_config.output_format =
        source_depth > 8 ? RTX_PIXEL_P010 : RTX_PIXEL_NV12;
    decoder_config.clock_rate = 90000;
    decoder_config.max_surfaces = 32;
    decoder_config.pool_slots = 16;

    // Destruction order matters: NVENC must unregister the bridge-owned P010
    // buffer before the bridge frees it, so the bridge handle is declared
    // first and destroyed last.
    struct BridgeDeleter {
        const NgxRemasterBridge* loader = nullptr;
        void operator()(RtxNgxBridge* bridge) const {
            if (loader != nullptr && bridge != nullptr) {
                loader->destroy(bridge);
            }
        }
    };

    RtxNgxBridgeConfig bridge_config{};
    std::memcpy(bridge_config.luid, diagnostics.device.luid, sizeof(bridge_config.luid));
    bridge_config.luid_node_mask = diagnostics.device.luid_node_mask;
    bridge_config.input_width = info.width;
    bridge_config.input_height = info.height;
    bridge_config.output_width = target_width;
    bridge_config.output_height = target_height;
    bridge_config.vsr_quality = options.vsr_quality;
    bridge_config.enable_truehdr = 1;
    bridge_config.contrast = 0;   // golden defaults inside the bridge
    bridge_config.saturation = 0; // (125 / 75 / 44 / 1000 nit)
    bridge_config.middle_gray = 0;
    bridge_config.max_luminance = 0;
    bridge_config.project_id = 0x52545831u;  // "RTX1": stable per-application id
    RtxNgxBridge* raw_bridge = nullptr;
    RtxResult result = bridge_loader.create(&bridge_config, &raw_bridge);
    if (result != RTX_OK || raw_bridge == nullptr) {
        return Result<HwTranscodeReport>::fail(
            mapShimResult(result), describe("rtx_ngx_bridge_create", result, nullptr),
            bridge_loader.lastError(nullptr));
    }
    std::unique_ptr<RtxNgxBridge, BridgeDeleter> bridge(raw_bridge,
                                                        BridgeDeleter{&bridge_loader});

    RtxDecoder* raw_decoder = nullptr;
    result = shim.decoderCreate(&decoder_config, &raw_decoder);
    if (result != RTX_OK) {
        return Result<HwTranscodeReport>::fail(
            mapShimResult(result), describe("rtx_shim_decoder_create", result, nullptr),
            shim.decoderLastError(nullptr));
    }
    DecoderHandle decoder(raw_decoder, DecoderDeleter{&shim});

    const Rational frame_rate = chooseFrameRate(options, info);
    RtxEncoderConfig encoder_config{};
    encoder_config.codec = encoderCodec(VideoCodec::HEVC);
    encoder_config.width = target_width;
    encoder_config.height = target_height;
    encoder_config.fps_num = static_cast<int32_t>(frame_rate.num);
    encoder_config.fps_den = static_cast<int32_t>(frame_rate.den);
    encoder_config.bitrate_kbps = options.bitrate_kbps;
    encoder_config.preset = std::clamp(options.preset, 1, 7);
    encoder_config.hdr = toShimHdr(hdr);
    encoder_config.input_format = RTX_PIXEL_P010;
    encoder_config.gop_length = static_cast<int32_t>(std::max<int64_t>(
        1, frame_rate.num > 0 ? frame_rate.den * 2 : 60));
    encoder_config.rate_control = std::clamp(options.rate_control, 0, 2);
    encoder_config.qp = options.qp;
    RtxEncoder* raw_encoder = nullptr;
    result = shim.encoderCreate(&encoder_config, &raw_encoder);
    if (result != RTX_OK || raw_encoder == nullptr) {
        return Result<HwTranscodeReport>::fail(
            mapShimResult(result), describe("rtx_shim_encoder_create", result, nullptr),
            shim.encoderLastError(nullptr));
    }
    EncoderHandle encoder(raw_encoder, EncoderDeleter{&shim});

    // The container must describe the same colour volume as the bitstream
    // VUI/SEI: 4K, 10-bit, BT.2020 PQ limited.
    MediaInfo output_info = info;
    output_info.codec = VideoCodec::HEVC;
    output_info.video_codec = "hevc";
    output_info.width = target_width;
    output_info.height = target_height;
    output_info.bit_depth = 10;
    output_info.format_name.clear();
    output_info.annex_b = true;
    output_info.color = hdr.color;
    output_info.hdr = hdr;
    output_info.time_base = Rational{1, 90000};
    output_info.frame_rate = frame_rate;
    if (options.bitrate_kbps > 0) {
        output_info.bitrate = options.bitrate_kbps * 1000;
    } else if (output_info.bitrate <= 0) {
        output_info.bitrate = static_cast<int>(std::max<int64_t>(
            8000000, static_cast<int64_t>(target_width) * target_height * 4));
    }

    std::unique_ptr<IMuxer> muxer;
    auto openMuxer = [&]() -> Status {
        if (muxer != nullptr) {
            return Status::success();
        }
        size_t header_size = 0;
        RtxResult header_result =
            shim.encoderSequenceHeader(encoder.get(), nullptr, 0, &header_size);
        if (header_result != RTX_OK || header_size == 0) {
            return Status::fail(ErrorCode::Internal,
                                "NVENC did not provide a sequence header",
                                shim.encoderLastError(encoder.get()));
        }
        std::vector<uint8_t> sequence_header(header_size);
        header_result = shim.encoderSequenceHeader(encoder.get(), sequence_header.data(),
                                                   sequence_header.size(), &header_size);
        if (header_result != RTX_OK) {
            return Status::fail(ErrorCode::Internal, "cannot fetch the NVENC sequence header",
                                shim.encoderLastError(encoder.get()));
        }
        sequence_header.resize(header_size);
        output_info.extradata =
            makeHevcConfigRecord(sequence_header.data(), sequence_header.size());
        if (output_info.extradata.empty()) {
            return Status::fail(ErrorCode::Internal,
                                "cannot build the ISO-BMFF codec configuration record");
        }
        muxer = makeFfmpegMuxer();
        return muxer->open(options.output_path, output_info, hdr);
    };

    int64_t estimated_total_frames = 0;
    if (info.duration_ms > 0 && frame_rate.num > 0 && frame_rate.den > 0) {
        estimated_total_frames = info.duration_ms * frame_rate.num /
                                 (1000 * static_cast<int64_t>(frame_rate.den));
    }
    if (options.frame_limit > 0) {
        estimated_total_frames = options.frame_limit;
    }
    const int64_t frame_cap = options.frame_limit > 0 ? options.frame_limit : 0;

    // The decoded frame's matrix/range drive the preprocess kernel so the
    // YUV->RGB conversion matches what the source actually encoded.
    const int32_t input_matrix = info.color.matrix == MatrixCoefficients::BT2020NC ? 1
                                : info.color.matrix == MatrixCoefficients::BT709    ? 0
                                                                                    : 2;
    const int32_t input_full_range = info.color.range == ColorRange::Full ? 1 : 0;

    RtxNgxRemasterStats previous_stats{};
    double pack_max_ms = 0.0;
    const int64_t frame_ticks =
        frame_rate.num > 0 && frame_rate.den > 0
            ? std::max<int64_t>(1, static_cast<int64_t>(90000) * frame_rate.den / frame_rate.num)
            : 3000;
    // Encode pipeline: NVENC sits on an engine independent of the NGX/CUDA
    // stages, so encoding frame i overlaps the remaster of frame i+1.  The
    // bridge hands out a two-buffer P010 ring; the producer therefore waits
    // until at most one encode is outstanding before issuing the next
    // remaster, which is exactly the ring-depth contract (frame i may only
    // be issued after frame i-2 completed encoding).
    struct PendingEncode {
        RtxNgxRemasterOutput out{};
        int64_t pts = 0;
    };
    std::mutex pipeline_mutex;
    std::condition_variable queue_ready;
    std::condition_variable slot_free;
    std::deque<PendingEncode> encode_queue;
    uint64_t remasters_issued = 0;
    uint64_t encodes_completed = 0;
    bool producer_done = false;
    Status consumer_error = Status::success();
    bool consumer_failed = false;
    auto cancelled = [&]() {
        return options.cancel != nullptr && options.cancel->load(std::memory_order_relaxed);
    };
    auto consumerStopped = [&]() { return consumer_failed || cancelled(); };
    auto reportProgress = [&]() {
        if (!options.progress) {
            return;
        }
        HwTranscodeProgress progress;
        {
            // Snapshot under the pipeline lock: the encode thread updates
            // frames_encoded while the producer updates frames_decoded.
            std::lock_guard<std::mutex> lock(pipeline_mutex);
            progress.frames_decoded = report.frames_decoded;
            progress.frames_encoded = report.frames_encoded;
        }
        progress.total_frames = estimated_total_frames;
        progress.elapsed_ms =
            std::chrono::duration<double, std::milli>(Clock::now() - started).count();
        options.progress(progress);
    };

    std::thread encode_thread([&]() {
        int64_t local_fallback_pts = 0;
        for (;;) {
            std::unique_lock<std::mutex> lock(pipeline_mutex);
            queue_ready.wait(lock, [&] { return !encode_queue.empty() || producer_done; });
            if (encode_queue.empty()) {
                if (producer_done) {
                    break;
                }
                continue;
            }
            const PendingEncode pending = encode_queue.front();
            encode_queue.pop_front();
            lock.unlock();

            RtxFrameDesc encode_frame{};
            encode_frame.planes[0] = pending.out.y_plane;
            encode_frame.planes[1] = pending.out.uv_plane;
            encode_frame.pitches[0] = pending.out.y_pitch;
            encode_frame.pitches[1] = pending.out.uv_pitch;
            encode_frame.width = pending.out.width;
            encode_frame.height = pending.out.height;
            encode_frame.format = pending.out.format;
            encode_frame.bit_depth = 10;
            encode_frame.pts = pending.pts;
            encode_frame.slot_index = 0;  // bridge-owned ring buffer
            RtxPacketDesc packet{};
            const RtxResult encoded = shim.encoderEncode(encoder.get(), &encode_frame, &packet);
            if (encoded != RTX_OK) {
                std::lock_guard<std::mutex> fail_lock(pipeline_mutex);
                consumer_error = Status::fail(
                    mapShimResult(encoded),
                    describe("NvEncEncodePicture", encoded, shim.encoderLastError(encoder.get())));
                consumer_failed = true;
                slot_free.notify_all();
                queue_ready.notify_all();
                return;
            }
            const Status opened_mux = openMuxer();
            if (!opened_mux.ok()) {
                std::lock_guard<std::mutex> fail_lock(pipeline_mutex);
                consumer_error = opened_mux;
                consumer_failed = true;
                slot_free.notify_all();
                queue_ready.notify_all();
                return;
            }
            Packet out;
            if (packet.size != 0) {
                out.data.assign(packet.data, packet.data + packet.size);
            }
            out.pts = packet.pts != 0 ? packet.pts : local_fallback_pts;
            out.dts = packet.dts;
            out.duration = packet.duration;
            out.keyframe = packet.keyframe != 0;
            out.time_base = output_info.time_base;
            const Status written = muxer->writePacket(out);
            if (!written.ok()) {
                std::lock_guard<std::mutex> fail_lock(pipeline_mutex);
                consumer_error = written;
                consumer_failed = true;
                slot_free.notify_all();
                queue_ready.notify_all();
                return;
            }
            ++report.frames_encoded;
            ++report.packets_written;
            report.bitstream_bytes += packet.size;
            local_fallback_pts += frame_ticks;
            {
                std::lock_guard<std::mutex> done_lock(pipeline_mutex);
                ++encodes_completed;
            }
            slot_free.notify_all();
            reportProgress();
        }
    });

    auto processFrame = [&](const RtxFrameDesc& frame) -> Status {
        // Ring-depth contract: block until at most one encode is outstanding.
        {
            std::unique_lock<std::mutex> lock(pipeline_mutex);
            slot_free.wait(lock, [&] {
                return remasters_issued - encodes_completed < 2 || consumerStopped();
            });
            if (consumerStopped()) {
                return consumer_failed ? consumer_error
                                       : Status::fail(ErrorCode::State, "transcode cancelled");
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
        RtxNgxRemasterOutput remaster_out{};
        RtxNgxRemasterStats frame_stats{};
        const RtxResult remastered =
            bridge_loader.remaster(bridge.get(), &remaster_in, &remaster_out, &frame_stats);
        if (remastered != RTX_OK) {
            (void)shim.decoderReleaseFrame(decoder.get(), frame.slot_index);
            return Status::fail(
                mapShimResult(remastered),
                describe("rtx_ngx_bridge_remaster", remastered, nullptr),
                bridge_loader.lastError(bridge.get()));
        }
        const double pack_delta =
            static_cast<double>(frame_stats.pack_kernel_ms - previous_stats.pack_kernel_ms);
        previous_stats = frame_stats;
        if (pack_delta > pack_max_ms) {
            pack_max_ms = pack_delta;
        }
        (void)shim.decoderReleaseFrame(decoder.get(), frame.slot_index);
        {
            std::lock_guard<std::mutex> lock(pipeline_mutex);
            encode_queue.push_back(PendingEncode{remaster_out, frame.pts});
            ++remasters_issued;
        }
        queue_ready.notify_all();
        return Status::success();
    };
    auto finishPipeline = [&]() -> Status {
        {
            std::lock_guard<std::mutex> lock(pipeline_mutex);
            producer_done = true;
        }
        queue_ready.notify_all();
        encode_thread.join();
        if (consumer_failed) {
            return consumer_error;
        }
        return Status::success();
    };
    auto drainFrames = [&](bool end_of_stream) -> Status {
        for (;;) {
            if (cancelled() || consumer_failed) {
                return Status::fail(ErrorCode::State,
                                    consumer_failed ? "encode thread failed"
                                                    : "transcode cancelled");
            }
            if (frame_cap > 0 && report.frames_decoded >= frame_cap) {
                return Status::success();
            }
            RtxFrameDesc frame{};
            const RtxResult received = shim.decoderReceive(decoder.get(), &frame);
            if (received == RTX_OK) {
                const Status processed = processFrame(frame);
                if (!processed.ok()) {
                    return processed;
                }
                ++report.frames_decoded;
                reportProgress();
                continue;
            }
            if (received == RTX_ERR_NOT_FOUND) {
                return Status::success();
            }
            if (received == RTX_ERR_IO && end_of_stream) {
                return Status::success();
            }
            return Status::fail(mapShimResult(received),
                                describe("rtx_shim_decoder_receive", received,
                                         shim.decoderLastError(decoder.get())));
        }
    };

    int64_t packet_pts = 0;
    const Rational input_time_base =
        info.time_base.valid() ? info.time_base : Rational{1, 90000};
    // The steady-state throughput metric excludes demuxer open, bridge/NGX
    // creation and the NVENC session: Gate 2.1 measures the loop, not setup.
    // Every early exit inside the lambda flows through finishPipeline(), so
    // the encode thread is always joined before the function returns.
    const Clock::time_point main_started = Clock::now();
    const Status pipeline_status = [&]() -> Status {
        for (;;) {
            if (cancelled() || consumer_failed) {
                return Status::fail(ErrorCode::State,
                                    consumer_failed ? "encode thread failed"
                                                    : "transcode cancelled");
            }
            if (frame_cap > 0 && report.frames_decoded >= frame_cap) {
                return Status::success();
            }
            Result<Packet> packet = demuxer->readPacket();
            if (!packet.ok()) {
                if (packet.code() == ErrorCode::NotFound) {
                    return Status::success();
                }
                return Result<HwTranscodeReport>::fromStatus(packet.status()).status();
            }
            ++report.packets_read;
            Packet& input = packet.value();
            int64_t pts = input.pts;
            if (pts == std::numeric_limits<int64_t>::min()) {
                pts = packet_pts;
            } else {
                pts = rescaleQ(pts, input_time_base, Rational{1, 90000});
            }
            int64_t dts = input.dts;
            if (dts == std::numeric_limits<int64_t>::min()) {
                dts = pts;
            } else {
                dts = rescaleQ(dts, input_time_base, Rational{1, 90000});
            }
            const RtxResult submitted =
                shim.decoderSubmit(decoder.get(), input.data.data(), input.data.size(), pts,
                                   dts, input.keyframe ? 1 : 0);
            if (submitted != RTX_OK) {
                return Status::fail(
                    mapShimResult(submitted),
                    describe("cuvidParseVideoData", submitted,
                             shim.decoderLastError(decoder.get())));
            }
            const Status drained = drainFrames(false);
            if (!drained.ok()) {
                return drained;
            }
            packet_pts += frame_ticks;
        }
    }();

    if (pipeline_status.ok()) {
        const RtxResult flushed_decoder = shim.decoderFlush(decoder.get());
        if (flushed_decoder != RTX_OK) {
            const Status joined = finishPipeline();
            (void)joined;
            return Result<HwTranscodeReport>::fail(
                mapShimResult(flushed_decoder),
                describe("cuvidParseVideoData(EOS)", flushed_decoder,
                         shim.decoderLastError(decoder.get())));
        }
    }
    if (pipeline_status.ok()) {
        const Status drained = drainFrames(true);
        if (!drained.ok()) {
            const Status joined = finishPipeline();
            (void)joined;
            return Result<HwTranscodeReport>::fromStatus(drained);
        }
    }
    report.main_loop_ms =
        std::chrono::duration<double, std::milli>(Clock::now() - main_started).count();
    {
        const Status finished = finishPipeline();
        if (!pipeline_status.ok()) {
            return Result<HwTranscodeReport>::fromStatus(pipeline_status);
        }
        if (!finished.ok()) {
            return Result<HwTranscodeReport>::fromStatus(finished);
        }
    }

    if (encoder != nullptr) {
        for (int attempt = 0; attempt < 8; ++attempt) {
            RtxPacketDesc packet{};
            const RtxResult flushed = shim.encoderFlush(encoder.get(), &packet);
            if (flushed == RTX_ERR_NOT_FOUND) {
                break;
            }
            if (flushed != RTX_OK) {
                return Result<HwTranscodeReport>::fail(
                    mapShimResult(flushed),
                    describe("NvEncEncodePicture(EOS)", flushed,
                             shim.encoderLastError(encoder.get())));
            }
            if (packet.size != 0) {
                const Status opened_mux = openMuxer();
                if (!opened_mux.ok()) {
                    return Result<HwTranscodeReport>::fromStatus(opened_mux);
                }
                Packet out;
                out.data.assign(packet.data, packet.data + packet.size);
                out.pts = packet.pts;
                out.dts = packet.dts;
                out.duration = packet.duration;
                out.keyframe = packet.keyframe != 0;
                out.time_base = output_info.time_base;
                const Status written = muxer->writePacket(out);
                if (!written.ok()) {
                    return Result<HwTranscodeReport>::fromStatus(written);
                }
                ++report.packets_written;
                report.bitstream_bytes += packet.size;
            }
        }
    }

    if (muxer == nullptr) {
        return Result<HwTranscodeReport>::fail(ErrorCode::Internal,
                                               "no frame was decoded; output was not created");
    }
    const Status finalized = muxer->finalize();
    if (!finalized.ok()) {
        return Result<HwTranscodeReport>::fromStatus(finalized);
    }

    RtxHwStats decoder_stats{};
    if (shim.decoderStats(decoder.get(), &decoder_stats) == RTX_OK) {
        report.decoder_device_to_device_bytes = decoder_stats.device_to_device_bytes;
        report.decoder_device_to_host_bytes = decoder_stats.device_to_host_pixel_bytes;
        report.decoder_host_pixel_copies = decoder_stats.host_pixel_copies;
    }
    if (encoder != nullptr) {
        RtxHwStats encoder_stats{};
        if (shim.encoderStats(encoder.get(), &encoder_stats) == RTX_OK) {
            report.encoder_host_to_device_bytes = encoder_stats.host_to_device_pixel_bytes;
            report.encoder_host_pixel_copies = encoder_stats.host_pixel_copies;
        }
    }
    // NVENC is destroyed (EncoderHandle) only after these counters were read;
    // the bridge handle declared before it outlives it, so the P010 buffer is
    // freed after NVENC unregistered it.
    report.zero_copy_main_loop =
        report.decoder_host_pixel_copies == 0 && report.encoder_host_pixel_copies == 0 &&
        report.decoder_device_to_host_bytes == 0 && report.encoder_host_to_device_bytes == 0;
    RtxNgxRemasterStats bridge_stats{};
    if (bridge_loader.getRemasterStats(bridge.get(), &bridge_stats) == RTX_OK &&
        bridge_stats.frames > 0) {
        const double frames = static_cast<double>(bridge_stats.frames);
        report.remaster_preprocess_ms = bridge_stats.preprocess_ms / frames;
        report.remaster_ngx_ms = bridge_stats.ngx_ms / frames;
        report.remaster_pack_kernel_ms = bridge_stats.pack_kernel_ms / frames;
        report.remaster_frame_ms = bridge_stats.frame_ms / frames;
        report.remaster_pack_kernel_max_ms = pack_max_ms;
        report.remaster_pack_launches = bridge_stats.pack_launches;
    }
    report.elapsed_ms =
        std::chrono::duration<double, std::milli>(Clock::now() - started).count();
    return Result<HwTranscodeReport>::ok(std::move(report));
}

namespace {

void nv12ToP010(const uint8_t* nv12, int width, int height, int nv12_pitch,
                std::vector<uint8_t>& p010) {    const size_t output_pitch = static_cast<size_t>(width) * 2;
    p010.resize(output_pitch * static_cast<size_t>(height) * 3 / 2);
    for (int y = 0; y < height; ++y) {
        const uint8_t* source = nv12 + static_cast<size_t>(y) * nv12_pitch;
        auto* destination = reinterpret_cast<uint16_t*>(p010.data() + output_pitch * y);
        for (int x = 0; x < width; ++x) {
            destination[x] = static_cast<uint16_t>(source[x]) << 8;
        }
    }
    const int chroma_height = (height + 1) / 2;
    for (int y = 0; y < chroma_height; ++y) {
        const uint8_t* source =
            nv12 + static_cast<size_t>(nv12_pitch) * height +
            static_cast<size_t>(y) * nv12_pitch;
        auto* destination = reinterpret_cast<uint16_t*>(
            p010.data() + output_pitch * static_cast<size_t>(height) +
            output_pitch * y);
        for (int x = 0; x < width; ++x) {
            destination[x] = static_cast<uint16_t>(source[x]) << 8;
        }
    }
}

Frame p010View(std::vector<uint8_t>& buffer, int width, int height) {
    Frame frame;
    frame.width = width;
    frame.height = height;
    frame.format = PixelFormat::P010;
    frame.bit_depth = 10;
    frame.chroma_width = width / 2;
    frame.chroma_height = (height + 1) / 2;
    frame.planes[0] = buffer.data();
    frame.planes[1] = buffer.data() + static_cast<size_t>(width) * 2 * height;
    frame.pitches[0] = width * 2;
    frame.pitches[1] = width * 2;
    frame.plane_sizes[0] = static_cast<size_t>(width) * 2 * height;
    frame.plane_sizes[1] = static_cast<size_t>(width) * 2 * ((height + 1) / 2);
    return frame;
}

}  // namespace

Result<HwTranscodeReport> runNvidiaRawTranscode(const HwTranscodeOptions& options) {
    if (options.input_path.empty() || options.output_path.empty()) {
        return Result<HwTranscodeReport>::fail(ErrorCode::InvalidArgument,
                                               "raw transcode requires input and output paths");
    }
    if (options.width <= 0 || options.height <= 0) {
        return Result<HwTranscodeReport>::fail(ErrorCode::InvalidArgument,
                                               "raw transcode requires --width and --height");
    }
    if (options.codec != VideoCodec::HEVC && options.codec != VideoCodec::H264) {
        return Result<HwTranscodeReport>::fail(
            ErrorCode::Unsupported, "stage-1 NVENC supports HEVC and H.264 output");
    }

    const Clock::time_point started = Clock::now();
    HwTranscodeReport report;
    report.path = "raw-artifact";
    report.input_container = "raw-nv12";
    report.output_container = "raw-p010";
    report.input_codec = "nv12";
    report.input_width = options.width;
    report.input_height = options.height;
    report.output_width = options.width;
    report.output_height = options.height;
    report.output_codec = options.codec == VideoCodec::HEVC ? "hevc" : "h264";

    NvidiaShim shim;
    std::string shim_error;
    if (!shim.open(&shim_error)) {
        return Result<HwTranscodeReport>::fail(ErrorCode::DllMissing,
                                               "cannot load the NVIDIA shim", shim_error);
    }
    RtxDiagnostics diagnostics{};
    if (shim.probe(&diagnostics) != RTX_OK || diagnostics.device.name[0] == '\0') {
        return Result<HwTranscodeReport>::fail(
            ErrorCode::DllMissing, "no NVIDIA CUDA device is available to rtx_shim",
            diagnostics.summary);
    }
    report.device = diagnostics.device.name;

    RawNv12Reader reader;
    const Status opened = reader.open(options.input_path, options.width, options.height);
    if (!opened.ok()) {
        return Result<HwTranscodeReport>::fromStatus(opened);
    }
    RawP010Writer writer;
    const Status output_opened = writer.open(options.output_path, options.width, options.height);
    if (!output_opened.ok()) {
        return Result<HwTranscodeReport>::fromStatus(output_opened);
    }

    const Rational frame_rate =
        options.fps.valid() && options.fps.num > 0 ? options.fps : Rational{30, 1};
    const bool prefer_ten_bit = options.bit_depth != 8 && options.codec == VideoCodec::HEVC;

    struct Attempt {
        VideoCodec codec;
        int bit_depth;
        int32_t format;
    };
    std::vector<Attempt> attempts;
    if (prefer_ten_bit) {
        attempts.push_back({VideoCodec::HEVC, 10, RTX_PIXEL_P010});
    }
    if (options.codec == VideoCodec::HEVC) {
        attempts.push_back({VideoCodec::HEVC, 8, RTX_PIXEL_NV12});
    }
    attempts.push_back({VideoCodec::H264, 8, RTX_PIXEL_NV12});
    if (!options.allow_encoder_fallback) {
        attempts.resize(1);
    }

    EncoderHandle encoder(nullptr, EncoderDeleter{&shim});
    DecoderHandle decoder(nullptr, DecoderDeleter{&shim});
    Attempt chosen = attempts.front();
    RtxResult last_create = RTX_ERR_UNSUPPORTED;
    std::string last_detail;
    for (const Attempt& attempt : attempts) {
        RtxEncoderConfig config{};
        config.codec = encoderCodec(attempt.codec);
        config.width = options.width;
        config.height = options.height;
        config.fps_num = static_cast<int32_t>(frame_rate.num);
        config.fps_den = static_cast<int32_t>(frame_rate.den);
        config.bitrate_kbps = options.bitrate_kbps;
        config.preset = std::clamp(options.preset, 1, 7);
        config.tier = 0;
        config.input_format = attempt.format;
        config.gop_length =
            static_cast<int32_t>(std::max<int64_t>(1, frame_rate.den * 2));
        config.rate_control = std::clamp(options.rate_control, 0, 2);
        config.qp = options.qp;

        RtxEncoder* raw = nullptr;
        const RtxResult created = shim.encoderCreate(&config, &raw);
        if (created != RTX_OK || raw == nullptr) {
            last_create = created;
            last_detail = shim.encoderLastError(nullptr);
            continue;
        }

        // The artefact path verifies the bitstream by decoding it again, so the
        // NVDEC engine must accept the same codec.  Maxwell GM204 has no HEVC
        // decoder, for example: fall back to H.264 instead of failing late.
        RtxDecoderConfig decoder_config{};
        decoder_config.codec = shimCodec(attempt.codec);
        decoder_config.width = options.width;
        decoder_config.height = options.height;
        decoder_config.bit_depth = attempt.bit_depth;
        decoder_config.output_format = attempt.format;
        decoder_config.clock_rate = 90000;
        decoder_config.max_surfaces = 32;
        decoder_config.pool_slots = 8;

        RtxDecoder* raw_decoder = nullptr;
        const RtxResult decoder_created = shim.decoderCreate(&decoder_config, &raw_decoder);
        if (decoder_created != RTX_OK || raw_decoder == nullptr) {
            shim.encoderDestroy(raw);
            last_create = decoder_created;
            last_detail = shim.decoderLastError(nullptr);
            continue;
        }
        encoder.reset(raw);
        decoder.reset(raw_decoder);
        chosen = attempt;
        last_create = RTX_OK;
        break;
    }
    if (encoder == nullptr || decoder == nullptr) {
        return Result<HwTranscodeReport>::fail(
            mapShimResult(last_create),
            describe("no usable NVDEC + NVENC pair for the raw artefact path", last_create,
                     last_detail.c_str()));
    }
    report.output_codec = chosen.codec == VideoCodec::HEVC ? "hevc" : "h264";
    report.output_bit_depth = chosen.bit_depth;
    if (chosen.codec != options.codec) {
        std::fprintf(stderr,
                     "warning: %s is unavailable on this GPU; the raw artefact path "
                     "fell back to %s\n",
                     options.codec == VideoCodec::HEVC ? "HEVC" : "H.264",
                     report.output_codec.c_str());
    }

    std::vector<uint8_t> p010_encode;
    std::vector<uint8_t> download;
    std::vector<uint8_t> p010_decode;
    int64_t frame_index = 0;

    auto drainDecoded = [&](bool end_of_stream) -> Status {
        for (;;) {
            RtxFrameDesc frame{};
            const RtxResult received = shim.decoderReceive(decoder.get(), &frame);
            if (received == RTX_ERR_NOT_FOUND) {
                return Status::success();
            }
            if (received == RTX_ERR_IO && end_of_stream) {
                return Status::success();
            }
            if (received != RTX_OK) {
                return Status::fail(mapShimResult(received),
                                    describe("rtx_shim_decoder_receive", received,
                                             shim.decoderLastError(decoder.get())));
            }
            const size_t sample_bytes = frame.format == RTX_PIXEL_P010 ? 2u : 1u;
            const size_t pitch = static_cast<size_t>(frame.width) * sample_bytes;
            download.resize(pitch * static_cast<size_t>(frame.height) * 3 / 2);
            const RtxResult downloaded =
                shim.decoderDownload(decoder.get(), &frame, download.data(), pitch);
            if (downloaded != RTX_OK) {
                (void)shim.decoderReleaseFrame(decoder.get(), frame.slot_index);
                return Status::fail(mapShimResult(downloaded),
                                    describe("rtx_shim_decoder_download", downloaded, nullptr));
            }
            if (frame.format == RTX_PIXEL_P010) {
                Frame view = p010View(download, frame.width, frame.height);
                const Status written = writer.write(view);
                if (!written.ok()) {
                    (void)shim.decoderReleaseFrame(decoder.get(), frame.slot_index);
                    return written;
                }
            } else {
                nv12ToP010(download.data(), frame.width, frame.height,
                           static_cast<int>(pitch), p010_decode);
                Frame view = p010View(p010_decode, frame.width, frame.height);
                const Status written = writer.write(view);
                if (!written.ok()) {
                    (void)shim.decoderReleaseFrame(decoder.get(), frame.slot_index);
                    return written;
                }
            }
            (void)shim.decoderReleaseFrame(decoder.get(), frame.slot_index);
            ++report.frames_decoded;
        }
    };

    auto submitEncoded = [&](const RtxPacketDesc& packet) -> Status {
        if (packet.size == 0) {
            return Status::success();
        }
        const RtxResult submitted =
            shim.decoderSubmit(decoder.get(), packet.data, packet.size, packet.pts, packet.dts,
                               packet.keyframe);
        if (submitted != RTX_OK) {
            return Status::fail(mapShimResult(submitted),
                                describe("cuvidParseVideoData", submitted,
                                         shim.decoderLastError(decoder.get())));
        }
        report.bitstream_bytes += packet.size;
        return drainDecoded(false);
    };

    while (options.frame_limit <= 0 || frame_index < options.frame_limit) {
        Result<Frame> frame = reader.read(frame_index, Rational{frame_rate.den, frame_rate.num});
        if (!frame.ok()) {
            if (frame.code() == ErrorCode::NotFound) {
                break;
            }
            return Result<HwTranscodeReport>::fromStatus(frame.status());
        }
        Frame& source = frame.value();
        const uint8_t* pixels = source.planes[0];
        uint32_t pitch = static_cast<uint32_t>(source.pitches[0]);
        int32_t format = RTX_PIXEL_NV12;
        if (chosen.format == RTX_PIXEL_P010) {
            nv12ToP010(source.planes[0], options.width, options.height, source.pitches[0],
                       p010_encode);
            pixels = p010_encode.data();
            pitch = static_cast<uint32_t>(options.width) * 2;
            format = RTX_PIXEL_P010;
        }

        RtxPacketDesc packet{};
        const RtxResult encoded =
            shim.encoderEncodeHost(encoder.get(), pixels, pitch, format, frame_index, &packet);
        if (encoded != RTX_OK) {
            return Result<HwTranscodeReport>::fail(
                mapShimResult(encoded),
                describe("NvEncEncodePicture(host)", encoded,
                         shim.encoderLastError(encoder.get())));
        }
        ++report.frames_encoded;
        const Status submitted = submitEncoded(packet);
        if (!submitted.ok()) {
            return Result<HwTranscodeReport>::fromStatus(submitted);
        }
        ++frame_index;
    }

    for (int attempt = 0; attempt < 8; ++attempt) {
        RtxPacketDesc packet{};
        const RtxResult flushed = shim.encoderFlush(encoder.get(), &packet);
        if (flushed == RTX_ERR_NOT_FOUND) {
            break;
        }
        if (flushed != RTX_OK) {
            return Result<HwTranscodeReport>::fail(
                mapShimResult(flushed),
                describe("NvEncEncodePicture(EOS)", flushed,
                         shim.encoderLastError(encoder.get())));
        }
        const Status submitted = submitEncoded(packet);
        if (!submitted.ok()) {
            return Result<HwTranscodeReport>::fromStatus(submitted);
        }
    }

    const RtxResult flushed_decoder = shim.decoderFlush(decoder.get());
    if (flushed_decoder != RTX_OK) {
        return Result<HwTranscodeReport>::fail(
            mapShimResult(flushed_decoder),
            describe("cuvidParseVideoData(EOS)", flushed_decoder,
                     shim.decoderLastError(decoder.get())));
    }
    const Status drained = drainDecoded(true);
    if (!drained.ok()) {
        return Result<HwTranscodeReport>::fromStatus(drained);
    }
    const Status finalized = writer.finalize();
    if (!finalized.ok()) {
        return Result<HwTranscodeReport>::fromStatus(finalized);
    }
    if (report.frames_decoded == 0) {
        return Result<HwTranscodeReport>::fail(ErrorCode::Internal,
                                               "NVDEC produced no frames from the NVENC bitstream");
    }

    RtxHwStats decoder_stats{};
    if (shim.decoderStats(decoder.get(), &decoder_stats) == RTX_OK) {
        report.decoder_device_to_device_bytes = decoder_stats.device_to_device_bytes;
        report.decoder_device_to_host_bytes = decoder_stats.device_to_host_pixel_bytes;
        report.decoder_host_pixel_copies = decoder_stats.host_pixel_copies;
    }
    RtxHwStats encoder_stats{};
    if (shim.encoderStats(encoder.get(), &encoder_stats) == RTX_OK) {
        report.encoder_host_to_device_bytes = encoder_stats.host_to_device_pixel_bytes;
        report.encoder_host_pixel_copies = encoder_stats.host_pixel_copies;
    }
    report.zero_copy_main_loop = false;
    report.elapsed_ms =
        std::chrono::duration<double, std::milli>(Clock::now() - started).count();
    return Result<HwTranscodeReport>::ok(std::move(report));
}

}  // namespace rtx
