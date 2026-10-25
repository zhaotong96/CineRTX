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

    // Canonical, endian-independent serialisation used by the sidecar and by
    // tests.  This is NOT the FFmpeg side-data struct.
    static std::vector<uint8_t> containerSideData(const Hdr10Metadata& metadata);

    static std::string toJson(const Hdr10Metadata& metadata);
    static Result<Hdr10Metadata> fromJson(const std::string& json);

    static std::vector<uint8_t> insertEmulationPrevention(const std::vector<uint8_t>& rbsp);
};

}  // namespace rtx
