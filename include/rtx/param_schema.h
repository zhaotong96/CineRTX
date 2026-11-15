// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
//
// Stage 2 Single Source of Truth (SSOT) Parameter Schema & Profile Contract.
//
// Design principles (SOLID + LoD + KISS + Occam's Razor):
//   - One canonical aggregate (`TranscodeProfile`) and one compile-time metadata
//     table (`kParamSchema`) govern CLI, Win32 GUI, and WebUI.
//   - Zero duplicated range checks, default values, output filename derivations,
//     or RIFE engine path lookups across frontends.
//   - Native JSON schema export (`exportSchemaJson`) and JSON profile ingestion
//     (`parseProfileFromJson`) power `CineRTX.exe --schema-json` and
//     `CineRTX.exe --session-stdio` with zero external JSON libraries.

#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

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
};

// Canonical transcode profile shared 1:1 across CLI, Win32 GUI, and WebUI.
// Default member initializers define the single out-of-the-box best experience:
// 4K UHD (3840x2160 AutoDirect), AV1 10-bit HDR10 (1000 nits), NVENC P5,
// VSR Level 4 Ultra, 60 FPS NVOFA hardware optical flow, audio stream copy.
struct TranscodeProfile {
    std::string input{};
    std::string output{};
    std::string codec{"av1"};           // "av1" | "hevc" (10-bit HDR10 pipeline)
    int preset = 5;                     // 1..7 (NVENC P1..P7)
    std::string rate_control{"cbr"};    // "cbr" | "vbr" | "qp"
    int bitrate_kbps = 0;               // 0..200000 (0 = auto)
    int qp = 24;                        // 0..51 (unified across CLI/GUI/WebUI)
    std::string resolution{"4k"};       // "4k" | "2k" | "fhd" | "native"
    int target_width = 3840;            // explicit override or derived from resolution
    int target_height = 2160;           // explicit override or derived from resolution
    std::string geometry{"auto"};       // "auto" | "pillarbox"
    bool vsr_enabled = true;            // true = VSR upscale enabled
    int vsr_quality = 4;                // 1..4 (Level 4 Ultra default)
    int peak_nits = 1000;               // 400..4000 cd/m^2
    std::string fps_mode{"60-nvofa"};   // "original" | "60-nvofa" | "120-nvofa" | "60-rife" | "120-rife"
    std::string rife_engine{};          // path to TensorRT .engine when fps_mode ends with "-rife"
    bool copy_audio = true;             // lossless audio/subtitle stream passthrough
    bool hdr10plus = false;             // SMPTE ST 2094-40 dynamic metadata
    std::string lut{};                  // optional .cube 3D LUT path
    int gpu = 0;                        // CUDA/NVENC device index (>= 0)
    int frames = 0;                     // frame limit (0 = whole stream)
    bool auto_output = true;            // auto-derive output path when empty or auto-generated
};

inline constexpr std::array<ParamMeta, 19> kParamSchema{{
    {.json_key = "input",        .cli_flag = "--input",        .kind = ParamKind::String, .label_id = StrId::ParamInput},
    {.json_key = "output",       .cli_flag = "--output",       .kind = ParamKind::String, .label_id = StrId::ParamOutput},
    {.json_key = "codec",        .cli_flag = "--codec",        .kind = ParamKind::Enum,   .label_id = StrId::ParamCodec,       .default_str = "av1",      .enum_csv = "av1,hevc"},
    {.json_key = "preset",       .cli_flag = "--preset",       .kind = ParamKind::Int,    .label_id = StrId::ParamPreset,      .min_val = 1,   .max_val = 7,      .default_int = 5},
    {.json_key = "rateControl",  .cli_flag = "--rate-control", .kind = ParamKind::Enum,   .label_id = StrId::ParamRateControl, .default_str = "cbr",      .enum_csv = "cbr,vbr,qp"},
    {.json_key = "bitrateKbps",  .cli_flag = "--bitrate",      .kind = ParamKind::Int,    .label_id = StrId::ParamBitrate,     .min_val = 0,   .max_val = 200000, .default_int = 0},
    {.json_key = "qp",           .cli_flag = "--qp",           .kind = ParamKind::Int,    .label_id = StrId::ParamCq,          .min_val = 0,   .max_val = 51,     .default_int = 24},
    {.json_key = "resolution",   .cli_flag = "--resolution",   .kind = ParamKind::Enum,   .label_id = StrId::ParamResolution,  .default_str = "4k",       .enum_csv = "4k,2k,fhd,native"},
    {.json_key = "geometry",     .cli_flag = "--pillarbox",    .kind = ParamKind::Enum,   .label_id = StrId::ParamAspect,      .default_str = "auto",     .enum_csv = "auto,pillarbox"},
    {.json_key = "vsrEnabled",   .cli_flag = "--no-vsr",       .kind = ParamKind::Bool,   .label_id = StrId::ParamVsr,         .default_bool = true},
    {.json_key = "vsrQuality",   .cli_flag = "--vsr-quality",  .kind = ParamKind::Int,    .label_id = StrId::ParamVsrQuality,  .min_val = 1,   .max_val = 4,      .default_int = 4},
    {.json_key = "peakNits",     .cli_flag = "--peak-nits",    .kind = ParamKind::Int,    .label_id = StrId::ParamPeakNits,    .min_val = 400, .max_val = 4000,   .default_int = 1000},
    {.json_key = "fpsMode",      .cli_flag = "--target-fps",   .kind = ParamKind::Enum,   .label_id = StrId::WuiParamFps,      .default_str = "60-nvofa", .enum_csv = "original,60-nvofa,120-nvofa,60-rife,120-rife"},
    {.json_key = "rifeEngine",   .cli_flag = "--rife-engine",  .kind = ParamKind::String, .label_id = StrId::WuiParamRifeEngine},
    {.json_key = "copyAudio",    .cli_flag = "--no-audio",     .kind = ParamKind::Bool,   .label_id = StrId::ParamAudioCopy,   .default_bool = true},
    {.json_key = "hdr10plus",    .cli_flag = "--hdr10plus",    .kind = ParamKind::Bool,   .label_id = StrId::WuiParamHdr10plus,.default_bool = false},
    {.json_key = "lut",          .cli_flag = "--lut",          .kind = ParamKind::String, .label_id = StrId::WuiParamLut},
    {.json_key = "gpu",          .cli_flag = "--gpu",          .kind = ParamKind::Int,    .label_id = StrId::WuiParamGpu,      .min_val = 0,   .max_val = 15,     .default_int = 0},
    {.json_key = "frames",       .cli_flag = "--frames",       .kind = ParamKind::Int,    .label_id = StrId::WuiParamFrames,   .min_val = 0,   .max_val = 100000000, .default_int = 0},
}};

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

// Validates all fields of `profile` against `kParamSchema`, normalizes case and
// derived geometry/FPS/output fields, and returns an Error on any contract violation.
[[nodiscard]] Result<void> validateAndNormalizeProfile(TranscodeProfile& profile,
                                                       bool require_io_paths = true);

// Converts a validated `TranscodeProfile` into `HwTranscodeOptions` using C++26
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
