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
#include <tuple>
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

// Phase 12 (Wave 12.2-D): standard tuple alias replaces ParamMeta descriptor wrapper.
// Layout: (json_key, cli_flag, kind, label_id, min_val, max_val, default_int, default_bool,
//          default_str, enum_csv, normalized_lower).
using ParamMeta = std::tuple<
    std::string_view, std::string_view, ParamKind, StrId,
    int, int, int, bool, std::string_view, std::string_view, bool>;

[[nodiscard]] constexpr ParamMeta makeParamMeta(
    std::string_view json_key, std::string_view cli_flag, ParamKind kind, StrId label_id,
    int min_val = 0, int max_val = 0, int default_int = 0, bool default_bool = false,
    std::string_view default_str = {}, std::string_view enum_csv = {},
    bool normalized_lower = false) noexcept {
    return {json_key, cli_flag, kind, label_id, min_val, max_val,
            default_int, default_bool, default_str, enum_csv, normalized_lower};
}

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
    {makeParamMeta("input",         "--input",          ParamKind::String, StrId::ParamInput), &TranscodeProfile::input},
    {makeParamMeta("output",        "--output",         ParamKind::String, StrId::ParamOutput), &TranscodeProfile::output},
    {makeParamMeta("codec",         "--codec",          ParamKind::Enum,   StrId::ParamCodec, 0, 0, 0, false, "av1", "av1,hevc", true), &TranscodeProfile::codec},
    {makeParamMeta("preset",        "--preset",         ParamKind::Int,    StrId::ParamPreset, 1, 7, 5), &TranscodeProfile::preset},
    {makeParamMeta("rateControl",   "--rate-control",   ParamKind::Enum,   StrId::ParamRateControl, 0, 0, 0, false, "cq", "cq,vbr,cbr,qp", true), &TranscodeProfile::rate_control},
    {makeParamMeta("bitrateKbps",   "--bitrate",        ParamKind::Int,    StrId::ParamBitrate, 0, 200000, 0), &TranscodeProfile::bitrate_kbps},
    {makeParamMeta("qp",            "--qp",             ParamKind::Int,    StrId::ParamCq, 0, 51, 22), &TranscodeProfile::qp},
    {makeParamMeta("resolution",    "--resolution",     ParamKind::Enum,   StrId::ParamResolution, 0, 0, 0, false, "4k", "4k,2k,fhd,native", true), &TranscodeProfile::resolution},
    {makeParamMeta("geometry",      "--pillarbox",      ParamKind::Enum,   StrId::ParamAspect, 0, 0, 0, false, "auto", "auto,pillarbox", true), &TranscodeProfile::geometry},
    {makeParamMeta("vsrEnabled",    "--no-vsr",         ParamKind::Bool,   StrId::ParamVsr, 0, 0, 0, true), &TranscodeProfile::vsr_enabled},
    {makeParamMeta("vsrQuality",    "--vsr-quality",    ParamKind::Int,    StrId::ParamVsrQuality, 1, 4, 4), &TranscodeProfile::vsr_quality},
    {makeParamMeta("peakNits",      "--peak-nits",      ParamKind::Int,    StrId::ParamPeakNits, 400, 4000, 1000), &TranscodeProfile::peak_nits},
    {makeParamMeta("fpsMode",       "--target-fps",     ParamKind::Enum,   StrId::WuiParamFps, 0, 0, 0, false, "60-nvofa", "original,60-nvofa,120-nvofa,60-rife,120-rife", true), &TranscodeProfile::fps_mode},
    {makeParamMeta("pipelineOrder", "--pipeline-order", ParamKind::Enum,   StrId::ParamPipelineOrder, 0, 0, 0, false, "vsr-first", "vsr-first,fg-first", true), &TranscodeProfile::pipeline_order},
    {makeParamMeta("rifeEngine",    "--rife-engine",    ParamKind::String, StrId::WuiParamRifeEngine), &TranscodeProfile::rife_engine},
    {makeParamMeta("copyAudio",     "--no-audio",       ParamKind::Bool,   StrId::ParamAudioCopy, 0, 0, 0, true), &TranscodeProfile::copy_audio},
    {makeParamMeta("hdr10plus",     "--hdr10plus",      ParamKind::Bool,   StrId::WuiParamHdr10plus, 0, 0, 0, false), &TranscodeProfile::hdr10plus},
    {makeParamMeta("lut",           "--lut",            ParamKind::String, StrId::WuiParamLut), &TranscodeProfile::lut},
    {makeParamMeta("gpu",           "--gpu",            ParamKind::Int,    StrId::WuiParamGpu, 0, 15, 0), &TranscodeProfile::gpu},
    {makeParamMeta("frames",        "--frames",         ParamKind::Int,    StrId::WuiParamFrames, 0, 100000000, 0), &TranscodeProfile::frames},
}};

consteval bool validateParamDescriptors() {
    for (std::size_t i = 0; i < kParamDescriptors.size(); ++i) {
        const auto& [a_key, a_flag, a_kind, a_lbl, a_min, a_max, a_di, a_db, a_ds, a_csv, a_low] =
            kParamDescriptors[i].first;
        (void)a_lbl; (void)a_db; (void)a_low;
        if (a_key.empty()) return false;
        if (a_kind == ParamKind::Int && (a_di < a_min || a_di > a_max)) {
            return false;
        }
        if (a_kind == ParamKind::Enum) {
            if (a_csv.empty() || a_ds.empty()) return false;
            bool found_default = false;
            std::string_view csv = a_csv;
            while (!csv.empty()) {
                const std::size_t comma = csv.find(',');
                const std::string_view tok =
                    (comma == std::string_view::npos) ? csv : csv.substr(0, comma);
                if (tok == a_ds) {
                    found_default = true;
                    break;
                }
                if (comma == std::string_view::npos) break;
                csv.remove_prefix(comma + 1);
            }
            if (!found_default) return false;
        }
        for (std::size_t j = i + 1; j < kParamDescriptors.size(); ++j) {
            const auto& [b_key, b_flag, b_kind, b_lbl, b_min, b_max, b_di, b_db, b_ds, b_csv, b_low] =
                kParamDescriptors[j].first;
            (void)b_kind; (void)b_lbl; (void)b_min; (void)b_max; (void)b_di; (void)b_db; (void)b_ds; (void)b_csv; (void)b_low;
            if (a_key == b_key) return false;
            if (!a_flag.empty() && a_flag == b_flag) return false;
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
        const auto& [k, f, kind, lbl, mn, mx, di, db, ds, csv, low] = kParamDescriptors[idx].first;
        (void)k; (void)kind; (void)lbl; (void)mn; (void)mx; (void)di; (void)db; (void)ds; (void)csv; (void)low;
        if (f != flag) return false;
    }
    return true;
}
static_assert(validateCliFlags(), "kCliFlags index/flag mismatch against kParamDescriptors");

template <class Profile>
inline constexpr std::array<ProfileMemberPtr, 20> kFieldBindings = []() consteval {
    std::array<ProfileMemberPtr, 20> out{};
    for (std::size_t i = 0; i < kParamDescriptors.size(); ++i) {
        out[i] = kParamDescriptors[i].second;
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
[[nodiscard]] constexpr const ProfileMemberPtr* findCliFieldBinding(
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

// Generates the CLI option help block directly from kParamDescriptors + i18n labels
// so CLI help text never drifts from the SSOT schema contract (Wave 12.2-4).
[[nodiscard]] std::string formatCliHelp();

}  // namespace rtx
