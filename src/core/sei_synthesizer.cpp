// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors

#include "rtx/hdr.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <sstream>

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

void putLe32(std::vector<uint8_t>& out, uint32_t value) {
    out.push_back(static_cast<uint8_t>(value & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
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

int findInt(const std::string& json, const std::string& key, int fallback) {
    const std::string needle = "\"" + key + "\"";
    size_t position = json.find(needle);
    if (position == std::string::npos) {
        return fallback;
    }
    position = json.find(':', position + needle.size());
    if (position == std::string::npos) {
        return fallback;
    }
    ++position;
    while (position < json.size() &&
           (json[position] == ' ' || json[position] == '\t' || json[position] == '\n')) {
        ++position;
    }
    try {
        return std::stoi(json.substr(position));
    } catch (const std::exception&) {
        return fallback;
    }
}

std::string boolString(bool value) { return value ? "true" : "false"; }

}  // namespace

Result<void> TrueHdrParameters::validate() const {
    if (contrast < 50 || contrast > 200) {
        return Result<void>::fail(ErrorCode::InvalidArgument,
                                  "TrueHDR contrast out of range [50,200]",
                                  std::to_string(contrast));
    }
    if (saturation < 50 || saturation > 200) {
        return Result<void>::fail(ErrorCode::InvalidArgument,
                                  "TrueHDR saturation out of range [50,200]",
                                  std::to_string(saturation));
    }
    if (middle_gray < 10 || middle_gray > 100) {
        return Result<void>::fail(ErrorCode::InvalidArgument,
                                  "TrueHDR middle grey out of range [10,100]",
                                  std::to_string(middle_gray));
    }
    if (max_luminance < 100 || max_luminance > 4000) {
        return Result<void>::fail(ErrorCode::InvalidArgument,
                                  "TrueHDR max luminance out of range [100,4000]",
                                  std::to_string(max_luminance));
    }
    if (max_fall < 0 || max_fall > 65535) {
        return Result<void>::fail(ErrorCode::InvalidArgument,
                                  "MaxFALL out of range [0,65535]");
    }
    return {};
}

MasteringDisplay SeiSynthesizer::makeMasteringDisplay(MasteringPreset preset, uint32_t max_nits,
                                                      uint32_t min_nits_10000) {
    MasteringDisplay display{};
    if (preset == MasteringPreset::Bt2020) {
        display.red_x = kBt2020RedX;
        display.red_y = kBt2020RedY;
        display.green_x = kBt2020GreenX;
        display.green_y = kBt2020GreenY;
        display.blue_x = kBt2020BlueX;
        display.blue_y = kBt2020BlueY;
    } else {
        display.red_x = kDciP3RedX;
        display.red_y = kDciP3RedY;
        display.green_x = kDciP3GreenX;
        display.green_y = kDciP3GreenY;
        display.blue_x = kDciP3BlueX;
        display.blue_y = kDciP3BlueY;
    }
    display.white_x = kD65WhiteX;
    display.white_y = kD65WhiteY;
    // units of 0.0001 cd/m^2, matching SMPTE ST 2086 / NVENC.
    display.max_luminance = max_nits * 10000u;
    display.min_luminance = std::max<uint32_t>(1u, min_nits_10000);
    return display;
}

Result<Hdr10Metadata> SeiSynthesizer::makeMetadata(const TrueHdrParameters& params) {
    const Status validation = params.validate();
    if (!validation.ok()) {
        return Result<Hdr10Metadata>::fromStatus(validation);
    }
    Hdr10Metadata metadata{};
    metadata.enabled = true;
    metadata.mastering = makeMasteringDisplay(params.mastering, static_cast<uint32_t>(params.max_luminance));
    metadata.cll.max_cll = static_cast<uint16_t>(
        std::min<int>(params.max_luminance, 65535));
    metadata.cll.max_fall = static_cast<uint16_t>(std::max(0, params.max_fall));
    metadata.color = ColorDescription{ColorPrimaries::BT2020, TransferFunction::SMPTE2084,
                                      MatrixCoefficients::BT2020NC, ColorRange::Limited};
    metadata.target_nits = static_cast<uint32_t>(params.max_luminance);
    return Result<Hdr10Metadata>::ok(metadata);
}

NvencHevcHdrSettings SeiSynthesizer::makeNvencSettings(const Hdr10Metadata& metadata) {
    NvencHevcHdrSettings settings{};
    settings.pixel_bit_depth_minus8 = 2;
    settings.output_mastering_display = metadata.enabled ? 1u : 0u;
    settings.output_max_cll = metadata.enabled ? 1u : 0u;
    settings.vui.colour_description_present_flag = 1;
    settings.vui.colour_primaries =
        static_cast<uint8_t>(metadata.color.primaries == ColorPrimaries::BT2020 ? 9 : 1);
    settings.vui.transfer_characteristics =
        static_cast<uint8_t>(metadata.color.transfer == TransferFunction::SMPTE2084 ? 16 : 1);
    settings.vui.matrix_coefficients =
        static_cast<uint8_t>(metadata.color.matrix == MatrixCoefficients::BT2020NC ? 9 : 1);
    settings.vui.chroma_sample_location_flag = 1;
    settings.vui.chroma_sample_loc_type_top_field = 2;
    settings.vui.chroma_sample_loc_type_bottom_field = 2;
    settings.vui.video_full_range_flag = 0;
    return settings;
}

std::vector<uint8_t> SeiSynthesizer::masteringDisplayPayload(const Hdr10Metadata& metadata) {
    std::vector<uint8_t> payload;
    payload.reserve(24);
    putU16(payload, metadata.mastering.green_x);
    putU16(payload, metadata.mastering.green_y);
    putU16(payload, metadata.mastering.blue_x);
    putU16(payload, metadata.mastering.blue_y);
    putU16(payload, metadata.mastering.red_x);
    putU16(payload, metadata.mastering.red_y);
    putU16(payload, metadata.mastering.white_x);
    putU16(payload, metadata.mastering.white_y);
    putU32(payload, metadata.mastering.max_luminance);
    putU32(payload, metadata.mastering.min_luminance);
    return payload;
}

std::vector<uint8_t> SeiSynthesizer::contentLightPayload(const Hdr10Metadata& metadata) {
    std::vector<uint8_t> payload;
    payload.reserve(4);
    putU16(payload, metadata.cll.max_cll);
    putU16(payload, metadata.cll.max_fall);
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
    // Annex-B start code.
    nal.insert(nal.end(), {0x00, 0x00, 0x00, 0x01});
    // HEVC NAL header: forbidden_zero_bit=0, nal_unit_type=39 (PREFIX_SEI),
    // nuh_layer_id=0, nuh_temporal_id_plus1=1.
    nal.push_back(0x4E);
    nal.push_back(0x01);
    nal.insert(nal.end(), escaped.begin(), escaped.end());
    return nal;
}

std::vector<uint8_t> SeiSynthesizer::av1MetadataObu(const Hdr10Metadata& metadata) {
    std::vector<uint8_t> obus;
    // OBU_METADATA: type 5, has_size_field=1 -> 0x2A.
    const auto appendObu = [&obus](const std::vector<uint8_t>& payload) {
        std::vector<uint8_t> body;
        appendLeb128(body, payload.size());
        obus.push_back(0x2A);
        obus.insert(obus.end(), body.begin(), body.end());
        obus.insert(obus.end(), payload.begin(), payload.end());
    };

    // metadata_type = 1 (HDR_CLL)
    std::vector<uint8_t> cll;
    cll.push_back(1);
    putU16(cll, metadata.cll.max_cll);
    putU16(cll, metadata.cll.max_fall);
    appendObu(cll);

    // metadata_type = 2 (HDR_MDCV)
    std::vector<uint8_t> mdcv;
    mdcv.push_back(2);
    const auto payload = masteringDisplayPayload(metadata);
    mdcv.insert(mdcv.end(), payload.begin(), payload.end());
    appendObu(mdcv);
    return obus;
}

std::vector<uint8_t> SeiSynthesizer::containerSideData(const Hdr10Metadata& metadata) {
    // Private, endian-independent sidecar format:
    //   "RTXH" | u16 version | u16 flags | u32 target nits
    //   8 x u16 chromaticities | u32 max/min luminance | u16 CLL/FALL
    //   u8 primaries/transfer/matrix/range
    std::vector<uint8_t> data;
    data.reserve(40);
    data.insert(data.end(), {'R', 'T', 'X', 'H'});
    putU16(data, 1);
    putU16(data, metadata.enabled ? 1 : 0);
    putLe32(data, metadata.target_nits);
    const uint16_t coordinates[8] = {
        metadata.mastering.red_x,    metadata.mastering.red_y,
        metadata.mastering.green_x,  metadata.mastering.green_y,
        metadata.mastering.blue_x,   metadata.mastering.blue_y,
        metadata.mastering.white_x,  metadata.mastering.white_y,
    };
    for (uint16_t value : coordinates) {
        putU16(data, value);
    }
    putLe32(data, metadata.mastering.max_luminance);
    putLe32(data, metadata.mastering.min_luminance);
    putU16(data, metadata.cll.max_cll);
    putU16(data, metadata.cll.max_fall);
    data.push_back(static_cast<uint8_t>(metadata.color.primaries));
    data.push_back(static_cast<uint8_t>(metadata.color.transfer));
    data.push_back(static_cast<uint8_t>(metadata.color.matrix));
    data.push_back(static_cast<uint8_t>(metadata.color.range));
    return data;
}

std::string SeiSynthesizer::toJson(const Hdr10Metadata& metadata) {
    std::ostringstream out;
    out << "{\n";
    out << "  \"enabled\": " << boolString(metadata.enabled) << ",\n";
    out << "  \"target_nits\": " << metadata.target_nits << ",\n";
    out << "  \"color\": {\n";
    out << "    \"primaries\": " << static_cast<int>(metadata.color.primaries) << ",\n";
    out << "    \"transfer\": " << static_cast<int>(metadata.color.transfer) << ",\n";
    out << "    \"matrix\": " << static_cast<int>(metadata.color.matrix) << ",\n";
    out << "    \"range\": " << static_cast<int>(metadata.color.range) << "\n";
    out << "  },\n";
    out << "  \"mastering_display\": {\n";
    out << "    \"red_x\": " << metadata.mastering.red_x << ",\n";
    out << "    \"red_y\": " << metadata.mastering.red_y << ",\n";
    out << "    \"green_x\": " << metadata.mastering.green_x << ",\n";
    out << "    \"green_y\": " << metadata.mastering.green_y << ",\n";
    out << "    \"blue_x\": " << metadata.mastering.blue_x << ",\n";
    out << "    \"blue_y\": " << metadata.mastering.blue_y << ",\n";
    out << "    \"white_x\": " << metadata.mastering.white_x << ",\n";
    out << "    \"white_y\": " << metadata.mastering.white_y << ",\n";
    out << "    \"max_luminance\": " << metadata.mastering.max_luminance << ",\n";
    out << "    \"min_luminance\": " << metadata.mastering.min_luminance << "\n";
    out << "  },\n";
    out << "  \"content_light_level\": {\n";
    out << "    \"max_cll\": " << metadata.cll.max_cll << ",\n";
    out << "    \"max_fall\": " << metadata.cll.max_fall << "\n";
    out << "  }\n";
    out << "}\n";
    return out.str();
}

Result<Hdr10Metadata> SeiSynthesizer::fromJson(const std::string& json) {
    if (json.find("\"mastering_display\"") == std::string::npos) {
        return Result<Hdr10Metadata>::fail(ErrorCode::InvalidArgument,
                                           "HDR sidecar is missing mastering_display");
    }
    Hdr10Metadata metadata{};
    metadata.enabled = json.find("\"enabled\": true") != std::string::npos;
    metadata.target_nits = static_cast<uint32_t>(findInt(json, "target_nits", 1000));
    metadata.mastering.red_x = static_cast<uint16_t>(findInt(json, "red_x", 34000));
    metadata.mastering.red_y = static_cast<uint16_t>(findInt(json, "red_y", 16000));
    metadata.mastering.green_x = static_cast<uint16_t>(findInt(json, "green_x", 13250));
    metadata.mastering.green_y = static_cast<uint16_t>(findInt(json, "green_y", 34500));
    metadata.mastering.blue_x = static_cast<uint16_t>(findInt(json, "blue_x", 7500));
    metadata.mastering.blue_y = static_cast<uint16_t>(findInt(json, "blue_y", 3000));
    metadata.mastering.white_x = static_cast<uint16_t>(findInt(json, "white_x", 15635));
    metadata.mastering.white_y = static_cast<uint16_t>(findInt(json, "white_y", 16450));
    metadata.mastering.max_luminance =
        static_cast<uint32_t>(findInt(json, "max_luminance", 10000000));
    metadata.mastering.min_luminance =
        static_cast<uint32_t>(findInt(json, "min_luminance", 1));
    metadata.cll.max_cll = static_cast<uint16_t>(findInt(json, "max_cll", 1000));
    metadata.cll.max_fall = static_cast<uint16_t>(findInt(json, "max_fall", 300));
    metadata.color = ColorDescription{
        static_cast<ColorPrimaries>(findInt(json, "primaries", 9)),
        static_cast<TransferFunction>(findInt(json, "transfer", 16)),
        static_cast<MatrixCoefficients>(findInt(json, "matrix", 9)),
        static_cast<ColorRange>(findInt(json, "range", 0)),
    };
    if (metadata.mastering.max_luminance == 0) {
        return Result<Hdr10Metadata>::fail(ErrorCode::InvalidArgument,
                                           "mastering display max luminance is zero");
    }
    return Result<Hdr10Metadata>::ok(metadata);
}

}  // namespace rtx
