// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
//
// Zero-allocation, O(1) strongly-typed localisation (W7-3 + Stage 2 SSOT).
//
// There is no resource file, no JSON parser, no heap allocation and no map
// lookup on the paint path: every user-visible string across Win32 GUI, CLI,
// and WebUI is a compile-time index into a static const array that lives in
// .rdata. The table size is checked against the StrId enum at compile time, so
// a forgotten translation is a build error rather than a blank label at run time.

#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "rtx/status.h"

namespace rtx {

enum class Language : uint8_t { En = 0, Zh = 1, Count = 2 };

enum class StrId : uint32_t {
    AppTitle = 0,
    AppSubtitle,
    StatusHardware,
    StatusNgxPlugins,
    StatusFrucEngine,
    StatusVramWatermark,
    ParamInput,
    ParamOutput,
    ParamCodec,
    ParamResolution,
    ParamPreset,
    ParamRateControl,
    ParamBitrate,
    ParamCq,
    ParamVsr,
    ParamVsrQuality,
    ParamTrueHdr,
    ParamPeakNits,
    ParamAspect,
    ParamAudioCopy,
    BtnBrowse,
    ButtonStart,
    ButtonStop,
    ButtonDropFirst,
    TelemetryTitle,
    TelemetrySubtitle,
    ProgressLabel,
    ProgressWorking,
    ProgressIdle,
    StatThroughput,
    StatFrames,
    StatElapsed,
    StatZeroCopy,
    ValueZeroCopyClean,
    ValueHostCopies,
    ValueRunning,
    ValueDash,
    StatusGpuProbing,
    StatusGpuMissing,
    StatusShimMissing,
    StatusReady,
    StatusWorking,
    StatusStopping,
    StatusCancelled,
    StatusFailed,
    StatusDonePrefix,
    StatusSamePath,
    StatusDropFileOnly,
    StatusBusy,
    StatusNoThread,
    LangToggleHint,
    ViewportTitle,
    ViewportSplitHint,
    AudioMixerTitle,
    WatermarkLow,
    WatermarkHigh,
    WatermarkCritical,
    ErrorInvalidArgument,
    ErrorIo,
    ErrorOutOfMemory,
    ErrorDeviceLost,
    SelfTestPass,
    SelfTestFail,
    StatusProbeOk,

// Stage 2 SSOT additions: unified WebUI & shared parameter labels
    WuiSubtitle,
    WuiParamsTitle,
    WuiParamFps,
    WuiParamRifeEngine,
    WuiParamHdr10plus,
    WuiAutoOutput,
    WuiParamLut,
    WuiParamGpu,
    WuiParamFrames,
    WuiEngine,
    WuiProbeTitle,
    WuiProbeRun,
    WuiReportTitle,
    WuiLogTitle,
    WuiShowLog,
    WuiFsGo,
    WuiFsUp,
    WuiFsSelect,
    WuiFoot,
    WuiProbeUnavailable,
    WuiReqFailed,

// Stage 2 SSOT additions: unified dropdown option labels (Win32 GUI + WebUI)
    OptCodecAv1,
    OptCodecHevc,
    OptCodecH264,
    OptPresetP1,
    OptPresetP2,
    OptPresetP3,
    OptPresetP4,
    OptPresetP5,
    OptPresetP6,
    OptPresetP7,
    OptRcCbr,
    OptRcVbr,
    OptRcQp,
    OptRes4k,
    OptRes2k,
    OptResFhd,
    OptResNative,
    OptGeomAuto,
    OptGeomPillar,
    OptVsrQ1,
    OptVsrQ2,
    OptVsrQ3,
    OptVsrQ4,
    OptFpsOriginal,
    OptFps60Nvofa,
    OptFps120Nvofa,
    OptFps60Rife,
    OptFps120Rife,

// Phase 10 SSOT additions (CQ / CRF / CPU Codecs / Pipeline Order / Queue)
    OptRcCq,
    OptRcCrf,
    OptCodecAv1Cpu,
    OptCodecHevcCpu,
    ParamPipelineOrder,
    OptPipeVsrFirst,
    OptPipeFgFirst,
    BtnQueueAdd,
    BtnQueueRemove,
    BtnQueueUp,
    BtnQueueDown,
    BtnQueueSkip,

    Count
};

using TextId = StrId;  // historical alias, kept for call sites

inline constexpr size_t kStrIdCount = static_cast<size_t>(StrId::Count);
static_assert(kStrIdCount == 125, "the i18n SSOT table is specified as 125 strongly-typed entries");

Language currentLanguage() noexcept;
void setLanguage(Language language) noexcept;
Language toggleLanguage() noexcept;
const char* languageTag(Language language) noexcept;
Result<Language> parseLanguage(std::string_view tag);

// Zero-allocation accessor for the Win32 paint path. Never returns nullptr;
// an out-of-range id yields L"".
const wchar_t* trW(StrId id) noexcept;
std::wstring_view trWView(StrId id) noexcept;

// UTF-8 accessor for the CLI, WebUI schema export, and logs.
std::string tr(StrId id);
std::string trForLanguage(Language language, StrId id);

// Convenience for diagnostics & WebUI binding: stable identifier of an enumerator.
const char* strIdName(StrId id) noexcept;
std::string_view strIdNameView(StrId id) noexcept;

// Maps an engine error code onto the localised message.
StrId strIdForError(ErrorCode code) noexcept;

// Exports the complete bilingual dictionary {"en":{...},"zh":{...}} as JSON
// so WebUI and external tools never duplicate translations.
std::string exportI18nJson();

}  // namespace rtx
