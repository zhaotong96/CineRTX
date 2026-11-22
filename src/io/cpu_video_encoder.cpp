// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors

#include "rtx/cpu_video_encoder.h"

#include <algorithm>
#include <cstring>
#include <string>

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
        const __m256i s = _mm256_srli_epi16(v, 6);
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(dst + x), s);
    }
#endif
    for (; x < width; ++x) {
        dst[x] = static_cast<uint16_t>(src[x] >> 6);
    }
}
void deinterleaveP010RowChroma(const uint16_t* src_uv, uint16_t* dst_u, uint16_t* dst_v, int chroma_w) noexcept {
    int x = 0;
#if defined(__AVX2__)
    const __m256i mask = _mm256_setr_epi8(
        0, 1, 4, 5, 8, 9, 12, 13, 2, 3, 6, 7, 10, 11, 14, 15,
        0, 1, 4, 5, 8, 9, 12, 13, 2, 3, 6, 7, 10, 11, 14, 15);
    for (; x + 8 <= chroma_w; x += 8) {
        const __m256i uv = _mm256_srli_epi16(
            _mm256_loadu_si256(reinterpret_cast<const __m256i*>(src_uv + x * 2)), 6);
        const __m256i shuf = _mm256_shuffle_epi8(uv, mask);
        const __m256i perm = _mm256_permute4x64_epi64(shuf, 0xD8);
        const __m128i u_lo = _mm256_castsi256_si128(perm);
        const __m128i v_hi = _mm256_extracti128_si256(perm, 1);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(dst_u + x), u_lo);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(dst_v + x), v_hi);
    }
#endif
    for (; x < chroma_w; ++x) {
        dst_u[x] = static_cast<uint16_t>(src_uv[x * 2] >> 6);
        dst_v[x] = static_cast<uint16_t>(src_uv[x * 2 + 1] >> 6);
    }
}
const char* codecEncoderName(VideoCodec codec) noexcept {
    switch (codec) {
        case VideoCodec::AV1:  return "libsvtav1";
        case VideoCodec::HEVC: return "libx265";
        case VideoCodec::H264: return "libx264";
        default:               return nullptr;
    }
}
} // namespace

// @rtx-type-justification
// Category: NonTrivial-StateMachine
// Why-Std-Fails: 协调多资源异步生命周期的宿主状态机：std::unique_ptr/std::jthread/std::tuple 无法表达其声明序即析构逆序、线程亲和与受控降级路径的交叉不变量。
// Invariant: 成员声明序即依赖序（先声明者后析构）；状态迁移仅在持锁线程或属主线程发生。
struct CpuVideoEncoder::Impl {
    CpuEncoderConfig cfg{};
    AVCodecContext* ctx = nullptr;
    AVFrame* frame = nullptr;
    AVPacket* pkt = nullptr;
    std::vector<uint8_t> extradata_buf;

    ~Impl() {
        if (pkt != nullptr) av_packet_free(&pkt);
        if (frame != nullptr) av_frame_free(&frame);
        if (ctx != nullptr) avcodec_free_context(&ctx);
    }
    Result<std::vector<Packet>> drainPackets() {
        std::vector<Packet> out;
        for (;;) {
            const int rc = avcodec_receive_packet(ctx, pkt);
            if (rc == AVERROR(EAGAIN) || rc == AVERROR_EOF) break;
            if (rc < 0) return err(ErrorCode::Internal, "avcodec_receive_packet failed", std::to_string(rc));
            Packet p;
            p.pts = pkt->pts;
            p.dts = pkt->dts;
            p.duration = pkt->duration;
            p.time_base = Rational{1, 90000};
            p.keyframe = (pkt->flags & AV_PKT_FLAG_KEY) != 0;
            p.data.assign(pkt->data, pkt->data + pkt->size);
            out.push_back(std::move(p));
            av_packet_unref(pkt);
        }
        return out;
    }
};

CpuVideoEncoder::~CpuVideoEncoder() noexcept = default;
CpuVideoEncoder::CpuVideoEncoder(CpuVideoEncoder&&) noexcept = default;
CpuVideoEncoder& CpuVideoEncoder::operator=(CpuVideoEncoder&&) noexcept = default;

Result<CpuVideoEncoder> CpuVideoEncoder::create(const CpuEncoderConfig& cfg) {
    if (cfg.width <= 0 || cfg.height <= 0 || (cfg.width & 1) != 0 || (cfg.height & 1) != 0) {
        return err(ErrorCode::InvalidArgument, "CpuVideoEncoder requires positive even dimensions");
    }
    const char* name = codecEncoderName(cfg.codec);
    if (name == nullptr) {
        return err(ErrorCode::Unsupported, "Unsupported CPU video codec");
    }
    const AVCodec* codec = avcodec_find_encoder_by_name(name);
    if (codec == nullptr) {
        return err(ErrorCode::Unsupported, std::string("FFmpeg static build lacks encoder: ") + name);
    }
    auto impl = std::make_unique<Impl>();
    impl->cfg = cfg;
    impl->ctx = avcodec_alloc_context3(codec);
    impl->frame = av_frame_alloc();
    impl->pkt = av_packet_alloc();
    if (!impl->ctx || !impl->frame || !impl->pkt) {
        return err(ErrorCode::OutOfMemory, "Failed to allocate FFmpeg CPU encoder context");
    }
    AVCodecContext* c = impl->ctx;
    c->width = cfg.width;
    c->height = cfg.height;
    c->time_base = AVRational{1, 90000};
    c->framerate = AVRational{static_cast<int>(cfg.frame_rate.num), static_cast<int>(cfg.frame_rate.den)};
    c->pix_fmt = (cfg.codec == VideoCodec::H264) ? AV_PIX_FMT_YUV420P : AV_PIX_FMT_YUV420P10LE;
    c->thread_count = cfg.threads;
    c->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    if (cfg.hdr.enabled) {
        c->color_primaries = AVCOL_PRI_BT2020;
        c->color_trc = AVCOL_TRC_SMPTE2084;
        c->colorspace = AVCOL_SPC_BT2020_NCL;
        c->color_range = (cfg.hdr.color.range == ColorRange::Full) ? AVCOL_RANGE_JPEG : AVCOL_RANGE_MPEG;
    }
    const int crf = std::clamp(cfg.crf, 0, 51);
    av_opt_set_int(c->priv_data, "crf", crf, 0);
    if (cfg.codec == VideoCodec::AV1) {
        av_opt_set_int(c->priv_data, "preset", std::clamp(cfg.preset, 1, 12), 0);
    } else if (cfg.codec == VideoCodec::HEVC) {
        static constexpr const char* kX265Presets[] = {
            "ultrafast", "superfast", "veryfast", "faster", "fast",
            "medium", "slow", "slower", "veryslow", "placebo"
        };
        av_opt_set(c->priv_data, "preset", kX265Presets[std::clamp(cfg.preset, 0, 9)], 0);
    }
    const int open_rc = avcodec_open2(c, codec, nullptr);
    if (open_rc < 0) {
        return err(ErrorCode::Internal, "avcodec_open2 failed for CPU CRF encoder", std::to_string(open_rc));
    }
    if (c->extradata != nullptr && c->extradata_size > 0) {
        impl->extradata_buf.assign(c->extradata, c->extradata + c->extradata_size);
    }
    impl->frame->format = c->pix_fmt;
    impl->frame->width = c->width;
    impl->frame->height = c->height;
    if (av_frame_get_buffer(impl->frame, 32) < 0) {
        return err(ErrorCode::OutOfMemory, "av_frame_get_buffer failed");
    }
    CpuVideoEncoder enc;
    enc.impl_ = std::move(impl);
    return enc;
}
Result<std::vector<Packet>> CpuVideoEncoder::encodeP010Frame(
    const uint16_t* y_plane, size_t y_pitch_bytes,
    const uint16_t* uv_plane, size_t uv_pitch_bytes,
    int64_t pts_90k, bool force_keyframe,
    std::span<const uint8_t> hdr10plus_payload) {
    if (!impl_ || !impl_->ctx) return err(ErrorCode::State, "CpuVideoEncoder not initialized");
    if (y_plane == nullptr || uv_plane == nullptr) return err(ErrorCode::InvalidArgument, "Null P010 plane");

    if (av_frame_make_writable(impl_->frame) < 0) {
        return err(ErrorCode::Internal, "av_frame_make_writable failed");
    }
    const int w = impl_->cfg.width;
    const int h = impl_->cfg.height;
    const size_t y_stride_u16 = y_pitch_bytes / sizeof(uint16_t);
    const size_t uv_stride_u16 = uv_pitch_bytes / sizeof(uint16_t);

    for (int y = 0; y < h; ++y) {
        convertP010RowLuma(
            y_plane + static_cast<size_t>(y) * y_stride_u16,
            reinterpret_cast<uint16_t*>(impl_->frame->data[0] + y * impl_->frame->linesize[0]),
            w);
    }
    for (int y = 0; y < h / 2; ++y) {
        deinterleaveP010RowChroma(
            uv_plane + static_cast<size_t>(y) * uv_stride_u16,
            reinterpret_cast<uint16_t*>(impl_->frame->data[1] + y * impl_->frame->linesize[1]),
            reinterpret_cast<uint16_t*>(impl_->frame->data[2] + y * impl_->frame->linesize[2]),
            w / 2);
    }
    impl_->frame->pts = pts_90k;
    impl_->frame->pict_type = force_keyframe ? AV_PICTURE_TYPE_I : AV_PICTURE_TYPE_NONE;
    if (force_keyframe) impl_->frame->flags |= AV_FRAME_FLAG_KEY;
    else impl_->frame->flags &= ~AV_FRAME_FLAG_KEY;

    av_frame_remove_side_data(impl_->frame, AV_FRAME_DATA_MASTERING_DISPLAY_METADATA);
    av_frame_remove_side_data(impl_->frame, AV_FRAME_DATA_CONTENT_LIGHT_LEVEL);
    av_frame_remove_side_data(impl_->frame, AV_FRAME_DATA_DYNAMIC_HDR_PLUS);
#if defined(AV_FRAME_DATA_ITU_T_T35)
    av_frame_remove_side_data(impl_->frame, AV_FRAME_DATA_ITU_T_T35);
#endif

    if (impl_->cfg.hdr.enabled) {
        if (auto* md = av_mastering_display_metadata_create_side_data(impl_->frame)) {
            md->has_primaries = 1;
            md->has_luminance = 1;
            md->display_primaries[0][0] = av_make_q(impl_->cfg.hdr.mastering.red_x, 50000);
            md->display_primaries[0][1] = av_make_q(impl_->cfg.hdr.mastering.red_y, 50000);
            md->display_primaries[1][0] = av_make_q(impl_->cfg.hdr.mastering.green_x, 50000);
            md->display_primaries[1][1] = av_make_q(impl_->cfg.hdr.mastering.green_y, 50000);
            md->display_primaries[2][0] = av_make_q(impl_->cfg.hdr.mastering.blue_x, 50000);
            md->display_primaries[2][1] = av_make_q(impl_->cfg.hdr.mastering.blue_y, 50000);
            md->white_point[0] = av_make_q(impl_->cfg.hdr.mastering.white_x, 50000);
            md->white_point[1] = av_make_q(impl_->cfg.hdr.mastering.white_y, 50000);
            md->max_luminance = av_make_q(impl_->cfg.hdr.mastering.max_luminance, 10000);
            md->min_luminance = av_make_q(impl_->cfg.hdr.mastering.min_luminance, 10000);
        }
        if (auto* cll = av_content_light_metadata_create_side_data(impl_->frame)) {
            cll->MaxCLL = impl_->cfg.hdr.cll.max_cll;
            cll->MaxFALL = impl_->cfg.hdr.cll.max_fall;
        }
    }
    if (!hdr10plus_payload.empty()) {
        AVBufferRef* buf = av_buffer_alloc(hdr10plus_payload.size());
        if (buf) {
            std::memcpy(buf->data, hdr10plus_payload.data(), hdr10plus_payload.size());
            if (!av_frame_new_side_data_from_buf(impl_->frame, AV_FRAME_DATA_DYNAMIC_HDR_PLUS, buf)) {
                av_buffer_unref(&buf);
            }
        }
    }
    const int rc = avcodec_send_frame(impl_->ctx, impl_->frame);
    if (rc < 0) return err(ErrorCode::Internal, "avcodec_send_frame failed", std::to_string(rc));
    return impl_->drainPackets();
}
Result<std::vector<Packet>> CpuVideoEncoder::flush() {
    if (!impl_ || !impl_->ctx) return std::vector<Packet>{};
    const int rc = avcodec_send_frame(impl_->ctx, nullptr);
    if (rc < 0 && rc != AVERROR_EOF) {
        return err(ErrorCode::Internal, "avcodec_send_frame(flush) failed", std::to_string(rc));
    }
    return impl_->drainPackets();
}
std::span<const uint8_t> CpuVideoEncoder::extradata() const noexcept {
    return impl_ ? std::span<const uint8_t>(impl_->extradata_buf) : std::span<const uint8_t>{};
}
} // namespace rtx
