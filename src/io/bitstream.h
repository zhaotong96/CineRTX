// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
//
// Annex-B / length-prefixed NAL unit helpers and codec configuration record
// builders.  Phase 12 (Wave 12.2-B2): eliminated HevcVuiColor and HevcSeiHdr10
// passive data bags in favor of std::tuple unpacked via structured bindings.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <tuple>
#include <vector>

namespace rtx {

std::vector<std::vector<uint8_t>> splitAnnexB(std::span<const uint8_t> data);
std::vector<std::vector<uint8_t>> splitAnnexB(const uint8_t* data, size_t size);

bool looksLikeAnnexB(std::span<const uint8_t> data);
bool looksLikeAnnexB(const uint8_t* data, size_t size);

std::vector<uint8_t> annexBToLengthPrefixed(const uint8_t* data, size_t size);
std::vector<uint8_t> lengthPrefixedToAnnexB(const uint8_t* data, size_t size);

std::vector<uint8_t> makeHevcConfigRecord(const uint8_t* annexb, size_t size);
std::vector<uint8_t> makeAvcConfigRecord(const uint8_t* annexb, size_t size);
std::vector<uint8_t> makeAv1ConfigRecord(const uint8_t* obu_data, size_t size, bool ten_bit = true);
std::vector<uint8_t> extractParameterSets(const uint8_t* annexb, size_t size);

int hevcConfigBitDepth(const uint8_t* record, size_t size);
int avcConfigBitDepth(const uint8_t* record, size_t size);

// VUI tuple layout: (has_colour_description, has_video_signal_type, primaries, transfer, matrix, full_range)
bool parseHevcVuiColor(const uint8_t* data, size_t size,
                       std::tuple<bool, bool, int, int, int, bool>& color);

// HDR10 SEI tuple layout:
// (has_mastering_display, has_content_light, [red_x, red_y, green_x, green_y, blue_x, blue_y, white_x, white_y],
//  max_luminance, min_luminance, max_cll, max_fall)
bool parseHevcSeiHdr10(const uint8_t* data, size_t size,
                       std::tuple<bool, bool, std::array<uint16_t, 8>, uint32_t, uint32_t, uint16_t, uint16_t>& hdr);

}  // namespace rtx
