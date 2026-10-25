// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
//
// Annex-B / length-prefixed NAL unit helpers and codec configuration record
// builders.  These routines are container-agnostic and deliberately do not
// include any FFmpeg or NVIDIA header.

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace rtx {

// Splits an Annex-B byte stream into individual NAL units (without start
// codes).  Empty and trailing-zero units are discarded.
std::vector<std::vector<uint8_t>> splitAnnexB(const uint8_t* data, size_t size);

// True when the buffer begins with an Annex-B start code.
bool looksLikeAnnexB(const uint8_t* data, size_t size);

// Converts every NAL unit in an Annex-B buffer to a 4-byte big-endian length
// prefixed buffer (the representation used by MP4/MKV codec private data).
std::vector<uint8_t> annexBToLengthPrefixed(const uint8_t* data, size_t size);

// Inverse of annexBToLengthPrefixed.  A stream that already starts with a
// start code is returned unchanged.
std::vector<uint8_t> lengthPrefixedToAnnexB(const uint8_t* data, size_t size);

// Builds the ISO/IEC 14496-15 HEVCDecoderConfigurationRecord (hvcC) from an
// Annex-B sequence header containing VPS/SPS/PPS.  Returns an empty vector if
// the required parameter sets are absent or the SPS cannot be parsed.
std::vector<uint8_t> makeHevcConfigRecord(const uint8_t* annexb, size_t size);

// Builds the ISO/IEC 14496-15 AVCDecoderConfigurationRecord (avcC).
std::vector<uint8_t> makeAvcConfigRecord(const uint8_t* annexb, size_t size);

// Extracts VPS/SPS/PPS from an Annex-B sequence header.
std::vector<uint8_t> extractParameterSets(const uint8_t* annexb, size_t size);

// Bit depth advertised by an hvcC/avcC record, or 0 when it cannot be parsed.
int hevcConfigBitDepth(const uint8_t* record, size_t size);
int avcConfigBitDepth(const uint8_t* record, size_t size);

// Colour description carried by the HEVC SPS VUI.  Values are ITU-T H.265
// code points (1 = BT.709, 9 = BT.2020 non-constant luminance, 16 = PQ,
// 18 = HLG, 2 = unspecified).
struct HevcVuiColor {
    bool has_colour_description = false;
    bool has_video_signal_type = false;
    int primaries = 2;
    int transfer = 2;
    int matrix = 2;
    bool full_range = false;
};

// Static HDR10 metadata carried by prefix SEI messages.
struct HevcSeiHdr10 {
    bool has_mastering_display = false;
    bool has_content_light = false;
    uint16_t red_x = 0;
    uint16_t red_y = 0;
    uint16_t green_x = 0;
    uint16_t green_y = 0;
    uint16_t blue_x = 0;
    uint16_t blue_y = 0;
    uint16_t white_x = 0;
    uint16_t white_y = 0;
    uint32_t max_luminance = 0;
    uint32_t min_luminance = 0;
    uint16_t max_cll = 0;
    uint16_t max_fall = 0;
};

// Parses the SPS VUI from hvcC or Annex-B extradata.  Returns false when no
// SPS or no VUI video signal information is present.
bool parseHevcVuiColor(const uint8_t* data, size_t size, HevcVuiColor& color);

// Scans Annex-B or length-prefixed NAL units for prefix/suffix SEI messages
// carrying HDR10 mastering-display and content-light metadata.
bool parseHevcSeiHdr10(const uint8_t* data, size_t size, HevcSeiHdr10& hdr);

}  // namespace rtx
