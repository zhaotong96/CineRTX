// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors

#include "rtx/color.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace rtx {
namespace {

constexpr double kKrBt2020 = 0.2627;
constexpr double kKbBt2020 = 0.0593;
constexpr double kKgBt2020 = 1.0 - kKrBt2020 - kKbBt2020;

inline uint16_t clamp10(int value) {
    return static_cast<uint16_t>(std::clamp(value, 0, 1023));
}

inline int quantizeLimited(double normalized, int offset, int range) {
    return offset + static_cast<int>(std::lround(std::clamp(normalized, 0.0, 1.0) * range));
}

inline int quantizeChroma(double normalized, int offset, int range) {
    return offset + static_cast<int>(std::lround(std::clamp(normalized, -0.5, 0.5) * range));
}

template <typename T>
T bilinearSample(const T* source, int source_width, int source_height, int pitch, double x,
                 double y) {
    x = std::clamp(x, 0.0, static_cast<double>(source_width - 1));
    y = std::clamp(y, 0.0, static_cast<double>(source_height - 1));
    const int x0 = static_cast<int>(std::floor(x));
    const int y0 = static_cast<int>(std::floor(y));
    const int x1 = std::min(x0 + 1, source_width - 1);
    const int y1 = std::min(y0 + 1, source_height - 1);
    const double fx = x - x0;
    const double fy = y - y0;
    const double top = source[y0 * pitch + x0] * (1.0 - fx) + source[y0 * pitch + x1] * fx;
    const double bottom = source[y1 * pitch + x0] * (1.0 - fx) + source[y1 * pitch + x1] * fx;
    return static_cast<T>(std::lround(top * (1.0 - fy) + bottom * fy));
}

}  // namespace

float halfToFloat(uint16_t value) {
    const uint32_t sign = static_cast<uint32_t>(value & 0x8000u) << 16;
    uint32_t exponent = (value >> 10) & 0x1Fu;
    uint32_t mantissa = value & 0x3FFu;
    uint32_t bits = 0;
    if (exponent == 0) {
        if (mantissa == 0) {
            bits = sign;
        } else {
            exponent = 127 - 15 + 1;
            while ((mantissa & 0x400u) == 0) {
                mantissa <<= 1;
                --exponent;
            }
            mantissa &= 0x3FFu;
            bits = sign | (exponent << 23) | (mantissa << 13);
        }
    } else if (exponent == 0x1F) {
        bits = sign | 0x7F800000u | (mantissa << 13);
    } else {
        bits = sign | ((exponent + 127 - 15) << 23) | (mantissa << 13);
    }
    float result = 0.0f;
    std::memcpy(&result, &bits, sizeof(result));
    return result;
}

uint16_t floatToHalf(float value) {
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    const uint32_t sign = (bits >> 16) & 0x8000u;
    const int32_t exponent = static_cast<int32_t>((bits >> 23) & 0xFFu) - 127;
    uint32_t mantissa = bits & 0x7FFFFFu;

    if (exponent > 15) {
        if (exponent == 128 && mantissa != 0) {
            return static_cast<uint16_t>(sign | 0x7E00u | (mantissa >> 13)); // preserve quiet NaN
        }
        return static_cast<uint16_t>(sign | 0x7C00u);  // infinity
    }
    if (exponent < -24) {
        return static_cast<uint16_t>(sign);  // underflow
    }
    if (exponent < -14) {
        mantissa |= 0x800000u;
        const int shift = -exponent - 14;
        const uint32_t half_mantissa = mantissa >> (13 + shift);
        return static_cast<uint16_t>(sign | half_mantissa);
    }
    const uint32_t half_exponent = static_cast<uint32_t>(exponent + 15) << 10;
    const uint32_t half_mantissa = mantissa >> 13;
    const uint32_t round_bit = (mantissa >> 12) & 1u;
    const uint32_t sticky_bits = mantissa & 0xFFFu;
    const uint32_t round_add = (round_bit != 0 && (sticky_bits != 0 || (half_mantissa & 1u) != 0)) ? 1u : 0u;
    return static_cast<uint16_t>(sign | ((half_exponent | half_mantissa) + round_add));
}

double pqEncode(double luminance_nits) {
    const double y = std::clamp(luminance_nits / 10000.0, 0.0, 1.0);
    if (y <= 0.0) {
        return 0.0;
    }
    constexpr double m1 = 2610.0 / 16384.0;
    constexpr double m2 = 2523.0 / 32.0;
    constexpr double c1 = 3424.0 / 4096.0;
    constexpr double c2 = 2413.0 / 128.0;
    constexpr double c3 = 2392.0 / 128.0;
    const double ym = std::pow(y, m1);
    const double numerator = c1 + c2 * ym;
    const double denominator = 1.0 + c3 * ym;
    return std::pow(numerator / denominator, m2);
}

double pqDecode(double signal) {
    const double e = std::clamp(signal, 0.0, 1.0);
    if (e <= 0.0) {
        return 0.0;
    }
    constexpr double m1 = 2610.0 / 16384.0;
    constexpr double m2 = 2523.0 / 32.0;
    constexpr double c1 = 3424.0 / 4096.0;
    constexpr double c2 = 2413.0 / 128.0;
    constexpr double c3 = 2392.0 / 128.0;
    const double em = std::pow(e, 1.0 / m2);
    const double numerator = std::max(em - c1, 0.0);
    const double denominator = c2 - c3 * em;
    if (denominator <= 0.0) {
        return 10000.0;
    }
    return std::pow(numerator / denominator, 1.0 / m1) * 10000.0;
}

double bt709Eotf(double signal) {
    const double v = std::clamp(signal, 0.0, 1.0);
    if (v < 0.081) {
        return v / 4.5;
    }
    return std::pow((v + 0.099) / 1.099, 1.0 / 0.45);
}

double bt709Oetf(double linear) {
    const double v = std::clamp(linear, 0.0, 1.0);
    if (v < 0.018) {
        return 4.5 * v;
    }
    return 1.099 * std::pow(v, 0.45) - 0.099;
}

double srgbEotf(double signal) {
    const double v = std::clamp(signal, 0.0, 1.0);
    if (v <= 0.04045) {
        return v / 12.92;
    }
    return std::pow((v + 0.055) / 1.055, 2.4);
}

double srgbOetf(double linear) {
    const double v = std::clamp(linear, 0.0, 1.0);
    if (v <= 0.0031308) {
        return 12.92 * v;
    }
    return 1.055 * std::pow(v, 1.0 / 2.4) - 0.055;
}

Yuv10 bt2020RgbToYuv10(float r, float g, float b) {
    const double rd = std::clamp(static_cast<double>(r), 0.0, 1.0);
    const double gd = std::clamp(static_cast<double>(g), 0.0, 1.0);
    const double bd = std::clamp(static_cast<double>(b), 0.0, 1.0);
    const double y = kKrBt2020 * rd + kKgBt2020 * gd + kKbBt2020 * bd;
    const double cb = (bd - y) / (2.0 * (1.0 - kKbBt2020));
    const double cr = (rd - y) / (2.0 * (1.0 - kKrBt2020));
    Yuv10 result;
    result.y = clamp10(quantizeLimited(y, 64, 876));
    result.u = clamp10(quantizeChroma(cb, 512, 896));
    result.v = clamp10(quantizeChroma(cr, 512, 896));
    return result;
}

void bt2020Yuv10ToRgb(uint16_t y, uint16_t u, uint16_t v, float& r, float& g, float& b) {
    const double yn = (static_cast<double>(std::min<uint16_t>(y, 1023)) - 64.0) / 876.0;
    const double cb = (static_cast<double>(std::min<uint16_t>(u, 1023)) - 512.0) / 896.0;
    const double cr = (static_cast<double>(std::min<uint16_t>(v, 1023)) - 512.0) / 896.0;
    const double rd = yn + 2.0 * (1.0 - kKrBt2020) * cr;
    const double bd = yn + 2.0 * (1.0 - kKbBt2020) * cb;
    const double gd = (yn - kKrBt2020 * rd - kKbBt2020 * bd) / kKgBt2020;
    r = static_cast<float>(std::clamp(rd, 0.0, 1.0));
    g = static_cast<float>(std::clamp(gd, 0.0, 1.0));
    b = static_cast<float>(std::clamp(bd, 0.0, 1.0));
}

void bt709YuvToRgb(uint16_t y, uint16_t u, uint16_t v, int bit_depth, float& r, float& g,
                   float& b) {
    const double max_value = static_cast<double>((1 << bit_depth) - 1);
    const double scale = static_cast<double>(1 << (bit_depth - 8));
    const double yn = (static_cast<double>(y) - 16.0 * scale) / (219.0 * scale);
    const double cb = (static_cast<double>(u) - 128.0 * scale) / (224.0 * scale);
    const double cr = (static_cast<double>(v) - 128.0 * scale) / (224.0 * scale);
    (void)max_value;
    const double rd = std::clamp(yn + 1.5748 * cr, 0.0, 1.0);
    const double gd = std::clamp(yn - 0.1873 * cb - 0.4681 * cr, 0.0, 1.0);
    const double bd = std::clamp(yn + 1.8556 * cb, 0.0, 1.0);
    r = static_cast<float>(rd);
    g = static_cast<float>(gd);
    b = static_cast<float>(bd);
}

void bt709RgbToYuv(float r, float g, float b, uint16_t& y, uint16_t& u, uint16_t& v,
                   int bit_depth) {
    const double scale = static_cast<double>(1 << (bit_depth - 8));
    const double yn = 0.2126 * std::clamp(static_cast<double>(r), 0.0, 1.0) +
                      0.7152 * std::clamp(static_cast<double>(g), 0.0, 1.0) +
                      0.0722 * std::clamp(static_cast<double>(b), 0.0, 1.0);
    const double cb = (std::clamp(static_cast<double>(b), 0.0, 1.0) - yn) / 1.8556;
    const double cr = (std::clamp(static_cast<double>(r), 0.0, 1.0) - yn) / 1.5748;
    y = static_cast<uint16_t>(std::lround((16.0 + 219.0 * yn) * scale));
    u = static_cast<uint16_t>(std::lround((128.0 + 224.0 * cb) * scale));
    v = static_cast<uint16_t>(std::lround((128.0 + 224.0 * cr) * scale));
}

void rgba16fToP010(const uint16_t* rgba, int width, int height, uint16_t* y_plane,
                   uint16_t* uv_plane, int y_pitch_samples, int uv_pitch_samples,
                   bool input_is_pq) {
    if (rgba == nullptr || y_plane == nullptr || uv_plane == nullptr || width <= 0 || height <= 0) {
        return;
    }
    const int chroma_width = (width + 1) / 2;
    const int chroma_height = (height + 1) / 2;

    std::vector<uint32_t> u_accum(static_cast<size_t>(chroma_width) * chroma_height, 0);
    std::vector<uint32_t> v_accum(static_cast<size_t>(chroma_width) * chroma_height, 0);
    std::vector<uint32_t> count(static_cast<size_t>(chroma_width) * chroma_height, 0);

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const size_t pixel = (static_cast<size_t>(y) * width + x) * 4;
            float r = halfToFloat(rgba[pixel + 0]);
            float g = halfToFloat(rgba[pixel + 1]);
            float b = halfToFloat(rgba[pixel + 2]);
            if (!input_is_pq) {
                r = static_cast<float>(pqEncode(std::max(0.0f, r) * 10000.0f));
                g = static_cast<float>(pqEncode(std::max(0.0f, g) * 10000.0f));
                b = static_cast<float>(pqEncode(std::max(0.0f, b) * 10000.0f));
            }
            const Yuv10 yuv = bt2020RgbToYuv10(r, g, b);
            y_plane[static_cast<size_t>(y) * y_pitch_samples + x] =
                static_cast<uint16_t>(yuv.y << 6);

            const int cx = x / 2;
            const int cy = y / 2;
            const size_t ci = static_cast<size_t>(cy) * chroma_width + cx;
            u_accum[ci] += yuv.u;
            v_accum[ci] += yuv.v;
            count[ci] += 1;
        }
    }

    for (int cy = 0; cy < chroma_height; ++cy) {
        for (int cx = 0; cx < chroma_width; ++cx) {
            const size_t ci = static_cast<size_t>(cy) * chroma_width + cx;
            const uint32_t n = std::max<uint32_t>(1, count[ci]);
            const uint16_t u = static_cast<uint16_t>((u_accum[ci] + n / 2) / n);
            const uint16_t v = static_cast<uint16_t>((v_accum[ci] + n / 2) / n);
            uint16_t* destination = uv_plane + static_cast<size_t>(cy) * uv_pitch_samples +
                                    static_cast<size_t>(cx) * 2;
            destination[0] = static_cast<uint16_t>(u << 6);
            destination[1] = static_cast<uint16_t>(v << 6);
        }
    }
}

void p010ToRgba16f(const uint16_t* y_plane, const uint16_t* uv_plane, int width, int height,
                   int y_pitch_samples, int uv_pitch_samples, uint16_t* rgba) {
    if (y_plane == nullptr || uv_plane == nullptr || rgba == nullptr) {
        return;
    }
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const uint16_t yy = static_cast<uint16_t>(y_plane[static_cast<size_t>(y) * y_pitch_samples + x] >> 6);
            const uint16_t* uv = uv_plane + static_cast<size_t>(y / 2) * uv_pitch_samples +
                                 static_cast<size_t>(x / 2) * 2;
            const uint16_t uu = static_cast<uint16_t>(uv[0] >> 6);
            const uint16_t vv = static_cast<uint16_t>(uv[1] >> 6);
            float r = 0.0f, g = 0.0f, b = 0.0f;
            bt2020Yuv10ToRgb(yy, uu, vv, r, g, b);
            const size_t pixel = (static_cast<size_t>(y) * width + x) * 4;
            rgba[pixel + 0] = floatToHalf(r);
            rgba[pixel + 1] = floatToHalf(g);
            rgba[pixel + 2] = floatToHalf(b);
            rgba[pixel + 3] = floatToHalf(1.0f);
        }
    }
}

void inverseToneMapNv12ToRgba16f(const uint8_t* nv12, int width, int height, int y_pitch,
                                 int uv_pitch, uint16_t* rgba, uint32_t target_nits) {
    if (nv12 == nullptr || rgba == nullptr || width <= 0 || height <= 0) {
        return;
    }
    const uint8_t* y_plane = nv12;
    const uint8_t* uv_plane = nv12 + static_cast<size_t>(y_pitch) * height;
    const double display_scale = std::max(1.0, static_cast<double>(target_nits) / 100.0);

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const uint16_t yy = y_plane[static_cast<size_t>(y) * y_pitch + x];
            const uint8_t* uv = uv_plane + static_cast<size_t>(y / 2) * uv_pitch +
                                static_cast<size_t>(x / 2) * 2;
            float r = 0.0f, g = 0.0f, b = 0.0f;
            bt709YuvToRgb(yy, uv[0], uv[1], 8, r, g, b);

            double lr = bt709Eotf(r);
            double lg = bt709Eotf(g);
            double lb = bt709Eotf(b);

            // Expand SDR diffuse white toward the HDR target with a gentle
            // shoulder so that specular highlights do not clip abruptly.
            const auto expand = [display_scale](double value) {
                const double boosted = value * display_scale;
                return boosted / (1.0 + boosted / (display_scale * 2.0));
            };
            lr = expand(lr);
            lg = expand(lg);
            lb = expand(lb);

            // BT.709 linear -> BT.2020 linear.
            const double r2020 = 0.6274 * lr + 0.3293 * lg + 0.0433 * lb;
            const double g2020 = 0.0691 * lr + 0.9195 * lg + 0.0114 * lb;
            const double b2020 = 0.0164 * lr + 0.0880 * lg + 0.8956 * lb;

            const size_t pixel = (static_cast<size_t>(y) * width + x) * 4;
            rgba[pixel + 0] = floatToHalf(static_cast<float>(pqEncode(r2020 * 10000.0)));
            rgba[pixel + 1] = floatToHalf(static_cast<float>(pqEncode(g2020 * 10000.0)));
            rgba[pixel + 2] = floatToHalf(static_cast<float>(pqEncode(b2020 * 10000.0)));
            rgba[pixel + 3] = floatToHalf(1.0f);
        }
    }
}

void bilinearScaleNv12(const uint8_t* source, int src_width, int src_height, int src_y_pitch,
                       int src_uv_pitch, uint8_t* destination, int dst_width, int dst_height,
                       int dst_y_pitch, int dst_uv_pitch) {
    if (source == nullptr || destination == nullptr || src_width <= 0 || src_height <= 0) {
        return;
    }
    const uint8_t* src_uv = source + static_cast<size_t>(src_y_pitch) * src_height;
    uint8_t* dst_uv = destination + static_cast<size_t>(dst_y_pitch) * dst_height;
    const int src_chroma_w = (src_width + 1) / 2;
    const int src_chroma_h = (src_height + 1) / 2;
    const int dst_chroma_w = (dst_width + 1) / 2;
    const int dst_chroma_h = (dst_height + 1) / 2;

    const double x_scale = static_cast<double>(src_width) / dst_width;
    const double y_scale = static_cast<double>(src_height) / dst_height;
    for (int y = 0; y < dst_height; ++y) {
        const double sy = (y + 0.5) * y_scale - 0.5;
        for (int x = 0; x < dst_width; ++x) {
            const double sx = (x + 0.5) * x_scale - 0.5;
            destination[static_cast<size_t>(y) * dst_y_pitch + x] =
                bilinearSample(source, src_width, src_height, src_y_pitch, sx, sy);
        }
    }

    const double cx_scale = static_cast<double>(src_chroma_w) / dst_chroma_w;
    const double cy_scale = static_cast<double>(src_chroma_h) / dst_chroma_h;
    for (int y = 0; y < dst_chroma_h; ++y) {
        const double sy = (y + 0.5) * cy_scale - 0.5;
        for (int x = 0; x < dst_chroma_w; ++x) {
            const double sx = (x + 0.5) * cx_scale - 0.5;
            uint8_t* destination_uv =
                dst_uv + static_cast<size_t>(y) * dst_uv_pitch + static_cast<size_t>(x) * 2;
            destination_uv[0] = bilinearSample(src_uv, src_chroma_w, src_chroma_h, src_uv_pitch,
                                               sx, sy);
            destination_uv[1] = bilinearSample(src_uv + 1, src_chroma_w, src_chroma_h, src_uv_pitch,
                                               sx, sy);
        }
    }
}

void blendNv12(const uint8_t* a, const uint8_t* b, int width, int height, int y_pitch,
               int uv_pitch, float t, uint8_t* out, int out_y_pitch, int out_uv_pitch) {
    const float blend = std::clamp(t, 0.0f, 1.0f);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const float av = a[static_cast<size_t>(y) * y_pitch + x];
            const float bv = b[static_cast<size_t>(y) * y_pitch + x];
            out[static_cast<size_t>(y) * out_y_pitch + x] =
                static_cast<uint8_t>(std::lround(av + (bv - av) * blend));
        }
    }
    const int chroma_height = (height + 1) / 2;
    const int chroma_width = (width + 1) / 2;
    const uint8_t* auv = a + static_cast<size_t>(y_pitch) * height;
    const uint8_t* buv = b + static_cast<size_t>(y_pitch) * height;
    uint8_t* ouv = out + static_cast<size_t>(out_y_pitch) * height;
    for (int y = 0; y < chroma_height; ++y) {
        for (int x = 0; x < chroma_width * 2; ++x) {
            const float av = auv[static_cast<size_t>(y) * uv_pitch + x];
            const float bv = buv[static_cast<size_t>(y) * uv_pitch + x];
            ouv[static_cast<size_t>(y) * out_uv_pitch + x] =
                static_cast<uint8_t>(std::lround(av + (bv - av) * blend));
        }
    }
}

double psnr(const uint8_t* a, const uint8_t* b, size_t luma_bytes) {
    if (a == nullptr || b == nullptr || luma_bytes == 0) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    double mse = 0.0;
    for (size_t i = 0; i < luma_bytes; ++i) {
        const double difference = static_cast<double>(a[i]) - static_cast<double>(b[i]);
        mse += difference * difference;
    }
    mse /= static_cast<double>(luma_bytes);
    if (mse == 0.0) {
        return std::numeric_limits<double>::infinity();
    }
    return 10.0 * std::log10((255.0 * 255.0) / mse);
}

}  // namespace rtx
