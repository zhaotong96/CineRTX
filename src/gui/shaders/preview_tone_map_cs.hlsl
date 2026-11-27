// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
//
// Phase 13 (Wave 1-B / Wave 2-A): DirectComposition viewport dual-side independent
// color-space EOTF (ST.2084 PQ / HLG / SDR Gamma 2.2) + ITU-R BT.2390 Hermite EETF
// + BT.2020->BT.709 gamut matrix + centered Letterbox/Pillarbox drawRect.
//
// Compile: fxc /T cs_5_1 /E CSMain /Vn g_preview_tone_map_cs /Fh preview_tone_map_cs.h

Buffer<uint>        gPreview : register(t0);
RWTexture2D<float4> gOutput  : register(u0);

cbuffer ViewportConstants : register(b0)
{
    uint2 srcSize;      // dynamic source frame size
    uint2 dstSize;      // physical swapchain pixel size
    uint  pitchWords;   // row stride in 32-bit words (rowBytes / 4)
    uint  hdrMode;      // 0 = SDR BT.2390 + sRGB, 1 = scRGB linear FP16 (1.0 == 80 nits)
    float split;        // 0..1 horizontal split ratio
    float leftMaxNits;  // left-side source peak luminance (nits)
    uint  splitOn;      // 0 = full frame (right side), 1 = A/B split
    uint  colorFlags;   // bit0=LPq bit1=LHlg bit2=L2020 bit3=RPq bit4=RHlg bit5=R2020
    float rightMaxNits; // right-side target peak luminance (nits)
    uint  reserved0;
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

// SMPTE ST.2084 EOTF -> absolute luminance in nits (0..10000)
float3 pqToNits(float3 pq)
{
    const float m1 = 2610.0 / 16384.0;
    const float m2 = 2523.0 / 4096.0 * 128.0;
    const float c1 = 3424.0 / 4096.0;
    const float c2 = 2413.0 / 4096.0 * 32.0;
    const float c3 = 2392.0 / 4096.0 * 32.0;
    const float3 p = pow(max(pq, 0.0), 1.0 / m2);
    return pow(max(p - c1, 0.0) / max(c2 - c3 * p, 1e-6), 1.0 / m1) * 10000.0;
}

// ARIB STD-B67 HLG EOTF -> absolute luminance in nits (1000 nits nominal peak)
float3 hlgToNits(float3 e)
{
    const float a = 0.17883277;
    const float b = 0.28466892;
    const float c = 0.55991073;
    const float3 n = max(e, 0.0);
    const float3 lo = (n * n) / 3.0;
    const float3 hi = (exp((n - c) / a) + b) / 12.0;
    return lerp(lo, hi, float3(n > 0.5)) * 1000.0;
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

// ITU-R BT.2390 EETF cubic Hermite highlight roll-off (nits -> 0..100 SDR nits)
float3 bt2390ToSdrNits(float3 colorNits, float srcPeakNits)
{
    float3 result = min(colorNits, 100.0);
    const float maxLum = max(srcPeakNits, 100.0);
    const float maxRgb = max(colorNits.r, max(colorNits.g, colorNits.b));
    if (maxRgb > 10.0 && maxLum > 100.0)
    {
        const float E = min(maxRgb / maxLum, 1.0);
        const float KS = 10.0 / maxLum;
        float Emapped = E;
        if (E > KS)
        {
            const float T = (E - KS) / max(1.0 - KS, 1e-5);
            const float T2 = T * T;
            const float T3 = T2 * T;
            Emapped = (2.0 * T3 - 3.0 * T2 + 1.0) * KS +
                      (T3 - 2.0 * T2 + T) * (1.0 - KS) +
                      (-2.0 * T3 + 3.0 * T2) * (100.0 / maxLum);
        }
        const float scale = (Emapped * maxLum) / max(maxRgb, 1e-5);
        result = min(colorNits * scale, 100.0);
    }
    return result;
}

[numthreads(16, 16, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId.x >= dstSize.x || dispatchThreadId.y >= dstSize.y)
    {
        return;
    }

    const float srcAspect = float(max(srcSize.x, 1u)) / float(max(srcSize.y, 1u));
    const float dstAspect = float(max(dstSize.x, 1u)) / float(max(dstSize.y, 1u));
    float2 drawSize = float2(dstSize);
    float2 offset = float2(0.0, 0.0);
    if (srcAspect > dstAspect)
    {
        drawSize.y = drawSize.x / srcAspect;
        offset.y = (float(dstSize.y) - drawSize.y) * 0.5;
    }
    else
    {
        drawSize.x = drawSize.y * srcAspect;
        offset.x = (float(dstSize.x) - drawSize.x) * 0.5;
    }
    const float2 p = float2(dispatchThreadId.xy) + 0.5;
    if (p.x < offset.x || p.x >= (offset.x + drawSize.x) ||
        p.y < offset.y || p.y >= (offset.y + drawSize.y))
    {
        gOutput[dispatchThreadId.xy] = float4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    const float2 uv = (p - offset) / drawSize;
    const float3 code = samplePreview(uv);
    const bool isLeft = (splitOn != 0u) && (uv.x < split);

    const bool sidePq     = isLeft ? ((colorFlags & 1u) != 0u)  : ((colorFlags & 8u) != 0u);
    const bool sideHlg    = isLeft ? ((colorFlags & 2u) != 0u)  : ((colorFlags & 16u) != 0u);
    const bool sideBt2020 = isLeft ? ((colorFlags & 4u) != 0u)  : ((colorFlags & 32u) != 0u);
    const float sidePeak  = isLeft ? leftMaxNits : rightMaxNits;

    float3 color;
    if (!sidePq && !sideHlg)
    {
        // Native SDR BT.709: 1:1 passthrough on SDR display (zero grey fog), scRGB lift on HDR
        color = (hdrMode != 0u) ? (srgbToLinear(code) * (100.0 / 80.0)) : code;
    }
    else
    {
        float3 nits = sidePq ? pqToNits(code) : hlgToNits(code);
        if (hdrMode != 0u)
        {
            float3 lin709 = sideBt2020 ? max(bt2020ToBt709(nits), 0.0) : nits;
            color = lin709 / 80.0;
        }
        else
        {
            float3 sdrNits = bt2390ToSdrNits(nits, sidePeak);
            float3 lin = sdrNits / 100.0;
            if (sideBt2020)
            {
                lin = bt2020ToBt709(lin);
            }
            color = linearToSrgb(saturate(lin));
        }
    }

    if (splitOn != 0u)
    {
        const float lineDist = abs(p.x - (offset.x + split * drawSize.x));
        if (lineDist < 1.0)
        {
            color = float3(0.463, 0.725, 0.0); // NVIDIA green split hairline
        }
    }
    gOutput[dispatchThreadId.xy] = float4(color, 1.0);
}
