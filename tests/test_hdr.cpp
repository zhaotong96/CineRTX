// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors

#include "test_framework.h"

#include <cstdint>
#include <string>
#include <vector>

#include "rtx/hdr.h"

using namespace rtx;

namespace {

uint16_t readU16(const std::vector<uint8_t>& data, size_t offset) {
    return static_cast<uint16_t>((static_cast<uint16_t>(data[offset]) << 8) |
                                 static_cast<uint16_t>(data[offset + 1]));
}

uint32_t readU32(const std::vector<uint8_t>& data, size_t offset) {
    return (static_cast<uint32_t>(data[offset]) << 24) |
           (static_cast<uint32_t>(data[offset + 1]) << 16) |
           (static_cast<uint32_t>(data[offset + 2]) << 8) |
           static_cast<uint32_t>(data[offset + 3]);
}

uint32_t readLe32(const std::vector<uint8_t>& data, size_t offset) {
    return static_cast<uint32_t>(data[offset]) |
           (static_cast<uint32_t>(data[offset + 1]) << 8) |
           (static_cast<uint32_t>(data[offset + 2]) << 16) |
           (static_cast<uint32_t>(data[offset + 3]) << 24);
}

std::vector<uint8_t> removeEmulationPrevention(const std::vector<uint8_t>& escaped) {
    std::vector<uint8_t> rbsp;
    int zeros = 0;
    for (size_t i = 0; i < escaped.size(); ++i) {
        if (zeros >= 2 && escaped[i] == 0x03) {
            zeros = 0;
            continue;
        }
        rbsp.push_back(escaped[i]);
        zeros = escaped[i] == 0x00 ? zeros + 1 : 0;
    }
    return rbsp;
}

Hdr10Metadata sampleMetadata() {
    TrueHdrParameters parameters;
    const Result<Hdr10Metadata> metadata = SeiSynthesizer::makeMetadata(parameters);
    return metadata.ok() ? metadata.value() : Hdr10Metadata{};
}

}  // namespace

RTX_TEST(hdr_truehdr_parameter_validation) {
    TrueHdrParameters parameters;
    RTX_CHECK(parameters.validate().ok());

    parameters.contrast = 49;
    RTX_CHECK_EQ(parameters.validate().code(), ErrorCode::InvalidArgument);
    parameters.contrast = 125;
    parameters.saturation = 201;
    RTX_CHECK_EQ(parameters.validate().code(), ErrorCode::InvalidArgument);
    parameters.saturation = 75;
    parameters.middle_gray = 9;
    RTX_CHECK_EQ(parameters.validate().code(), ErrorCode::InvalidArgument);
    parameters.middle_gray = 44;
    parameters.max_luminance = 99;
    RTX_CHECK_EQ(parameters.validate().code(), ErrorCode::InvalidArgument);
    parameters.max_luminance = 4001;
    RTX_CHECK_EQ(parameters.validate().code(), ErrorCode::InvalidArgument);
    parameters.max_luminance = 1000;
    parameters.max_fall = 65536;
    RTX_CHECK_EQ(parameters.validate().code(), ErrorCode::InvalidArgument);
    parameters.max_fall = 300;
    RTX_CHECK(parameters.validate().ok());
}

RTX_TEST(hdr_make_metadata_and_mastering_presets) {
    TrueHdrParameters parameters;
    parameters.mastering = MasteringPreset::DciP3D65;
    const Result<Hdr10Metadata> metadata = SeiSynthesizer::makeMetadata(parameters);
    RTX_REQUIRE(metadata.ok());
    const Hdr10Metadata& hdr = metadata.value();
    RTX_CHECK(hdr.enabled);
    RTX_CHECK_EQ(hdr.mastering.red_x, static_cast<uint16_t>(34000));
    RTX_CHECK_EQ(hdr.mastering.green_y, static_cast<uint16_t>(34500));
    RTX_CHECK_EQ(hdr.mastering.white_x, static_cast<uint16_t>(15635));
    RTX_CHECK_EQ(hdr.mastering.max_luminance, static_cast<uint32_t>(10000000));
    RTX_CHECK_EQ(hdr.mastering.min_luminance, static_cast<uint32_t>(1));
    RTX_CHECK_EQ(hdr.cll.max_cll, static_cast<uint16_t>(1000));
    RTX_CHECK_EQ(hdr.cll.max_fall, static_cast<uint16_t>(300));
    RTX_CHECK_EQ(hdr.target_nits, static_cast<uint32_t>(1000));
    RTX_CHECK(hdr.color.primaries == ColorPrimaries::BT2020);
    RTX_CHECK(hdr.color.transfer == TransferFunction::SMPTE2084);
    RTX_CHECK(hdr.color.matrix == MatrixCoefficients::BT2020NC);

    const MasteringDisplay bt2020 =
        SeiSynthesizer::makeMasteringDisplay(MasteringPreset::Bt2020, 4000, 5);
    RTX_CHECK_EQ(bt2020.red_x, static_cast<uint16_t>(35400));
    RTX_CHECK_EQ(bt2020.green_y, static_cast<uint16_t>(39850));
    RTX_CHECK_EQ(bt2020.blue_y, static_cast<uint16_t>(2300));
    RTX_CHECK_EQ(bt2020.max_luminance, static_cast<uint32_t>(40000000));
    RTX_CHECK_EQ(bt2020.min_luminance, static_cast<uint32_t>(5));

    TrueHdrParameters invalid;
    invalid.max_luminance = 10;
    RTX_CHECK(!SeiSynthesizer::makeMetadata(invalid).ok());
}

RTX_TEST(hdr_nvenc_vui_settings) {
    const Hdr10Metadata hdr = sampleMetadata();
    const NvencHevcHdrSettings settings = SeiSynthesizer::makeNvencSettings(hdr);
    RTX_CHECK_EQ(settings.pixel_bit_depth_minus8, static_cast<uint32_t>(2));
    RTX_CHECK_EQ(settings.output_mastering_display, static_cast<uint32_t>(1));
    RTX_CHECK_EQ(settings.output_max_cll, static_cast<uint32_t>(1));
    RTX_CHECK_EQ(settings.vui.colour_description_present_flag, static_cast<uint8_t>(1));
    RTX_CHECK_EQ(settings.vui.colour_primaries, static_cast<uint8_t>(9));
    RTX_CHECK_EQ(settings.vui.transfer_characteristics, static_cast<uint8_t>(16));
    RTX_CHECK_EQ(settings.vui.matrix_coefficients, static_cast<uint8_t>(9));
    RTX_CHECK_EQ(settings.vui.chroma_sample_loc_type_top_field, static_cast<uint8_t>(2));
    RTX_CHECK_EQ(settings.vui.chroma_sample_loc_type_bottom_field, static_cast<uint8_t>(2));
    RTX_CHECK_EQ(settings.vui.video_full_range_flag, static_cast<uint8_t>(0));

    Hdr10Metadata disabled = hdr;
    disabled.enabled = false;
    RTX_CHECK_EQ(SeiSynthesizer::makeNvencSettings(disabled).output_mastering_display,
                 static_cast<uint32_t>(0));
}

RTX_TEST(hdr_mastering_and_cll_payload_layout) {
    Hdr10Metadata hdr{};
    hdr.mastering = MasteringDisplay{1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    hdr.cll = ContentLightLevel{0x1234, 0x5678};

    const std::vector<uint8_t> mdcv = SeiSynthesizer::masteringDisplayPayload(hdr);
    RTX_CHECK_EQ(mdcv.size(), static_cast<size_t>(24));
    // SEI order is G, B, R, white.
    RTX_CHECK_EQ(readU16(mdcv, 0), static_cast<uint16_t>(3));
    RTX_CHECK_EQ(readU16(mdcv, 2), static_cast<uint16_t>(4));
    RTX_CHECK_EQ(readU16(mdcv, 4), static_cast<uint16_t>(5));
    RTX_CHECK_EQ(readU16(mdcv, 6), static_cast<uint16_t>(6));
    RTX_CHECK_EQ(readU16(mdcv, 8), static_cast<uint16_t>(1));
    RTX_CHECK_EQ(readU16(mdcv, 10), static_cast<uint16_t>(2));
    RTX_CHECK_EQ(readU16(mdcv, 12), static_cast<uint16_t>(7));
    RTX_CHECK_EQ(readU16(mdcv, 14), static_cast<uint16_t>(8));
    RTX_CHECK_EQ(readU32(mdcv, 16), static_cast<uint32_t>(9));
    RTX_CHECK_EQ(readU32(mdcv, 20), static_cast<uint32_t>(10));

    const std::vector<uint8_t> cll = SeiSynthesizer::contentLightPayload(hdr);
    RTX_CHECK_EQ(cll.size(), static_cast<size_t>(4));
    RTX_CHECK_EQ(readU16(cll, 0), static_cast<uint16_t>(0x1234));
    RTX_CHECK_EQ(readU16(cll, 2), static_cast<uint16_t>(0x5678));
}

RTX_TEST(hdr_emulation_prevention) {
    const std::vector<uint8_t> rbsp = {0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
                                       0x02, 0x00, 0x00, 0x03, 0x7F};
    const std::vector<uint8_t> escaped = SeiSynthesizer::insertEmulationPrevention(rbsp);
    const std::vector<uint8_t> expected = {0x00, 0x00, 0x03, 0x00, 0x01, 0x00,
                                           0x00, 0x03, 0x02, 0x00, 0x00, 0x03,
                                           0x03, 0x7F};
    RTX_CHECK(escaped == expected);
    RTX_CHECK(SeiSynthesizer::insertEmulationPrevention({}).empty());
}

RTX_TEST(hdr_prefix_sei_nal) {
    const Hdr10Metadata hdr = sampleMetadata();
    const std::vector<uint8_t> nal = SeiSynthesizer::prefixSeiNal(hdr);
    RTX_REQUIRE(nal.size() >= 8);
    RTX_CHECK_EQ(nal[0], static_cast<uint8_t>(0));
    RTX_CHECK_EQ(nal[1], static_cast<uint8_t>(0));
    RTX_CHECK_EQ(nal[2], static_cast<uint8_t>(0));
    RTX_CHECK_EQ(nal[3], static_cast<uint8_t>(1));
    RTX_CHECK_EQ(nal[4], static_cast<uint8_t>(0x4E));  // PREFIX_SEI, layer 0, TID 1
    RTX_CHECK_EQ(nal[5], static_cast<uint8_t>(0x01));
    RTX_CHECK_EQ(nal[6], static_cast<uint8_t>(137));
    RTX_CHECK_EQ(nal[7], static_cast<uint8_t>(24));
    RTX_CHECK(nal.size() >= 39);

    const std::vector<uint8_t> rbsp =
        removeEmulationPrevention(std::vector<uint8_t>(nal.begin() + 6, nal.end()));
    const std::vector<uint8_t> mdcv = SeiSynthesizer::masteringDisplayPayload(hdr);
    const std::vector<uint8_t> cll = SeiSynthesizer::contentLightPayload(hdr);
    std::vector<uint8_t> expected;
    expected.push_back(137);
    expected.push_back(static_cast<uint8_t>(mdcv.size()));
    expected.insert(expected.end(), mdcv.begin(), mdcv.end());
    expected.push_back(144);
    expected.push_back(static_cast<uint8_t>(cll.size()));
    expected.insert(expected.end(), cll.begin(), cll.end());
    expected.push_back(0x80);
    RTX_CHECK(rbsp == expected);
}

RTX_TEST(hdr_av1_metadata_obus) {
    Hdr10Metadata hdr{};
    hdr.mastering = MasteringDisplay{1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    hdr.cll = ContentLightLevel{1000, 400};
    const std::vector<uint8_t> obus = SeiSynthesizer::av1MetadataObu(hdr);
    RTX_REQUIRE(obus.size() >= 12);
    RTX_CHECK_EQ(obus[0], static_cast<uint8_t>(0x2A));
    RTX_CHECK_EQ(obus[1], static_cast<uint8_t>(5));
    RTX_CHECK_EQ(obus[2], static_cast<uint8_t>(1));  // HDR_CLL
    RTX_CHECK_EQ(readU16(obus, 3), static_cast<uint16_t>(1000));
    RTX_CHECK_EQ(readU16(obus, 5), static_cast<uint16_t>(400));
    const size_t second = 7;
    RTX_CHECK_EQ(obus[second], static_cast<uint8_t>(0x2A));
    RTX_CHECK_EQ(obus[second + 1], static_cast<uint8_t>(25));
    RTX_CHECK_EQ(obus[second + 2], static_cast<uint8_t>(2));  // HDR_MDCV
    RTX_CHECK_EQ(obus.size(), static_cast<size_t>(second + 2 + 25));
}

RTX_TEST(hdr_container_side_data) {
    Hdr10Metadata hdr = sampleMetadata();
    const std::vector<uint8_t> side = SeiSynthesizer::containerSideData(hdr);
    RTX_CHECK_EQ(side.size(), static_cast<size_t>(44));
    RTX_CHECK_EQ(side[0], static_cast<uint8_t>('R'));
    RTX_CHECK_EQ(side[1], static_cast<uint8_t>('T'));
    RTX_CHECK_EQ(side[2], static_cast<uint8_t>('X'));
    RTX_CHECK_EQ(side[3], static_cast<uint8_t>('H'));
    RTX_CHECK_EQ(readU16(side, 4), static_cast<uint16_t>(1));
    RTX_CHECK_EQ(readU16(side, 6), static_cast<uint16_t>(1));
    RTX_CHECK_EQ(side[8], static_cast<uint8_t>(1000 & 0xFF));
    RTX_CHECK_EQ(side[9], static_cast<uint8_t>((1000 >> 8) & 0xFF));
    RTX_CHECK_EQ(readU16(side, 12), static_cast<uint16_t>(hdr.mastering.red_x));
    RTX_CHECK_EQ(readU16(side, 24), static_cast<uint16_t>(hdr.mastering.white_x));
    RTX_CHECK_EQ(readU16(side, 26), static_cast<uint16_t>(hdr.mastering.white_y));
    RTX_CHECK_EQ(readLe32(side, 28), static_cast<uint32_t>(10000000));
    RTX_CHECK_EQ(readLe32(side, 32), static_cast<uint32_t>(1));
    RTX_CHECK_EQ(readU16(side, 36), static_cast<uint16_t>(hdr.cll.max_cll));
    RTX_CHECK_EQ(readU16(side, 38), static_cast<uint16_t>(hdr.cll.max_fall));
    RTX_CHECK_EQ(readLe32(side, 8), static_cast<uint32_t>(1000));
    RTX_CHECK_EQ(side[40], static_cast<uint8_t>(9));
    RTX_CHECK_EQ(side[41], static_cast<uint8_t>(16));
    RTX_CHECK_EQ(side[42], static_cast<uint8_t>(9));
    RTX_CHECK_EQ(side[43], static_cast<uint8_t>(0));
}

RTX_TEST(hdr_json_round_trip) {
    Hdr10Metadata hdr = sampleMetadata();
    hdr.target_nits = 1600;
    hdr.mastering.max_luminance = 16000000;
    hdr.cll.max_fall = 450;
    const std::string json = SeiSynthesizer::toJson(hdr);
    RTX_CHECK(json.find("\"mastering_display\"") != std::string::npos);
    RTX_CHECK(json.find("\"enabled\": true") != std::string::npos);
    RTX_CHECK(json.find("\"max_fall\": 450") != std::string::npos);

    const Result<Hdr10Metadata> parsed = SeiSynthesizer::fromJson(json);
    RTX_REQUIRE(parsed.ok());
    const Hdr10Metadata& value = parsed.value();
    RTX_CHECK(value.enabled);
    RTX_CHECK_EQ(value.target_nits, static_cast<uint32_t>(1600));
    RTX_CHECK_EQ(value.mastering.red_x, hdr.mastering.red_x);
    RTX_CHECK_EQ(value.mastering.max_luminance, hdr.mastering.max_luminance);
    RTX_CHECK_EQ(value.mastering.min_luminance, hdr.mastering.min_luminance);
    RTX_CHECK_EQ(value.cll.max_cll, hdr.cll.max_cll);
    RTX_CHECK_EQ(value.cll.max_fall, hdr.cll.max_fall);
    RTX_CHECK(value.color.primaries == ColorPrimaries::BT2020);
    RTX_CHECK(value.color.transfer == TransferFunction::SMPTE2084);

    RTX_CHECK_EQ(SeiSynthesizer::fromJson("{}").code(), ErrorCode::InvalidArgument);
    const std::string zero_luminance =
        R"({"enabled": true, "mastering_display": {"max_luminance": 0}})";
    RTX_CHECK_EQ(SeiSynthesizer::fromJson(zero_luminance).code(),
                 ErrorCode::InvalidArgument);
}
