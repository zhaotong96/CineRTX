// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
//
// HDR10 metadata synthesis.  The same metadata object feeds three independent
// sinks: NVENC VUI/SEI, the container layer, and the JSON sidecar used by the
// QA gates.  Keeping a single source of truth is what prevents the classic
// "PQ10 without HDR10 badge" failure mode.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "rtx/status.h"
#include "rtx/types.h"

namespace rtx {

enum class MasteringPreset {
    DciP3D65,
    Bt2020,
};

struct TrueHdrParameters {
    // RTX Video SDK uses 100-based integers for contrast/saturation and
    // middle-grey; these are the values aligned with the NVIDIA App defaults.
    int contrast = 125;
    int saturation = 75;
    int middle_gray = 44;
    int max_luminance = 1000;
    MasteringPreset mastering = MasteringPreset::DciP3D65;
    int max_fall = 300;

    Result<void> validate() const;
};

// Mirrors NV_ENC_CONFIG_HEVC_VUI_PARAMETERS for the fields the engine sets.
// The shim copies these values verbatim into the SDK structure; no NVIDIA
// header is required in the GPL core.
struct HevcVuiParameters {
    uint8_t aspect_ratio_info_present_flag = 0;
    uint8_t colour_description_present_flag = 1;
    uint8_t colour_primaries = 9;
    uint8_t transfer_characteristics = 16;
    uint8_t matrix_coefficients = 9;
    uint8_t chroma_sample_location_flag = 1;
    uint8_t chroma_sample_loc_type_top_field = 2;    // top-left
    uint8_t chroma_sample_loc_type_bottom_field = 2; // top-left
    uint8_t video_full_range_flag = 0;
};

struct NvencHevcHdrSettings {
    uint32_t pixel_bit_depth_minus8 = 2;
    uint32_t output_mastering_display = 1;
    uint32_t output_max_cll = 1;
    HevcVuiParameters vui{};
};

// Phase 7 (Task 7-B2): one frame's real GPU-reduced luminance statistics for
// the SMPTE ST 2094-40 (HDR10+) dynamic metadata synthesis.  Plain aggregate
// so designated initialisers keep working.
struct Hdr10PlusFrameStats {
    std::uint32_t maxscl_r_nits = 1000;
    std::uint32_t maxscl_g_nits = 1000;
    std::uint32_t maxscl_b_nits = 1000;
    std::uint32_t average_maxrgb_nits = 400;
    std::uint16_t knee_point_x = 500;
    std::uint16_t knee_point_y = 500;
};

class SeiSynthesizer {
public:
    static Result<Hdr10Metadata> makeMetadata(const TrueHdrParameters& params);

    static MasteringDisplay makeMasteringDisplay(MasteringPreset preset, uint32_t max_nits,
                                                 uint32_t min_nits_10000 = 1);
    static NvencHevcHdrSettings makeNvencSettings(const Hdr10Metadata& metadata);

    // HEVC SEI payloads, excluding the payload-type and payload-size fields.
    static std::vector<uint8_t> masteringDisplayPayload(const Hdr10Metadata& metadata);
    static std::vector<uint8_t> contentLightPayload(const Hdr10Metadata& metadata);

    // Complete Annex-B prefix SEI NAL unit (start code included) carrying both
    // payload type 137 and 144.  Emulation-prevention bytes are inserted.
    static std::vector<uint8_t> prefixSeiNal(const Hdr10Metadata& metadata);

    // AV1 metadata OBU payload (mdcv + clli) for container/bitstream use.
    static std::vector<uint8_t> av1MetadataObu(const Hdr10Metadata& metadata);

    // Phase 7 (Task 7-B2): ITU-T T.35 carrier of the SMPTE ST 2094-40
    // (HDR10+) dynamic metadata payload: country_code 0xB5, terminal provider
    // code 0x003C, terminal provider oriented code 0x00, application
    // identifier 0x04, followed by the ST 2094-40 v1 bitstream built from the
    // frame statistics.
    static std::vector<uint8_t> hdr10PlusItuT35Payload(const Hdr10PlusFrameStats& stats);

    // Phase 7 (Task 7-B2): the same T.35 payload wrapped as an AV1
    // OBU_METADATA (OBU type 6, METADATA_TYPE_ITUT_T35 = 3) temporal-unit
    // fragment ready to prepend to an encoded AV1 packet.
    static std::vector<uint8_t> av1Hdr10PlusMetadataObu(const Hdr10PlusFrameStats& stats);

    // Phase 7 (Task 7-B2): complete Annex-B prefix SEI NAL (start code
    // included, payload type 4 = user data registered by ITU-T T.35) for the
    // HEVC Main10 HDR10+ path.
    static std::vector<uint8_t> hevcHdr10PlusPrefixSei(const Hdr10PlusFrameStats& stats);

    static std::vector<uint8_t> insertEmulationPrevention(const std::vector<uint8_t>& rbsp);
};

}  // namespace rtx
