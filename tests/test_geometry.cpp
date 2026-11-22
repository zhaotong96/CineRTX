// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors

#include "test_framework.h"

#include <array>
#include <string_view>
#include <utility>

#include "rtx/geometry.h"
#include "rtx/param_schema.h"

using namespace rtx;

namespace {

void checkPortraitPreset(rtx::test::Context& rtx_test_context, int src_w, int src_h, const char* preset, int expected_w, int expected_h) {
    TranscodeProfile p{};
    p.resolution = preset;
    auto norm = validateAndNormalizeProfile(p, false);
    RTX_REQUIRE(norm.has_value());

    if (std::string_view(preset) == "native") {
        RTX_CHECK_EQ(p.target_width, 0);
        RTX_CHECK_EQ(p.target_height, 0);
        return;
    }

    const GeometryResult g =
        calculateSmartGeometry(src_w, src_h, GeometryTargetMode::AutoDirect,
                               p.target_width, p.target_height);
    RTX_CHECK(g.is_portrait);
    RTX_CHECK_EQ(g.canvas_width, expected_w);
    RTX_CHECK_EQ(g.canvas_height, expected_h);
    RTX_CHECK_EQ(g.canvas_width & 1, 0);
    RTX_CHECK_EQ(g.canvas_height & 1, 0);
    RTX_CHECK(g.canvas_width >= src_w);
    RTX_CHECK(g.canvas_height >= src_h);
    // Preserve 9:16 portrait aspect ratio
    RTX_CHECK_EQ(g.canvas_width * 16, g.canvas_height * 9);
}

}  // namespace

RTX_TEST(geometry_portrait_1080x1920_all_presets) {
    checkPortraitPreset(rtx_test_context, 1080, 1920, "4k", 2160, 3840);
    checkPortraitPreset(rtx_test_context, 1080, 1920, "2k", 1440, 2560);
    checkPortraitPreset(rtx_test_context, 1080, 1920, "fhd", 1080, 1920);
    checkPortraitPreset(rtx_test_context, 1080, 1920, "native", 1080, 1920);
}

RTX_TEST(geometry_portrait_720x1280_all_presets) {
    checkPortraitPreset(rtx_test_context, 720, 1280, "4k", 2160, 3840);
    checkPortraitPreset(rtx_test_context, 720, 1280, "2k", 1440, 2560);
    checkPortraitPreset(rtx_test_context, 720, 1280, "fhd", 1080, 1920);
    checkPortraitPreset(rtx_test_context, 720, 1280, "native", 720, 1280);
}

RTX_TEST(geometry_preset_switching_clears_stale_dimensions) {
    TranscodeProfile p{};
    p.resolution = "2k";
    RTX_REQUIRE(validateAndNormalizeProfile(p, false).has_value());
    RTX_CHECK_EQ(p.target_width, 2560);
    RTX_CHECK_EQ(p.target_height, 1440);

    // Switching back from 2K to 4K must unconditionally overwrite target_width/height
    p.resolution = "4k";
    RTX_REQUIRE(validateAndNormalizeProfile(p, false).has_value());
    RTX_CHECK_EQ(p.target_width, 3840);
    RTX_CHECK_EQ(p.target_height, 2160);

    p.resolution = "fhd";
    RTX_REQUIRE(validateAndNormalizeProfile(p, false).has_value());
    RTX_CHECK_EQ(p.target_width, 1920);
    RTX_CHECK_EQ(p.target_height, 1080);

    p.resolution = "native";
    RTX_REQUIRE(validateAndNormalizeProfile(p, false).has_value());
    RTX_CHECK_EQ(p.target_width, 0);
    RTX_CHECK_EQ(p.target_height, 0);

    p.resolution = "4k";
    RTX_REQUIRE(validateAndNormalizeProfile(p, false).has_value());
    RTX_CHECK_EQ(p.target_width, 3840);
    RTX_CHECK_EQ(p.target_height, 2160);
}

RTX_TEST(geometry_portrait_floor_and_pillarbox) {
    // 4K portrait source with FHD preset must not shrink below source dimensions
    const GeometryResult floor_g =
        calculateSmartGeometry(2160, 3840, GeometryTargetMode::AutoDirect, 1920, 1080);
    RTX_CHECK_EQ(floor_g.canvas_width, 2160);
    RTX_CHECK_EQ(floor_g.canvas_height, 3840);

    // Pillarbox on 1080x1920 -> 3840x2160 canvas computes centered 9:16 subrect
    const GeometryResult pb =
        calculateSmartGeometry(1080, 1920, GeometryTargetMode::Pillarbox, 3840, 2160);
    RTX_CHECK_EQ(pb.canvas_width, 3840);
    RTX_CHECK_EQ(pb.canvas_height, 2160);
    RTX_CHECK_EQ(pb.scaled_height, 2160);
    RTX_CHECK_EQ(pb.scaled_width & 1, 0);
    RTX_CHECK(pb.scaled_width == 1214 || pb.scaled_width == 1216);
    RTX_CHECK(pb.offset_x > 0);
    RTX_CHECK_EQ(pb.offset_x & 1, 0);
    RTX_CHECK_EQ(pb.pillarbox_left + pb.scaled_width + pb.pillarbox_right, pb.canvas_width);
}
