// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
//
// Stage 2 Single Source of Truth (SSOT) Parameter Schema & Profile Contract.
//
// Design principles (SOLID + LoD + KISS + Occam's Razor):
//   - One canonical aggregate (`TranscodeProfile`) and one C++23 native
//     compile-time descriptor table (`kParamDescriptors`) govern CLI, Win32 GUI,
//     and WebUI with zero out-of-tree codegen scripts.
//   - Compile-time `consteval validateParamDescriptors()` intercepts duplicate
//     keys/flags and out-of-range defaults at build time.
//   - Native JSON schema export (`exportSchemaJson`) and JSON profile ingestion
//     (`parseProfileFromJson`) power `CineRTX.exe --schema-json` and
//     `CineRTX.exe --session-stdio` with zero external JSON libraries.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

#include "rtx/hw_transcode.h"
#include "rtx/i18n.h"
#include "rtx/status.h"

namespace rtx {

enum class ParamKind : uint8_t {
    String = 0,
    Int = 1,
    Bool = 2,
    Enum = 3,
};

// @rtx-type-justification
// Category: Domain-Model
// Why-Std-Fails: 多字段领域模型：具名字段承载序列化/校验/反射契约，压进 std::tuple 会丢失全部字段语义与默认值契约。
// Invariant: 字段集合与默认值即对外契约（schema/测试逐字段锚定）；只增不删。
struct ParamMeta {
    std::string_view json_key;
    std::string_view cli_flag;
    ParamKind kind = ParamKind::String;
    StrId label_id = StrId::AppTitle;
    int min_val = 0;
    int max_val = 0;
    int default_int = 0;
    bool default_bool = false;
    std::string_view default_str{};
    std::string_view enum_csv{};  // comma-separated allowed tokens for Enum kind
    bool normalized_lower = false;  // ASCII-lowercase this string field during normalization
};

// Canonical transcode profile shared 1:1 across CLI, Win32 GUI and WebUI.
// The default member initializers define the single out-of-the-box best
// experience: 4K UHD (3840x2160 AutoDirect), AV1 10-bit HDR10 (1000 nits),
// NVENC P5, VSR Level 4 Ultra, 60 FPS NVOFA hardware optical flow, audio copy.
// @rtx-type-justification
// Category: Domain-Model
// Why-Std-Fails: 三端（CLI/Win32 GUI/WebUI）共享的 23 字段转码配置领域聚合，承载全部默认值契约与成员指针静态绑定。
// Invariant: 20 项对外契约字段与 kParamDescriptors 严格一一对应；target_width/target_height/auto_output 为派生及 UI 控制字段。
struct TranscodeProfile {
    std::string input{};
    std::string output{};
    std::string codec{"av1"};
    int preset = 5;
    std::string rate_control{"cq"};
    int bitrate_kbps = 0;
    int qp = 22;
    std::string resolution{"4k"};
    int target_width = 3840;
    int target_height = 2160;
    std::string geometry{"auto"};
    bool vsr_enabled = true;
    int vsr_quality = 4;
    int peak_nits = 1000;
    std::string fps_mode{"60-nvofa"};
    std::string pipeline_order{"vsr-first"};
    std::string rife_engine{};
    bool copy_audio = true;
    bool hdr10plus = false;
    std::string lut{};
    int gpu = 0;
    int frames = 0;
    bool auto_output = true;
};

using ProfileMemberPtr = std::variant<std::string TranscodeProfile::*,
                                      int TranscodeProfile::*,
                                      bool TranscodeProfile::*>;

inline constexpr std::array<std::pair<ParamMeta, ProfileMemberPtr>, 20> kParamDescriptors{{
    {{.json_key = "input",         .cli_flag = "--input",          .kind = ParamKind::String, .label_id = StrId::ParamInput},
     &TranscodeProfile::input},
    {{.json_key = "output",        .cli_flag = "--output",         .kind = ParamKind::String, .label_id = StrId::ParamOutput},
     &TranscodeProfile::output},
    {{.json_key = "codec",         .cli_flag = "--codec",          .kind = ParamKind::Enum,   .label_id = StrId::ParamCodec,
      .default_str = "av1", .enum_csv = "av1,hevc", .normalized_lower = true},
     &TranscodeProfile::codec},
    {{.json_key = "preset",        .cli_flag = "--preset",         .kind = ParamKind::Int,    .label_id = StrId::ParamPreset,
      .min_val = 1, .max_val = 7, .default_int = 5},
     &TranscodeProfile::preset},
    {{.json_key = "rateControl",   .cli_flag = "--rate-control",   .kind = ParamKind::Enum,   .label_id = StrId::ParamRateControl,
      .default_str = "cq", .enum_csv = "cq,vbr,cbr,qp", .normalized_lower = true},
     &TranscodeProfile::rate_control},
    {{.json_key = "bitrateKbps",   .cli_flag = "--bitrate",        .kind = ParamKind::Int,    .label_id = StrId::ParamBitrate,
      .min_val = 0, .max_val = 200000, .default_int = 0},
     &TranscodeProfile::bitrate_kbps},
    {{.json_key = "qp",            .cli_flag = "--qp",             .kind = ParamKind::Int,    .label_id = StrId::ParamCq,
      .min_val = 0, .max_val = 51, .default_int = 22},
     &TranscodeProfile::qp},
    {{.json_key = "resolution",    .cli_flag = "--resolution",     .kind = ParamKind::Enum,   .label_id = StrId::ParamResolution,
      .default_str = "4k", .enum_csv = "4k,2k,fhd,native", .normalized_lower = true},
     &TranscodeProfile::resolution},
    {{.json_key = "geometry",      .cli_flag = "--pillarbox",      .kind = ParamKind::Enum,   .label_id = StrId::ParamAspect,
      .default_str = "auto", .enum_csv = "auto,pillarbox", .normalized_lower = true},
     &TranscodeProfile::geometry},
    {{.json_key = "vsrEnabled",    .cli_flag = "--no-vsr",         .kind = ParamKind::Bool,   .label_id = StrId::ParamVsr,
      .default_bool = true},
     &TranscodeProfile::vsr_enabled},
    {{.json_key = "vsrQuality",    .cli_flag = "--vsr-quality",    .kind = ParamKind::Int,    .label_id = StrId::ParamVsrQuality,
      .min_val = 1, .max_val = 4, .default_int = 4},
     &TranscodeProfile::vsr_quality},
    {{.json_key = "peakNits",      .cli_flag = "--peak-nits",      .kind = ParamKind::Int,    .label_id = StrId::ParamPeakNits,
      .min_val = 400, .max_val = 4000, .default_int = 1000},
     &TranscodeProfile::peak_nits},
    {{.json_key = "fpsMode",       .cli_flag = "--target-fps",     .kind = ParamKind::Enum,   .label_id = StrId::WuiParamFps,
      .default_str = "60-nvofa", .enum_csv = "original,60-nvofa,120-nvofa,60-rife,120-rife", .normalized_lower = true},
     &TranscodeProfile::fps_mode},
    {{.json_key = "pipelineOrder", .cli_flag = "--pipeline-order", .kind = ParamKind::Enum,   .label_id = StrId::ParamPipelineOrder,
      .default_str = "vsr-first", .enum_csv = "vsr-first,fg-first", .normalized_lower = true},
     &TranscodeProfile::pipeline_order},
    {{.json_key = "rifeEngine",    .cli_flag = "--rife-engine",    .kind = ParamKind::String, .label_id = StrId::WuiParamRifeEngine},
     &TranscodeProfile::rife_engine},
    {{.json_key = "copyAudio",     .cli_flag = "--no-audio",       .kind = ParamKind::Bool,   .label_id = StrId::ParamAudioCopy,
      .default_bool = true},
     &TranscodeProfile::copy_audio},
    {{.json_key = "hdr10plus",     .cli_flag = "--hdr10plus",      .kind = ParamKind::Bool,   .label_id = StrId::WuiParamHdr10plus,
      .default_bool = false},
     &TranscodeProfile::hdr10plus},
    {{.json_key = "lut",           .cli_flag = "--lut",            .kind = ParamKind::String, .label_id = StrId::WuiParamLut},
     &TranscodeProfile::lut},
    {{.json_key = "gpu",           .cli_flag = "--gpu",            .kind = ParamKind::Int,    .label_id = StrId::WuiParamGpu,
      .min_val = 0, .max_val = 15, .default_int = 0},
     &TranscodeProfile::gpu},
    {{.json_key = "frames",        .cli_flag = "--frames",         .kind = ParamKind::Int,    .label_id = StrId::WuiParamFrames,
      .min_val = 0, .max_val = 100000000, .default_int = 0},
     &TranscodeProfile::frames},
}};

consteval bool validateParamDescriptors() {
    for (std::size_t i = 0; i < kParamDescriptors.size(); ++i) {
        const auto& a = kParamDescriptors[i].first;
        if (a.json_key.empty()) return false;
        if (a.kind == ParamKind::Int && (a.default_int < a.min_val || a.default_int > a.max_val)) {
            return false;
        }
        if (a.kind == ParamKind::Enum) {
            if (a.enum_csv.empty() || a.default_str.empty()) return false;
            bool found_default = false;
            std::string_view csv = a.enum_csv;
            while (!csv.empty()) {
                const std::size_t comma = csv.find(',');
                const std::string_view tok =
                    (comma == std::string_view::npos) ? csv : csv.substr(0, comma);
                if (tok == a.default_str) {
                    found_default = true;
                    break;
                }
                if (comma == std::string_view::npos) break;
                csv.remove_prefix(comma + 1);
            }
            if (!found_default) return false;
        }
        for (std::size_t j = i + 1; j < kParamDescriptors.size(); ++j) {
            const auto& b = kParamDescriptors[j].first;
            if (a.json_key == b.json_key) return false;
            if (!a.cli_flag.empty() && a.cli_flag == b.cli_flag) return false;
        }
    }
    return true;
}
static_assert(validateParamDescriptors(), "Duplicate or out-of-range SSOT parameter descriptor");

// Compile-time projection of kParamDescriptors metadata for legacy callers.
inline constexpr std::array<ParamMeta, 20> kParamSchema = []() consteval {
    std::array<ParamMeta, 20> out{};
    for (std::size_t i = 0; i < kParamDescriptors.size(); ++i) {
        out[i] = kParamDescriptors[i].first;
    }
    return out;
}();

// Regular single-value CLI flags only; valueless inversion flags and subcommands
// stay handwritten in src/cli/main.cpp.
inline constexpr std::array<std::pair<std::string_view, std::size_t>, 15> kCliFlags{{
    {"--input",          0},
    {"--output",         1},
    {"--codec",          2},
    {"--preset",         3},
    {"--rate-control",   4},
    {"--bitrate",        5},
    {"--qp",             6},
    {"--resolution",     7},
    {"--vsr-quality",    10},
    {"--peak-nits",      11},
    {"--pipeline-order", 13},
    {"--rife-engine",    14},
    {"--lut",            17},
    {"--gpu",            18},
    {"--frames",         19},
}};

consteval bool validateCliFlags() {
    for (const auto& [flag, idx] : kCliFlags) {
        if (idx >= kParamDescriptors.size()) return false;
        if (kParamDescriptors[idx].first.cli_flag != flag) return false;
    }
    return true;
}
static_assert(validateCliFlags(), "kCliFlags index/flag mismatch against kParamDescriptors");

// @rtx-type-justification
// Category: Domain-Model
// Why-Std-Fails: CLI 解析层（src/cli/main.cpp）通过 binding->member 具名访问成员指针变体，直接别名 std::variant 会破坏既有 CLI 契约。
// Invariant: 仅持有与 kParamDescriptors 同源的成员指针变体，编译期只读，零运行期状态。
template <class Profile>
struct FieldBinding {
    std::variant<std::string Profile::*, int Profile::*, bool Profile::*> member;
};

template <class Profile>
inline constexpr std::array<FieldBinding<Profile>, 20> kFieldBindings = []() consteval {
    std::array<FieldBinding<Profile>, 20> out{};
    for (std::size_t i = 0; i < kParamDescriptors.size(); ++i) {
        out[i].member = kParamDescriptors[i].second;
    }
    return out;
}();

template <typename Profile, typename Visitor>
constexpr void forEachProfileField(Profile& profile, Visitor&& visitor) {
    for (const auto& [meta, ptr] : kParamDescriptors) {
        std::visit([&](auto member) { visitor(meta, profile.*member); }, ptr);
    }
}

template <class Profile>
[[nodiscard]] constexpr const FieldBinding<std::remove_cv_t<Profile>>* findCliFieldBinding(
    std::string_view flag) noexcept {
    constexpr auto& bindings = kFieldBindings<std::remove_cv_t<Profile>>;
    for (const auto& [cli_flag, schema_index] : kCliFlags) {
        if (cli_flag == flag) return &bindings[schema_index];
    }
    return nullptr;
}

// Derives the canonical output path `<input_dir>/<input_stem>_remaster_<codec>.mp4`
// when `current_output` is empty or (when `auto_output` is true) matches a previously
// auto-generated `_remaster_<codec>.(mp4|mkv)` filename.
[[nodiscard]] std::string deriveOutputPath(std::string_view input_path,
                                           std::string_view codec,
                                           std::string_view current_output = {},
                                           bool auto_output = true);

// Resolves the RIFE TensorRT engine path using the project's canonical search order:
//   1. Explicit user path (if non-empty)
//   2. <exe_dir>/models/rife_v4.25.engine
//   3. models/rife_v4.25.engine
//   4. <exe_dir>/rife.engine
//   5. rife.engine
[[nodiscard]] std::filesystem::path resolveDefaultRifeEnginePath(
    std::string_view explicit_path = {});

// Validates all fields of `profile` against `kParamDescriptors`, normalizes case and
// derived geometry/FPS/output fields, and returns an Error on any contract violation.
[[nodiscard]] Result<void> validateAndNormalizeProfile(TranscodeProfile& profile,
                                                       bool require_io_paths = true);

// Converts a validated `TranscodeProfile` into `HwTranscodeOptions` using C++23
// designated initializers.
[[nodiscard]] HwTranscodeOptions profileToHwOptions(const TranscodeProfile& profile);

// Parses a JSON document (either a direct profile object `{"codec":"av1",...}`
// or a state.json document `{"profile":{...},"ui":{"autoOutput":true}}`) into
// `TranscodeProfile` without external dependencies.
[[nodiscard]] Result<TranscodeProfile> parseProfileFromJson(std::string_view json_text);

// Serializes a `TranscodeProfile` as a JSON object string.
[[nodiscard]] std::string profileToJson(const TranscodeProfile& profile);

// Exports the complete SSOT contract JSON (`schema` + `defaults` + `i18n`) for
// `CineRTX.exe --schema-json`.
[[nodiscard]] std::string exportSchemaJson();

}  // namespace rtx
