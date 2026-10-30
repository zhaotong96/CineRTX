// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (c) 2026 RTX Transcode Engine contributors
//
// Split-screen PQ -> sRGB tone map for the DirectComposition live viewport
// (W9-2).
//
// Left of the split: the 1080p original, bilinear, straight BT.709 decode.
// Right of the split: the 4K remastered frame, PQ (SMPTE ST 2084) decoded to
// display-linear, ACES-filmic tone mapped, then encoded to sRGB.
//
// The kernel is dispatched on a D3D12_COMMAND_QUEUE_PRIORITY_LOW queue and is
// budgeted at < 0.15 ms for a 1920x1080 viewport on sm_120.

Texture2D<float4>   gOriginal   : register(t0);  // 1080p BT.709 RGBA8
Texture2D<float4>   gRemastered : register(t1);  // 4K PQ RGBA16F
RWTexture2D<float4> gOutput     : register(u0);  // BGRA8 swap-chain buffer

SamplerState gLinearClamp : register(s0);

cbuffer ViewportConstants : register(b0)
{
    float  split;        // 0..1, horizontal split position
    float  maxNits;      // display peak luminance, e.g. 1000
    float2 outputSize;   // viewport dimensions in pixels
    uint   enabled;      // 0 = show the remastered frame only
};

// SMPTE ST 2084 EOTF: PQ code value -> display-linear (1.0 == 10000 nits).
float3 pqToLinear(float3 pq)
{
    const float m1 = 2610.0 / 16384.0;
    const float m2 = 2523.0 / 4096.0 * 128.0;
    const float c1 = 3424.0 / 4096.0;
    const float c2 = 2413.0 / 4096.0 * 32.0;
    const float c3 = 2392.0 / 4096.0 * 32.0;
    float3 p = pow(max(pq, 0.0), 1.0 / m2);
    return pow(max(p - c1, 0.0) / max(c2 - c3 * p, 1e-6), 1.0 / m1);
}

// ACES filmic (Narkowicz fit) on scene-linear input.
float3 acesFilmic(float3 x)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

float3 linearToSrgb(float3 linear)
{
    const float3 low  = linear * 12.92;
    const float3 high = 1.055 * pow(max(linear, 1e-6), 1.0 / 2.4) - 0.055;
    return select(low, high, linear > 0.0031308);
}

[numthreads(16, 16, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId.x >= (uint)outputSize.x || dispatchThreadId.y >= (uint)outputSize.y)
    {
        return;
    }

    const float2 uv = (float2(dispatchThreadId.xy) + 0.5) / outputSize;

    float3 color;
    if (enabled != 0 && uv.x < split)
    {
        // Original 1080p: BT.709 limited-range decode, no tone map.
        const float3 encoded = gOriginal.SampleLevel(gLinearClamp, uv, 0).rgb;
        color = saturate((encoded - 16.0 / 255.0) * (255.0 / 219.0));
    }
    else
    {
        // Remastered 4K: PQ -> linear -> ACES -> sRGB, normalised to the
        // display peak so a 1000-nit grade does not clip to white.
        const float3 pq = gRemastered.SampleLevel(gLinearClamp, uv, 0).rgb;
        const float3 linear = pqToLinear(pq) * (10000.0 / max(maxNits, 1.0));
        color = linearToSrgb(acesFilmic(linear));
    }

    // The split line itself is a 1-pixel accent so the operator can see it.
    if (enabled != 0 && abs(uv.x - split) * outputSize.x < 1.0)
    {
        color = float3(0.46, 0.72, 0.0);
    }

    gOutput[dispatchThreadId.xy] = float4(color, 1.0);
}
