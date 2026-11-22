// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
//
// Track 11D-2 DirectComposition viewport tone map.
//
// 输入是桥接的 R10G10B10A2 共享预览缓冲（Buffer<uint>，行距 pitchWords 个
// 32-bit 字，源几何随帧动态）。源传递函数语义由 inputIsPq 显式声明：
//   0 = Studio-Swing SDR 显示参考信号（SDR 预览恒直通，根治误按 PQ 解码导致
//       的异常压暗）；
//   1 = PQ ST.2084 BT.2020 信号（remaster/TrueHDR 输出）。
//
// 输出直接写合成交换链后备缓冲的 UAV：HDR 显示器出 scRGB 线性 FP16
// (1.0 == 80 cd/m^2)，SDR 显示器出 ACES Filmic 色调映射后的 sRGB RGBA8。
// 全程 GPU 内部，零主机像素回读，且绝不触碰送往 NVENC 的 4K 表面。
//
// 编译：fxc /T cs_5_1 /E CSMain /Vn g_preview_tone_map_cs /Fh preview_tone_map_cs.h

Buffer<uint>        gPreview : register(t0);
RWTexture2D<float4> gOutput  : register(u0);

cbuffer ViewportConstants : register(b0)
{
    uint2 srcSize;      // 预览源尺寸（随源动态）
    uint2 dstSize;      // 视口像素尺寸
    uint  pitchWords;   // 每行 32-bit 字数
    uint  hdrMode;      // 0 = SDR ACES + sRGB, 1 = scRGB 线性直出
    float split;        // 0..1 水平分割位置
    float maxNits;      // 显示峰值亮度
    uint  splitOn;      // 0 = 只看 remastered
    uint  inputIsPq;    // 0 = Studio-Swing SDR, 1 = PQ ST.2084
};

float3 unpackRgb10(uint word)
{
    return float3(word & 0x3FFu, (word >> 10) & 0x3FFu, (word >> 20) & 0x3FFu) / 1023.0;
}

uint fetchWord(int2 texel)
{
    const int2 clamped = clamp(texel, int2(0, 0), int2(srcSize) - int2(1, 1));
    return gPreview[clamped.y * pitchWords + clamped.x];
}

// 手写双线性：源是 ROW_MAJOR 缓冲而非纹理，没有采样器可用。
float3 samplePreview(float2 uv)
{
    const float2 st = uv * float2(srcSize) - 0.5;
    const int2 base = int2(floor(st));
    const float2 frac = st - float2(base);
    const float3 c00 = unpackRgb10(fetchWord(base));
    const float3 c10 = unpackRgb10(fetchWord(base + int2(1, 0)));
    const float3 c01 = unpackRgb10(fetchWord(base + int2(0, 1)));
    const float3 c11 = unpackRgb10(fetchWord(base + int2(1, 1)));
    return lerp(lerp(c00, c10, frac.x), lerp(c01, c11, frac.x), frac.y);
}

// SMPTE ST 2084 EOTF：PQ 码值 -> 显示线性，1.0 == 10000 nits。
float3 pqToLinear(float3 pq)
{
    const float m1 = 2610.0 / 16384.0;
    const float m2 = 2523.0 / 4096.0 * 128.0;
    const float c1 = 3424.0 / 4096.0;
    const float c2 = 2413.0 / 4096.0 * 32.0;
    const float c3 = 2392.0 / 4096.0 * 32.0;
    const float3 p = pow(max(pq, 0.0), 1.0 / m2);
    return pow(max(p - c1, 0.0) / max(c2 - c3 * p, 1e-6), 1.0 / m1);
}

float3 srgbToLinear(float3 encoded)
{
    const float3 low = encoded / 12.92;
    const float3 high = pow(max((encoded + 0.055) / 1.055, 0.0), 2.4);
    return lerp(low, high, float3(encoded > 0.04045));
}

float3 linearToSrgb(float3 value)
{
    const float3 low = value * 12.92;
    const float3 high = 1.055 * pow(max(value, 1e-6), 1.0 / 2.4) - 0.055;
    return lerp(low, high, float3(value > 0.0031308));
}

float3 bt2020ToBt709(float3 c)
{
    return float3(dot(float3(1.6605, -0.5876, -0.0728), c),
                  dot(float3(-0.1246, 1.1329, -0.0083), c),
                  dot(float3(-0.0182, -0.1006, 1.1187), c));
}

// ACES filmic (Narkowicz fit)，输入为场景线性。
float3 acesFilmic(float3 x)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

[numthreads(16, 16, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId.x >= dstSize.x || dispatchThreadId.y >= dstSize.y)
    {
        return;
    }

    const float2 uv = (float2(dispatchThreadId.xy) + 0.5) / float2(dstSize);
    const float3 code = samplePreview(uv);
    const bool leftHalf = (splitOn != 0) && (uv.x < split);
    // A/B 分屏只在 PQ 源上有意义：SDR 源的"原始侧"与"母版侧"传递函数相同。
    const bool leftIsOriginal = (splitOn != 0) && leftHalf && (inputIsPq != 0);

    float3 color;
    if (inputIsPq == 0)
    {
        // Studio-Swing SDR：预处理核函数已经输出显示参考 RGB，任何显示器都
        // 不做二次色调映射；HDR 屏只做 scRGB 线性升格。
        color = (hdrMode != 0) ? srgbToLinear(code) * (100.0 / 80.0) : code;
    }
    else if (hdrMode != 0)
    {
        // scRGB：线性 BT.709/sRGB 原色，1.0 == 80 nits。
        color = leftIsOriginal ? srgbToLinear(code) * (100.0 / 80.0)
                               : bt2020ToBt709(pqToLinear(code) * 10000.0) / 80.0;
    }
    else if (leftIsOriginal)
    {
        // 原始帧已经是显示参考 sRGB，直接透传，绝不二次色调映射。
        color = code;
    }
    else
    {
        const float3 nits = pqToLinear(code) * 10000.0;
        const float3 scene = bt2020ToBt709(nits / max(maxNits, 1.0));
        color = linearToSrgb(acesFilmic(scene));
    }

    // 分割线本身画成 1 像素强调色，操作员才看得见 A/B 边界。
    if (splitOn != 0 && abs(uv.x - split) * dstSize.x < 1.0)
    {
        color = float3(0.46, 0.72, 0.0);
    }

    gOutput[dispatchThreadId.xy] = float4(color, 1.0);
}