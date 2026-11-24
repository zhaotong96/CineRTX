// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
//
// HDR10 metadata synthesis.  The same metadata object feeds three independent
// sinks: NVENC VUI/SEI, the container layer, and the JSON sidecar used by the
// QA gates.  Keeping a single source of truth is what prevents the classic
// "PQ10 without HDR10 badge" failure mode.

#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <tuple>
#include <vector>

#include "rtx/status.h"
#include "rtx/types.h"

namespace rtx {

enum class MasteringPreset {
    DciP3D65,
    Bt2020,
};

// Phase 12 (Wave 12.2-B2): standard tuple/array aliases replace passive HDR parameter bags.
// TrueHdrParameters: (contrast, saturation, middle_gray, max_luminance, mastering, max_fall)
using TrueHdrParameters = std::tuple<int, int, int, int, MasteringPreset, int>;

[[nodiscard]] constexpr TrueHdrParameters defaultTrueHdrParameters() noexcept {
    return {125, 75, 44, 1000, MasteringPreset::DciP3D65, 300};
}

// HevcVuiParameters: [aspect_ratio_info_present_flag, colour_description_present_flag,
//   colour_primaries, transfer_characteristics, matrix_coefficients,
//   chroma_sample_location_flag, chroma_sample_loc_type_top_field,
//   chroma_sample_loc_type_bottom_field, video_full_range_flag]
using HevcVuiParameters = std::array<uint8_t, 9>;

// NvencHevcHdrSettings: (pixel_bit_depth_minus8, output_mastering_display, output_max_cll, vui)
using NvencHevcHdrSettings = std::tuple<uint32_t, uint32_t, uint32_t, HevcVuiParameters>;

// Hdr10PlusFrameStats: (maxscl_r_nits, maxscl_g_nits, maxscl_b_nits,
//   average_maxrgb_nits, knee_point_x, knee_point_y)
using Hdr10PlusFrameStats = std::tuple<uint32_t, uint32_t, uint32_t, uint32_t, uint16_t, uint16_t>;

namespace SeiSynthesizer {
    Result<void> validateTrueHdr(const TrueHdrParameters& params);
    Result<Hdr10Metadata> makeMetadata(const TrueHdrParameters& params);

    MasteringDisplay makeMasteringDisplay(MasteringPreset preset, uint32_t max_nits,
                                          uint32_t min_nits_10000 = 1);
    NvencHevcHdrSettings makeNvencSettings(const Hdr10Metadata& metadata);

    // HEVC SEI payloads, excluding the payload-type and payload-size fields.
    std::vector<uint8_t> masteringDisplayPayload(const Hdr10Metadata& metadata);
    std::vector<uint8_t> contentLightPayload(const Hdr10Metadata& metadata);

    // Complete Annex-B prefix SEI NAL unit (start code included) carrying both
    // payload type 137 and 144.  Emulation-prevention bytes are inserted.
    std::vector<uint8_t> prefixSeiNal(const Hdr10Metadata& metadata);

    // AV1 metadata OBU payload (mdcv + clli) for container/bitstream use.
    std::vector<uint8_t> av1MetadataObu(const Hdr10Metadata& metadata);

    // Phase 7 (Task 7-B2): ITU-T T.35 carrier of SMPTE ST 2094-40 (HDR10+).
    std::vector<uint8_t> hdr10PlusItuT35Payload(const Hdr10PlusFrameStats& stats);
    std::vector<uint8_t> av1Hdr10PlusMetadataObu(const Hdr10PlusFrameStats& stats);
    std::vector<uint8_t> hevcHdr10PlusPrefixSei(const Hdr10PlusFrameStats& stats);

    std::vector<uint8_t> insertEmulationPrevention(const std::vector<uint8_t>& rbsp);
}  // namespace SeiSynthesizer

}  // namespace rtx
