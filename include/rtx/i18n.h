// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
//
// Zero-allocation, O(1) strongly-typed localisation (W7-3).
//
// There is no resource file, no JSON parser, no heap allocation and no map
// lookup on the paint path: every user-visible string is a compile-time index
// into a static const array that lives in .rdata.  The table size is checked
// against the StrId enum at compile time, so a forgotten translation is a build
// error rather than a blank label at run time.
//
// Adding a string:
//   1. add the enumerator immediately before Count;
//   2. add the English and Chinese entries at the same index in i18n.cpp;
//   3. the static_assert fires until both tables agree.

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
    ModeTranscodeFast,
    ModeRemasterMaster,
    ModeInterpOffRuc,
    ModeInterpRife,
    DialogMissingPluginsTitle,
    DialogMissingPluginsBody,
    BtnExplorePlugins,
    BtnBuildRife,
    DropTitle,
    DropHint,
    DropSupported,
    DropOutputPrefix,
    CodecLabelHevc,
    CodecLabelH264,
    PresetLabel,
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
    Count
};

using TextId = StrId;  // historical alias, kept for call sites

inline constexpr size_t kStrIdCount = static_cast<size_t>(StrId::Count);
static_assert(kStrIdCount == 64, "the i18n table is specified as 64 strongly-typed entries");

// Current language.  Relaxed atomic: a torn read is impossible for a byte, and
// the paint thread only needs eventual consistency with Ctrl+L.
Language currentLanguage() noexcept;
void setLanguage(Language language) noexcept;
Language toggleLanguage() noexcept;
const char* languageTag(Language language) noexcept;
Result<Language> parseLanguage(std::string_view tag);

// Zero-allocation accessor for the Win32 paint path.  Never returns nullptr;
// an out-of-range id yields L"".
const wchar_t* trW(StrId id) noexcept;

// UTF-8 accessor for the CLI and for logs.  Allocates a std::string, so it is
// deliberately kept off the per-frame paint path.
std::string tr(StrId id);

// Convenience for diagnostics: the stable identifier of an enumerator.
const char* strIdName(StrId id) noexcept;

// Maps an engine error code onto the localised message.
StrId strIdForError(ErrorCode code) noexcept;

}  // namespace rtx
