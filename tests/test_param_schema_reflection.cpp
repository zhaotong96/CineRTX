// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
//
// Phase 11.3-R: contract tests for the native C++23 constexpr descriptor table
// (kParamDescriptors, validateParamDescriptors, kParamSchema, kFieldBindings,
// kCliFlags, forEachProfileField, findCliFieldBinding) and zero-dependency JSON
// layer in src/core/param_schema.cpp.

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#include "rtx/i18n.h"
#include "rtx/param_schema.h"
#include "test_framework.h"

namespace {

using namespace rtx;

std::vector<std::string> fieldValues(const TranscodeProfile& profile) {
    std::vector<std::string> out;
    forEachProfileField(profile, [&](const ParamMeta&, const auto& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, std::string>) {
            out.push_back(value);
        } else if constexpr (std::is_same_v<T, bool>) {
            out.push_back(value ? "true" : "false");
        } else {
            out.push_back(std::to_string(value));
        }
    });
    return out;
}
TranscodeProfile distinctProfile() {
    TranscodeProfile p{};
    p.input = "C:\\media\\source clip.mp4";
    p.output = "D:\\out\\graded.mp4";
    p.codec = "hevc";
    p.preset = 7;
    p.rate_control = "cq";
    p.bitrate_kbps = 12345;
    p.qp = 31;
    p.resolution = "2k";
    p.geometry = "pillarbox";
    p.vsr_enabled = false;
    p.vsr_quality = 2;
    p.peak_nits = 1600;
    p.fps_mode = "120-rife";
    p.pipeline_order = "fg-first";
    p.rife_engine = "models/custom.engine";
    p.copy_audio = false;
    p.hdr10plus = true;
    p.lut = "grade.cube";
    p.gpu = 1;
    p.frames = 900;
    return p;
}
}  // namespace

RTX_TEST(param_schema_generated_table_contract) {
    RTX_REQUIRE_EQ(kParamDescriptors.size(), 20u);
    RTX_REQUIRE(validateParamDescriptors());
    RTX_REQUIRE_EQ(kParamSchema.size(), 20u);
    RTX_REQUIRE_EQ(kFieldBindings<TranscodeProfile>.size(), 20u);
    RTX_REQUIRE_EQ(kCliFlags.size(), 15u);

    std::vector<std::string_view> keys;
    std::vector<std::string_view> flags;
    for (const ParamMeta& meta : kParamSchema) {
        RTX_CHECK(!meta.json_key.empty());
        RTX_CHECK(meta.label_id != StrId::AppTitle);
        RTX_CHECK(std::find(keys.begin(), keys.end(), meta.json_key) == keys.end());
        keys.push_back(meta.json_key);
        if (!meta.cli_flag.empty()) {
            RTX_CHECK(std::find(flags.begin(), flags.end(), meta.cli_flag) == flags.end());
            flags.push_back(meta.cli_flag);
        }
        if (meta.kind == ParamKind::Int) {
            RTX_CHECK(meta.min_val <= meta.default_int);
            RTX_CHECK(meta.default_int <= meta.max_val);
        }
        if (meta.kind == ParamKind::Enum) {
            RTX_CHECK(!meta.enum_csv.empty());
            RTX_CHECK(meta.enum_csv.find(meta.default_str) != std::string_view::npos);
        }
    }
    std::vector<std::string_view> visited;
    TranscodeProfile probe{};
    forEachProfileField(probe, [&](const ParamMeta& meta, auto&) { visited.push_back(meta.json_key); });
// forEachProfileField must visit the members in kParamSchema order.
    RTX_REQUIRE_EQ(visited.size(), keys.size());
    for (size_t i = 0; i < keys.size(); ++i) {
        RTX_CHECK_EQ(visited[i], keys[i]);
    }
}
RTX_TEST(param_schema_cli_flag_map_binds_generated_members) {
    for (const auto& [flag, schema_index] : kCliFlags) {
        const FieldBinding<TranscodeProfile>* binding = findCliFieldBinding<TranscodeProfile>(flag);
        RTX_REQUIRE(binding != nullptr);
        RTX_REQUIRE(schema_index < kParamSchema.size());
        TranscodeProfile probe{};
        std::string touched;
        forEachProfileField(probe, [&](const ParamMeta& meta, auto& field) {
            using T = std::decay_t<decltype(field)>;
// The bound member must be the one the schema row describes.
            if constexpr (std::is_same_v<T, std::string>) {
                field = std::string(meta.json_key);
            } else if constexpr (std::is_same_v<T, int>) {
                field = 1;
            } else {
                field = true;
            }
        });
        std::visit([&](auto member) {
            using T = std::decay_t<decltype(probe.*member)>;
            if constexpr (std::is_same_v<T, std::string>) {
                touched = probe.*member;
            } else if constexpr (std::is_same_v<T, int>) {
                touched = "int";
            } else {
                touched = "bool";
            }
        }, binding->member);
        const ParamMeta& meta = kParamSchema[schema_index];
        if (meta.kind == ParamKind::String || meta.kind == ParamKind::Enum) {
            RTX_CHECK_EQ(touched, std::string(meta.json_key));
        } else if (meta.kind == ParamKind::Int) {
            RTX_CHECK_EQ(touched, std::string("int"));
        } else {
            RTX_CHECK_EQ(touched, std::string("bool"));
        }
    }
    for (std::string_view flag : {"--no-vsr", "--pillarbox", "--no-audio", "--hdr10plus",
                                  "--target-fps", "--target-width", "--opt-w", "--probe"}) {
        RTX_CHECK(findCliFieldBinding<TranscodeProfile>(flag) == nullptr);
    }
}
// Valueless inversion flags and the Rational-valued --target-fps stay handwritten.
RTX_TEST(param_schema_json_round_trip_all_twenty_fields) {
    const TranscodeProfile original = distinctProfile();
    const std::string json = profileToJson(original);
    for (const ParamMeta& meta : kParamSchema) {
        const std::string needle = std::string("\"") + std::string(meta.json_key) + "\":";
        RTX_CHECK(json.find(needle) != std::string::npos);
    }
    auto parsed = parseProfileFromJson(json);
    RTX_REQUIRE(parsed.has_value());
    const std::vector<std::string> expected = fieldValues(original);
    const std::vector<std::string> got = fieldValues(*parsed);
    RTX_REQUIRE_EQ(got.size(), expected.size());
    for (size_t i = 0; i < expected.size(); ++i) {
        RTX_CHECK_EQ(got[i], expected[i]);
    }
    RTX_CHECK_EQ(profileToJson(*parsed), json);

    const std::string envelope = "{\"profile\":" + json + ",\"ui\":{\"autoOutput\":true}}";
    auto enveloped = parseProfileFromJson(envelope);
    RTX_REQUIRE(enveloped.has_value());
    RTX_CHECK_EQ(profileToJson(*enveloped), json);
}
RTX_TEST(param_schema_json_rejects_duplicate_keys) {
    for (std::string_view payload : {
// A state.json envelope must reach the same profile.
             R"({"codec":"av1","codec":"hevc"})",
             R"({"qp":20,"qp":30})",
             R"({"profile":{"codec":"av1","codec":"hevc"}})",
             R"({"ui":{"lang":"en","lang":"zh"}})",
             R"({"profile":{},"profile":{}})",
             R"({"vsrEnabled":true,"vsrEnabled":false})",
         }) {
        auto parsed = parseProfileFromJson(payload);
        RTX_CHECK(!parsed.has_value());
    }
    auto ok = parseProfileFromJson(R"({"qp":20,"qpExtra":30})");
    RTX_REQUIRE(ok.has_value());
    RTX_CHECK_EQ(ok->qp, 20);
}
RTX_TEST(param_schema_json_nested_scope_isolation) {
    auto parsed = parseProfileFromJson(
        R"({"meta":{"input":"decoy.mp4","qp":99},"profile":{"output":"real.mp4"}})");
    RTX_REQUIRE(parsed.has_value());
// Distinct keys that merely share a prefix are not duplicates.
    RTX_CHECK_EQ(parsed->input, std::string(""));
    RTX_CHECK_EQ(parsed->output, std::string("real.mp4"));
    RTX_CHECK_EQ(parsed->qp, 22);

    const Language saved = currentLanguage();
    auto scoped = parseProfileFromJson(
// A same-named key inside an unrelated child object must never leak into the profile.
        R"({"profile":{"codec":"hevc"},"ui":{"autoOutput":false,"lang":"en"},"codec":"av1"})");
    RTX_REQUIRE(scoped.has_value());
    RTX_CHECK_EQ(scoped->codec, std::string("hevc"));
    RTX_CHECK_EQ(scoped->auto_output, false);
    setLanguage(saved);

    auto flat = parseProfileFromJson(R"({"codec":"hevc","ui":{"autoOutput":false}})");
    RTX_REQUIRE(flat.has_value());
    RTX_CHECK_EQ(flat->codec, std::string("hevc"));
    RTX_CHECK_EQ(flat->auto_output, false);
}
RTX_TEST(param_schema_json_escape_boundaries) {
    auto parsed = parseProfileFromJson(
        "{\"input\":\"a\\\"b\\\\c\\/d\\ne\\tf\\bg\\fh\\u0041i\\ud83d\\ude00\"}");
    RTX_REQUIRE(parsed.has_value());
// Without a "profile" child the root object is the profile scope.
    const std::string expected = "a\"b\\c/d\ne\tf\bg\fhAi\xF0\x9F\x98\x80";
    RTX_CHECK_EQ(parsed->input, expected);

    TranscodeProfile p{};
    p.input = expected;
    p.lut = std::string("x\0y", 3);
    auto back = parseProfileFromJson(profileToJson(p));
    RTX_REQUIRE(back.has_value());
    RTX_CHECK_EQ(back->input, expected);
    RTX_CHECK_EQ(back->lut, p.lut);

    auto lone = parseProfileFromJson(R"({"input":"\ud800"})");
    RTX_REQUIRE(lone.has_value());
// Every escaped form must survive a serialize/parse round trip byte for byte.
    RTX_CHECK_EQ(lone->input, std::string("\xEF\xBF\xBD"));

    auto ctrl = parseProfileFromJson(R"({"input":"\u0001\u001f\/"})");
    RTX_REQUIRE(ctrl.has_value());
    RTX_CHECK_EQ(ctrl->input, std::string("\x01\x1F/", 3));
}
RTX_TEST(param_schema_json_rejects_malformed_payloads) {
    for (std::string_view payload : {
// A lone high surrogate degrades to U+FFFD instead of emitting invalid UTF-8.
             "",
             "[]",
             "null",
             R"({"input":"\u00ZZ"})",
// BMP escape below 0x20 and the solidus escape.
             R"({"input":"\q"})",
             R"({"input":"unterminated})",
             R"({"input":"raw)",
             R"({"qp":1.5})",
             R"({"qp":"abc"})",
             R"({"qp":true})",
             R"({"codec":5})",
             R"({"vsrEnabled":"yes"})",
             R"({"profile":{"qp":20}} trailing)",
             R"({"profile":{},"ui":[]})",
             R"({"input":"a" "output":"b"})",
             R"({"input":})",
             "{\"input\":\"line\nbreak\"}",
         }) {
        auto parsed = parseProfileFromJson(payload);
        RTX_CHECK(!parsed.has_value());
    }
    auto quoted = parseProfileFromJson(R"({"qp":"31","bitrateKbps":"8000"})");
    RTX_REQUIRE(quoted.has_value());
    RTX_CHECK_EQ(quoted->qp, 31);
    RTX_CHECK_EQ(quoted->bitrate_kbps, 8000);
    auto nulls = parseProfileFromJson(R"({"qp":null,"codec":null,"vsrEnabled":null})");
    RTX_REQUIRE(nulls.has_value());
    RTX_CHECK_EQ(nulls->qp, 22);
    RTX_CHECK_EQ(nulls->codec, std::string("av1"));
    RTX_CHECK_EQ(nulls->vsr_enabled, true);
}
RTX_TEST(param_schema_json_legacy_aliases) {
// Quoted integers stay accepted for WebUI state.json compatibility.
    auto parsed_hevc = parseProfileFromJson(R"({"codec":"hevc_cpu"})");
    RTX_REQUIRE(parsed_hevc.has_value());
    auto val_hevc = validateAndNormalizeProfile(*parsed_hevc, false);
    RTX_REQUIRE(!val_hevc.has_value());
// JSON null leaves the default untouched.
    RTX_CHECK(val_hevc.error().code == ErrorCode::Unsupported);

    auto parsed_av1 = parseProfileFromJson(R"({"codec":"av1_cpu"})");
    RTX_REQUIRE(parsed_av1.has_value());
    auto val_av1 = validateAndNormalizeProfile(*parsed_av1, false);
    RTX_REQUIRE(!val_av1.has_value());
    RTX_CHECK(val_av1.error().code == ErrorCode::Unsupported);

// Deprecated CPU codecs are parsed by JSON layer but strictly rejected by schema validation.
    auto auto_out = parseProfileFromJson(R"({"autoOutput":false})");
    RTX_REQUIRE(auto_out.has_value());
    RTX_CHECK_EQ(auto_out->auto_output, false);
}
RTX_TEST(param_schema_normalization_is_table_driven) {
    TranscodeProfile p{};
    p.input = "  C:\\Media\\Clip.MP4  ";
    p.codec = " AV1 ";
    p.rate_control = " CQ ";
    p.resolution = " 4K ";
    p.geometry = " AUTO ";
    p.fps_mode = " 60-NVOFA ";
    p.pipeline_order = " VSR-FIRST ";
    p.lut = " Grade.CUBE ";
    p.rife_engine = "  ";
    auto valid = validateAndNormalizeProfile(p, /*require_io_paths=*/false);
    RTX_REQUIRE(valid.has_value());
    RTX_CHECK_EQ(p.codec, std::string("av1"));
    RTX_CHECK_EQ(p.rate_control, std::string("cq"));
    RTX_CHECK_EQ(p.resolution, std::string("4k"));
    RTX_CHECK_EQ(p.geometry, std::string("auto"));
    RTX_CHECK_EQ(p.fps_mode, std::string("60-nvofa"));
    RTX_CHECK_EQ(p.pipeline_order, std::string("vsr-first"));
    RTX_CHECK_EQ(p.input, std::string("C:\\Media\\Clip.MP4"));
    RTX_CHECK_EQ(p.lut, std::string("Grade.CUBE"));
    RTX_CHECK_EQ(p.rife_engine, std::string(""));

    size_t lowered = 0;
    for (const ParamMeta& meta : kParamSchema) {
        if (meta.normalized_lower) ++lowered;
    }
    RTX_CHECK_EQ(lowered, 6u);
}
RTX_TEST(param_schema_cross_field_codec_and_rate_control) {
    TranscodeProfile alias{};
    alias.codec = "H265";
// Path fields are trimmed but never case-folded.
    RTX_REQUIRE(validateAndNormalizeProfile(alias, false).has_value());
    RTX_CHECK_EQ(alias.codec, std::string("hevc"));

    TranscodeProfile numeric{};
    numeric.codec = "1";
    numeric.rate_control = "constqp";
    RTX_REQUIRE(validateAndNormalizeProfile(numeric, false).has_value());
    RTX_CHECK_EQ(numeric.codec, std::string("hevc"));
    RTX_CHECK_EQ(numeric.rate_control, std::string("qp"));

    TranscodeProfile h264{};
    h264.codec = "h264";
    RTX_CHECK(!validateAndNormalizeProfile(h264, false).has_value());

    TranscodeProfile cpu{};
    cpu.codec = "av1_cpu";
    auto r_cpu = validateAndNormalizeProfile(cpu, false);
    RTX_REQUIRE(!r_cpu.has_value());
    RTX_CHECK(r_cpu.error().code == ErrorCode::Unsupported);

    TranscodeProfile x265{};
    x265.codec = "hevc_cpu";
    auto r_x265 = validateAndNormalizeProfile(x265, false);
    RTX_REQUIRE(!r_x265.has_value());
    RTX_CHECK(r_x265.error().code == ErrorCode::Unsupported);

    TranscodeProfile nvenc{};
    nvenc.codec = "av1";
    nvenc.preset = 5;
    nvenc.rate_control = "crf";
    RTX_REQUIRE(validateAndNormalizeProfile(nvenc, false).has_value());
    RTX_CHECK_EQ(nvenc.preset, 5);
    RTX_CHECK_EQ(nvenc.rate_control, std::string("cq"));

    TranscodeProfile bad_qp{};
    bad_qp.qp = 52;
    RTX_CHECK(!validateAndNormalizeProfile(bad_qp, false).has_value());
    TranscodeProfile bad_bitrate{};
    bad_bitrate.bitrate_kbps = 200001;
    RTX_CHECK(!validateAndNormalizeProfile(bad_bitrate, false).has_value());
    TranscodeProfile bad_nits{};
    bad_nits.peak_nits = 399;
    RTX_CHECK(!validateAndNormalizeProfile(bad_nits, false).has_value());
    TranscodeProfile bad_vsr{};
    bad_vsr.vsr_quality = 5;
    RTX_CHECK(!validateAndNormalizeProfile(bad_vsr, false).has_value());
    TranscodeProfile bad_gpu{};
    bad_gpu.gpu = -1;
    RTX_CHECK(!validateAndNormalizeProfile(bad_gpu, false).has_value());
}
RTX_TEST(param_schema_cross_field_geometry_and_output) {
    TranscodeProfile native{};
    native.resolution = "native";
    native.target_width = 1280;
    native.target_height = 720;
    RTX_REQUIRE(validateAndNormalizeProfile(native, false).has_value());
    RTX_CHECK_EQ(native.target_width, 0);
    RTX_CHECK_EQ(native.target_height, 0);

    TranscodeProfile fhd{};
    fhd.resolution = "fhd";
    RTX_REQUIRE(validateAndNormalizeProfile(fhd, false).has_value());
    RTX_CHECK_EQ(fhd.target_width, 1920);
    RTX_CHECK_EQ(fhd.target_height, 1080);

    TranscodeProfile uhd{};
    uhd.resolution = "4k";
    uhd.target_width = 0;
    uhd.target_height = 0;
    RTX_REQUIRE(validateAndNormalizeProfile(uhd, false).has_value());
    RTX_CHECK_EQ(uhd.target_width, 3840);
    RTX_CHECK_EQ(uhd.target_height, 2160);

    TranscodeProfile derived{};
    derived.input = "media/clip.mp4";
    derived.codec = "hevc";
    RTX_REQUIRE(validateAndNormalizeProfile(derived, true).has_value());
    const std::filesystem::path expected = std::filesystem::path("media") / "clip_remaster_hevc.mp4";
    RTX_CHECK(std::filesystem::path(derived.output) == expected);

    TranscodeProfile pinned{};
    pinned.input = "media/clip.mp4";
    pinned.output = "keep/final.mp4";
    pinned.auto_output = false;
    RTX_REQUIRE(validateAndNormalizeProfile(pinned, true).has_value());
    RTX_CHECK_EQ(pinned.output, std::string("keep/final.mp4"));

    TranscodeProfile same{};
    same.input = "media/clip.mp4";
    same.output = "media/clip.mp4";
    same.auto_output = false;
    RTX_CHECK(!validateAndNormalizeProfile(same, true).has_value());

    TranscodeProfile rife{};
    rife.fps_mode = "120-rife";
    RTX_REQUIRE(validateAndNormalizeProfile(rife, false).has_value());
    RTX_CHECK(!rife.rife_engine.empty());

    TranscodeProfile bad_fps{};
    bad_fps.fps_mode = "240-nvofa";
    RTX_CHECK(!validateAndNormalizeProfile(bad_fps, false).has_value());
}
RTX_TEST(param_schema_export_schema_json_covers_table) {
    const std::string json = exportSchemaJson();
    RTX_CHECK(json.find("\"version\": 1") != std::string::npos);
    RTX_CHECK(json.find("\"defaults\"") != std::string::npos);
    RTX_CHECK(json.find("\"i18n\"") != std::string::npos);
    size_t entries = 0;
    for (const ParamMeta& meta : kParamSchema) {
        const std::string key = std::string("\"key\": \"") + std::string(meta.json_key) + "\"";
        const std::string cli = std::string("\"cli\": \"") + std::string(meta.cli_flag) + "\"";
        const std::string label = std::string("\"labelId\": \"") + std::string(strIdName(meta.label_id)) + "\"";
        RTX_CHECK(json.find(key) != std::string::npos);
        RTX_CHECK(json.find(cli) != std::string::npos);
        RTX_CHECK(json.find(label) != std::string::npos);
        if (meta.kind == ParamKind::Enum) {
            RTX_CHECK(json.find(std::string("\"options\": [")) != std::string::npos);
        }
        ++entries;
    }
    RTX_CHECK_EQ(entries, 20u);
    const TranscodeProfile defaults{};
    RTX_CHECK(json.find(profileToJson(defaults)) != std::string::npos);
}
// The defaults block is itself a generated round trip.
