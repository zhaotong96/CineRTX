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
    return metadata.has_value() ? metadata.value() : Hdr10Metadata{};
}

}  // namespace

RTX_TEST(hdr_truehdr_parameter_validation) {
    TrueHdrParameters parameters;
    RTX_CHECK(parameters.validate().has_value());

    parameters.contrast = 49;
    RTX_CHECK_EQ(parameters.validate().error().code, ErrorCode::InvalidArgument);
    parameters.contrast = 125;
    parameters.saturation = 201;
    RTX_CHECK_EQ(parameters.validate().error().code, ErrorCode::InvalidArgument);
    parameters.saturation = 75;
    parameters.middle_gray = 9;
    RTX_CHECK_EQ(parameters.validate().error().code, ErrorCode::InvalidArgument);
    parameters.middle_gray = 44;
    parameters.max_luminance = 99;
    RTX_CHECK_EQ(parameters.validate().error().code, ErrorCode::InvalidArgument);
    parameters.max_luminance = 4001;
    RTX_CHECK_EQ(parameters.validate().error().code, ErrorCode::InvalidArgument);
    parameters.max_luminance = 1000;
    parameters.max_fall = 65536;
    RTX_CHECK_EQ(parameters.validate().error().code, ErrorCode::InvalidArgument);
    parameters.max_fall = 300;
    RTX_CHECK(parameters.validate().has_value());
}

RTX_TEST(hdr_make_metadata_and_mastering_presets) {
    TrueHdrParameters parameters;
    parameters.mastering = MasteringPreset::DciP3D65;
    const Result<Hdr10Metadata> metadata = SeiSynthesizer::makeMetadata(parameters);
    RTX_REQUIRE(metadata.has_value());
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
    RTX_CHECK(!SeiSynthesizer::makeMetadata(invalid).has_value());
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

// ---------------------------------------------------------------------------
// Phase 7 (Task 7-B2): SMPTE ST 2094-40 (HDR10+) dynamic metadata synthesis.
// ---------------------------------------------------------------------------

namespace {

// MSB-first bit reader mirroring the writer in sei_synthesizer.cpp.
class Hdr10PlusBitReader {
public:
    explicit Hdr10PlusBitReader(const std::vector<uint8_t>& bytes) : bytes_(bytes) {}

    uint32_t get(uint32_t bits) {
        uint32_t value = 0;
        for (uint32_t i = 0; i < bits; ++i) {
            const uint32_t byte = bytes_[position_ >> 3];
            const uint32_t bit = (byte >> (7 - (position_ & 7))) & 1u;
            value = (value << 1) | bit;
            ++position_;
        }
        return value;
    }

private:
    const std::vector<uint8_t>& bytes_;
    size_t position_ = 0;
};

}  // namespace

RTX_TEST(hdr10plus_itut_t35_obu_synthesis) {
    Hdr10PlusFrameStats stats{};
    stats.maxscl_r_nits = 950;
    stats.maxscl_g_nits = 1005;
    stats.maxscl_b_nits = 780;
    stats.average_maxrgb_nits = 210;
    stats.knee_point_x = 500;
    stats.knee_point_y = 620;

    // 1. T.35 carrier: country 0xB5, provider 0x003C, oriented 0, app id 4,
    //    application version 1.
    const std::vector<uint8_t> t35 = SeiSynthesizer::hdr10PlusItuT35Payload(stats);
    RTX_REQUIRE(t35.size() >= 6);
    RTX_CHECK_EQ(t35[0], 0xB5);
    RTX_CHECK_EQ(t35[1], 0x00);
    RTX_CHECK_EQ(t35[2], 0x3C);
    RTX_CHECK_EQ(t35[3], 0x00);
    RTX_CHECK_EQ(t35[4], 0x04);
    RTX_CHECK_EQ(t35[5], 0x01);

    // 2. Bit-level round trip of the luminance statistics and knee points.
    Hdr10PlusBitReader reader(t35);
    RTX_CHECK_EQ(reader.get(8), 0xB5);
    RTX_CHECK_EQ(reader.get(8), 0x00);
    RTX_CHECK_EQ(reader.get(8), 0x3C);
    RTX_CHECK_EQ(reader.get(8), 0x00);
    RTX_CHECK_EQ(reader.get(8), 0x04);
    RTX_CHECK_EQ(reader.get(8), 0x01);
    RTX_CHECK_EQ(reader.get(2), 0);           // num_windows - 1
    RTX_CHECK_EQ(reader.get(16), 0);          // center_of_ellipse_x
    RTX_CHECK_EQ(reader.get(16), 0);          // center_of_ellipse_y
    RTX_CHECK_EQ(reader.get(8), 0);           // major_axis
    RTX_CHECK_EQ(reader.get(8), 0);           // minor_axis
    RTX_CHECK_EQ(reader.get(8), 0);           // rotation
    RTX_CHECK_EQ(reader.get(8), 0);           // reflectance
    RTX_CHECK_EQ(reader.get(1), 0);           // tone_mapping_mode_flag
    RTX_CHECK_EQ(reader.get(1), 0);           // sharpening_flag
    // maxscl encoding: nits * 10 in 17-bit words (0.0001 resolution).
    RTX_CHECK_EQ(reader.get(17), stats.maxscl_r_nits * 10u);
    RTX_CHECK_EQ(reader.get(17), stats.maxscl_g_nits * 10u);
    RTX_CHECK_EQ(reader.get(17), stats.maxscl_b_nits * 10u);
    RTX_CHECK_EQ(reader.get(17), stats.average_maxrgb_nits * 10u);
    RTX_CHECK_EQ(reader.get(1), 0);           // distribution percentiles
    RTX_CHECK_EQ(reader.get(1), 0);           // fraction_bright_pixels
    RTX_CHECK_EQ(reader.get(1), 1);           // tone_mapping_flag
    RTX_CHECK_EQ(reader.get(16), stats.knee_point_x);
    RTX_CHECK_EQ(reader.get(16), stats.knee_point_y);
    RTX_CHECK_EQ(reader.get(3), 0);           // bezier anchors
    RTX_CHECK_EQ(reader.get(10), 0);          // color_saturation_weight

    // 3. AV1 OBU_METADATA wrapper: OBU header 0x2A (type 5, has_size_field),
    //    leb128 group size, metadata_type 3 (METADATA_TYPE_ITUT_T35), payload.
    const std::vector<uint8_t> obu = SeiSynthesizer::av1Hdr10PlusMetadataObu(stats);
    RTX_REQUIRE(obu.size() > t35.size() + 2);
    RTX_CHECK_EQ(obu[0], 0x2A);
    // The group (1 leb128 metadata_type byte + payload) must fit one leb128
    // size byte for this payload size.
    RTX_CHECK_EQ(obu[1], static_cast<uint8_t>(t35.size() + 1));
    RTX_CHECK_EQ(obu[2], 0x03);  // metadata_type = ITUT_T35
    RTX_CHECK_EQ(obu[3], 0xB5);  // T.35 country code starts the payload

    // 4. HEVC prefix SEI: Annex-B start code + PREFIX_SEI_NUT header
    //    (0x04 0x01) + payload type 4 (user data registered by ITU-T T.35).
    const std::vector<uint8_t> sei = SeiSynthesizer::hevcHdr10PlusPrefixSei(stats);
    RTX_REQUIRE(sei.size() > 10);
    RTX_CHECK_EQ(sei[0], 0x00);
    RTX_CHECK_EQ(sei[1], 0x00);
    RTX_CHECK_EQ(sei[2], 0x00);
    RTX_CHECK_EQ(sei[3], 0x01);
    RTX_CHECK_EQ(sei[4], 0x04);
    RTX_CHECK_EQ(sei[5], 0x01);
    // The T.35 carrier bytes must appear after the SEI header.
    bool carrier_found = false;
    for (size_t i = 6; i + 3 < sei.size(); ++i) {
        if (sei[i] == 0xB5 && sei[i + 1] == 0x00 && sei[i + 2] == 0x3C) {
            carrier_found = true;
            break;
        }
    }
    RTX_CHECK(carrier_found);
}
