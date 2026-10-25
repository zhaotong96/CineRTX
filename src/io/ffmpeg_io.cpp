// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
//
// Real libavformat/libavcodec demuxer and muxer.  The translation unit is
// always compiled; when the build cannot find FFmpeg development files the
// implementation falls back to a structured diagnostic instead of an
// exception.  Set -DRTX_FFMPEG_ROOT=<prefix> or the RTX_FFMPEG_ROOT
// environment variable to enable the real path.

#include "rtx/io.h"

#include "bitstream.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstring>
#include <deque>
#include <sstream>
#include <utility>

#ifdef RTX_HAVE_FFMPEG
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavcodec/bsf.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/error.h>
#include <libavutil/mastering_display_metadata.h>
#include <libavutil/pixdesc.h>
}
#endif

namespace rtx {
namespace {

#ifdef RTX_HAVE_FFMPEG

std::string avErrorString(int error) {
    char buffer[AV_ERROR_MAX_STRING_SIZE] = {};
    if (av_strerror(error, buffer, sizeof(buffer)) < 0) {
        return "FFmpeg error " + std::to_string(error);
    }
    return buffer;
}

std::string pathExtension(const std::string& path) {
    const size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) {
        return {};
    }
    std::string extension = path.substr(dot);
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(c | 0x20); });
    return extension;
}

VideoCodec codecFromAv(AVCodecID id) {
    switch (id) {
        case AV_CODEC_ID_H264:
            return VideoCodec::H264;
        case AV_CODEC_ID_HEVC:
            return VideoCodec::HEVC;
        case AV_CODEC_ID_AV1:
            return VideoCodec::AV1;
        default:
            return VideoCodec::Unknown;
    }
}

AVCodecID codecToAv(VideoCodec codec) {
    switch (codec) {
        case VideoCodec::H264:
            return AV_CODEC_ID_H264;
        case VideoCodec::HEVC:
            return AV_CODEC_ID_HEVC;
        case VideoCodec::AV1:
            return AV_CODEC_ID_AV1;
        default:
            return AV_CODEC_ID_NONE;
    }
}

ColorDescription colorFromAv(const AVCodecParameters& parameters) {
    ColorDescription color;
    switch (parameters.color_primaries) {
        case AVCOL_PRI_BT2020:
            color.primaries = ColorPrimaries::BT2020;
            break;
        default:
            color.primaries = ColorPrimaries::BT709;
            break;
    }
    switch (parameters.color_trc) {
        case AVCOL_TRC_SMPTE2084:
            color.transfer = TransferFunction::SMPTE2084;
            break;
        case AVCOL_TRC_ARIB_STD_B67:
            color.transfer = TransferFunction::HLG;
            break;
        default:
            color.transfer = TransferFunction::BT709;
            break;
    }
    switch (parameters.color_space) {
        case AVCOL_SPC_BT2020_NCL:
            color.matrix = MatrixCoefficients::BT2020NC;
            break;
        default:
            color.matrix = MatrixCoefficients::BT709;
            break;
    }
    color.range = parameters.color_range == AVCOL_RANGE_JPEG ? ColorRange::Full
                                                            : ColorRange::Limited;
    return color;
}

void colorToAv(const ColorDescription& color, AVCodecParameters& parameters) {
    parameters.color_primaries = color.primaries == ColorPrimaries::BT2020 ? AVCOL_PRI_BT2020
                                                                           : AVCOL_PRI_BT709;
    parameters.color_trc = color.transfer == TransferFunction::SMPTE2084
                               ? AVCOL_TRC_SMPTE2084
                               : (color.transfer == TransferFunction::HLG
                                      ? AVCOL_TRC_ARIB_STD_B67
                                      : AVCOL_TRC_BT709);
    parameters.color_space = color.matrix == MatrixCoefficients::BT2020NC
                                 ? AVCOL_SPC_BT2020_NCL
                                 : AVCOL_SPC_BT709;
    parameters.color_range = color.range == ColorRange::Full ? AVCOL_RANGE_JPEG
                                                             : AVCOL_RANGE_MPEG;
}

Hdr10Metadata hdrFromSideData(const AVCodecParameters& parameters) {
    Hdr10Metadata hdr;
    for (int i = 0; i < parameters.nb_coded_side_data; ++i) {
        const AVPacketSideData& side = parameters.coded_side_data[i];
        if (side.type == AV_PKT_DATA_MASTERING_DISPLAY_METADATA &&
            side.size >= sizeof(AVMasteringDisplayMetadata)) {
            AVMasteringDisplayMetadata mastering{};
            std::memcpy(&mastering, side.data, sizeof(mastering));
            if (mastering.has_primaries) {
                auto scale = [](AVRational value) {
                    return static_cast<uint16_t>(
                        std::min<int64_t>(65535, std::llround(value.num * 50000.0 / value.den)));
                };
                hdr.mastering.red_x = scale(mastering.display_primaries[0][0]);
                hdr.mastering.red_y = scale(mastering.display_primaries[0][1]);
                hdr.mastering.green_x = scale(mastering.display_primaries[1][0]);
                hdr.mastering.green_y = scale(mastering.display_primaries[1][1]);
                hdr.mastering.blue_x = scale(mastering.display_primaries[2][0]);
                hdr.mastering.blue_y = scale(mastering.display_primaries[2][1]);
                hdr.mastering.white_x = scale(mastering.white_point[0]);
                hdr.mastering.white_y = scale(mastering.white_point[1]);
                hdr.enabled = true;
            }
            if (mastering.has_luminance) {
                hdr.mastering.max_luminance = static_cast<uint32_t>(
                    std::llround(mastering.max_luminance.num * 10000.0 /
                                 mastering.max_luminance.den));
                hdr.mastering.min_luminance = static_cast<uint32_t>(
                    std::llround(mastering.min_luminance.num * 10000.0 /
                                 mastering.min_luminance.den));
                hdr.enabled = true;
            }
        } else if (side.type == AV_PKT_DATA_CONTENT_LIGHT_LEVEL &&
                   side.size >= sizeof(AVContentLightMetadata)) {
            AVContentLightMetadata light{};
            std::memcpy(&light, side.data, sizeof(light));
            hdr.cll.max_cll = light.MaxCLL;
            hdr.cll.max_fall = light.MaxFALL;
            hdr.enabled = true;
        }
    }
    if (!hdr.enabled && parameters.color_trc == AVCOL_TRC_SMPTE2084) {
        hdr.enabled = true;
    }
    return hdr;
}

// The minimal FFmpeg build has no HEVC decoder, so
// avformat_find_stream_info() cannot lift the SPS VUI or prefix-SEI HDR10
// metadata into AVCodecParameters.  These helpers recover them from the coded
// bitstream; container-level values always take precedence.
void mergeHevcBitstreamMetadata(const AVCodecParameters& parameters,
                                const HevcVuiColor& vui, const HevcSeiHdr10& sei,
                                MediaInfo& info) {
    const bool container_primaries = parameters.color_primaries != AVCOL_PRI_UNSPECIFIED;
    const bool container_transfer = parameters.color_trc != AVCOL_TRC_UNSPECIFIED;
    const bool container_matrix = parameters.color_space != AVCOL_SPC_UNSPECIFIED;
    const bool vui_primaries = vui.has_colour_description && vui.primaries != 2;
    const bool vui_transfer = vui.has_colour_description && vui.transfer != 2;
    const bool vui_matrix = vui.has_colour_description && vui.matrix != 2;

    if (!container_primaries && vui_primaries) {
        if (vui.primaries == 9) {
            info.color.primaries = ColorPrimaries::BT2020;
        } else if (vui.primaries == 1) {
            info.color.primaries = ColorPrimaries::BT709;
        }
    }
    if (!container_transfer && vui_transfer) {
        if (vui.transfer == 16) {
            info.color.transfer = TransferFunction::SMPTE2084;
        } else if (vui.transfer == 18) {
            info.color.transfer = TransferFunction::HLG;
        } else if (vui.transfer == 1) {
            info.color.transfer = TransferFunction::BT709;
        }
    }
    if (!container_matrix && vui_matrix) {
        if (vui.matrix == 9) {
            info.color.matrix = MatrixCoefficients::BT2020NC;
        } else if (vui.matrix == 1) {
            info.color.matrix = MatrixCoefficients::BT709;
        }
    }
    if (parameters.color_range == AVCOL_RANGE_UNSPECIFIED && vui.has_video_signal_type) {
        info.color.range = vui.full_range ? ColorRange::Full : ColorRange::Limited;
    }

    const bool has_primaries = container_primaries || vui_primaries;
    const bool has_transfer = container_transfer || vui_transfer;
    const bool has_matrix = container_matrix || vui_matrix;

    if (sei.has_mastering_display || sei.has_content_light) {
        if (!info.hdr.enabled) {
            info.hdr.enabled = true;
        }
        if (sei.has_mastering_display &&
            (info.hdr.mastering.red_x == 0 || info.hdr.mastering.max_luminance == 0)) {
            info.hdr.mastering.red_x = sei.red_x;
            info.hdr.mastering.red_y = sei.red_y;
            info.hdr.mastering.green_x = sei.green_x;
            info.hdr.mastering.green_y = sei.green_y;
            info.hdr.mastering.blue_x = sei.blue_x;
            info.hdr.mastering.blue_y = sei.blue_y;
            info.hdr.mastering.white_x = sei.white_x;
            info.hdr.mastering.white_y = sei.white_y;
            info.hdr.mastering.max_luminance = sei.max_luminance;
            info.hdr.mastering.min_luminance = sei.min_luminance;
        }
        if (sei.has_content_light && info.hdr.cll.max_cll == 0 &&
            info.hdr.cll.max_fall == 0) {
            info.hdr.cll.max_cll = sei.max_cll;
            info.hdr.cll.max_fall = sei.max_fall;
        }
    }

    if (info.hdr.enabled) {
        // HDR10 signals that omit colour metadata still require the PQ
        // container description; fill only the gaps so an explicit HLG or
        // BT.2020 declaration survives untouched.
        if (!has_primaries) {
            info.color.primaries = ColorPrimaries::BT2020;
        }
        if (!has_transfer) {
            info.color.transfer = TransferFunction::SMPTE2084;
        }
        if (!has_matrix) {
            info.color.matrix = MatrixCoefficients::BT2020NC;
        }
        info.hdr.color = info.color;
    }
    info.hdr_source = info.hdr.enabled;
}

int detectBitDepth(const AVCodecParameters& parameters, VideoCodec codec) {
    int bits = parameters.bits_per_raw_sample;
    if (bits <= 0 && parameters.format >= 0) {
        const AVPixFmtDescriptor* descriptor =
            av_pix_fmt_desc_get(static_cast<AVPixelFormat>(parameters.format));
        if (descriptor != nullptr && descriptor->comp[0].depth > 0) {
            bits = descriptor->comp[0].depth;
        }
    }
    if (bits <= 0 && parameters.extradata != nullptr && parameters.extradata_size > 0) {
        if (codec == VideoCodec::HEVC) {
            bits = hevcConfigBitDepth(parameters.extradata,
                                      static_cast<size_t>(parameters.extradata_size));
        } else if (codec == VideoCodec::H264) {
            bits = avcConfigBitDepth(parameters.extradata,
                                     static_cast<size_t>(parameters.extradata_size));
        }
    }
    return bits > 0 ? bits : 8;
}

class FfmpegDemuxer final : public IDemuxer {
public:
    ~FfmpegDemuxer() override { close(); }

    Result<MediaInfo> open(const std::string& path) override {
        close();
        int status = avformat_open_input(&format_, path.c_str(), nullptr, nullptr);
        if (status < 0) {
            return Result<MediaInfo>::fail(ErrorCode::Io,
                                           "cannot open media container: " + path,
                                           avErrorString(status));
        }
        status = avformat_find_stream_info(format_, nullptr);
        if (status < 0) {
            close();
            return Result<MediaInfo>::fail(ErrorCode::Io, "cannot read stream information",
                                           avErrorString(status));
        }

        const int stream_index = av_find_best_stream(format_, AVMEDIA_TYPE_VIDEO, -1, -1,
                                                     nullptr, 0);
        if (stream_index < 0) {
            close();
            return Result<MediaInfo>::fail(ErrorCode::InvalidArgument,
                                           "container has no decodable video stream");
        }
        video_stream_ = stream_index;
        AVStream* stream = format_->streams[stream_index];

        MediaInfo info;
        info.container = pathExtension(path);
        if (!info.container.empty() && info.container.front() == '.') {
            info.container.erase(info.container.begin());
        }
        info.format_name = format_->iformat != nullptr && format_->iformat->name != nullptr
                               ? format_->iformat->name
                               : "unknown";
        info.codec = codecFromAv(stream->codecpar->codec_id);
        info.video_codec = avcodec_get_name(stream->codecpar->codec_id);
        info.width = stream->codecpar->width;
        info.height = stream->codecpar->height;
        info.bit_depth = detectBitDepth(*stream->codecpar, info.codec);
        info.bitrate = static_cast<int>(std::max<int64_t>(0, stream->codecpar->bit_rate));
        info.time_base = Rational{stream->time_base.num, stream->time_base.den};
        const AVRational guessed = av_guess_frame_rate(format_, stream, nullptr);
        info.frame_rate = Rational{guessed.num > 0 ? guessed.num : 0,
                                   guessed.den > 0 ? guessed.den : 1};
        if (format_->duration > 0) {
            info.duration_ms = format_->duration * 1000 / AV_TIME_BASE;
        }
        for (unsigned int i = 0; i < format_->nb_streams; ++i) {
            if (format_->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
                ++info.audio_streams;
            } else if (format_->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE) {
                ++info.subtitle_streams;
            }
        }
        info.color = colorFromAv(*stream->codecpar);
        info.hdr = hdrFromSideData(*stream->codecpar);
        info.hdr_source = info.hdr.enabled;
        if (stream->codecpar->extradata != nullptr && stream->codecpar->extradata_size > 0) {
            info.extradata.assign(stream->codecpar->extradata,
                                  stream->codecpar->extradata + stream->codecpar->extradata_size);
        }
        HevcVuiColor vui;
        if (info.codec == VideoCodec::HEVC && !info.extradata.empty()) {
            (void)parseHevcVuiColor(info.extradata.data(), info.extradata.size(), vui);
        }

        // MP4/MOV store length-prefixed NAL units; MKV/TS can already deliver
        // start codes.  Detect the actual packet syntax instead of guessing
        // from the container name: MP4 always needs h264_mp4toannexb /
        // hevc_mp4toannexb, while Matroska is demuxed to Annex-B by FFmpeg.
        info_ = info;
        packet_ = av_packet_alloc();
        if (packet_ == nullptr) {
            close();
            return Result<MediaInfo>::fail(ErrorCode::Io, "out of memory for AVPacket");
        }

        AVPacket* probe = av_packet_alloc();
        if (probe == nullptr) {
            close();
            return Result<MediaInfo>::fail(ErrorCode::Io, "out of memory for AVPacket");
        }
        bool packets_are_annex_b = false;
        bool probe_valid = false;
        HevcSeiHdr10 sei;
        while (av_read_frame(format_, probe) >= 0) {
            if (probe->stream_index == video_stream_) {
                packets_are_annex_b = looksLikeAnnexB(probe->data,
                                                      static_cast<size_t>(probe->size));
                if (info.codec == VideoCodec::HEVC) {
                    (void)parseHevcSeiHdr10(probe->data, static_cast<size_t>(probe->size),
                                            sei);
                }
                probe_valid = true;
                break;
            }
            av_packet_unref(probe);
        }
        if (info.codec == VideoCodec::HEVC) {
            mergeHevcBitstreamMetadata(*stream->codecpar, vui, sei, info);
            info_ = info;
        }
        if (probe_valid && !packets_are_annex_b && info.codec == VideoCodec::H264) {
            configureBitstreamFilter("h264_mp4toannexb");
        } else if (probe_valid && !packets_are_annex_b && info.codec == VideoCodec::HEVC) {
            configureBitstreamFilter("hevc_mp4toannexb");
        }
        info_.annex_b = packets_are_annex_b || bitstream_filter_ != nullptr;
        if (probe_valid) {
            if (bitstream_filter_ != nullptr) {
                const int status = av_bsf_send_packet(bitstream_filter_, probe);
                av_packet_unref(probe);
                if (status < 0) {
                    warnings_.push_back("bitstream filter rejected the first packet");
                } else {
                    drainBitstreamFilter();
                }
            } else {
                pending_.push_back(convertPacket(*probe));
                av_packet_unref(probe);
            }
        }
        av_packet_free(&probe);
        return Result<MediaInfo>::ok(info_);
    }

    Result<Packet> readPacket() override {
        if (format_ == nullptr || video_stream_ < 0) {
            return Result<Packet>::fail(ErrorCode::State, "demuxer is not open");
        }
        for (;;) {
            if (!pending_.empty()) {
                Packet packet = std::move(pending_.front());
                pending_.pop_front();
                return Result<Packet>::ok(std::move(packet));
            }
            if (drained_) {
                return Result<Packet>::fail(ErrorCode::NotFound, "end of stream");
            }
            int status = av_read_frame(format_, packet_);
            if (status == AVERROR_EOF) {
                drained_ = true;
                if (bitstream_filter_ != nullptr && !bitstream_filter_flushed_) {
                    bitstream_filter_flushed_ = true;
                    (void)av_bsf_send_packet(bitstream_filter_, nullptr);
                    drainBitstreamFilter();
                    continue;
                }
                return Result<Packet>::fail(ErrorCode::NotFound, "end of stream");
            }
            if (status < 0) {
                return Result<Packet>::fail(ErrorCode::Io, "cannot read packet",
                                            avErrorString(status));
            }
            if (packet_->stream_index != video_stream_) {
                av_packet_unref(packet_);
                continue;
            }
            if (bitstream_filter_ != nullptr) {
                status = av_bsf_send_packet(bitstream_filter_, packet_);
                av_packet_unref(packet_);
                if (status < 0) {
                    return Result<Packet>::fail(ErrorCode::Io, "bitstream filter rejected packet",
                                                avErrorString(status));
                }
                drainBitstreamFilter();
                continue;
            }
            pending_.push_back(convertPacket(*packet_));
            av_packet_unref(packet_);
        }
    }

    std::vector<std::string> warnings() const override { return warnings_; }

private:
    void configureBitstreamFilter(const char* name) {
        const AVBitStreamFilter* filter = av_bsf_get_by_name(name);
        if (filter == nullptr) {
            warnings_.push_back(std::string("bitstream filter unavailable: ") + name);
            return;
        }
        if (av_bsf_alloc(filter, &bitstream_filter_) < 0) {
            bitstream_filter_ = nullptr;
            warnings_.push_back(std::string("cannot allocate bitstream filter: ") + name);
            return;
        }
        AVCodecParameters* parameters = format_->streams[video_stream_]->codecpar;
        if (avcodec_parameters_copy(bitstream_filter_->par_in, parameters) < 0 ||
            av_bsf_init(bitstream_filter_) < 0) {
            av_bsf_free(&bitstream_filter_);
            bitstream_filter_ = nullptr;
            warnings_.push_back(std::string("cannot initialise bitstream filter: ") + name);
        }
    }

    void drainBitstreamFilter() {
        for (;;) {
            int status = av_bsf_receive_packet(bitstream_filter_, packet_);
            if (status == AVERROR(EAGAIN) || status == AVERROR_EOF) {
                return;
            }
            if (status < 0) {
                warnings_.push_back("bitstream filter error: " + avErrorString(status));
                return;
            }
            pending_.push_back(convertPacket(*packet_));
            av_packet_unref(packet_);
        }
    }

    Packet convertPacket(const AVPacket& source) const {
        Packet packet;
        packet.data.assign(source.data, source.data + source.size);
        packet.pts = source.pts;
        packet.dts = source.dts;
        packet.duration = source.duration;
        packet.keyframe = (source.flags & AV_PKT_FLAG_KEY) != 0;
        packet.stream_index = source.stream_index;
        packet.time_base = info_.time_base;
        return packet;
    }

    void close() {
        pending_.clear();
        if (bitstream_filter_ != nullptr) {
            av_bsf_free(&bitstream_filter_);
            bitstream_filter_ = nullptr;
        }
        if (packet_ != nullptr) {
            av_packet_free(&packet_);
            packet_ = nullptr;
        }
        if (format_ != nullptr) {
            avformat_close_input(&format_);
            format_ = nullptr;
        }
        drained_ = false;
        bitstream_filter_flushed_ = false;
        video_stream_ = -1;
    }

    AVFormatContext* format_ = nullptr;
    AVBSFContext* bitstream_filter_ = nullptr;
    AVPacket* packet_ = nullptr;
    std::deque<Packet> pending_;
    std::vector<std::string> warnings_;
    MediaInfo info_;
    int video_stream_ = -1;
    bool drained_ = false;
    bool bitstream_filter_flushed_ = false;
};

class FfmpegMuxer final : public IMuxer {
public:
    ~FfmpegMuxer() override { (void)finalize(); }

    Status open(const std::string& path, const MediaInfo& info,
                const Hdr10Metadata& hdr) override {
        if (format_ != nullptr) {
            return Status::fail(ErrorCode::State, "muxer is already open", path);
        }
        const AVCodecID codec = codecToAv(info.codec);
        if (codec == AV_CODEC_ID_NONE) {
            return Status::fail(ErrorCode::InvalidArgument,
                                "unsupported output video codec", path);
        }
        int status = avformat_alloc_output_context2(&format_, nullptr, nullptr, path.c_str());
        if (format_ == nullptr) {
            return Status::fail(ErrorCode::Io, "cannot create output context",
                                avErrorString(status));
        }
        stream_ = avformat_new_stream(format_, nullptr);
        if (stream_ == nullptr) {
            close();
            return Status::fail(ErrorCode::Io, "cannot create output stream");
        }
        AVCodecParameters* parameters = stream_->codecpar;
        parameters->codec_type = AVMEDIA_TYPE_VIDEO;
        parameters->codec_id = codec;
        parameters->width = info.width;
        parameters->height = info.height;
        parameters->format = info.bit_depth > 8 ? AV_PIX_FMT_YUV420P10LE : AV_PIX_FMT_YUV420P;
        parameters->bit_rate = info.bitrate;
        colorToAv(info.color, *parameters);
        if (info.extradata.empty()) {
            close();
            return Status::fail(ErrorCode::InvalidArgument,
                                "muxer requires a codec configuration record", path);
        }
        parameters->extradata = static_cast<uint8_t*>(
            av_mallocz(info.extradata.size() + AV_INPUT_BUFFER_PADDING_SIZE));
        if (parameters->extradata == nullptr) {
            close();
            return Status::fail(ErrorCode::OutOfMemory, "cannot allocate codec extradata");
        }
        std::memcpy(parameters->extradata, info.extradata.data(), info.extradata.size());
        parameters->extradata_size = static_cast<int>(info.extradata.size());

        const AVRational frame_rate{info.frame_rate.num > 0
                                        ? static_cast<int>(info.frame_rate.num)
                                        : 25,
                                    info.frame_rate.den > 0
                                        ? static_cast<int>(info.frame_rate.den)
                                        : 1};
        stream_->avg_frame_rate = frame_rate;
        stream_->time_base =
            AVRational{info.time_base.num > 0 ? static_cast<int>(info.time_base.num) : 1,
                       info.time_base.den > 0 ? static_cast<int>(info.time_base.den) : 90000};

        // HDR10 static metadata must reach the container, not only the HEVC
        // bitstream: the mp4 muxer derives the mdcv/clli boxes from the coded
        // side data of the output stream.  Adding it here (before
        // avformat_write_header) is the only point where movenc will emit it.
        if (hdr.enabled) {
            const Status side = applyHdrSideData(hdr);
            if (!side.ok()) {
                close();
                return side;
            }
        }

        status = avio_open(&format_->pb, path.c_str(), AVIO_FLAG_WRITE);
        if (status < 0) {
            close();
            return Status::fail(ErrorCode::Io, "cannot open output file",
                                path + ": " + avErrorString(status));
        }
        status = avformat_write_header(format_, nullptr);
        if (status < 0) {
            close();
            return Status::fail(ErrorCode::Io, "cannot write container header",
                                avErrorString(status));
        }
        packet_ = av_packet_alloc();
        if (packet_ == nullptr) {
            close();
            return Status::fail(ErrorCode::OutOfMemory, "cannot allocate output packet");
        }
        annex_b_ = info.annex_b;
        container_ = info.format_name.empty() ? pathExtension(path) : info.format_name;
        hdr_ = hdr;
        opened_ = true;
        return Status::success();
    }

    Status writePacket(const Packet& packet) override {
        if (!opened_ || format_ == nullptr || stream_ == nullptr) {
            return Status::fail(ErrorCode::State, "muxer is not open");
        }
        const bool native_annexb = container_.find("mpegts") != std::string::npos ||
                                   container_.find("m2ts") != std::string::npos ||
                                   container_.find("h264") != std::string::npos ||
                                   container_.find("hevc") != std::string::npos;
        std::vector<uint8_t> converted;
        const uint8_t* data = packet.data.data();
        size_t size = packet.data.size();
        if (annex_b_ && !native_annexb) {
            converted = annexBToLengthPrefixed(data, size);
            data = converted.data();
            size = converted.size();
        } else if (!annex_b_ && native_annexb) {
            converted = lengthPrefixedToAnnexB(data, size);
            data = converted.data();
            size = converted.size();
        }
        if (size > static_cast<size_t>(INT_MAX)) {
            return Status::fail(ErrorCode::InvalidArgument, "packet is too large");
        }
        av_packet_unref(packet_);
        int status = av_new_packet(packet_, static_cast<int>(size));
        if (status < 0) {
            return Status::fail(ErrorCode::OutOfMemory, "cannot allocate output packet");
        }
        std::memcpy(packet_->data, data, size);
        packet_->stream_index = stream_->index;
        const AVRational source_base{
            packet.time_base.num > 0 ? static_cast<int>(packet.time_base.num) : 1,
            packet.time_base.den > 0 ? static_cast<int>(packet.time_base.den) : 90000};
        packet_->pts = av_rescale_q(packet.pts, source_base, stream_->time_base);
        packet_->dts = av_rescale_q(packet.dts, source_base, stream_->time_base);
        packet_->duration = av_rescale_q(packet.duration, source_base, stream_->time_base);
        packet_->flags = packet.keyframe ? AV_PKT_FLAG_KEY : 0;
        status = av_interleaved_write_frame(format_, packet_);
        av_packet_unref(packet_);
        if (status < 0) {
            return Status::fail(ErrorCode::Io, "cannot write video packet",
                                avErrorString(status));
        }
        return Status::success();
    }

    Status finalize() override {
        Status result = Status::success();
        if (format_ != nullptr && opened_) {
            const int status = av_write_trailer(format_);
            if (status < 0) {
                result = Status::fail(ErrorCode::Io, "cannot finalise container",
                                      avErrorString(status));
            }
        }
        close();
        return result;
    }

private:
    // Serialises the canonical HDR10 metadata into the two side-data structs
    // movenc turns into the mdcv and clli boxes.  Chromaticity is stored in
    // 0.00002 units and luminance in 0.0001 cd/m^2, so 50000 / 10000 are the
    // exact rational denominators.
    //
    // Failure is fatal for an HDR10 output: shipping a PQ bitstream whose
    // container has no mdcv/clli would produce a file that most players refuse
    // to badge as HDR10, which is exactly the failure this engine must prevent.
    Status applyHdrSideData(const Hdr10Metadata& hdr) {
        AVCodecParameters* parameters = stream_->codecpar;
        size_t size = 0;
        AVMasteringDisplayMetadata* mastering =
            av_mastering_display_metadata_alloc_size(&size);
        if (mastering == nullptr) {
            return Status::fail(ErrorCode::OutOfMemory,
                                "cannot allocate mastering-display side data");
        }
        const MasteringDisplay& m = hdr.mastering;
        mastering->display_primaries[0][0] = av_make_q(m.red_x, 50000);
        mastering->display_primaries[0][1] = av_make_q(m.red_y, 50000);
        mastering->display_primaries[1][0] = av_make_q(m.green_x, 50000);
        mastering->display_primaries[1][1] = av_make_q(m.green_y, 50000);
        mastering->display_primaries[2][0] = av_make_q(m.blue_x, 50000);
        mastering->display_primaries[2][1] = av_make_q(m.blue_y, 50000);
        mastering->white_point[0] = av_make_q(m.white_x, 50000);
        mastering->white_point[1] = av_make_q(m.white_y, 50000);
        mastering->max_luminance = av_make_q(static_cast<int>(m.max_luminance), 10000);
        mastering->min_luminance = av_make_q(static_cast<int>(m.min_luminance), 10000);
        mastering->has_primaries = m.red_x != 0 ? 1 : 0;
        mastering->has_luminance = m.max_luminance != 0 ? 1 : 0;
        if (av_packet_side_data_add(&parameters->coded_side_data,
                                    &parameters->nb_coded_side_data,
                                    AV_PKT_DATA_MASTERING_DISPLAY_METADATA, mastering, size,
                                    0) == nullptr) {
            av_free(mastering);
            return Status::fail(ErrorCode::OutOfMemory,
                                "cannot attach mastering-display side data");
        }

        AVContentLightMetadata* light = av_content_light_metadata_alloc(&size);
        if (light == nullptr) {
            return Status::fail(ErrorCode::OutOfMemory,
                                "cannot allocate content-light side data");
        }
        light->MaxCLL = hdr.cll.max_cll;
        light->MaxFALL = hdr.cll.max_fall;
        if (av_packet_side_data_add(&parameters->coded_side_data,
                                    &parameters->nb_coded_side_data,
                                    AV_PKT_DATA_CONTENT_LIGHT_LEVEL, light, size, 0) ==
            nullptr) {
            av_free(light);
            return Status::fail(ErrorCode::OutOfMemory,
                                "cannot attach content-light side data");
        }
        return Status::success();
    }

    void close() {
        opened_ = false;
        if (packet_ != nullptr) {
            av_packet_free(&packet_);
            packet_ = nullptr;
        }
        if (format_ != nullptr) {
            if (format_->pb != nullptr) {
                avio_closep(&format_->pb);
            }
            avformat_free_context(format_);
            format_ = nullptr;
        }
        stream_ = nullptr;
        hdr_ = Hdr10Metadata{};
    }

    AVFormatContext* format_ = nullptr;
    AVStream* stream_ = nullptr;
    AVPacket* packet_ = nullptr;
    std::string container_;
    Hdr10Metadata hdr_{};
    bool annex_b_ = false;
    bool opened_ = false;
};

#else  // RTX_HAVE_FFMPEG

class FfmpegNotCompiledDemuxer final : public IDemuxer {
public:
    Result<MediaInfo> open(const std::string& path) override {
        return Result<MediaInfo>::fail(
            ErrorCode::FfmpegMissing,
            "FFmpeg demuxer is not compiled into this build",
            "path=" + path +
                "; reconfigure with -DRTX_FFMPEG_ROOT=<prefix> (or set RTX_FFMPEG_ROOT)");
    }
    Result<Packet> readPacket() override {
        return Result<Packet>::fail(ErrorCode::FfmpegMissing,
                                    "FFmpeg demuxer is not compiled into this build");
    }
};

class FfmpegNotCompiledMuxer final : public IMuxer {
public:
    Status open(const std::string& path, const MediaInfo&, const Hdr10Metadata&) override {
        return Status::fail(ErrorCode::FfmpegMissing,
                            "FFmpeg muxer is not compiled into this build", path);
    }
    Status writePacket(const Packet&) override {
        return Status::fail(ErrorCode::FfmpegMissing,
                            "FFmpeg muxer is not compiled into this build");
    }
    Status finalize() override { return Status::success(); }
};

#endif  // RTX_HAVE_FFMPEG

}  // namespace

bool ffmpegAvailable() {
#ifdef RTX_HAVE_FFMPEG
    return true;
#else
    return false;
#endif
}

std::unique_ptr<IDemuxer> makeFfmpegDemuxer() {
#ifdef RTX_HAVE_FFMPEG
    return std::make_unique<FfmpegDemuxer>();
#else
    return std::make_unique<FfmpegNotCompiledDemuxer>();
#endif
}

std::unique_ptr<IMuxer> makeFfmpegMuxer() {
#ifdef RTX_HAVE_FFMPEG
    return std::make_unique<FfmpegMuxer>();
#else
    return std::make_unique<FfmpegNotCompiledMuxer>();
#endif
}

}  // namespace rtx
