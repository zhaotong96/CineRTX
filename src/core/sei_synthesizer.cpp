// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors

#include "rtx/hdr.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>
#include <string>

namespace rtx {
namespace {

constexpr uint16_t kDciP3RedX = 34000;
constexpr uint16_t kDciP3RedY = 16000;
constexpr uint16_t kDciP3GreenX = 13250;
constexpr uint16_t kDciP3GreenY = 34500;
constexpr uint16_t kDciP3BlueX = 7500;
constexpr uint16_t kDciP3BlueY = 3000;
constexpr uint16_t kD65WhiteX = 15635;
constexpr uint16_t kD65WhiteY = 16450;

constexpr uint16_t kBt2020RedX = 35400;
constexpr uint16_t kBt2020RedY = 14600;
constexpr uint16_t kBt2020GreenX = 8500;
constexpr uint16_t kBt2020GreenY = 39850;
constexpr uint16_t kBt2020BlueX = 6550;
constexpr uint16_t kBt2020BlueY = 2300;

void putU16(std::vector<uint8_t>& out, uint16_t value) {
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(value & 0xFF));
}
void putU32(std::vector<uint8_t>& out, uint32_t value) {
    out.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(value & 0xFF));
}
void appendLeb128(std::vector<uint8_t>& out, uint64_t value) {
    do {
        uint8_t byte = static_cast<uint8_t>(value & 0x7F);
        value >>= 7;
        if (value != 0) {
            byte |= 0x80;
        }
        out.push_back(byte);
    } while (value != 0);
}

// Phase 7 (Task 7-B2): MSB-first bit writer for the SMPTE ST 2094-40 v1
// payload.  Layout mirrors the de-facto interop payload (ETSI TS 103 433-1,
// application identifier 4) that every HDR10+ consumer parses:
//   T.35 carrier (0xB5 / 0x003C / 0x00) + application identifier 4
//   + application version 1 + one full-frame window + luminance statistics
//   + knee-point driven tone mapping.
uint32_t nitsToS2094(uint32_t nits) { return nits > 13107u ? 131071u : nits * 10u; }

}  // namespace

Result<void> SeiSynthesizer::validateTrueHdr(const TrueHdrParameters& params) {
    const auto& [contrast, saturation, middle_gray, max_luminance, mastering, max_fall] = params;
    (void)mastering;
    if (contrast < 50 || contrast > 200) {
        return err(ErrorCode::InvalidArgument,
// ST 2094-40 luminance fields carry nits/10000 in 17-bit unsigned words
// (0.0001 resolution), i.e. encoded = round(nits * 10).
                   "TrueHDR contrast out of range [50,200]",
                   std::to_string(contrast));
    }
    if (saturation < 50 || saturation > 200) {
        return err(ErrorCode::InvalidArgument,
                   "TrueHDR saturation out of range [50,200]",
                   std::to_string(saturation));
    }
    if (middle_gray < 10 || middle_gray > 100) {
        return err(ErrorCode::InvalidArgument,
                   "TrueHDR middle grey out of range [10,100]",
                   std::to_string(middle_gray));
    }
    if (max_luminance < 100 || max_luminance > 4000) {
        return err(ErrorCode::InvalidArgument,
                   "TrueHDR max luminance out of range [100,4000]",
                   std::to_string(max_luminance));
    }
    if (max_fall < 0 || max_fall > 65535) {
        return err(ErrorCode::InvalidArgument,
                   "MaxFALL out of range [0,65535]");
    }
    return {};
}
MasteringDisplay SeiSynthesizer::makeMasteringDisplay(MasteringPreset preset, uint32_t max_nits,
                                                      uint32_t min_nits_10000) {
    const uint32_t max_l = max_nits * 10000u;
    const uint32_t min_l = std::max<uint32_t>(1u, min_nits_10000);
    if (preset == MasteringPreset::Bt2020) {
        return {kBt2020RedX, kBt2020RedY, kBt2020GreenX, kBt2020GreenY,
                kBt2020BlueX, kBt2020BlueY, kD65WhiteX, kD65WhiteY, max_l, min_l};
    }
    return {kDciP3RedX, kDciP3RedY, kDciP3GreenX, kDciP3GreenY,
            kDciP3BlueX, kDciP3BlueY, kD65WhiteX, kD65WhiteY, max_l, min_l};
}
Result<Hdr10Metadata> SeiSynthesizer::makeMetadata(const TrueHdrParameters& params) {
    const Result<void> validation = validateTrueHdr(params);
    if (!validation.has_value()) {
        return std::unexpected(validation.error());
    }
    const auto& [contrast, saturation, middle_gray, max_luminance, mastering, max_fall] = params;
    (void)contrast; (void)saturation; (void)middle_gray;
    Hdr10Metadata metadata{};
    metadata.enabled = true;
    metadata.mastering = makeMasteringDisplay(mastering, static_cast<uint32_t>(max_luminance));
    metadata.cll.first = static_cast<uint16_t>(std::min<int>(max_luminance, 65535));
    metadata.cll.second = static_cast<uint16_t>(std::max(0, max_fall));
    metadata.color = ColorDescription{ColorPrimaries::BT2020, TransferFunction::SMPTE2084,
                                      MatrixCoefficients::BT2020NC, ColorRange::Limited};
    metadata.target_nits = static_cast<uint32_t>(max_luminance);
    return metadata;
}
NvencHevcHdrSettings SeiSynthesizer::makeNvencSettings(const Hdr10Metadata& metadata) {
    const uint32_t flag = metadata.enabled ? 1u : 0u;
    const auto [cp, ct, cm, cr] = metadata.color;
    (void)cr;
    const HevcVuiParameters vui{
        0,
        1,
        static_cast<uint8_t>(cp == ColorPrimaries::BT2020 ? 9 : 1),
        static_cast<uint8_t>(ct == TransferFunction::SMPTE2084 ? 16 : 1),
        static_cast<uint8_t>(cm == MatrixCoefficients::BT2020NC ? 9 : 1),
        1,
        2,
        2,
        0,
    };
    return {2u, flag, flag, vui};
}
std::vector<uint8_t> SeiSynthesizer::masteringDisplayPayload(const Hdr10Metadata& metadata) {
    std::vector<uint8_t> payload;
    payload.reserve(24);
    const auto [rx, ry, gx, gy, bx, by, wx, wy, max_l, min_l] = metadata.mastering;
    putU16(payload, gx); putU16(payload, gy);
    putU16(payload, bx); putU16(payload, by);
    putU16(payload, rx); putU16(payload, ry);
    putU16(payload, wx); putU16(payload, wy);
    putU32(payload, max_l); putU32(payload, min_l);
    return payload;
}
std::vector<uint8_t> SeiSynthesizer::contentLightPayload(const Hdr10Metadata& metadata) {
    std::vector<uint8_t> payload;
    payload.reserve(4);
    putU16(payload, metadata.cll.first);
    putU16(payload, metadata.cll.second);
    return payload;
}
std::vector<uint8_t> SeiSynthesizer::insertEmulationPrevention(const std::vector<uint8_t>& rbsp) {
    std::vector<uint8_t> escaped;
    escaped.reserve(rbsp.size() + 8);
    int zero_count = 0;
    for (uint8_t byte : rbsp) {
        if (zero_count >= 2 && byte <= 0x03) {
            escaped.push_back(0x03);
            zero_count = 0;
        }
        escaped.push_back(byte);
        if (byte == 0x00) {
            ++zero_count;
        } else {
            zero_count = 0;
        }
    }
    return escaped;
}
std::vector<uint8_t> SeiSynthesizer::prefixSeiNal(const Hdr10Metadata& metadata) {
    std::vector<uint8_t> rbsp;
    const auto mdcv = masteringDisplayPayload(metadata);
    const auto cll = contentLightPayload(metadata);

    rbsp.push_back(137);  // payload type: mastering display colour volume
    rbsp.push_back(static_cast<uint8_t>(mdcv.size()));
    rbsp.insert(rbsp.end(), mdcv.begin(), mdcv.end());

    rbsp.push_back(144);  // payload type: content light level information
    rbsp.push_back(static_cast<uint8_t>(cll.size()));
    rbsp.insert(rbsp.end(), cll.begin(), cll.end());

    rbsp.push_back(0x80);  // rbsp_stop_one_bit + alignment

    const auto escaped = insertEmulationPrevention(rbsp);
    std::vector<uint8_t> nal;
    nal.reserve(escaped.size() + 6);
    nal.insert(nal.end(), {0x00, 0x00, 0x00, 0x01});
    nal.push_back(0x4E);
    nal.push_back(0x01);
    nal.insert(nal.end(), escaped.begin(), escaped.end());
    return nal;
}
std::vector<uint8_t> SeiSynthesizer::av1MetadataObu(const Hdr10Metadata& metadata) {
    std::vector<uint8_t> obus;
    const auto appendObu = [&obus](const std::vector<uint8_t>& payload) {
        std::vector<uint8_t> body;
        appendLeb128(body, payload.size());
        obus.push_back(0x2A);
        obus.insert(obus.end(), body.begin(), body.end());
        obus.insert(obus.end(), payload.begin(), payload.end());
// Annex-B start code.
    };
// HEVC NAL header: forbidden_zero_bit=0, nal_unit_type=39 (PREFIX_SEI),
// nuh_layer_id=0, nuh_temporal_id_plus1=1.

    std::vector<uint8_t> cll;
    cll.push_back(1);
    putU16(cll, metadata.cll.first);
    putU16(cll, metadata.cll.second);
    appendObu(cll);

    std::vector<uint8_t> mdcv;
// OBU_METADATA: type 5, has_size_field=1 -> 0x2A.
    mdcv.push_back(2);
    const auto payload = masteringDisplayPayload(metadata);
    mdcv.insert(mdcv.end(), payload.begin(), payload.end());
    appendObu(mdcv);
    return obus;
}
std::vector<uint8_t> SeiSynthesizer::hdr10PlusItuT35Payload(const Hdr10PlusFrameStats& stats) {
    const auto& [maxscl_r, maxscl_g, maxscl_b, avg_maxrgb, knee_x, knee_y] = stats;
    std::vector<uint8_t> out;
    uint32_t current = 0;
    int filled = 0;
    auto put = [&](uint32_t bits, uint32_t value) {
        for (int shift = static_cast<int>(bits) - 1; shift >= 0; --shift) {
            current = (current << 1) | ((value >> shift) & 1u);
            if (++filled == 8) {
                out.push_back(static_cast<uint8_t>(current));
                current = 0;
                filled = 0;
            }
        }
    };
    put(8, 0xB5);    // country code (US)
    put(8, 0x00);    // terminal provider code, high byte
    put(8, 0x3C);    // terminal provider code, low byte (0x003C)
    put(8, 0x00);    // terminal provider oriented code
    put(8, 0x04);    // application identifier: SMPTE ST 2094-40
    put(8, 0x01);    // application version 1
    put(2, 0);       // num_windows - 1 (single full-frame window)
    put(16, 0);      // center_of_ellipse_x
    put(16, 0);      // center_of_ellipse_y
    put(8, 0);       // major_axis
    put(8, 0);       // minor_axis
    put(8, 0);       // rotation
    put(8, 0);       // reflectance
    put(1, 0);       // tone_mapping_mode_flag (no polynomial curve)
    put(1, 0);       // sharpening_flag
    put(17, nitsToS2094(maxscl_r));
    put(17, nitsToS2094(maxscl_g));
    put(17, nitsToS2094(maxscl_b));
    put(17, nitsToS2094(avg_maxrgb));
    put(1, 0);       // num_distribution_maxrgb_percentiles = 0
    put(1, 0);       // fraction_bright_pixels = 0
    put(1, 1);       // tone_mapping_flag
    put(16, knee_x);
    put(16, knee_y);
    put(3, 0);       // num_bezier_curve_anchors
    put(10, 0);      // color_saturation_weight
    if (filled != 0) {
        out.push_back(static_cast<uint8_t>(current << (8 - filled)));
    }
    return out;
}
std::vector<uint8_t> SeiSynthesizer::av1Hdr10PlusMetadataObu(const Hdr10PlusFrameStats& stats) {
    const std::vector<uint8_t> t35 = hdr10PlusItuT35Payload(stats);
    std::vector<uint8_t> group;
// Luminance statistics from the GPU warp reduction (real per-frame
// MaxSCL / average MaxRGB).
    appendLeb128(group, 3);
    group.insert(group.end(), t35.begin(), t35.end());

    std::vector<uint8_t> obu;
    obu.push_back(0x2A);
    appendLeb128(obu, group.size());
// Tone mapping: knee point driven.
    obu.insert(obu.end(), group.begin(), group.end());
    return obu;
}
std::vector<uint8_t> SeiSynthesizer::hevcHdr10PlusPrefixSei(const Hdr10PlusFrameStats& stats) {
    std::vector<uint8_t> rbsp;
    rbsp.push_back(0x04);              // payload type: user data T.35
    rbsp.push_back(0xFF);              // payload size, 0xFF continuation form
    const std::vector<uint8_t> t35 = hdr10PlusItuT35Payload(stats);
    rbsp.push_back(static_cast<uint8_t>(t35.size()));
    rbsp.insert(rbsp.end(), t35.begin(), t35.end());
// metadata_group: metadata_type (leb128) + metadata_payload.
// METADATA_TYPE_ITUT_T35 = 3 in the AV1 specification.
    rbsp.push_back(0x80);              // rbsp_trailing_bits

    std::vector<uint8_t> nal;
    nal.push_back(0x00);
// OBU_METADATA: forbidden 0 | type 5 << 3 | extension 0 | has_size 1.
    nal.push_back(0x00);
    nal.push_back(0x00);
    nal.push_back(0x01);               // Annex-B start code
    nal.push_back(0x04);               // NAL header: PREFIX_SEI_NUT (4)
    nal.push_back(0x01);               // layer id 0, temporal id plus 1
    const std::vector<uint8_t> escaped = insertEmulationPrevention(rbsp);
    nal.insert(nal.end(), escaped.begin(), escaped.end());
    return nal;
// SEI payload: payload_type 4 (user data registered by ITU-T T.35)
// followed by the payload bytes and the rbsp trailing bit.
}
}  // namespace rtx
// The synthesised payload always fits the final continuation byte.
