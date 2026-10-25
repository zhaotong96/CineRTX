// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors

#include "bitstream.h"

#include <algorithm>
#include <cstring>

namespace rtx {
namespace {

constexpr size_t kNoPosition = static_cast<size_t>(-1);

class BitReader {
public:
    BitReader(const uint8_t* data, size_t size) : data_(data), size_(size) {}

    bool ok() const noexcept { return ok_; }
    size_t bitsLeft() const noexcept { return size_ * 8 - position_; }

    uint32_t u(int count) {
        if (count < 0 || count > 32 || bitsLeft() < static_cast<size_t>(count)) {
            ok_ = false;
            return 0;
        }
        uint32_t value = 0;
        for (int i = 0; i < count; ++i) {
            const size_t byte = position_ / 8;
            const int bit = 7 - static_cast<int>(position_ % 8);
            value = (value << 1) | ((data_[byte] >> bit) & 1u);
            ++position_;
        }
        return value;
    }

    uint32_t ue() {
        int leading_zero_bits = 0;
        while (true) {
            if (bitsLeft() == 0) {
                ok_ = false;
                return 0;
            }
            if (u(1) != 0) {
                break;
            }
            ++leading_zero_bits;
            if (leading_zero_bits > 31) {
                ok_ = false;
                return 0;
            }
        }
        if (leading_zero_bits == 0) {
            return 0;
        }
        return ((1u << leading_zero_bits) - 1u) + u(leading_zero_bits);
    }

    int32_t se() {
        const uint32_t code = ue();
        if (!ok_) {
            return 0;
        }
        return (code & 1u) != 0 ? static_cast<int32_t>((code + 1u) >> 1)
                                : -static_cast<int32_t>(code >> 1);
    }

    void skipBits(size_t count) {
        while (count > 0) {
            const size_t chunk = count > 32 ? 32 : count;
            (void)u(static_cast<int>(chunk));
            count -= chunk;
        }
    }

private:
    const uint8_t* data_ = nullptr;
    size_t size_ = 0;
    size_t position_ = 0;
    bool ok_ = true;
};

std::vector<std::vector<uint8_t>> nalUnits(const uint8_t* data, size_t size) {
    std::vector<std::vector<uint8_t>> units;
    if (data == nullptr || size == 0) {
        return units;
    }
    if (looksLikeAnnexB(data, size)) {
        return splitAnnexB(data, size);
    }
    if (data[0] == 1 && size >= 23) {
        // HEVCDecoderConfigurationRecord: arrays of length-prefixed NALs.
        size_t position = 23;
        const int arrays = data[22];
        for (int array = 0; array < arrays && position + 3 <= size; ++array) {
            ++position;
            const int count = (data[position] << 8) | data[position + 1];
            position += 2;
            for (int i = 0; i < count && position + 2 <= size; ++i) {
                const size_t length = (static_cast<size_t>(data[position]) << 8) |
                                      data[position + 1];
                position += 2;
                if (length == 0 || length > size - position) {
                    return units;
                }
                units.emplace_back(data + position, data + position + length);
                position += length;
            }
        }
        return units;
    }
    size_t position = 0;
    while (position + 4 <= size) {
        const size_t length = (static_cast<size_t>(data[position]) << 24) |
                              (static_cast<size_t>(data[position + 1]) << 16) |
                              (static_cast<size_t>(data[position + 2]) << 8) |
                              static_cast<size_t>(data[position + 3]);
        position += 4;
        if (length == 0 || length > size - position) {
            break;
        }
        units.emplace_back(data + position, data + position + length);
        position += length;
    }
    return units;
}

std::vector<uint8_t> rbspFromNal(const std::vector<uint8_t>& nal) {
    std::vector<uint8_t> rbsp;
    rbsp.reserve(nal.size());
    int zeros = 0;
    for (uint8_t byte : nal) {
        if (zeros >= 2 && byte == 0x03) {
            zeros = 0;
            continue;
        }
        rbsp.push_back(byte);
        zeros = byte == 0x00 ? zeros + 1 : 0;
    }
    return rbsp;
}

struct HevcSpsInfo {
    int profile_space = 0;
    int tier = 0;
    int profile_idc = 0;
    uint32_t compatibility = 0;
    uint64_t constraints = 0;
    int level_idc = 0;
    int chroma_format_idc = 1;
    int bit_depth_luma_minus8 = 2;
    int bit_depth_chroma_minus8 = 2;
    int max_sub_layers_minus1 = 0;
};

bool parseHevcSps(const std::vector<uint8_t>& nal, HevcSpsInfo& info) {
    if (nal.size() < 3 || ((nal[0] >> 1) & 0x3F) != 33) {
        return false;
    }
    const std::vector<uint8_t> rbsp = rbspFromNal(
        std::vector<uint8_t>(nal.begin() + 2, nal.end()));
    BitReader reader(rbsp.data(), rbsp.size());

    (void)reader.u(4);  // sps_video_parameter_set_id
    info.max_sub_layers_minus1 = static_cast<int>(reader.u(3));
    (void)reader.u(1);  // sps_temporal_id_nesting_flag

    info.profile_space = static_cast<int>(reader.u(2));
    info.tier = static_cast<int>(reader.u(1));
    info.profile_idc = static_cast<int>(reader.u(5));
    info.compatibility = reader.u(32);
    info.constraints = (static_cast<uint64_t>(reader.u(16)) << 32);
    info.constraints |= reader.u(32);
    info.level_idc = static_cast<int>(reader.u(8));

    for (int i = 0; i < info.max_sub_layers_minus1; ++i) {
        const bool profile_present = reader.u(1) != 0;
        const bool level_present = reader.u(1) != 0;
        if (profile_present) {
            (void)reader.u(32);
            (void)reader.u(32);
            (void)reader.u(24);
        }
        if (level_present) {
            (void)reader.u(8);
        }
    }
    if (info.max_sub_layers_minus1 > 0) {
        for (int i = info.max_sub_layers_minus1; i < 8; ++i) {
            (void)reader.u(2);
        }
    }

    (void)reader.ue();  // sps_seq_parameter_set_id
    info.chroma_format_idc = static_cast<int>(reader.ue());
    if (info.chroma_format_idc == 3) {
        (void)reader.u(1);  // separate_colour_plane_flag
    }
    (void)reader.ue();  // pic_width_in_luma_samples
    (void)reader.ue();  // pic_height_in_luma_samples
    if (reader.u(1) != 0) {  // conformance_window_flag
        for (int i = 0; i < 4; ++i) {
            (void)reader.ue();
        }
    }
    info.bit_depth_luma_minus8 = static_cast<int>(reader.ue());
    info.bit_depth_chroma_minus8 = static_cast<int>(reader.ue());
    return reader.ok() && info.bit_depth_luma_minus8 >= 0 &&
           info.bit_depth_luma_minus8 <= 6;
}

bool skipProfileFields(BitReader& reader) {
    (void)reader.u(2);  // profile_space
    (void)reader.u(1);  // tier_flag
    const int profile_idc = static_cast<int>(reader.u(5));
    bool compatibility[32] = {};
    for (int i = 0; i < 32; ++i) {
        compatibility[i] = reader.u(1) != 0;
    }
    const auto has_profile = [&](int idc) {
        return profile_idc == idc || (idc >= 0 && idc < 32 && compatibility[idc]);
    };
    (void)reader.u(4);  // progressive/interlaced/non-packed/frame-only flags
    if (has_profile(4) || has_profile(5) || has_profile(6) || has_profile(7) ||
        has_profile(8) || has_profile(9) || has_profile(10)) {
        reader.skipBits(9);
        if (has_profile(5) || has_profile(9) || has_profile(10)) {
            (void)reader.u(1);
            reader.skipBits(33);
        } else {
            reader.skipBits(34);
        }
    } else if (has_profile(2)) {
        reader.skipBits(7);
        (void)reader.u(1);
        reader.skipBits(35);
    } else {
        reader.skipBits(43);
    }
    (void)reader.u(1);  // inbld_flag / reserved
    return reader.ok();
}

bool skipProfileTierLevel(BitReader& reader, int max_sub_layers_minus1) {
    if (!skipProfileFields(reader)) {
        return false;
    }
    (void)reader.u(8);  // level_idc
    bool profile_present[8] = {};
    bool level_present[8] = {};
    for (int i = 0; i < max_sub_layers_minus1; ++i) {
        profile_present[i] = reader.u(1) != 0;
        level_present[i] = reader.u(1) != 0;
    }
    if (max_sub_layers_minus1 > 0) {
        for (int i = max_sub_layers_minus1; i < 8; ++i) {
            (void)reader.u(2);
        }
    }
    for (int i = 0; i < max_sub_layers_minus1; ++i) {
        if (profile_present[i] && !skipProfileFields(reader)) {
            return false;
        }
        if (level_present[i]) {
            (void)reader.u(8);
        }
    }
    return reader.ok();
}

bool skipScalingListData(BitReader& reader) {
    for (int size_id = 0; size_id < 4; ++size_id) {
        for (int matrix_id = 0; matrix_id < 6; matrix_id += (size_id == 3 ? 3 : 1)) {
            if (reader.u(1) == 0) {
                (void)reader.ue();
                continue;
            }
            const int coefficient_count = std::min(64, 1 << (4 + (size_id << 1)));
            if (size_id > 1) {
                (void)reader.se();
            }
            for (int i = 0; i < coefficient_count; ++i) {
                (void)reader.se();
            }
        }
    }
    return reader.ok();
}

bool skipShortTermRps(BitReader& reader, size_t index, std::vector<int>& num_delta_pocs) {
    bool predicted = false;
    if (index > 0) {
        predicted = reader.u(1) != 0;
    }
    if (predicted) {
        (void)reader.u(1);  // delta_rps_sign
        (void)reader.ue();  // abs_delta_rps_minus1
        int count = 0;
        const int previous = num_delta_pocs[index - 1];
        for (int i = 0; i <= previous; ++i) {
            const bool used = reader.u(1) != 0;
            bool use_delta = false;
            if (!used) {
                use_delta = reader.u(1) != 0;
            }
            if (used || use_delta) {
                ++count;
            }
        }
        if (count > 32) {
            return false;
        }
        num_delta_pocs[index] = count;
    } else {
        const uint32_t negative = reader.ue();
        const uint32_t positive = reader.ue();
        if (negative > 32 || positive > 32) {
            return false;
        }
        num_delta_pocs[index] = static_cast<int>(negative + positive);
        for (uint32_t i = 0; i < negative; ++i) {
            (void)reader.ue();
            (void)reader.u(1);
        }
        for (uint32_t i = 0; i < positive; ++i) {
            (void)reader.ue();
            (void)reader.u(1);
        }
    }
    return reader.ok();
}

bool parseHevcVuiColorFromSps(const std::vector<uint8_t>& nal, HevcVuiColor& color) {
    if (nal.size() < 3 || ((nal[0] >> 1) & 0x3F) != 33) {
        return false;
    }
    const std::vector<uint8_t> rbsp =
        rbspFromNal(std::vector<uint8_t>(nal.begin() + 2, nal.end()));
    BitReader reader(rbsp.data(), rbsp.size());
    (void)reader.u(4);  // sps_video_parameter_set_id
    const int max_sub_layers_minus1 = static_cast<int>(reader.u(3));
    if (max_sub_layers_minus1 > 6) {
        return false;
    }
    (void)reader.u(1);  // sps_temporal_id_nesting_flag
    if (!skipProfileTierLevel(reader, max_sub_layers_minus1)) {
        return false;
    }
    (void)reader.ue();  // sps_seq_parameter_set_id
    const uint32_t chroma_format_idc = reader.ue();
    if (chroma_format_idc > 3) {
        return false;
    }
    if (chroma_format_idc == 3) {
        (void)reader.u(1);  // separate_colour_plane_flag
    }
    (void)reader.ue();  // pic_width_in_luma_samples
    (void)reader.ue();  // pic_height_in_luma_samples
    if (reader.u(1) != 0) {
        for (int i = 0; i < 4; ++i) {
            (void)reader.ue();
        }
    }
    (void)reader.ue();  // bit_depth_luma_minus8
    (void)reader.ue();  // bit_depth_chroma_minus8
    const uint32_t poc_lsb_minus4 = reader.ue();
    if (!reader.ok() || poc_lsb_minus4 > 12u) {
        return false;
    }
    const int log2_max_poc_lsb = static_cast<int>(poc_lsb_minus4) + 4;
    const int ordering_start = reader.u(1) != 0 ? 0 : max_sub_layers_minus1;
    for (int i = ordering_start; i <= max_sub_layers_minus1; ++i) {
        (void)reader.ue();
        (void)reader.ue();
        (void)reader.ue();
    }
    for (int i = 0; i < 6; ++i) {
        (void)reader.ue();
    }
    if (reader.u(1) != 0) {
        if (reader.u(1) != 0 && !skipScalingListData(reader)) {
            return false;
        }
    }
    (void)reader.u(1);  // amp_enabled_flag
    (void)reader.u(1);  // sample_adaptive_offset_enabled_flag
    if (reader.u(1) != 0) {
        (void)reader.u(4);
        (void)reader.u(4);
        (void)reader.ue();
        (void)reader.ue();
        (void)reader.u(1);
    }
    const uint32_t num_short_term_rps = reader.ue();
    if (num_short_term_rps > 64) {
        return false;
    }
    std::vector<int> num_delta_pocs(num_short_term_rps, 0);
    for (uint32_t i = 0; i < num_short_term_rps; ++i) {
        if (!skipShortTermRps(reader, i, num_delta_pocs)) {
            return false;
        }
    }
    if (reader.u(1) != 0) {
        const uint32_t long_term_count = reader.ue();
        if (long_term_count > 32) {
            return false;
        }
        for (uint32_t i = 0; i < long_term_count; ++i) {
            reader.skipBits(static_cast<size_t>(log2_max_poc_lsb));
            (void)reader.u(1);
        }
    }
    (void)reader.u(1);  // sps_temporal_mvp_enabled_flag
    (void)reader.u(1);  // strong_intra_smoothing_enabled_flag
    if (reader.u(1) == 0) {
        return false;
    }
    if (reader.u(1) != 0) {
        const uint32_t aspect_idc = reader.u(8);
        if (aspect_idc == 255) {
            (void)reader.u(16);
            (void)reader.u(16);
        }
    }
    if (reader.u(1) != 0) {
        (void)reader.u(1);
    }
    if (reader.u(1) == 0) {
        return false;
    }
    color.has_video_signal_type = true;
    (void)reader.u(3);  // video_format
    color.full_range = reader.u(1) != 0;
    if (reader.u(1) != 0) {
        color.has_colour_description = true;
        color.primaries = static_cast<int>(reader.u(8));
        color.transfer = static_cast<int>(reader.u(8));
        color.matrix = static_cast<int>(reader.u(8));
    }
    return reader.ok();
}

bool parseSeiPayloads(const std::vector<uint8_t>& nal, HevcSeiHdr10& hdr) {
    if (nal.size() < 3) {
        return false;
    }
    const int nal_type = (nal[0] >> 1) & 0x3F;
    if (nal_type != 39 && nal_type != 40) {
        return false;
    }
    const std::vector<uint8_t> rbsp =
        rbspFromNal(std::vector<uint8_t>(nal.begin() + 2, nal.end()));
    size_t position = 0;
    while (position < rbsp.size()) {
        uint32_t payload_type = 0;
        while (position < rbsp.size() && rbsp[position] == 0xFF) {
            payload_type += 255;
            ++position;
        }
        if (position >= rbsp.size()) {
            break;
        }
        payload_type += rbsp[position++];
        uint32_t payload_size = 0;
        while (position < rbsp.size() && rbsp[position] == 0xFF) {
            payload_size += 255;
            ++position;
        }
        if (position >= rbsp.size()) {
            break;
        }
        payload_size += rbsp[position++];
        if (payload_size > rbsp.size() - position) {
            break;
        }
        const uint8_t* payload = rbsp.data() + position;
        const auto read_u16 = [payload](size_t offset) {
            return static_cast<uint16_t>((payload[offset] << 8) | payload[offset + 1]);
        };
        const auto read_u32 = [payload](size_t offset) {
            return (static_cast<uint32_t>(payload[offset]) << 24) |
                   (static_cast<uint32_t>(payload[offset + 1]) << 16) |
                   (static_cast<uint32_t>(payload[offset + 2]) << 8) |
                   static_cast<uint32_t>(payload[offset + 3]);
        };
        if (payload_type == 137 && payload_size >= 24) {
            hdr.green_x = read_u16(0);
            hdr.green_y = read_u16(2);
            hdr.blue_x = read_u16(4);
            hdr.blue_y = read_u16(6);
            hdr.red_x = read_u16(8);
            hdr.red_y = read_u16(10);
            hdr.white_x = read_u16(12);
            hdr.white_y = read_u16(14);
            hdr.max_luminance = read_u32(16);
            hdr.min_luminance = read_u32(20);
            hdr.has_mastering_display = true;
        } else if (payload_type == 144 && payload_size >= 4) {
            hdr.max_cll = read_u16(0);
            hdr.max_fall = read_u16(2);
            hdr.has_content_light = true;
        }
        position += payload_size;
    }
    return hdr.has_mastering_display || hdr.has_content_light;
}

void appendU16(std::vector<uint8_t>& out, size_t value) {
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(value & 0xFF));
}

void appendBe32(std::vector<uint8_t>& out, uint32_t value) {
    out.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(value & 0xFF));
}

}  // namespace

std::vector<std::vector<uint8_t>> splitAnnexB(const uint8_t* data, size_t size) {
    std::vector<std::vector<uint8_t>> units;
    if (data == nullptr || size < 3) {
        return units;
    }
    size_t nal_start = kNoPosition;
    size_t i = 0;
    while (i + 2 < size) {
        if (data[i] == 0x00 && data[i + 1] == 0x00 && data[i + 2] == 0x01) {
            if (nal_start != kNoPosition) {
                size_t end = i;
                while (end > nal_start && data[end - 1] == 0x00) {
                    --end;
                }
                if (end > nal_start) {
                    units.emplace_back(data + nal_start, data + end);
                }
            }
            i += 3;
            nal_start = i;
        } else {
            ++i;
        }
    }
    if (nal_start != kNoPosition && nal_start < size) {
        size_t end = size;
        while (end > nal_start && data[end - 1] == 0x00) {
            --end;
        }
        if (end > nal_start) {
            units.emplace_back(data + nal_start, data + end);
        }
    }
    return units;
}

bool looksLikeAnnexB(const uint8_t* data, size_t size) {
    if (data == nullptr || size < 3) {
        return false;
    }
    return (data[0] == 0 && data[1] == 0 && data[2] == 1) ||
           (size >= 4 && data[0] == 0 && data[1] == 0 && data[2] == 0 && data[3] == 1);
}

std::vector<uint8_t> annexBToLengthPrefixed(const uint8_t* data, size_t size) {
    if (looksLikeAnnexB(data, size)) {
        std::vector<uint8_t> out;
        for (const auto& unit : splitAnnexB(data, size)) {
            appendBe32(out, unit.size());
            out.insert(out.end(), unit.begin(), unit.end());
        }
        return out;
    }
    // Already length-prefixed (or a single raw NAL).  Normalise single NALs.
    if (size >= 4) {
        const uint32_t first = (static_cast<uint32_t>(data[0]) << 24) |
                               (static_cast<uint32_t>(data[1]) << 16) |
                               (static_cast<uint32_t>(data[2]) << 8) |
                               static_cast<uint32_t>(data[3]);
        if (first > 0 && first <= size - 4) {
            return std::vector<uint8_t>(data, data + size);
        }
    }
    std::vector<uint8_t> out;
    appendBe32(out, size);
    out.insert(out.end(), data, data + size);
    return out;
}

std::vector<uint8_t> lengthPrefixedToAnnexB(const uint8_t* data, size_t size) {
    if (looksLikeAnnexB(data, size)) {
        return std::vector<uint8_t>(data, data + size);
    }
    static const uint8_t kStartCode[4] = {0x00, 0x00, 0x00, 0x01};
    std::vector<uint8_t> out;
    size_t position = 0;
    while (position + 4 <= size) {
        const size_t length = (static_cast<size_t>(data[position]) << 24) |
                              (static_cast<size_t>(data[position + 1]) << 16) |
                              (static_cast<size_t>(data[position + 2]) << 8) |
                              static_cast<size_t>(data[position + 3]);
        position += 4;
        if (length == 0 || length > size - position) {
            break;
        }
        out.insert(out.end(), kStartCode, kStartCode + 4);
        out.insert(out.end(), data + position, data + position + length);
        position += length;
    }
    if (out.empty()) {
        out.assign(data, data + size);
    }
    return out;
}

std::vector<uint8_t> extractParameterSets(const uint8_t* annexb, size_t size) {
    std::vector<uint8_t> out;
    static const uint8_t kStartCode[4] = {0x00, 0x00, 0x00, 0x01};
    for (const auto& unit : splitAnnexB(annexb, size)) {
        if (unit.size() < 2) {
            continue;
        }
        const int type = (unit[0] >> 1) & 0x3F;
        const bool hevc = type == 32 || type == 33 || type == 34;
        const int h264_type = unit[0] & 0x1F;
        const bool avc = h264_type == 7 || h264_type == 8;
        if (!hevc && !avc) {
            continue;
        }
        out.insert(out.end(), kStartCode, kStartCode + 4);
        out.insert(out.end(), unit.begin(), unit.end());
    }
    return out;
}

std::vector<uint8_t> makeHevcConfigRecord(const uint8_t* annexb, size_t size) {
    std::vector<uint8_t> vps;
    std::vector<uint8_t> sps;
    std::vector<uint8_t> pps;
    for (const auto& unit : splitAnnexB(annexb, size)) {
        if (unit.size() < 3) {
            continue;
        }
        switch ((unit[0] >> 1) & 0x3F) {
            case 32:
                if (vps.empty()) vps = unit;
                break;
            case 33:
                if (sps.empty()) sps = unit;
                break;
            case 34:
                if (pps.empty()) pps = unit;
                break;
            default:
                break;
        }
    }
    if (sps.empty()) {
        return {};
    }
    HevcSpsInfo info;
    if (!parseHevcSps(sps, info)) {
        return {};
    }

    std::vector<uint8_t> out;
    out.reserve(64 + vps.size() + sps.size() + pps.size());
    out.push_back(1);  // configurationVersion
    out.push_back(static_cast<uint8_t>((info.profile_space << 6) |
                                       (info.tier << 5) | info.profile_idc));
    appendBe32(out, info.compatibility);
    out.push_back(static_cast<uint8_t>((info.constraints >> 40) & 0xFF));
    out.push_back(static_cast<uint8_t>((info.constraints >> 32) & 0xFF));
    out.push_back(static_cast<uint8_t>((info.constraints >> 24) & 0xFF));
    out.push_back(static_cast<uint8_t>((info.constraints >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((info.constraints >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(info.constraints & 0xFF));
    out.push_back(static_cast<uint8_t>(info.level_idc));
    out.push_back(0xF0);  // min_spatial_segmentation_idc high nibble
    out.push_back(0x00);
    out.push_back(0xFC);  // parallelismType
    out.push_back(static_cast<uint8_t>(0xFC | (info.chroma_format_idc & 0x03)));
    out.push_back(static_cast<uint8_t>(0xF8 | (info.bit_depth_luma_minus8 & 0x07)));
    out.push_back(static_cast<uint8_t>(0xF8 | (info.bit_depth_chroma_minus8 & 0x07)));
    appendU16(out, 0);  // avgFrameRate
    const int temporal_layers = std::min(info.max_sub_layers_minus1 + 1, 7);
    out.push_back(static_cast<uint8_t>((0 << 6) | (temporal_layers << 3) | (1 << 2) | 3));

    uint8_t arrays = 0;
    if (!vps.empty()) ++arrays;
    ++arrays;  // SPS
    if (!pps.empty()) ++arrays;
    out.push_back(arrays);

    auto appendArray = [&out](const std::vector<uint8_t>& unit, uint8_t type) {
        if (unit.empty() || unit.size() > 65535u) {
            return;
        }
        out.push_back(static_cast<uint8_t>(0x80 | type));
        appendU16(out, 1);
        appendU16(out, unit.size());
        out.insert(out.end(), unit.begin(), unit.end());
    };
    appendArray(vps, 32);
    appendArray(sps, 33);
    appendArray(pps, 34);
    return out;
}

std::vector<uint8_t> makeAvcConfigRecord(const uint8_t* annexb, size_t size) {
    std::vector<std::vector<uint8_t>> sps_units;
    std::vector<std::vector<uint8_t>> pps_units;
    for (const auto& unit : splitAnnexB(annexb, size)) {
        if (unit.size() < 4) {
            continue;
        }
        if ((unit[0] & 0x1F) == 7) {
            sps_units.push_back(unit);
        } else if ((unit[0] & 0x1F) == 8) {
            pps_units.push_back(unit);
        }
    }
    if (sps_units.empty()) {
        return {};
    }
    const auto& sps = sps_units.front();
    std::vector<uint8_t> out;
    out.push_back(1);
    out.push_back(sps[1]);
    out.push_back(sps[2]);
    out.push_back(sps[3]);
    out.push_back(0xFF);  // reserved(6) + lengthSizeMinusOne(2) = 3
    out.push_back(static_cast<uint8_t>(0xE0 | (sps_units.size() & 0x1F)));
    for (const auto& unit : sps_units) {
        appendU16(out, unit.size());
        out.insert(out.end(), unit.begin(), unit.end());
    }
    out.push_back(static_cast<uint8_t>(pps_units.size() & 0xFF));
    for (const auto& unit : pps_units) {
        appendU16(out, unit.size());
        out.insert(out.end(), unit.begin(), unit.end());
    }
    return out;
}

int hevcConfigBitDepth(const uint8_t* record, size_t size) {
    if (record == nullptr || size < 18 || record[0] != 1) {
        return 0;
    }
    return 8 + (record[17] & 0x07);
}

int avcConfigBitDepth(const uint8_t* record, size_t size) {
    if (record == nullptr || size < 4 || record[0] != 1) {
        return 0;
    }
    const int profile = record[1];
    return (profile == 110 || profile == 122 || profile == 244 || profile == 44) ? 10 : 8;
}

bool parseHevcVuiColor(const uint8_t* data, size_t size, HevcVuiColor& color) {
    for (const auto& unit : nalUnits(data, size)) {
        if (parseHevcVuiColorFromSps(unit, color)) {
            return true;
        }
    }
    return false;
}

bool parseHevcSeiHdr10(const uint8_t* data, size_t size, HevcSeiHdr10& hdr) {
    for (const auto& unit : nalUnits(data, size)) {
        parseSeiPayloads(unit, hdr);
    }
    return hdr.has_mastering_display || hdr.has_content_light;
}

}  // namespace rtx
