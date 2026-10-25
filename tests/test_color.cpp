// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors

#include "test_framework.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

#include "rtx/color.h"
#include "rtx/types.h"
#include "test_util.h"

using namespace rtx;

namespace {

std::vector<uint16_t> makeRgba16f(int width, int height, float r, float g, float b) {
    std::vector<uint16_t> rgba(static_cast<size_t>(width) * height * 4, 0);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const size_t pixel = (static_cast<size_t>(y) * width + x) * 4;
            rgba[pixel + 0] = floatToHalf(r);
            rgba[pixel + 1] = floatToHalf(g);
            rgba[pixel + 2] = floatToHalf(b);
            rgba[pixel + 3] = floatToHalf(1.0f);
        }
    }
    return rgba;
}

}  // namespace

RTX_TEST(color_pq_transfer_function) {
    RTX_CHECK_NEAR(pqEncode(0.0), 0.0, 1e-12);
    RTX_CHECK_NEAR(pqEncode(10000.0), 1.0, 1e-9);
    RTX_CHECK_NEAR(pqEncode(100.0), 0.5080784215, 1e-6);
    RTX_CHECK_NEAR(pqEncode(1000.0), 0.751827, 1e-5);
    RTX_CHECK_NEAR(pqDecode(0.0), 0.0, 1e-9);
    RTX_CHECK_NEAR(pqDecode(1.0), 10000.0, 0.1);
    for (double nits : {1.0, 10.0, 100.0, 203.0, 1000.0, 4000.0, 9000.0}) {
        RTX_CHECK_NEAR(pqDecode(pqEncode(nits)), nits, nits * 1e-6 + 1e-6);
    }
    RTX_CHECK_NEAR(pqEncode(-5.0), 0.0, 1e-12);
    RTX_CHECK_NEAR(pqEncode(20000.0), 1.0, 1e-12);
    RTX_CHECK_NEAR(pqDecode(2.0), 10000.0, 0.1);
}

RTX_TEST(color_bt709_and_srgb_round_trip) {
    RTX_CHECK_NEAR(bt709Eotf(0.0), 0.0, 1e-12);
    RTX_CHECK_NEAR(bt709Eotf(1.0), 1.0, 1e-9);
    RTX_CHECK_NEAR(bt709Oetf(0.0), 0.0, 1e-12);
    RTX_CHECK_NEAR(bt709Oetf(1.0), 1.0, 1e-9);
    // Avoid the exact piecewise thresholds: the two branches only agree to
    // within the precision of the published constants at those points.
    for (double signal : {0.01, 0.05, 0.1, 0.2, 0.5, 0.9, 1.0}) {
        RTX_CHECK_NEAR(bt709Oetf(bt709Eotf(signal)), signal, 1e-9);
    }
    for (double linear : {0.001, 0.018, 0.1, 0.5, 1.0}) {
        RTX_CHECK_NEAR(bt709Eotf(bt709Oetf(linear)), linear, 1e-9);
    }
    for (double signal : {0.0, 0.02, 0.05, 0.2, 0.5, 1.0}) {
        RTX_CHECK_NEAR(srgbOetf(srgbEotf(signal)), signal, 1e-9);
    }
    for (double linear : {0.0, 0.004, 0.01, 0.5, 1.0}) {
        RTX_CHECK_NEAR(srgbEotf(srgbOetf(linear)), linear, 1e-9);
    }
}

RTX_TEST(color_bt2020_yuv_round_trip) {
    const Yuv10 black = bt2020RgbToYuv10(0.0f, 0.0f, 0.0f);
    RTX_CHECK_EQ(black.y, static_cast<uint16_t>(64));
    RTX_CHECK_EQ(black.u, static_cast<uint16_t>(512));
    RTX_CHECK_EQ(black.v, static_cast<uint16_t>(512));

    const Yuv10 white = bt2020RgbToYuv10(1.0f, 1.0f, 1.0f);
    RTX_CHECK_EQ(white.y, static_cast<uint16_t>(940));
    RTX_CHECK_EQ(white.u, static_cast<uint16_t>(512));
    RTX_CHECK_EQ(white.v, static_cast<uint16_t>(512));

    for (const auto& sample : {std::array<float, 3>{0.1f, 0.2f, 0.3f},
                                std::array<float, 3>{0.8f, 0.1f, 0.05f},
                                std::array<float, 3>{0.0f, 0.6f, 0.9f}}) {
        const Yuv10 yuv = bt2020RgbToYuv10(sample[0], sample[1], sample[2]);
        float r = 0.0f, g = 0.0f, b = 0.0f;
        bt2020Yuv10ToRgb(yuv.y, yuv.u, yuv.v, r, g, b);
        RTX_CHECK_NEAR(r, sample[0], 0.01);
        RTX_CHECK_NEAR(g, sample[1], 0.01);
        RTX_CHECK_NEAR(b, sample[2], 0.01);
    }
}

RTX_TEST(color_bt709_yuv_round_trip) {
    float r = 0.0f, g = 0.0f, b = 0.0f;
    bt709YuvToRgb(16, 128, 128, 8, r, g, b);
    RTX_CHECK_NEAR(r, 0.0, 1e-6);
    RTX_CHECK_NEAR(g, 0.0, 1e-6);
    RTX_CHECK_NEAR(b, 0.0, 1e-6);
    bt709YuvToRgb(235, 128, 128, 8, r, g, b);
    RTX_CHECK_NEAR(r, 1.0, 1e-6);
    RTX_CHECK_NEAR(g, 1.0, 1e-6);
    RTX_CHECK_NEAR(b, 1.0, 1e-6);

    uint16_t y = 0, u = 0, v = 0;
    bt709RgbToYuv(0.0f, 0.0f, 0.0f, y, u, v, 8);
    RTX_CHECK_EQ(y, static_cast<uint16_t>(16));
    RTX_CHECK_EQ(u, static_cast<uint16_t>(128));
    RTX_CHECK_EQ(v, static_cast<uint16_t>(128));
    bt709RgbToYuv(1.0f, 1.0f, 1.0f, y, u, v, 8);
    RTX_CHECK_EQ(y, static_cast<uint16_t>(235));
    RTX_CHECK_EQ(u, static_cast<uint16_t>(128));
    RTX_CHECK_EQ(v, static_cast<uint16_t>(128));

    for (const auto& sample : {std::array<float, 3>{0.2f, 0.4f, 0.6f},
                                std::array<float, 3>{0.9f, 0.3f, 0.1f}}) {
        bt709RgbToYuv(sample[0], sample[1], sample[2], y, u, v, 8);
        bt709YuvToRgb(y, u, v, 8, r, g, b);
        RTX_CHECK_NEAR(r, sample[0], 0.01);
        RTX_CHECK_NEAR(g, sample[1], 0.01);
        RTX_CHECK_NEAR(b, sample[2], 0.01);
    }

    bt709RgbToYuv(0.5f, 0.5f, 0.5f, y, u, v, 10);
    RTX_CHECK_EQ(y, static_cast<uint16_t>((16 + 219 * 0.5) * 4));
    RTX_CHECK_EQ(u, static_cast<uint16_t>(128 * 4));
}

RTX_TEST(color_half_float_conversion) {
    RTX_CHECK_EQ(floatToHalf(0.0f), static_cast<uint16_t>(0x0000));
    RTX_CHECK_EQ(floatToHalf(-0.0f), static_cast<uint16_t>(0x8000));
    RTX_CHECK_EQ(floatToHalf(1.0f), static_cast<uint16_t>(0x3C00));
    RTX_CHECK_EQ(floatToHalf(-2.0f), static_cast<uint16_t>(0xC000));
    RTX_CHECK_EQ(halfToFloat(0x3C00), 1.0f);
    RTX_CHECK_EQ(halfToFloat(0xC000), -2.0f);
    RTX_CHECK_EQ(halfToFloat(0x0000), 0.0f);
    RTX_CHECK(std::isinf(halfToFloat(0x7C00)));
    RTX_CHECK(std::isinf(halfToFloat(0xFC00)));
    RTX_CHECK(std::isnan(halfToFloat(0x7E00)));

    for (float value : {0.5f, 2.0f, 3.5f, -7.25f, 0.1f, 1000.0f}) {
        RTX_CHECK_NEAR(halfToFloat(floatToHalf(value)), value,
                       std::fabs(value) * 1e-3f + 1e-6f);
    }
    RTX_CHECK_EQ(floatToHalf(65504.0f), static_cast<uint16_t>(0x7BFF));
    RTX_CHECK(std::isinf(halfToFloat(floatToHalf(1.0e6f))));
    RTX_CHECK_EQ(floatToHalf(1.0e-9f), static_cast<uint16_t>(0x0000));
}

RTX_TEST(color_rgba16f_to_p010) {
    Frame p010;
    RTX_REQUIRE(p010.allocateHost(8, 8, PixelFormat::P010).ok());
    const auto black = makeRgba16f(8, 8, 0.0f, 0.0f, 0.0f);
    rgba16fToP010(black.data(), 8, 8, reinterpret_cast<uint16_t*>(p010.planes[0]),
                  reinterpret_cast<uint16_t*>(p010.planes[1]), p010.pitches[0] / 2,
                  p010.pitches[1] / 2, true);
    const auto* y_row = reinterpret_cast<const uint16_t*>(p010.planes[0]);
    const auto* uv_row = reinterpret_cast<const uint16_t*>(p010.planes[1]);
    RTX_CHECK_EQ(static_cast<int>(y_row[0] >> 6), 64);
    RTX_CHECK_EQ(static_cast<int>(uv_row[0] >> 6), 512);
    RTX_CHECK_EQ(static_cast<int>(uv_row[1] >> 6), 512);

    const auto white = makeRgba16f(8, 8, 1.0f, 1.0f, 1.0f);
    rgba16fToP010(white.data(), 8, 8, reinterpret_cast<uint16_t*>(p010.planes[0]),
                  reinterpret_cast<uint16_t*>(p010.planes[1]), p010.pitches[0] / 2,
                  p010.pitches[1] / 2, true);
    RTX_CHECK_EQ(static_cast<int>(y_row[7] >> 6), 940);
    RTX_CHECK_EQ(static_cast<int>(y_row[7 * (p010.pitches[0] / 2)] >> 6), 940);

    // Linear-light input is PQ-encoded before the YUV matrix.
    const auto linear_mid = makeRgba16f(8, 8, 0.18f, 0.18f, 0.18f);
    rgba16fToP010(linear_mid.data(), 8, 8, reinterpret_cast<uint16_t*>(p010.planes[0]),
                  reinterpret_cast<uint16_t*>(p010.planes[1]), p010.pitches[0] / 2,
                  p010.pitches[1] / 2, false);
    const int encoded = static_cast<int>(y_row[0] >> 6);
    const int expected = static_cast<int>(std::lround(
        64.0 + 876.0 * std::min(1.0, pqEncode(0.18 * 10000.0))));
    RTX_CHECK_EQ(encoded, expected);
}

RTX_TEST(color_p010_round_trip) {
    const int width = 6;
    const int height = 4;
    const int chroma_width = (width + 1) / 2;
    std::vector<uint16_t> rgba(static_cast<size_t>(width) * height * 4, 0);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const size_t pixel = (static_cast<size_t>(y) * width + x) * 4;
            rgba[pixel + 0] = floatToHalf(0.5f);
            rgba[pixel + 1] = floatToHalf(0.4f);
            rgba[pixel + 2] = floatToHalf(0.25f);
            rgba[pixel + 3] = floatToHalf(1.0f);
        }
    }
    std::vector<uint16_t> y_plane(static_cast<size_t>(width) * height, 0);
    std::vector<uint16_t> uv_plane(static_cast<size_t>(chroma_width) * ((height + 1) / 2) * 2, 0);
    rgba16fToP010(rgba.data(), width, height, y_plane.data(), uv_plane.data(), width,
                  chroma_width * 2, true);

    std::vector<uint16_t> restored(rgba.size(), 0);
    p010ToRgba16f(y_plane.data(), uv_plane.data(), width, height, width, chroma_width * 2,
                  restored.data());
    for (size_t i = 0; i < restored.size(); i += 4) {
        RTX_CHECK_NEAR(halfToFloat(restored[i + 0]), 0.5, 0.02);
        RTX_CHECK_NEAR(halfToFloat(restored[i + 1]), 0.4, 0.02);
        RTX_CHECK_NEAR(halfToFloat(restored[i + 2]), 0.25, 0.02);
        RTX_CHECK_NEAR(halfToFloat(restored[i + 3]), 1.0, 1e-3);
    }
}

RTX_TEST(color_inverse_tone_map) {
    const int width = 4;
    const int height = 4;
    Frame nv12;
    RTX_REQUIRE(nv12.allocateHost(width, height, PixelFormat::NV12).ok());
    test::fillNV12(nv12, 16, 128, 128);
    std::vector<uint16_t> rgba(static_cast<size_t>(width) * height * 4, 0);
    inverseToneMapNv12ToRgba16f(nv12.planes[0], width, height, nv12.pitches[0], nv12.pitches[1],
                                rgba.data(), 1000);
    RTX_CHECK_NEAR(halfToFloat(rgba[0]), 0.0, 1e-6);
    RTX_CHECK_NEAR(halfToFloat(rgba[1]), 0.0, 1e-6);
    RTX_CHECK_NEAR(halfToFloat(rgba[2]), 0.0, 1e-6);

    test::fillNV12(nv12, 235, 128, 128);
    inverseToneMapNv12ToRgba16f(nv12.planes[0], width, height, nv12.pitches[0], nv12.pitches[1],
                                rgba.data(), 1000);
    const float white = halfToFloat(rgba[0]);
    RTX_CHECK(white > 0.5f);
    RTX_CHECK(white <= 1.0f);
    RTX_CHECK_NEAR(halfToFloat(rgba[0]), halfToFloat(rgba[1]), 1e-4);
    RTX_CHECK_NEAR(halfToFloat(rgba[0]), halfToFloat(rgba[2]), 1e-4);

    std::vector<uint16_t> brighter(rgba.size(), 0);
    inverseToneMapNv12ToRgba16f(nv12.planes[0], width, height, nv12.pitches[0], nv12.pitches[1],
                                brighter.data(), 4000);
    RTX_CHECK(halfToFloat(brighter[0]) >= white);
}

RTX_TEST(color_bilinear_scale_nv12) {
    Frame source;
    RTX_REQUIRE(source.allocateHost(2, 2, PixelFormat::NV12).ok());
    const uint8_t values[2][2] = {{10, 20}, {30, 40}};
    for (int y = 0; y < 2; ++y) {
        for (int x = 0; x < 2; ++x) {
            source.planes[0][static_cast<size_t>(y) * source.pitches[0] + x] = values[y][x];
        }
    }
    for (int y = 0; y < 1; ++y) {
        source.planes[1][static_cast<size_t>(y) * source.pitches[1] + 0] = 100;
        source.planes[1][static_cast<size_t>(y) * source.pitches[1] + 1] = 120;
    }

    Frame destination;
    RTX_REQUIRE(destination.allocateHost(4, 4, PixelFormat::NV12).ok());
    bilinearScaleNv12(source.planes[0], 2, 2, source.pitches[0], source.pitches[1],
                      destination.planes[0], 4, 4, destination.pitches[0], destination.pitches[1]);

    const auto at = [&destination](int x, int y) {
        return static_cast<int>(destination.planes[0][static_cast<size_t>(y) * destination.pitches[0] + x]);
    };
    RTX_CHECK_EQ(at(0, 0), 10);
    RTX_CHECK_EQ(at(3, 0), 20);
    RTX_CHECK_EQ(at(0, 3), 30);
    RTX_CHECK_EQ(at(3, 3), 40);
    // Pixel-centre sampling at 2x places (1,1) at source (0.25,0.25) and
    // (2,2) at (0.75,0.75), which round to 18 and 33 respectively.
    RTX_CHECK_EQ(at(1, 1), 18);
    RTX_CHECK_EQ(at(2, 2), 33);
    // Bilinear weights must sum to one, so the frame stays in range.
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            RTX_CHECK(at(x, y) >= 10 && at(x, y) <= 40);
        }
    }

    // A constant image must remain bit-exact through scaling.
    test::fillNV12(source, 77, 66, 99);
    bilinearScaleNv12(source.planes[0], 2, 2, source.pitches[0], source.pitches[1],
                      destination.planes[0], 4, 4, destination.pitches[0], destination.pitches[1]);
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            RTX_CHECK_EQ(at(x, y), 77);
        }
    }
    RTX_CHECK_EQ(destination.planes[1][0], static_cast<uint8_t>(66));
    RTX_CHECK_EQ(destination.planes[1][1], static_cast<uint8_t>(99));
}

RTX_TEST(color_blend_nv12) {
    Frame a;
    Frame b;
    Frame out;
    RTX_REQUIRE(a.allocateHost(6, 4, PixelFormat::NV12).ok());
    RTX_REQUIRE(b.allocateHost(6, 4, PixelFormat::NV12).ok());
    RTX_REQUIRE(out.allocateHost(6, 4, PixelFormat::NV12).ok());
    test::fillNV12(a, 0, 100, 50);
    test::fillNV12(b, 200, 120, 90);
    blendNv12(a.planes[0], b.planes[0], 6, 4, a.pitches[0], a.pitches[1], 0.5f, out.planes[0],
              out.pitches[0], out.pitches[1]);
    RTX_CHECK_EQ(out.planes[0][0], static_cast<uint8_t>(100));
    RTX_CHECK_EQ(out.planes[1][0], static_cast<uint8_t>(110));
    RTX_CHECK_EQ(out.planes[1][1], static_cast<uint8_t>(70));

    blendNv12(a.planes[0], b.planes[0], 6, 4, a.pitches[0], a.pitches[1], 0.0f, out.planes[0],
              out.pitches[0], out.pitches[1]);
    RTX_CHECK_EQ(out.planes[0][0], static_cast<uint8_t>(0));
    RTX_CHECK_EQ(out.planes[1][0], static_cast<uint8_t>(100));
    blendNv12(a.planes[0], b.planes[0], 6, 4, a.pitches[0], a.pitches[1], 1.0f, out.planes[0],
              out.pitches[0], out.pitches[1]);
    RTX_CHECK_EQ(out.planes[0][0], static_cast<uint8_t>(200));
    RTX_CHECK_EQ(out.planes[1][0], static_cast<uint8_t>(120));

    // Out-of-range timesteps are clamped, not extrapolated.
    blendNv12(a.planes[0], b.planes[0], 6, 4, a.pitches[0], a.pitches[1], 2.0f, out.planes[0],
              out.pitches[0], out.pitches[1]);
    RTX_CHECK_EQ(out.planes[0][0], static_cast<uint8_t>(200));
}

RTX_TEST(color_psnr) {
    const uint8_t a[4] = {0, 10, 20, 30};
    const uint8_t b[4] = {0, 10, 20, 30};
    RTX_CHECK(std::isinf(psnr(a, b, 4)));
    const uint8_t shifted[4] = {1, 11, 21, 31};
    RTX_CHECK_NEAR(psnr(a, shifted, 4), 20.0 * std::log10(255.0), 1e-9);
    RTX_CHECK(std::isnan(psnr(nullptr, b, 4)));
    RTX_CHECK(std::isnan(psnr(a, b, 0)));
}
