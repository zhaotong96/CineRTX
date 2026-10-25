// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
//
// Color science used by the reference path and mirrored by the CUDA kernels in
// src/shim/ngx_bridge/p010_pack.cu.  Every function is deterministic and has a
// CPU implementation so that the QA gates can run on machines without RTX GPUs.

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "rtx/types.h"

namespace rtx {

// SMPTE ST 2084 (PQ).  pqEncode takes absolute luminance in cd/m^2 and returns
// a normalised signal in [0,1]; pqDecode is the inverse.
double pqEncode(double luminance_nits);
double pqDecode(double signal);

// BT.709 / BT.2020 transfer helpers.
double bt709Eotf(double signal);   // signal -> linear [0,1]
double bt709Oetf(double linear);   // linear -> signal
double srgbEotf(double signal);
double srgbOetf(double linear);

struct Yuv10 {
    uint16_t y = 64;
    uint16_t u = 512;
    uint16_t v = 512;
};

// Non-linear R'G'B' in [0,1] -> 10-bit limited-range BT.2020 NCL.
Yuv10 bt2020RgbToYuv10(float r, float g, float b);
void bt2020Yuv10ToRgb(uint16_t y, uint16_t u, uint16_t v, float& r, float& g, float& b);

// BT.709 limited-range Y'CbCr -> non-linear R'G'B' [0,1].
void bt709YuvToRgb(uint16_t y, uint16_t u, uint16_t v, int bit_depth, float& r, float& g,
                   float& b);
void bt709RgbToYuv(float r, float g, float b, uint16_t& y, uint16_t& u, uint16_t& v,
                   int bit_depth);

// RGBA16F (half floats, 4 channels) -> P010.  When input_is_pq is false the
// RGB values are treated as linear BT.2020 and encoded to PQ with a 10000-nit
// peak.  P010 samples are written with the 10-bit value in the high bits.
void rgba16fToP010(const uint16_t* rgba, int width, int height, uint16_t* y_plane,
                   uint16_t* uv_plane, int y_pitch_samples, int uv_pitch_samples,
                   bool input_is_pq = true);

// P010 -> RGBA16F PQ, used by round-trip tests.
void p010ToRgba16f(const uint16_t* y_plane, const uint16_t* uv_plane, int width, int height,
                   int y_pitch_samples, int uv_pitch_samples, uint16_t* rgba);

// Reference SDR (BT.709 NV12, 8-bit limited) -> HDR10 RGBA16F (BT.2020 PQ).
// This is a deterministic inverse tone mapper used when TrueHDR is absent; it
// is intentionally conservative and does not claim to match the NGX model.
void inverseToneMapNv12ToRgba16f(const uint8_t* nv12, int width, int height, int y_pitch,
                                 int uv_pitch, uint16_t* rgba, uint32_t target_nits);

// Reference spatial upscale of NV12 to the requested size using bilinear
// interpolation with top-left chroma alignment.  Used by the CPU fallback and
// by correctness tests, never on the RTX path.
void bilinearScaleNv12(const uint8_t* source, int src_width, int src_height, int src_y_pitch,
                       int src_uv_pitch, uint8_t* destination, int dst_width, int dst_height,
                       int dst_y_pitch, int dst_uv_pitch);

// Reference FRUC: symmetric blend of two NV12 frames.
void blendNv12(const uint8_t* a, const uint8_t* b, int width, int height, int y_pitch,
               int uv_pitch, float t, uint8_t* out, int out_y_pitch, int out_uv_pitch);

// Half-float conversion helpers.
float halfToFloat(uint16_t value);
uint16_t floatToHalf(float value);

double psnr(const uint8_t* a, const uint8_t* b, size_t luma_bytes);

}  // namespace rtx
