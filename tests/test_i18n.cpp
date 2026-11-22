// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
//
// Strongly-typed localisation and Stage 2 SSOT parameter schema contract tests.

#include "test_framework.h"

#include <chrono>
#include <string>

#include "rtx/i18n.h"
#include "rtx/logger.h"
#include "rtx/param_schema.h"
#include "rtx/status.h"

using namespace rtx;

RTX_TEST(i18n_table_bounds) {
    RTX_CHECK_EQ(static_cast<size_t>(StrId::Count), static_cast<size_t>(125));
    RTX_CHECK_EQ(static_cast<size_t>(Language::Count), static_cast<size_t>(2));

    const Language saved = currentLanguage();
    for (Language language : {Language::En, Language::Zh}) {
        setLanguage(language);
        RTX_CHECK(currentLanguage() == language);
        for (size_t index = 0; index < static_cast<size_t>(StrId::Count); ++index) {
            const StrId id = static_cast<StrId>(index);
            const wchar_t* wide = trW(id);
            RTX_REQUIRE(wide != nullptr);
            RTX_CHECK(wide[0] != L'\0');
            RTX_CHECK(!tr(id).empty());
            RTX_CHECK(strIdName(id) != std::string("Unknown"));
        }
    }
    RTX_CHECK(trW(static_cast<StrId>(9999))[0] == L'\0');
    RTX_CHECK_EQ(std::string(strIdName(static_cast<StrId>(9999))), std::string("Unknown"));

    setLanguage(Language::En);
    const std::string english = tr(StrId::AppTitle);
    setLanguage(Language::Zh);
    const std::string chinese = tr(StrId::AppTitle);
    RTX_CHECK(english != chinese);
    RTX_CHECK(!english.empty());
    RTX_CHECK(!chinese.empty());

    const std::string json_dict = exportI18nJson();
    RTX_CHECK(json_dict.find("\"en\":") != std::string::npos);
    RTX_CHECK(json_dict.find("\"zh\":") != std::string::npos);
    RTX_CHECK(json_dict.find("\"OptFps60Rife\":") != std::string::npos);
    setLanguage(saved);
}
RTX_TEST(i18n_switch_latency) {
    const Language saved = currentLanguage();
    constexpr int kIterations = 2000;
    volatile size_t sink = 0;

    const auto start = std::chrono::steady_clock::now();
    for (int iteration = 0; iteration < kIterations; ++iteration) {
        const Language language = (iteration & 1) == 0 ? Language::En : Language::Zh;
        setLanguage(language);
        for (size_t index = 0; index < static_cast<size_t>(StrId::Count); ++index) {
            const wchar_t* text = trW(static_cast<StrId>(index));
            sink = sink + (text != nullptr ? 1u : 0u);
        }
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
                             std::chrono::steady_clock::now() - start)
                             .count();

    const double per_lookup_ns =
        static_cast<double>(elapsed) / (static_cast<double>(kIterations) *
                                        static_cast<double>(StrId::Count));
    RTX_CHECK(per_lookup_ns < 1000.0);
    RTX_CHECK(sink > 0);

    RTX_CHECK(parseLanguage("zh-CN").has_value());
    RTX_CHECK(parseLanguage("EN").has_value());
    RTX_CHECK(parseLanguage("").has_value());
    RTX_CHECK_EQ(parseLanguage("klingon").error().code, ErrorCode::InvalidArgument);
    RTX_CHECK(parseLanguage("zh").value() == Language::Zh);
    RTX_CHECK(parseLanguage("en").value() == Language::En);

    setLanguage(saved);
}
RTX_TEST(param_schema_ssot_contract) {
    RTX_CHECK_EQ(kParamSchema.size(), static_cast<size_t>(20));

    TranscodeProfile profile{};
    profile.input = "clip.mp4";
    const auto valid = validateAndNormalizeProfile(profile, /*require_io_paths=*/true);
    RTX_REQUIRE(valid.has_value());
    RTX_CHECK_EQ(profile.output, std::string("clip_remaster_av1.mp4"));
    RTX_CHECK_EQ(profile.rate_control, std::string("cq"));
    RTX_CHECK_EQ(profile.qp, 22);
    RTX_CHECK_EQ(profile.preset, 5);
    RTX_CHECK_EQ(profile.vsr_quality, 4);
    RTX_CHECK_EQ(profile.peak_nits, 1000);
    RTX_CHECK_EQ(profile.fps_mode, std::string("60-nvofa"));
    RTX_CHECK_EQ(profile.pipeline_order, std::string("vsr-first"));

    profile.codec = "hevc";
    RTX_REQUIRE(validateAndNormalizeProfile(profile, true).has_value());
    RTX_CHECK_EQ(profile.output, std::string("clip_remaster_hevc.mp4"));
// Changing codec to hevc with auto_output=true updates auto-generated output suffix.

    profile.codec = "av1_cpu";
    auto r_cpu_av1 = validateAndNormalizeProfile(profile, true);
    RTX_REQUIRE(!r_cpu_av1.has_value());
// CPU codecs return structured ErrorCode::Unsupported
    RTX_CHECK(r_cpu_av1.error().code == ErrorCode::Unsupported);

    profile.codec = "hevc_cpu";
    auto r_cpu_hevc = validateAndNormalizeProfile(profile, true);
    RTX_REQUIRE(!r_cpu_hevc.has_value());
    RTX_CHECK(r_cpu_hevc.error().code == ErrorCode::Unsupported);

    profile.codec = "h264";
    RTX_CHECK(!validateAndNormalizeProfile(profile, true).has_value());

// Deprecated 8-bit SDR H.264 output is rejected by the 10-bit HDR10 pipeline.
    const std::string schema_json = exportSchemaJson();
    RTX_CHECK(schema_json.find("\"defaults\":") != std::string::npos);
    RTX_CHECK(schema_json.find("\"qp\": 22") != std::string::npos);
// JSON parsing and schema export round-trip.
    RTX_CHECK(schema_json.find("\"pipelineOrder\": \"vsr-first\"") != std::string::npos);

    auto parsed = parseProfileFromJson(
        "{\"profile\":{\"input\":\"a.mp4\",\"codec\":\"hevc\",\"preset\":6,\"peakNits\":1500,\"fpsMode\":\"60-rife\",\"pipelineOrder\":\"fg-first\"}}");
    RTX_REQUIRE(parsed.has_value());
    RTX_REQUIRE(validateAndNormalizeProfile(parsed.value(), true).has_value());
    const HwTranscodeOptions hw = profileToHwOptions(parsed.value());
    RTX_CHECK(hw.codec == VideoCodec::HEVC);
    RTX_CHECK_EQ(hw.preset, 6);
    RTX_CHECK_EQ(hw.peak_nits, 1500);
    RTX_CHECK(!hw.rife_engine_path.empty());
    RTX_CHECK_EQ(hw.vsr_before_fg, false);
}
