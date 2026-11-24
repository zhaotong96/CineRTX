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

    const auto [cw, ch, sw, sh, ox, oy, pl, pr, is_portrait] =
        calculateSmartGeometry(src_w, src_h, GeometryTargetMode::AutoDirect,
                               p.target_width, p.target_height);
    (void)sw; (void)sh; (void)ox; (void)oy; (void)pl; (void)pr;
    RTX_CHECK(is_portrait);
    RTX_CHECK_EQ(cw, expected_w);
    RTX_CHECK_EQ(ch, expected_h);
    RTX_CHECK_EQ(cw & 1, 0);
    RTX_CHECK_EQ(ch & 1, 0);
    RTX_CHECK(cw >= src_w);
    RTX_CHECK(ch >= src_h);
    // Preserve 9:16 portrait aspect ratio
    RTX_CHECK_EQ(cw * 16, ch * 9);
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
    const auto [fcw, fch, fsw, fsh, fox, foy, fpl, fpr, fip] =
        calculateSmartGeometry(2160, 3840, GeometryTargetMode::AutoDirect, 1920, 1080);
    (void)fsw; (void)fsh; (void)fox; (void)foy; (void)fpl; (void)fpr; (void)fip;
    RTX_CHECK_EQ(fcw, 2160);
    RTX_CHECK_EQ(fch, 3840);

    // Pillarbox on 1080x1920 -> 3840x2160 canvas computes centered 9:16 subrect
    const auto [pcw, pch, psw, psh, pox, poy, ppl, ppr, pip] =
        calculateSmartGeometry(1080, 1920, GeometryTargetMode::Pillarbox, 3840, 2160);
    (void)poy; (void)pip;
    RTX_CHECK_EQ(pcw, 3840);
    RTX_CHECK_EQ(pch, 2160);
    RTX_CHECK_EQ(psh, 2160);
    RTX_CHECK_EQ(psw & 1, 0);
    RTX_CHECK(psw == 1214 || psw == 1216);
    RTX_CHECK(pox > 0);
    RTX_CHECK_EQ(pox & 1, 0);
    RTX_CHECK_EQ(ppl + psw + ppr, pcw);
}
