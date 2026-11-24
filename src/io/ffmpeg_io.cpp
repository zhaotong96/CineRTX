// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
//
// Real libavformat/libavcodec demuxer and muxer.
//
// The translation unit has no conditional compilation and no degraded stub:
// libavformat, libavcodec, libavutil and libswresample are statically linked
// into rtx_core in every configuration (Gate 3.0,
// SPEC-FFMPEG-STATIC-CONSOLIDATION-20261026).  A build that cannot resolve the
// surgical static archives fails at configure time, so this file can never be
// compiled into a binary that lacks container I/O.
//
// Two source kinds are supported by the same classes:
//   * a filesystem path (production), and
//   * an in-memory buffer through rtx::io::MemoryAvioContext (hermetic tests).
//
// Every packet is also projected onto the Flicks integer timeline
// (705,600,000 Hz) with a remainder-carrying 128-bit rescale and a strict
// monotonicity repair, so a VFR or B-frame-reordered source can never make the
// muxer's interleaver step backwards (W7-2).

#include "rtx/io.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstring>
#include <deque>
#include <filesystem>
#include <format>
#include <limits>
#include <memory>
#include <mutex>
#include <utility>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavcodec/bsf.h>
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <libavutil/avutil.h>
#include <libavutil/error.h>
#include <libavutil/mastering_display_metadata.h>
#include <libavutil/mem.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
#include <libswresample/swresample.h>
}
#include "bitstream.h"
#include "memory_avio.h"
#include "rtx/rational.h"
#include "rtx/timestamp_guard.h"
#include "rtx/status.h"

namespace rtx {
namespace {

std::string avErrorString(int error) {
    char buffer[AV_ERROR_MAX_STRING_SIZE] = {};
    if (av_strerror(error, buffer, sizeof(buffer)) < 0) {
        return "FFmpeg error " + std::to_string(error);
    }
    return buffer;
}
std::string pathExtension(const std::filesystem::path& p) {
    std::string ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(c | 0x20); });
    return ext;
}
VideoCodec codecFromAv(AVCodecID id) {
    switch (id) {
        case AV_CODEC_ID_H264:
            return VideoCodec::H264;
        case AV_CODEC_ID_HEVC:
            return VideoCodec::HEVC;
        case AV_CODEC_ID_AV1:
            return VideoCodec::AV1;
        case AV_CODEC_ID_PRORES:
            return VideoCodec::ProRes;
        case AV_CODEC_ID_DNXHD:
            return VideoCodec::Dnxhd;
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
    const ColorPrimaries pri = parameters.color_primaries == AVCOL_PRI_BT2020
                                   ? ColorPrimaries::BT2020 : ColorPrimaries::BT709;
    const TransferFunction trc = parameters.color_trc == AVCOL_TRC_SMPTE2084
                                     ? TransferFunction::SMPTE2084
                                     : (parameters.color_trc == AVCOL_TRC_ARIB_STD_B67
                                            ? TransferFunction::HLG : TransferFunction::BT709);
    const MatrixCoefficients mat = parameters.color_space == AVCOL_SPC_BT2020_NCL
                                       ? MatrixCoefficients::BT2020NC : MatrixCoefficients::BT709;
    const ColorRange rng = parameters.color_range == AVCOL_RANGE_JPEG
                               ? ColorRange::Full : ColorRange::Limited;
    return {pri, trc, mat, rng};
}
void colorToAv(const ColorDescription& color, AVCodecParameters& parameters) {
    const auto [pri, trc, mat, rng] = color;
    parameters.color_primaries = pri == ColorPrimaries::BT2020 ? AVCOL_PRI_BT2020 : AVCOL_PRI_BT709;
    parameters.color_trc = trc == TransferFunction::SMPTE2084
                               ? AVCOL_TRC_SMPTE2084
                               : (trc == TransferFunction::HLG ? AVCOL_TRC_ARIB_STD_B67 : AVCOL_TRC_BT709);
    parameters.color_space = mat == MatrixCoefficients::BT2020NC ? AVCOL_SPC_BT2020_NCL : AVCOL_SPC_BT709;
    parameters.color_range = rng == ColorRange::Full ? AVCOL_RANGE_JPEG : AVCOL_RANGE_MPEG;
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
                auto& [rx, ry, gx, gy, bx, by, wx, wy, max_l, min_l] = hdr.mastering;
                (void)max_l; (void)min_l;
                rx = scale(mastering.display_primaries[0][0]);
                ry = scale(mastering.display_primaries[0][1]);
                gx = scale(mastering.display_primaries[1][0]);
                gy = scale(mastering.display_primaries[1][1]);
                bx = scale(mastering.display_primaries[2][0]);
                by = scale(mastering.display_primaries[2][1]);
                wx = scale(mastering.white_point[0]);
                wy = scale(mastering.white_point[1]);
                hdr.enabled = true;
            }
            if (mastering.has_luminance) {
                auto& [rx, ry, gx, gy, bx, by, wx, wy, max_l, min_l] = hdr.mastering;
                (void)rx; (void)ry; (void)gx; (void)gy; (void)bx; (void)by; (void)wx; (void)wy;
                max_l = static_cast<uint32_t>(
                    std::llround(mastering.max_luminance.num * 10000.0 /
                                 mastering.max_luminance.den));
                min_l = static_cast<uint32_t>(
                    std::llround(mastering.min_luminance.num * 10000.0 /
                                 mastering.min_luminance.den));
                hdr.enabled = true;
            }
        } else if (side.type == AV_PKT_DATA_CONTENT_LIGHT_LEVEL &&
                   side.size >= sizeof(AVContentLightMetadata)) {
            AVContentLightMetadata light{};
            std::memcpy(&light, side.data, sizeof(light));
            const auto toContentLightLevel = [](std::uint32_t value) {
                return static_cast<std::uint16_t>(std::min<std::uint32_t>(
                    value, std::numeric_limits<std::uint16_t>::max()));
            };
            hdr.cll.first = toContentLightLevel(light.MaxCLL);
            hdr.cll.second = toContentLightLevel(light.MaxFALL);
            hdr.enabled = true;
        }
    }
    if (!hdr.enabled && parameters.color_trc == AVCOL_TRC_SMPTE2084) {
        hdr.enabled = true;
    }
    return hdr;
}
void mergeHevcBitstreamMetadata(
    const AVCodecParameters& parameters,
    const std::tuple<bool, bool, int, int, int, bool>& vui,
    const std::tuple<bool, bool, std::array<uint16_t, 8>, uint32_t, uint32_t, uint16_t, uint16_t>& sei,
    MediaInfo& info) {
    const auto& [vui_has_col, vui_has_vst, vui_pri, vui_trc, vui_mat, vui_full] = vui;
    const auto& [sei_has_md, sei_has_cll, sei_prim, sei_max_l, sei_min_l, sei_max_cll, sei_max_fall] = sei;
    const bool container_primaries = parameters.color_primaries != AVCOL_PRI_UNSPECIFIED;
    const bool container_transfer = parameters.color_trc != AVCOL_TRC_UNSPECIFIED;
    const bool container_matrix = parameters.color_space != AVCOL_SPC_UNSPECIFIED;
    const bool vui_primaries = vui_has_col && vui_pri != 2;
    const bool vui_transfer = vui_has_col && vui_trc != 2;
    const bool vui_matrix = vui_has_col && vui_mat != 2;

    if (!container_primaries && vui_primaries) {
        auto& [c_pri, c_trc, c_mat, c_rng] = info.color;
        if (vui_pri == 9) c_pri = ColorPrimaries::BT2020;
        else if (vui_pri == 1) c_pri = ColorPrimaries::BT709;
        if (vui_trc == 16) c_trc = TransferFunction::SMPTE2084;
        else if (vui_trc == 18) c_trc = TransferFunction::HLG;
        else if (vui_trc == 1) c_trc = TransferFunction::BT709;
        if (vui_mat == 9) c_mat = MatrixCoefficients::BT2020NC;
        else if (vui_mat == 1) c_mat = MatrixCoefficients::BT709;
        c_rng = vui_full ? ColorRange::Full : ColorRange::Limited;
    }
    const bool has_primaries = container_primaries || vui_primaries;
    const bool has_transfer = container_transfer || vui_transfer;
    const bool has_matrix = container_matrix || vui_matrix;

    if (sei_has_md || sei_has_cll) {
        info.hdr.enabled = true;
        auto& [m_rx, m_ry, m_gx, m_gy, m_bx, m_by, m_wx, m_wy, m_max, m_min] = info.hdr.mastering;
        if (sei_has_md && (m_rx == 0 || m_max == 0)) {
            m_rx = sei_prim[0]; m_ry = sei_prim[1];
            m_gx = sei_prim[2]; m_gy = sei_prim[3];
            m_bx = sei_prim[4]; m_by = sei_prim[5];
            m_wx = sei_prim[6]; m_wy = sei_prim[7];
            m_max = sei_max_l; m_min = sei_min_l;
        }
        if (sei_has_cll && info.hdr.cll.first == 0 && info.hdr.cll.second == 0) {
            info.hdr.cll.first = sei_max_cll;
            info.hdr.cll.second = sei_max_fall;
        }
    }
    if (info.hdr.enabled) {
        auto& [c_pri, c_trc, c_mat, c_rng] = info.color;
        (void)c_rng;
        if (!has_primaries) {
            c_pri = ColorPrimaries::BT2020;
        }
        if (!has_transfer) {
            c_trc = TransferFunction::SMPTE2084;
        }
        if (!has_matrix) {
            c_mat = MatrixCoefficients::BT2020NC;
        }
        info.hdr.color = info.color;
        // HDR10 signals that omit colour metadata still require the PQ
        // container description; fill only the gaps so an explicit HLG or
        // BT.2020 declaration survives untouched.
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
}  // namespace

FfmpegDemuxer::FfmpegDemuxer(std::string container_hint)
    : container_hint_(std::move(container_hint)) {}

FfmpegDemuxer::FfmpegDemuxer(std::vector<uint8_t> bytes, std::string container_hint)
    : memory_bytes_(std::move(bytes)),
      container_hint_(std::move(container_hint)),
      memory_source_(true) {}

FfmpegDemuxer::~FfmpegDemuxer() noexcept { close(); }

Result<MediaInfo> FfmpegDemuxer::open(const std::filesystem::path& path) {
    close();
// Filesystem source.  `container_hint` is only used to label the result.
    if (memory_source_) {
        auto avio_ctx = io::makeMemoryAvioRead(std::span<const uint8_t>(memory_bytes_.data(), memory_bytes_.size()));
        memory_avio_ = avio_ctx;
// In-memory source.  The bytes are owned by the demuxer so the AVIOContext
// span stays valid for the whole lifetime of the AVFormatContext.
        if (!io::memoryAvioValid(avio_ctx)) {
            close();
            return err(ErrorCode::OutOfMemory,
                       "cannot allocate the in-memory AVIO context");
        }
        format_ = avformat_alloc_context();
        if (format_ == nullptr) {
            close();
            return err(ErrorCode::OutOfMemory,
                       "cannot allocate AVFormatContext");
// Zero-disk path: `path` is a container-name hint only.  No file is
// opened and no environment variable is consulted.
        }
        format_->pb = io::memoryAvioRaw(std::static_pointer_cast<io::MemoryAvioState>(memory_avio_));
        format_->flags |= AVFMT_FLAG_CUSTOM_IO;
        const int memory_status = avformat_open_input(&format_, nullptr, nullptr, nullptr);
        if (memory_status < 0) {
            close();
            return err(ErrorCode::Io,
// The AVIO context must be attached to a freshly allocated
// AVFormatContext *before* avformat_open_input(); passing it
// afterwards would let the probe open a different (file) source.
                       "cannot open in-memory media container",
                       avErrorString(memory_status));
        }
    } else {
        const std::string path_str = path.string();
        const int status = avformat_open_input(&format_, path_str.c_str(), nullptr, nullptr);
        if (status < 0) {
            return err(ErrorCode::Io,
                       "cannot open media container: " + path_str,
                       avErrorString(status));
        }
    }
    return finishOpen(path);
}

Result<Packet> FfmpegDemuxer::readPacket() {
    if (format_ == nullptr || video_stream_ < 0) {
        return err(ErrorCode::State, "demuxer is not open");
    }
    for (;;) {
        if (!pending_.empty()) {
            Packet packet = std::move(pending_.front());
            pending_.pop_front();
            return packet;
        }
        if (drained_) {
            return err(ErrorCode::NotFound, "end of stream");
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
            return err(ErrorCode::NotFound, "end of stream");
        }
        if (status < 0) {
            return err(ErrorCode::Io, "cannot read packet",
                       avErrorString(status));
        }
        if (packet_->stream_index == audio_stream_) {
            bool audio_valid = false;
            last_audio_flicks_ = projectWithTimebase(
                *packet_, info_.time_base, audio_drift_, audio_guard_, &audio_valid);
            last_audio_flicks_valid_ = audio_valid;
            Packet p = convertPacket(*packet_);
            p.stream_index = packet_->stream_index;
            av_packet_unref(packet_);
            return p;
        }
        if (isPassthroughIndex(packet_->stream_index)) {
            Packet p = convertPacket(*packet_);
            p.stream_index = packet_->stream_index;
            av_packet_unref(packet_);
            return p;
        }
        if (packet_->stream_index != video_stream_) {
            av_packet_unref(packet_);
            continue;
        }
        if (bitstream_filter_ != nullptr) {
            status = av_bsf_send_packet(bitstream_filter_, packet_);
            av_packet_unref(packet_);
// Phase 7 (Task 7-B3): forward every passthrough-eligible
// secondary audio / subtitle packet with its own time base.
            if (status < 0) {
                return err(ErrorCode::Io, "bitstream filter rejected packet",
                           avErrorString(status));
            }
            drainBitstreamFilter();
            continue;
        }
        pending_.push_back(convertPacket(*packet_));
        av_packet_unref(packet_);
    }
}

Result<void> FfmpegDemuxer::seekToKeyframeBefore(int64_t video_stream_pts) {
    if (format_ == nullptr || video_stream_ < 0) {
        return err(ErrorCode::State, "demuxer is not open");
    }
    const int64_t target =
        video_stream_pts == std::numeric_limits<int64_t>::min()
            ? 0
            : video_stream_pts;
    const int status = av_seek_frame(format_, video_stream_, target, AVSEEK_FLAG_BACKWARD);
    if (status < 0) {
        return err(ErrorCode::Io, "cannot seek to the requested key frame", avErrorString(status));
    }
    if (bitstream_filter_ != nullptr) {
        av_bsf_flush(bitstream_filter_);
    }
    pending_.clear();
    drained_ = false;
    bitstream_filter_flushed_ = false;
    resetFlicksMonotonic(video_guard_);
    resetFlicksMonotonic(audio_guard_);
    return {};
}
bool FfmpegDemuxer::isPassthroughIndex(int stream_index) const noexcept {
    if (stream_index == audio_stream_) {
        return false;  // handled by the primary-audio branch above
// Phase 11.2 (Track 11C 提纯项): EOF 排空时 av_bsf_send_packet(nullptr)
// 会把过滤器锁进终态，seek 后的第一个包会被拒绝
// ("A non-NULL packet sent after an EOF")。av_bsf_flush 解除该终态，
// 实时播放的 GOP Seek 与回放才能反复进行。
    }
    return std::any_of(passthrough_indices_.begin(), passthrough_indices_.end(),
                       [stream_index](int index) { return index == stream_index; });
}

Result<MediaInfo> FfmpegDemuxer::finishOpen(const std::filesystem::path& path) {
    int status = avformat_find_stream_info(format_, nullptr);
    if (status < 0) {
        close();
        return err(ErrorCode::Io, "cannot read stream information",
                   avErrorString(status));
    }
    const int stream_index = av_find_best_stream(format_, AVMEDIA_TYPE_VIDEO, -1, -1,
// Phase 7 (Task 7-B3): true when the stream rides the passthrough set.
                                                 nullptr, 0);
    if (stream_index < 0) {
        close();
        return err(ErrorCode::InvalidArgument,
                   "container has no decodable video stream");
    }
    video_stream_ = stream_index;
    audio_stream_ = av_find_best_stream(format_, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    AVStream* stream = format_->streams[stream_index];

    MediaInfo info;
    info.container = !container_hint_.empty() ? container_hint_ : pathExtension(path);
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
    for (int sd_idx = 0; sd_idx < stream->codecpar->nb_coded_side_data; ++sd_idx) {
        const AVPacketSideData& sd = stream->codecpar->coded_side_data[sd_idx];
        if (sd.type == AV_PKT_DATA_DISPLAYMATRIX &&
            sd.data != nullptr && sd.size >= sizeof(int32_t) * 9) {
            std::memcpy(info.display_matrix.data(), sd.data, sizeof(int32_t) * 9);
            info.has_display_matrix = true;
            break;
        }
    }
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
        AVStream* st = format_->streams[i];
        if (st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            ++info.audio_streams;
            std::shared_ptr<void> owned_par;
            AVCodecParameters* cloned_par = avcodec_parameters_alloc();
            if (cloned_par != nullptr) {
                avcodec_parameters_copy(cloned_par, st->codecpar);
                owned_par = std::shared_ptr<void>(cloned_par, [](void* p) {
                    if (p != nullptr) {
                        AVCodecParameters* cp = static_cast<AVCodecParameters*>(p);
                        avcodec_parameters_free(&cp);
                    }
                });
            }
            info.audio_tracks.emplace_back(
                static_cast<int>(i), st->codecpar->sample_rate,
                st->codecpar->ch_layout.nb_channels, owned_par);
            info.passthrough_streams.emplace_back(
                static_cast<int>(i), 1, static_cast<int>(st->codecpar->codec_id),
                std::string(avcodec_get_name(st->codecpar->codec_id)),
                Rational{st->time_base.num, st->time_base.den},
                owned_par);
        } else if (st->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE) {
            ++info.subtitle_streams;
            const int codec_id = static_cast<int>(st->codecpar->codec_id);
            if (codec_id == AV_CODEC_ID_SUBRIP || codec_id == AV_CODEC_ID_ASS ||
                codec_id == AV_CODEC_ID_SSA || codec_id == AV_CODEC_ID_WEBVTT ||
                codec_id == AV_CODEC_ID_MOV_TEXT) {
                std::shared_ptr<void> sub_opaque;
                if (AVCodecParameters* cloned_par = avcodec_parameters_alloc(); cloned_par != nullptr) {
                    avcodec_parameters_copy(cloned_par, st->codecpar);
                    sub_opaque = std::shared_ptr<void>(cloned_par, [](void* p) {
                        if (p != nullptr) {
                            AVCodecParameters* cp = static_cast<AVCodecParameters*>(p);
                            avcodec_parameters_free(&cp);
                        }
                    });
                }
                info.passthrough_streams.emplace_back(
                    static_cast<int>(i), 2, codec_id,
                    std::string(avcodec_get_name(st->codecpar->codec_id)),
                    Rational{st->time_base.num, st->time_base.den},
                    std::move(sub_opaque));
            }
        }
    }
    info.primary_audio_index = audio_stream_;
    passthrough_indices_.clear();
    for (const auto& [ps_idx, ps_ct, ps_cid, ps_cn, ps_tb, ps_op] : info.passthrough_streams) {
        (void)ps_ct; (void)ps_cid; (void)ps_cn; (void)ps_tb; (void)ps_op;
        passthrough_indices_.push_back(ps_idx);
    }
    info.color = colorFromAv(*stream->codecpar);
    info.hdr = hdrFromSideData(*stream->codecpar);
    info.hdr_source = info.hdr.enabled;
    if (stream->codecpar->extradata != nullptr && stream->codecpar->extradata_size > 0) {
        info.extradata.assign(stream->codecpar->extradata,
                              stream->codecpar->extradata + stream->codecpar->extradata_size);
    }
    std::tuple<bool, bool, int, int, int, bool> vui{false, false, 2, 2, 2, false};
    if (info.codec == VideoCodec::HEVC && !info.extradata.empty()) {
        (void)parseHevcVuiColor(info.extradata.data(), info.extradata.size(), vui);
    }
    info_ = info;
    video_drift_ = FlicksDriftCompensator{};
// Phase 7 (Task 7-B3): snapshot the passthrough set for readPacket.
    audio_drift_ = FlicksDriftCompensator{};
    resetFlicksMonotonic(video_guard_);
    resetFlicksMonotonic(audio_guard_);
    packet_ = av_packet_alloc();
    if (packet_ == nullptr) {
        close();
        return err(ErrorCode::Io, "out of memory for AVPacket");
    }
    AVPacket* probe = av_packet_alloc();
    if (probe == nullptr) {
        close();
        return err(ErrorCode::Io, "out of memory for AVPacket");
    }
    bool packets_are_annex_b = false;
    bool probe_valid = false;
    std::tuple<bool, bool, std::array<uint16_t, 8>, uint32_t, uint32_t, uint16_t, uint16_t> sei{false, false, {}, 0, 0, 0, 0};
// MP4/MOV store length-prefixed NAL units; MKV/TS can already deliver
// start codes.  Detect the actual packet syntax instead of guessing
// from the container name: MP4 always needs h264_mp4toannexb /
// hevc_mp4toannexb, while Matroska is demuxed to Annex-B by FFmpeg.
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
            const int bsf_status = av_bsf_send_packet(bitstream_filter_, probe);
            av_packet_unref(probe);
            if (bsf_status < 0) {
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
    return info_;
}

void FfmpegDemuxer::configureBitstreamFilter(const char* name) {
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

void FfmpegDemuxer::drainBitstreamFilter() {
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

int64_t FfmpegDemuxer::projectWithTimebase(const AVPacket& source, Rational time_base,
                            FlicksDriftCompensator& drift, FlicksMonotonicGuard& guard,
                            bool* valid) noexcept {
    if (source.pts == AV_NOPTS_VALUE || !time_base.valid()) {
        *valid = false;
        return flicksMonotonicPrimed(guard) ? flicksMonotonicLast(guard) : 0;
    }
    *valid = true;
    return applyFlicksMonotonic(guard, rescaleToFlicksAccum(drift, 
        source.pts, static_cast<uint32_t>(time_base.num),
        static_cast<uint32_t>(time_base.den)));
}

int64_t FfmpegDemuxer::projectVideoPtsToFlicks(int64_t pts, Rational time_base,
                                FlicksDriftCompensator& drift, bool* valid) noexcept {
    if (pts == AV_NOPTS_VALUE || !time_base.valid()) {
        *valid = false;
        return 0;
    }
    *valid = true;
    return rescaleToFlicksAccum(drift, 
        pts, static_cast<uint32_t>(time_base.num),
        static_cast<uint32_t>(time_base.den));
// Projects a container timestamp onto the Flicks axis and repairs a
// regression for monotonic streams (e.g. audio). Returns the (possibly repaired)
// Flicks value; `valid` is false when the container supplies no timestamp at all.
}

Packet FfmpegDemuxer::convertPacket(const AVPacket& source) {
    Packet packet;
    packet.data.assign(source.data, source.data + source.size);
    packet.pts = source.pts;
    packet.dts = source.dts;
    packet.duration = source.duration;
    packet.keyframe = (source.flags & AV_PKT_FLAG_KEY) != 0;
    packet.stream_index = source.stream_index;
    if (source.stream_index >= 0 &&
        static_cast<unsigned int>(source.stream_index) < format_->nb_streams) {
        packet.time_base = Rational{format_->streams[source.stream_index]->time_base.num,
                                    format_->streams[source.stream_index]->time_base.den};
// Projects a video packet presentation timestamp (PTS) onto the Flicks timeline.
// In demuxed packet streams with B-frames, packet delivery strictly follows
// decoding order (DTS), causing valid presentation timestamps (PTS) to be non-monotonic.
// Video PTS must be preserved without monotonic clamping.
    } else {
        packet.time_base = info_.time_base;
    }
    packet.pts_flicks = projectVideoPtsToFlicks(source.pts, packet.time_base, video_drift_,
                                                &packet.flicks_valid);
    return packet;
}

void FfmpegDemuxer::close() noexcept {
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
        if (memory_avio_ != nullptr) {
            format_->pb = nullptr;
        }
        avformat_close_input(&format_);
// Phase 7 (Task 7-B3): every packet carries its own stream's time
// base so the muxer can rescale any passthrough stream correctly.
        format_ = nullptr;
    }
    memory_avio_.reset();
    drained_ = false;
    bitstream_filter_flushed_ = false;
    video_stream_ = -1;
    audio_stream_ = -1;
    resetFlicksMonotonic(video_guard_);
    resetFlicksMonotonic(audio_guard_);
}

FfmpegMuxer::FfmpegMuxer(std::vector<uint8_t>* memory_target, std::string format)
    : memory_target_(memory_target), memory_format_(std::move(format)) {}

FfmpegMuxer::~FfmpegMuxer() noexcept { (void)finalize(); }

Result<void> FfmpegMuxer::open(const std::filesystem::path& path, const MediaInfo& info,
                  const Hdr10Metadata& hdr) {
    const std::string path_str = path.string();
    if (format_ != nullptr) {
        return err(ErrorCode::State, "muxer is already open", path_str);
    }
    const AVCodecID codec = codecToAv(info.codec);
    if (codec == AV_CODEC_ID_NONE) {
        return err(ErrorCode::InvalidArgument,
// Phase 7 (Task 7-B3): secondary audio + subtitle indices forwarded to
// the caller (subset of info_.passthrough_streams).
                   "unsupported output video codec", path_str);
    }
    const bool in_memory = memory_target_ != nullptr;
    int status = in_memory
                     ? avformat_alloc_output_context2(
                           &format_, nullptr,
                           memory_format_.empty() ? "matroska" : memory_format_.c_str(),
                           nullptr)
                     : avformat_alloc_output_context2(&format_, nullptr, nullptr,
                                                      path_str.c_str());
    if (format_ == nullptr) {
        return err(ErrorCode::Io, "cannot create output context",
                   avErrorString(status));
    }
    stream_ = avformat_new_stream(format_, nullptr);
    if (stream_ == nullptr) {
        close();
        return err(ErrorCode::Io, "cannot create output stream");
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
        return err(ErrorCode::InvalidArgument,
                   "muxer requires a codec configuration record", path_str);
    }
    parameters->extradata = static_cast<uint8_t*>(
        av_mallocz(info.extradata.size() + AV_INPUT_BUFFER_PADDING_SIZE));
    if (parameters->extradata == nullptr) {
        close();
        return err(ErrorCode::OutOfMemory, "cannot allocate codec extradata");
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
    stream_->r_frame_rate = frame_rate;
    stream_->time_base =
        AVRational{info.time_base.num > 0 ? static_cast<int>(info.time_base.num) : 1,
                   info.time_base.den > 0 ? static_cast<int>(info.time_base.den) : 90000};

    if (hdr.enabled) {
        const Result<void> side = applyHdrSideData(hdr);
        if (!side.has_value()) {
            close();
            return side;
        }
    }
    if (info.has_display_matrix) {
        constexpr size_t kMatrixBytes = sizeof(int32_t) * 9;
        auto* matrix_buf = static_cast<int32_t*>(av_malloc(kMatrixBytes));
        if (matrix_buf == nullptr) {
            close();
            return err(ErrorCode::OutOfMemory, "cannot allocate display-matrix side data");
        }
        std::memcpy(matrix_buf, info.display_matrix.data(), kMatrixBytes);
        if (av_packet_side_data_add(&parameters->coded_side_data,
                                    &parameters->nb_coded_side_data,
                                    AV_PKT_DATA_DISPLAYMATRIX, matrix_buf, kMatrixBytes,
                                    0) == nullptr) {
            av_free(matrix_buf);
            close();
            return err(ErrorCode::OutOfMemory, "cannot attach display-matrix side data");
        }
    }
    container_ = info.format_name.empty()
                     ? (in_memory ? memory_format_ : pathExtension(path))
                     : info.format_name;

    const bool matroska_output =
        container_.find("matroska") != std::string::npos ||
        container_.find("webm") != std::string::npos;
    const bool mp4_output = container_.find("mp4") != std::string::npos ||
                            container_.find("mov") != std::string::npos;
    for (const auto& [ps_idx, ps_ct, ps_cid, ps_cn, ps_tb, ps_op] : info.passthrough_streams) {
        (void)ps_cid;
        if (ps_op == nullptr || ps_idx < 0) continue;
        AVCodecParameters* src_par = static_cast<AVCodecParameters*>(ps_op.get());
        if (ps_ct == 2) {
            const int codec_id = static_cast<int>(src_par->codec_id);
            const bool supported =
                (matroska_output &&
                 (codec_id == AV_CODEC_ID_SUBRIP || codec_id == AV_CODEC_ID_ASS ||
                  codec_id == AV_CODEC_ID_SSA || codec_id == AV_CODEC_ID_WEBVTT)) ||
                (mp4_output && codec_id == AV_CODEC_ID_MOV_TEXT);
            if (!supported) {
                warnings_.push_back("subtitle stream " + std::to_string(ps_idx) + " (" +
                                    ps_cn + ") is not supported by the " + container_ +
                                    " output container; dropped");
                continue;
            }
        }
        AVStream* out_stream = avformat_new_stream(format_, nullptr);
        if (out_stream == nullptr) {
            close();
            return err(ErrorCode::Io, "cannot create passthrough output stream");
        }
        avcodec_parameters_copy(out_stream->codecpar, src_par);
        out_stream->codecpar->codec_tag = 0;
        out_stream->time_base = AVRational{
            ps_tb.num > 0 ? static_cast<int>(ps_tb.num) : 1,
            ps_tb.den > 0 ? static_cast<int>(ps_tb.den) : (ps_ct == 1 ? 48000 : 1000)};
        passthrough_streams_.push_back(std::make_tuple(ps_idx, out_stream, makeTimestampGuard()));
        if (ps_idx == info.primary_audio_index) {
            audio_stream_ = out_stream;
            in_audio_time_base_ = ps_tb;
            out_audio_time_base_ = Rational{out_stream->time_base.num, out_stream->time_base.den};
            audio_stream_index_ = out_stream->index;
            src_audio_stream_index_ = ps_idx;
        }
    }
    if (in_memory) {
        auto avio_ctx = io::makeMemoryAvioWrite(memory_target_);
        memory_avio_ = avio_ctx;
        if (!io::memoryAvioValid(avio_ctx)) {
            close();
            return err(ErrorCode::OutOfMemory,
                       "cannot allocate the in-memory AVIO context");
        }
        format_->pb = io::memoryAvioRaw(std::static_pointer_cast<io::MemoryAvioState>(memory_avio_));
        format_->flags |= AVFMT_FLAG_CUSTOM_IO;
    } else {
        status = avio_open(&format_->pb, path_str.c_str(), AVIO_FLAG_WRITE);
        if (status < 0) {
            close();
            return err(ErrorCode::Io, "cannot open output file",
                       path_str + ": " + avErrorString(status));
        }
    }
    status = avformat_write_header(format_, nullptr);
    if (status < 0) {
        close();
        return err(ErrorCode::Io, "cannot write container header",
                   avErrorString(status));
    }
    packet_ = av_packet_alloc();
    if (packet_ == nullptr) {
        close();
        return err(ErrorCode::OutOfMemory, "cannot allocate output packet");
    }
    annex_b_ = info.annex_b;
    hdr_ = hdr;
    opened_ = true;
    return {};
}

Result<void> FfmpegMuxer::writePacket(const Packet& packet) {
    std::lock_guard<std::mutex> lock(muxer_mutex_);
    if (!opened_ || format_ == nullptr) {
        return err(ErrorCode::State, "muxer is not open");
    }
    for (auto& [src_index, out_stream, pt_guard] : passthrough_streams_) {
        if (packet.stream_index != src_index) {
            continue;
        }
        if (packet.data.size() > static_cast<size_t>(INT_MAX)) {
            return err(ErrorCode::InvalidArgument, "passthrough packet is too large");
        }
        av_packet_unref(packet_);
        int status = av_new_packet(packet_, static_cast<int>(packet.data.size()));
        if (status < 0) {
            return err(ErrorCode::OutOfMemory, "cannot allocate output passthrough packet");
        }
        std::memcpy(packet_->data, packet.data.data(), packet.data.size());
        packet_->stream_index = out_stream->index;
        const AVRational source_base{
            packet.time_base.num > 0 ? static_cast<int>(packet.time_base.num) : 1,
            packet.time_base.den > 0 ? static_cast<int>(packet.time_base.den)
                                     : (out_stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO
                                            ? 48000
                                            : 1000)};
        packet_->pts = av_rescale_q(packet.pts, source_base, out_stream->time_base);
        packet_->dts = av_rescale_q(packet.dts, source_base, out_stream->time_base);
        packet_->duration = av_rescale_q(packet.duration, source_base, out_stream->time_base);
        packet_->flags = packet.keyframe ? AV_PKT_FLAG_KEY : 0;
        sanitizeTimestamp(pt_guard, packet_->pts, packet_->dts, 1);
        status = av_interleaved_write_frame(format_, packet_);
        av_packet_unref(packet_);
// Phase 7 (Task 7-B3): route any passthrough stream (multi-audio +
// subtitles) by source stream index, rescaling from the packet's own
// time base to the output stream's.
        if (status < 0) {
            return err(ErrorCode::Io, "cannot write passthrough packet",
                       avErrorString(status));
        }
        return {};
    }
    if (stream_ == nullptr) {
        return err(ErrorCode::State, "video stream is not open");
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
// Phase 8 (Task 8-B2): strictly monotonic DTS per output stream,
// so a VFR / damaged source can never trip the interleaver.
    if (size > static_cast<size_t>(INT_MAX)) {
        return err(ErrorCode::InvalidArgument, "packet is too large");
    }
    av_packet_unref(packet_);
    int status = av_new_packet(packet_, static_cast<int>(size));
    if (status < 0) {
        return err(ErrorCode::OutOfMemory, "cannot allocate output packet");
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
    sanitizeTimestamp(video_guard_, packet_->pts, packet_->dts, 1);
    status = av_interleaved_write_frame(format_, packet_);
    av_packet_unref(packet_);
    if (status < 0) {
        return err(ErrorCode::Io, "cannot write video packet",
                   avErrorString(status));
    }
    return {};
}

Result<void> FfmpegMuxer::finalize() {
    Result<void> result;
    if (format_ != nullptr && opened_) {
        const int status = av_write_trailer(format_);
        if (status < 0) {
            result = err(ErrorCode::Io, "cannot finalise container",
                         avErrorString(status));
        }
        if (memory_avio_ != nullptr) {
            avio_flush(io::memoryAvioRaw(std::static_pointer_cast<io::MemoryAvioState>(memory_avio_)));
        }
    }
    close();
    return result;
}

Result<void> FfmpegMuxer::applyHdrSideData(const Hdr10Metadata& hdr) {
    AVCodecParameters* parameters = stream_->codecpar;
    size_t size = 0;
// Phase 8 (Task 8-B2): strictly monotonic DTS on the video stream too.
    AVMasteringDisplayMetadata* mastering =
        av_mastering_display_metadata_alloc_size(&size);
    if (mastering == nullptr) {
        return err(ErrorCode::OutOfMemory,
                   "cannot allocate mastering-display side data");
    }
    const auto& [m_rx, m_ry, m_gx, m_gy, m_bx, m_by, m_wx, m_wy, m_max, m_min] = hdr.mastering;
    mastering->display_primaries[0][0] = av_make_q(m_rx, 50000);
    mastering->display_primaries[0][1] = av_make_q(m_ry, 50000);
    mastering->display_primaries[1][0] = av_make_q(m_gx, 50000);
    mastering->display_primaries[1][1] = av_make_q(m_gy, 50000);
    mastering->display_primaries[2][0] = av_make_q(m_bx, 50000);
    mastering->display_primaries[2][1] = av_make_q(m_by, 50000);
    mastering->white_point[0] = av_make_q(m_wx, 50000);
    mastering->white_point[1] = av_make_q(m_wy, 50000);
    mastering->max_luminance = av_make_q(static_cast<int>(m_max), 10000);
    mastering->min_luminance = av_make_q(static_cast<int>(m_min), 10000);
    mastering->has_primaries = m_rx != 0 ? 1 : 0;
    mastering->has_luminance = m_max != 0 ? 1 : 0;
    if (av_packet_side_data_add(&parameters->coded_side_data,
                                &parameters->nb_coded_side_data,
                                AV_PKT_DATA_MASTERING_DISPLAY_METADATA, mastering, size,
                                0) == nullptr) {
        av_free(mastering);
        return err(ErrorCode::OutOfMemory,
                   "cannot attach mastering-display side data");
    }
    AVContentLightMetadata* light = av_content_light_metadata_alloc(&size);
    if (light == nullptr) {
        return err(ErrorCode::OutOfMemory,
                   "cannot allocate content-light side data");
    }
    light->MaxCLL = hdr.cll.first;
    light->MaxFALL = hdr.cll.second;
    if (av_packet_side_data_add(&parameters->coded_side_data,
                                &parameters->nb_coded_side_data,
                                AV_PKT_DATA_CONTENT_LIGHT_LEVEL, light, size, 0) ==
        nullptr) {
        av_free(light);
        return err(ErrorCode::OutOfMemory,
                   "cannot attach content-light side data");
    }
    return {};
}

void FfmpegMuxer::close() noexcept {
    opened_ = false;
    if (packet_ != nullptr) {
        av_packet_free(&packet_);
        packet_ = nullptr;
    }
    if (format_ != nullptr) {
        if (format_->pb != nullptr) {
            if (memory_avio_ != nullptr) {
                format_->pb = nullptr;  // custom I/O: owned by memory_avio_
            } else {
                avio_closep(&format_->pb);
            }
        }
        avformat_free_context(format_);
        format_ = nullptr;
    }
    memory_avio_.reset();
    stream_ = nullptr;
    audio_stream_ = nullptr;
    audio_stream_index_ = -1;
    src_audio_stream_index_ = -1;
    passthrough_streams_.clear();
    resetTimestampGuard(video_guard_);
    warnings_.clear();
    hdr_ = Hdr10Metadata{};
}

std::unique_ptr<FfmpegDemuxer> makeFfmpegDemuxer() { return std::make_unique<FfmpegDemuxer>(); }

std::unique_ptr<FfmpegMuxer> makeFfmpegMuxer() { return std::make_unique<FfmpegMuxer>(); }

std::unique_ptr<FfmpegDemuxer> makeMemoryDemuxer(std::vector<uint8_t> bytes) {
    return std::make_unique<FfmpegDemuxer>(std::move(bytes), std::string{"memory"});
}

std::unique_ptr<FfmpegMuxer> makeMemoryMuxer(std::vector<uint8_t>* output,
                                             std::string container_format) {
    return std::make_unique<FfmpegMuxer>(output, std::move(container_format));
}

}  // namespace rtx
