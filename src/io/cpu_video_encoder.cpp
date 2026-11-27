// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors

#include "rtx/cpu_video_encoder.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <utility>

#if defined(__AVX2__) || defined(_MSC_VER)
#include <immintrin.h>
#endif

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/buffer.h>
#include <libavutil/frame.h>
#include <libavutil/mastering_display_metadata.h>
#include <libavutil/opt.h>
#include <libavutil/pixfmt.h>
}

namespace rtx {
namespace {

void convertP010RowLuma(const uint16_t* src, uint16_t* dst, int width) noexcept {
    int x = 0;
#if defined(__AVX2__)
    for (; x + 16 <= width; x += 16) {
        const __m256i v = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(src + x));
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(dst + x), _mm256_srli_epi16(v, 6));
    }
#endif
    for (; x < width; ++x) dst[x] = static_cast<uint16_t>(src[x] >> 6);
}

void deinterleaveP010RowChroma(const uint16_t* src_uv, uint16_t* dst_u, uint16_t* dst_v, int chroma_w) noexcept {
    int x = 0;
#if defined(__AVX2__)
    const __m256i mask = _mm256_setr_epi8(
        0, 1, 4, 5, 8, 9, 12, 13, 2, 3, 6, 7, 10, 11, 14, 15,
        0, 1, 4, 5, 8, 9, 12, 13, 2, 3, 6, 7, 10, 11, 14, 15);
    for (; x + 8 <= chroma_w; x += 8) {
        const __m256i uv = _mm256_srli_epi16(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(src_uv + x * 2)), 6);
        const __m256i perm = _mm256_permute4x64_epi64(_mm256_shuffle_epi8(uv, mask), 0xD8);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(dst_u + x), _mm256_castsi256_si128(perm));
        _mm_storeu_si128(reinterpret_cast<__m128i*>(dst_v + x), _mm256_extracti128_si256(perm, 1));
    }
#endif
    for (; x < chroma_w; ++x) {
        dst_u[x] = static_cast<uint16_t>(src_uv[x * 2] >> 6);
        dst_v[x] = static_cast<uint16_t>(src_uv[x * 2 + 1] >> 6);
    }
}

const char* codecEncoderName(VideoCodec codec) noexcept {
    // Phase 13 (Wave 0-B / 76_ & 78_ Ruling): Pure LGPL + BSD-3-Clause-Clear SVT-AV1 only (zero GPL x265/x264).
    switch (codec) {
        case VideoCodec::AV1: return "libsvtav1";
        default:              return nullptr;
    }
}

}  // namespace

void CpuVideoEncoder::release() noexcept {
    if (pkt_) av_packet_free(&pkt_);
    if (frame_) av_frame_free(&frame_);
    if (ctx_) avcodec_free_context(&ctx_);
}

CpuVideoEncoder::~CpuVideoEncoder() noexcept { release(); }

CpuVideoEncoder::CpuVideoEncoder(CpuVideoEncoder&& other) noexcept
    : ctx_(std::exchange(other.ctx_, nullptr)),
      frame_(std::exchange(other.frame_, nullptr)),
      pkt_(std::exchange(other.pkt_, nullptr)),
      width_(other.width_),
      height_(other.height_),
      hdr_(other.hdr_),
      extradata_buf_(std::move(other.extradata_buf_)) {}

CpuVideoEncoder& CpuVideoEncoder::operator=(CpuVideoEncoder&& other) noexcept {
    if (this != &other) {
        release();
        ctx_ = std::exchange(other.ctx_, nullptr);
        frame_ = std::exchange(other.frame_, nullptr);
        pkt_ = std::exchange(other.pkt_, nullptr);
        width_ = other.width_;
        height_ = other.height_;
        hdr_ = other.hdr_;
        extradata_buf_ = std::move(other.extradata_buf_);
    }
    return *this;
}

Result<std::vector<Packet>> CpuVideoEncoder::drainPackets() {
    std::vector<Packet> out;
    for (;;) {
        const int rc = avcodec_receive_packet(ctx_, pkt_);
        if (rc == AVERROR(EAGAIN) || rc == AVERROR_EOF) break;
        if (rc < 0) return err(ErrorCode::Internal, "avcodec_receive_packet failed", std::to_string(rc));
        Packet p;
        p.pts = pkt_->pts;
        p.dts = pkt_->dts;
        p.duration = pkt_->duration;
        p.time_base = Rational{1, 90000};
        p.keyframe = (pkt_->flags & AV_PKT_FLAG_KEY) != 0;
        p.data.assign(pkt_->data, pkt_->data + pkt_->size);
        out.push_back(std::move(p));
        av_packet_unref(pkt_);
    }
    return out;
}

Result<CpuVideoEncoder> CpuVideoEncoder::create(
    VideoCodec codec_id, int width, int height, Rational frame_rate,
    int crf, int preset, int threads, const Hdr10Metadata& hdr) {
    if (width <= 0 || height <= 0 || (width & 1) != 0 || (height & 1) != 0) {
        return err(ErrorCode::InvalidArgument, "CpuVideoEncoder requires positive even dimensions");
    }
    const char* name = codecEncoderName(codec_id);
    if (!name) return err(ErrorCode::Unsupported, "Unsupported CPU video codec");
    const AVCodec* codec = avcodec_find_encoder_by_name(name);
    if (!codec) return err(ErrorCode::Unsupported, std::string("FFmpeg static build lacks encoder: ") + name);

    CpuVideoEncoder enc;
    enc.width_ = width;
    enc.height_ = height;
    enc.hdr_ = hdr;
    enc.ctx_ = avcodec_alloc_context3(codec);
    enc.frame_ = av_frame_alloc();
    enc.pkt_ = av_packet_alloc();
    if (!enc.ctx_ || !enc.frame_ || !enc.pkt_) {
        return err(ErrorCode::OutOfMemory, "Failed to allocate FFmpeg CPU encoder context");
    }
    AVCodecContext* c = enc.ctx_;
    c->width = width;
    c->height = height;
    c->time_base = AVRational{1, 90000};
    c->framerate = AVRational{static_cast<int>(frame_rate.num), static_cast<int>(frame_rate.den)};
    c->pix_fmt = (codec_id == VideoCodec::H264) ? AV_PIX_FMT_YUV420P : AV_PIX_FMT_YUV420P10LE;
    c->thread_count = threads;
    c->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    if (hdr.enabled) {
        c->color_primaries = AVCOL_PRI_BT2020;
        c->color_trc = AVCOL_TRC_SMPTE2084;
        c->colorspace = AVCOL_SPC_BT2020_NCL;
        const auto [cp, ct, cm, cr] = hdr.color; (void)cp; (void)ct; (void)cm;
        c->color_range = (cr == ColorRange::Full) ? AVCOL_RANGE_JPEG : AVCOL_RANGE_MPEG;
    }
    if (codec_id == VideoCodec::AV1) {
        av_opt_set_int(c->priv_data, "rc", 0, 0);
        av_opt_set_int(c->priv_data, "crf", std::clamp(crf, 0, 63), 0);
        av_opt_set_int(c->priv_data, "preset", std::clamp(preset, 1, 12), 0);
    } else if (codec_id == VideoCodec::HEVC) {
        av_opt_set_int(c->priv_data, "crf", std::clamp(crf, 0, 51), 0);
        static constexpr const char* kX265Presets[] = {
            "ultrafast", "superfast", "veryfast", "faster", "fast",
            "medium", "slow", "slower", "veryslow", "placebo"
        };
        av_opt_set(c->priv_data, "preset", kX265Presets[std::clamp(preset, 0, 9)], 0);
    } else {
        av_opt_set_int(c->priv_data, "crf", std::clamp(crf, 0, 51), 0);
    }
    if (const int rc = avcodec_open2(c, codec, nullptr); rc < 0) {
        return err(ErrorCode::Internal, "avcodec_open2 failed for CPU CRF encoder", std::to_string(rc));
    }
    if (c->extradata && c->extradata_size > 0) {
        enc.extradata_buf_.assign(c->extradata, c->extradata + c->extradata_size);
    }
    enc.frame_->format = c->pix_fmt;
    enc.frame_->width = c->width;
    enc.frame_->height = c->height;
    if (av_frame_get_buffer(enc.frame_, 32) < 0) {
        return err(ErrorCode::OutOfMemory, "av_frame_get_buffer failed");
    }
    return enc;
}

Result<std::vector<Packet>> CpuVideoEncoder::encodeP010Frame(
    const uint16_t* y_plane, size_t y_pitch_bytes,
    const uint16_t* uv_plane, size_t uv_pitch_bytes,
    int64_t pts_90k, bool force_keyframe, std::span<const uint8_t> hdr10plus_payload) {
    if (!ctx_) return err(ErrorCode::State, "CpuVideoEncoder not initialized");
    if (!y_plane || !uv_plane) return err(ErrorCode::InvalidArgument, "Null P010 plane");
    if (av_frame_make_writable(frame_) < 0) return err(ErrorCode::Internal, "av_frame_make_writable failed");

    const size_t y_stride_u16 = y_pitch_bytes / sizeof(uint16_t);
    const size_t uv_stride_u16 = uv_pitch_bytes / sizeof(uint16_t);
    for (int y = 0; y < height_; ++y) {
        convertP010RowLuma(y_plane + static_cast<size_t>(y) * y_stride_u16,
                           reinterpret_cast<uint16_t*>(frame_->data[0] + y * frame_->linesize[0]), width_);
    }
    for (int y = 0; y < height_ / 2; ++y) {
        deinterleaveP010RowChroma(uv_plane + static_cast<size_t>(y) * uv_stride_u16,
                                  reinterpret_cast<uint16_t*>(frame_->data[1] + y * frame_->linesize[1]),
                                  reinterpret_cast<uint16_t*>(frame_->data[2] + y * frame_->linesize[2]), width_ / 2);
    }
    frame_->pts = pts_90k;
    frame_->pict_type = force_keyframe ? AV_PICTURE_TYPE_I : AV_PICTURE_TYPE_NONE;
    if (force_keyframe) frame_->flags |= AV_FRAME_FLAG_KEY;
    else frame_->flags &= ~AV_FRAME_FLAG_KEY;

    av_frame_remove_side_data(frame_, AV_FRAME_DATA_MASTERING_DISPLAY_METADATA);
    av_frame_remove_side_data(frame_, AV_FRAME_DATA_CONTENT_LIGHT_LEVEL);
    av_frame_remove_side_data(frame_, AV_FRAME_DATA_DYNAMIC_HDR_PLUS);
#if defined(AV_FRAME_DATA_ITU_T_T35)
    av_frame_remove_side_data(frame_, AV_FRAME_DATA_ITU_T_T35);
#endif
    if (hdr_.enabled) {
        if (auto* md = av_mastering_display_metadata_create_side_data(frame_)) {
            md->has_primaries = 1;
            md->has_luminance = 1;
            const auto [rx, ry, gx, gy, bx, by, wx, wy, max_l, min_l] = hdr_.mastering;
            md->display_primaries[0][0] = av_make_q(rx, 50000);
            md->display_primaries[0][1] = av_make_q(ry, 50000);
            md->display_primaries[1][0] = av_make_q(gx, 50000);
            md->display_primaries[1][1] = av_make_q(gy, 50000);
            md->display_primaries[2][0] = av_make_q(bx, 50000);
            md->display_primaries[2][1] = av_make_q(by, 50000);
            md->white_point[0] = av_make_q(wx, 50000);
            md->white_point[1] = av_make_q(wy, 50000);
            md->max_luminance = av_make_q(max_l, 10000);
            md->min_luminance = av_make_q(min_l, 10000);
        }
        if (auto* cll = av_content_light_metadata_create_side_data(frame_)) {
            cll->MaxCLL = hdr_.cll.first;
            cll->MaxFALL = hdr_.cll.second;
        }
    }
    if (!hdr10plus_payload.empty()) {
        if (AVBufferRef* buf = av_buffer_alloc(hdr10plus_payload.size())) {
            std::memcpy(buf->data, hdr10plus_payload.data(), hdr10plus_payload.size());
            if (!av_frame_new_side_data_from_buf(frame_, AV_FRAME_DATA_DYNAMIC_HDR_PLUS, buf)) av_buffer_unref(&buf);
        }
    }
    if (const int rc = avcodec_send_frame(ctx_, frame_); rc < 0) {
        return err(ErrorCode::Internal, "avcodec_send_frame failed", std::to_string(rc));
    }
    return drainPackets();
}

Result<std::vector<Packet>> CpuVideoEncoder::flush() {
    if (!ctx_) return std::vector<Packet>{};
    if (const int rc = avcodec_send_frame(ctx_, nullptr); rc < 0 && rc != AVERROR_EOF) {
        return err(ErrorCode::Internal, "avcodec_send_frame(flush) failed", std::to_string(rc));
    }
    return drainPackets();
}

}  // namespace rtx
