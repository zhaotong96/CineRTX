// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
//
// Stage 2 (Track B) workstation canvas: NVIDIA Signature Green chrome,
// "3+1" workspace model, viewport-driven dynamic VSR scaling, GDI
// double-buffered four-track Flicks timeline, the golden preset table, the
// SSOT control factory and the 19-parameter profile <-> UI binding.
//
// Design principles (SSOT zero-duplication + KISS):
//   - Every colour token mirrors webui/app.css 1:1 (#76b900 on #0b0d0b).
//   - computeViewportVsrTarget() is a pure, unit-tested function; the GUI
//     never hand-writes scale math or odd-alignment rounding.
//   - Flicks math is shared with the WebUI: 705,600 flicks per millisecond.
//   - UI chrome strings that have no i18n StrId live in ONE bilingual table
//     (chromeText) instead of scattered bilingual ternaries.
//   - The GUI session profile lives in ONE place (guiProfile()); collect /
//     apply / validate route through rtx::TranscodeProfile only.

#ifndef RTX_GUI_WORKSTATION_CANVAS_H
#define RTX_GUI_WORKSTATION_CANVAS_H

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "rtx/io.h"
#include "rtx/param_schema.h"

namespace rtx::gui {

// ---------------------------------------------------------------------------
// The "3+1" workstation model (SSOT with webui setWorkspace()).
//   Encoder = ME 转码, Player = VLC 播放, Nle = PR 非线编;
//   the "+1" is borderless fullscreen quick-play driven by file association.
// ---------------------------------------------------------------------------
enum class WorkspaceMode : int {
    Encoder = 0,
    Player = 1,
    Nle = 2,
};
inline constexpr int kWorkspaceModeCount = 3;

// ---------------------------------------------------------------------------
// NVIDIA Signature Green palette — mirrors webui/app.css exactly.
// ---------------------------------------------------------------------------
inline constexpr COLORREF kClrBg          = RGB(11, 13, 11);    // --bg          #0b0d0b
inline constexpr COLORREF kClrPanel       = RGB(19, 22, 19);    // --panel       #131613
inline constexpr COLORREF kClrPanel2      = RGB(26, 30, 26);    // --panel-2     #1a1e1a
inline constexpr COLORREF kClrCtrl        = RGB(34, 39, 34);    // --ctrl        #222722
inline constexpr COLORREF kClrLine        = RGB(44, 51, 44);    // --line        #2c332c
inline constexpr COLORREF kClrLineStrong  = RGB(61, 71, 61);    // --line-strong #3d473d
inline constexpr COLORREF kClrText        = RGB(238, 242, 238); // --text        #eef2ee
inline constexpr COLORREF kClrTextDim     = RGB(146, 156, 146); // --text-dim    #929c92
inline constexpr COLORREF kClrNvGreen     = RGB(118, 185, 0);   // --accent      #76b900
inline constexpr COLORREF kClrNvGreenHover = RGB(138, 208, 0);  // --accent-hover #8ad000
inline constexpr COLORREF kClrNvGreenDim  = RGB(78, 122, 0);    // --accent-dim  #4e7a00
inline constexpr COLORREF kClrOnAccent    = RGB(7, 13, 0);      // --on-accent   #070d00
inline constexpr COLORREF kClrHdrGold     = RGB(255, 209, 102); // --hdr-badge   #ffd166

// ---------------------------------------------------------------------------
// Viewport-driven dynamic VSR scaling (spec 2.3, pure + unit tested).
// scale = min(vp_w/src_w, vp_h/src_h); scale <= 1.0 bypasses VSR; targets are
// even-aligned via (lround(src*scale)+1) & ~1.
// ---------------------------------------------------------------------------
struct DynamicVsrTarget {
    int target_width = 0;
    int target_height = 0;
    double scale = 1.0;
    bool vsr_active = false;
};

// The shell stores the latest recomputed target under this alias so the
// preview/quick-play pipeline consumes one well-known global (RW-3 fix).
using ViewportVsrTarget = DynamicVsrTarget;

[[nodiscard]] inline ViewportVsrTarget& active_vsr_target() noexcept {
    static ViewportVsrTarget target{};
    return target;
}

inline void storeActiveVsrTarget(const ViewportVsrTarget& target) noexcept {
    active_vsr_target() = target;
}

[[nodiscard]] inline const ViewportVsrTarget& activeVsrTarget() noexcept {
    return active_vsr_target();
}

[[nodiscard]] inline DynamicVsrTarget computeViewportVsrTarget(int src_w, int src_h,
                                                               int viewport_w,
                                                               int viewport_h) noexcept {
    if (src_w <= 0 || src_h <= 0 || viewport_w <= 0 || viewport_h <= 0) {
        return {src_w, src_h, 1.0, false};
    }
    const double scale_x = static_cast<double>(viewport_w) / static_cast<double>(src_w);
    const double scale_y = static_cast<double>(viewport_h) / static_cast<double>(src_h);
    const double scale = (scale_x < scale_y) ? scale_x : scale_y;
    if (scale <= 1.0) {
        return {src_w, src_h, scale, false};
    }
    const int out_w = (static_cast<int>(std::lround(static_cast<double>(src_w) * scale)) + 1) & ~1;
    const int out_h = (static_cast<int>(std::lround(static_cast<double>(src_h) * scale)) + 1) & ~1;
    return {out_w, out_h, scale, true};
}

// ---------------------------------------------------------------------------
// Flicks timebase (WebUI parity: FLICKS_PER_MS = 705600; 705,600,000 Hz).
// ---------------------------------------------------------------------------
inline constexpr int64_t kFlicksPerMs = 705600LL;

[[nodiscard]] inline int64_t msToFlicks(int64_t ms) noexcept {
    return ms * kFlicksPerMs;
}

// "SMPTE HH:MM:SS:FF | <flicks> Flicks (705,600,000 Hz)" — same dual readout
// as the WebUI program monitor.  Inline so the unit tests need no GDI link.
[[nodiscard]] inline std::wstring formatFlicksTimecode(int64_t playhead_ms, int64_t duration_ms) {
    const int64_t total = std::clamp(playhead_ms, int64_t{0},
                                     duration_ms > 0 ? duration_ms : playhead_ms);
    const int64_t frames = (total * 24LL) / 1000LL;  // SMPTE at 24 fps film base
    const int64_t ff = frames % 24LL;
    const int64_t secs = frames / 24LL;
    wchar_t buf[128];
    _snwprintf_s(buf, _countof(buf), _TRUNCATE,
                 L"SMPTE %02d:%02d:%02d:%02d | %lld Flicks (705,600,000 Hz)",
                 static_cast<int>((secs / 3600) % 100), static_cast<int>((secs / 60) % 60),
                 static_cast<int>(secs % 60), static_cast<int>(ff),
                 static_cast<long long>(msToFlicks(total)));
    return buf;
}

// ---------------------------------------------------------------------------
// CPU-side media probing through rtx_core's statically linked FFmpeg demuxer
// (RW-2 fix: the shell never hardcodes source geometry or clip duration).
// ---------------------------------------------------------------------------
inline constexpr int64_t kAssumedClipMs = 30000;  // fallback span when probing fails

struct ProbedMedia {
    int width = 1920;
    int height = 1080;
    int64_t duration_ms = kAssumedClipMs;
    bool ok = false;
};

// Inline (not .cpp) so the unit tests need no GDI link — the demuxer comes
// from rtx_core which rtx_tests already links.
[[nodiscard]] inline ProbedMedia probeMediaFile(const std::filesystem::path& path) noexcept {
    ProbedMedia out{};
    if (path.empty()) {
        return out;
    }
    auto demux = rtx::makeFfmpegDemuxer();
    if (demux == nullptr) {
        return out;
    }
    auto info = demux->open(path);
    if (info.has_value()) {
        const rtx::MediaInfo& media = info.value();
        if (media.width > 0 && media.height > 0) {
            out.width = media.width;
            out.height = media.height;
        }
        if (media.duration_ms > 0) {
            out.duration_ms = media.duration_ms;
        }
        out.ok = true;
    }
    return out;
}

// Exact scale formatting: trailing zeros trimmed, minimum two decimals —
// 2.0 -> "2.00x", 1.6875 -> "1.6875x", 0.64 -> "0.64x" (RW-3 fix).
[[nodiscard]] inline std::wstring formatVsrScale(float scale) noexcept {
    wchar_t buf[32];
    _snwprintf_s(buf, _countof(buf), _TRUNCATE, L"%.4f", static_cast<double>(scale));
    std::wstring s(buf);
    const size_t dot = s.find(L'.');
    if (dot != std::wstring::npos) {
        while (s.size() > dot + 3 && s.back() == L'0') {
            s.pop_back();
        }
    }
    s += L"x";
    return s;
}

// UTF-8 <-> UTF-16 conversion shared by the GUI shell and the SSOT binding.
[[nodiscard]] inline std::wstring utf8ToWide(const std::string& str) {
    if (str.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.size()),
                                         nullptr, 0);
    std::wstring out(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.size()), out.data(), size);
    return out;
}

[[nodiscard]] inline std::string wideToUtf8(const std::wstring& wstr) {
    if (wstr.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), static_cast<int>(wstr.size()),
                                         nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), static_cast<int>(wstr.size()), out.data(), size,
                        nullptr, nullptr);
    return out;
}

// ---------------------------------------------------------------------------
// UI chrome strings without an i18n StrId: ONE bilingual table, zero scattered
// bilingual ternaries (Bug 5 containment).
// ---------------------------------------------------------------------------
enum class ChromeText : int {
    WsEncoder = 0,
    WsPlayer,
    WsNle,
    PresetsTitle,
    PresetMaster,
    PresetRife,
    PresetEsports,
    PresetCompat,
    PresetNative,
    QueueTitle,
    QueueAdd,
    QueueRemove,
    QueueClear,
    BinTitle,
    MarkIn,
    MarkOut,
    Razor,
    StepBack,
    StepFwd,
    SendEncoder,
    Probe,
    LangButton,
    Play,
    Pause,
    FullscreenHint,
    TimelineTitle,
    Subtitle,
};

[[nodiscard]] const wchar_t* chromeText(ChromeText id) noexcept;

// ---------------------------------------------------------------------------
// Golden presets (spec table 2.5): one click applies a full SSOT profile.
// Returns false for an unknown preset id.
// ---------------------------------------------------------------------------
[[nodiscard]] bool applyGoldenPreset(std::string_view preset_id, rtx::TranscodeProfile& profile);

// Applies one golden preset to the shared session profile and refreshes the
// whole inspector (logs through rtx::logInfo).
void applyGoldenPresetById(HWND hwnd, const std::string& preset_id);

[[nodiscard]] std::vector<std::pair<std::string, ChromeText>> goldenPresetList();

// ---------------------------------------------------------------------------
// SSOT session profile (spec 1.2): the single rtx::TranscodeProfile the whole
// GUI reads from and writes to.
// ---------------------------------------------------------------------------
[[nodiscard]] rtx::TranscodeProfile& guiProfile();

// Busy flag shared between the shell (transcode lifecycle) and the SSOT
// control states.
void setGuiTranscoding(bool busy) noexcept;
[[nodiscard]] bool guiIsTranscoding() noexcept;

// Highlights the active workspace tab in owner-draw painting.
void setActiveWorkspace(WorkspaceMode mode) noexcept;

// Quick-play playback state (RW-4): drives the play/pause owner-draw caption.
void setGuiPlaying(bool playing) noexcept;
[[nodiscard]] bool guiIsPlaying() noexcept;

// Player/暂停 chrome captions for the playback toggle button (RW-6).
[[nodiscard]] const wchar_t* playbackCaption() noexcept;

// ---------------------------------------------------------------------------
// Win32 control ids (one table for the shell and the control factory).
// ---------------------------------------------------------------------------
enum ControlId : int {
    IDC_LBL_INPUT = 1001,
    IDC_EDT_INPUT,
    IDC_BTN_BROWSE_IN,
    IDC_LBL_OUTPUT,
    IDC_EDT_OUTPUT,
    IDC_BTN_BROWSE_OUT,
    IDC_LBL_CODEC,
    IDC_CMB_CODEC,
    IDC_LBL_PRESET,
    IDC_CMB_PRESET,
    IDC_LBL_RC,
    IDC_CMB_RC,
    IDC_LBL_BITRATE,
    IDC_EDT_BITRATE,
    IDC_LBL_QP,
    IDC_EDT_QP,
    IDC_LBL_RES,
    IDC_CMB_RES,
    IDC_LBL_GEOM,
    IDC_CMB_GEOM,
    IDC_CHK_VSR,
    IDC_LBL_VSR_QUAL,
    IDC_CMB_VSR_QUAL,
    IDC_LBL_PEAK_NITS,
    IDC_EDT_PEAK_NITS,
    IDC_LBL_FPS,
    IDC_CMB_FPS,
    IDC_LBL_RIFE,
    IDC_EDT_RIFE_ENGINE,
    IDC_BTN_BROWSE_RIFE,
    IDC_LBL_GPU,
    IDC_EDT_GPU,
    IDC_LBL_FRAMES,
    IDC_EDT_FRAMES,
    IDC_CHK_AUDIO,
    IDC_CHK_HDR10PLUS,
    IDC_LBL_LUT,
    IDC_EDT_LUT,
    IDC_BTN_BROWSE_LUT,
    IDC_BTN_START,
    IDC_BTN_STOP,
    IDC_PROGRESS,
    IDC_LBL_STATUS,
    IDC_LBL_TELEMETRY,
    IDC_TRK_SCRUB,
    IDC_LST_QUEUE,
    // Stage 2 topbar: 3-mode navigation + probe + language.
    IDC_TAB_WS_ENCODER,
    IDC_TAB_WS_PLAYER,
    IDC_TAB_WS_NLE,
    IDC_BTN_PROBE,
    IDC_BTN_LANG,
    // Golden presets (left rail).
    IDC_BTN_PRESET_MASTER,
    IDC_BTN_PRESET_RIFE,
    IDC_BTN_PRESET_ESPORTS,
    IDC_BTN_PRESET_COMPAT,
    IDC_BTN_PRESET_NATIVE,
    // Batch queue controls.
    IDC_BTN_QUEUE_ADD,
    IDC_BTN_QUEUE_REMOVE,
    IDC_BTN_QUEUE_CLEAR,
    // NLE workstation.
    IDC_LST_BIN,
    IDC_LBL_TIMECODE,
    IDC_BTN_MARK_IN,
    IDC_BTN_MARK_OUT,
    IDC_BTN_RAZOR,
    IDC_NLE_TIMELINE,
    // Player transport.
    IDC_BTN_STEP_BACK,
    IDC_BTN_STEP_FWD,
    IDC_BTN_NLE_EXPORT,
    IDC_LBL_VSR_OSD,
};

// Player play/pause toggle (TASK-PHASE8-TRACKB3, RW-4): outside the enum so
// the interaction harness can address it as a standalone constant.
inline constexpr int IDC_BTN_PLAYER_PLAY = 2109;

// ---------------------------------------------------------------------------
// SSOT profile <-> UI (spec Task 1 / Bug 4): collect and apply go through the
// canonical rtx::TranscodeProfile; no hand-written ranges or suffixes.
// ---------------------------------------------------------------------------
[[nodiscard]] rtx::TranscodeProfile collectProfileFromUi(HWND hwnd);
void applyProfileToUi(HWND hwnd, const rtx::TranscodeProfile& profile);
void autoFillOutputPath(HWND hwnd);
void populateComboBoxes(HWND hwnd);
void syncControlStates(HWND hwnd);

// Control-group tables shared by the shell layout and the language refresh.
constexpr int kInspectorIds[] = {
    IDC_LBL_INPUT, IDC_EDT_INPUT, IDC_BTN_BROWSE_IN,
    IDC_LBL_OUTPUT, IDC_EDT_OUTPUT, IDC_BTN_BROWSE_OUT,
    IDC_LBL_CODEC, IDC_CMB_CODEC, IDC_LBL_PRESET, IDC_CMB_PRESET,
    IDC_LBL_RC, IDC_CMB_RC, IDC_LBL_BITRATE, IDC_EDT_BITRATE,
    IDC_LBL_QP, IDC_EDT_QP, IDC_LBL_RES, IDC_CMB_RES,
    IDC_LBL_GEOM, IDC_CMB_GEOM, IDC_CHK_VSR, IDC_LBL_VSR_QUAL, IDC_CMB_VSR_QUAL,
    IDC_LBL_PEAK_NITS, IDC_EDT_PEAK_NITS, IDC_LBL_FPS, IDC_CMB_FPS,
    IDC_LBL_RIFE, IDC_EDT_RIFE_ENGINE, IDC_BTN_BROWSE_RIFE,
    IDC_LBL_GPU, IDC_EDT_GPU, IDC_LBL_FRAMES, IDC_EDT_FRAMES,
    IDC_CHK_AUDIO, IDC_CHK_HDR10PLUS, IDC_LBL_LUT, IDC_EDT_LUT, IDC_BTN_BROWSE_LUT,
    IDC_BTN_START, IDC_BTN_STOP, IDC_PROGRESS, IDC_LBL_STATUS, IDC_LBL_TELEMETRY,
};
constexpr int kEncoderIds[] = {
    IDC_BTN_PRESET_MASTER, IDC_BTN_PRESET_RIFE, IDC_BTN_PRESET_ESPORTS,
    IDC_BTN_PRESET_COMPAT, IDC_BTN_PRESET_NATIVE,
    IDC_LST_QUEUE, IDC_BTN_QUEUE_ADD, IDC_BTN_QUEUE_REMOVE, IDC_BTN_QUEUE_CLEAR,
};
constexpr int kPlayerIds[] = {
    IDC_BTN_STEP_BACK, IDC_BTN_STEP_FWD, IDC_BTN_PLAYER_PLAY, IDC_BTN_NLE_EXPORT,
    IDC_LBL_VSR_OSD, IDC_TRK_SCRUB,
};
constexpr int kNleIds[] = {
    IDC_LST_BIN, IDC_LBL_TIMECODE, IDC_BTN_MARK_IN, IDC_BTN_MARK_OUT, IDC_BTN_RAZOR,
    IDC_NLE_TIMELINE,
};

// Ctrl+L / EN-中文 button: full-interface instant bilingual refresh (RW-6).
void refreshUiLanguage(HWND hwnd);

// Hardware probe button: reuses the CLI's versioned C-ABI shim probe.
void runHardwareProbe(HWND hwnd);

// Owner-draw dispatch: maps control ids to skin + caption.  Returns false for
// ids the canvas does not own (the shell falls through to DefWindowProc).
bool paintCommandButton(const DRAWITEMSTRUCT* dis, HFONT font);

// Creates every child control except the realtime viewport (which the shell
// creates last so it z-orders above).  All labels are SSOT i18n ids.
void createWorkstationControls(HWND hwnd, HFONT font, HFONT font_bold);

// ---------------------------------------------------------------------------
// NLE timeline model (V2/V1/A1/S1 lanes, aligned with webui/state.json).
//   Track order index: 0 = V2 (adjustment), 1 = V1 (video), 2 = A1 (audio),
//   3 = S1 (subtitle).
// ---------------------------------------------------------------------------
struct NleClip {
    std::wstring name;        // display caption (file name)
    std::wstring path;        // media path
    int64_t start_ms = 0;     // lane position
    int64_t duration_ms = 0;  // clip length (default span until probed on GPU host)
    int track = 1;            // 0..3
};

struct NleTimeline {
    std::vector<NleClip> clips;
    int64_t playhead_ms = 0;
    int64_t in_ms = -1;       // marked IN (unset = -1)
    int64_t out_ms = -1;      // marked OUT (unset = -1)
    int64_t duration_ms = 30000;
    int selected = -1;        // index into clips
};

[[nodiscard]] NleTimeline& sharedNleTimeline();

// NLE workstation actions (shell-facing, RW-2/RW-6): bin refresh, real media
// probing per clip, timecode readout, I/O + razor editing and the export-to-
// encoder jump (returns true when a bin clip was loaded into the inspector).
void refreshBinListbox(HWND hwnd);
void addFilesToBin(HWND hwnd, const std::vector<std::wstring>& paths);
void updateNleTimecode(HWND hwnd);
void onNleMarkIn(HWND hwnd);
void onNleMarkOut(HWND hwnd);
void onNleRazor(HWND hwnd);
[[nodiscard]] bool exportSelectedClipToEncoder(HWND hwnd);

// The timeline child window posts WM_APP_TIMELINE_SEEK to its parent with
// lParam = new playhead position in milliseconds while dragging.
constexpr UINT WM_APP_TIMELINE_SEEK = WM_APP + 40;

[[nodiscard]] bool registerNleTimelineWindowClass(HINSTANCE instance);

// ---------------------------------------------------------------------------
// GDI painting (implemented in workstation_canvas.cpp, all double buffered).
// ---------------------------------------------------------------------------
// Main window background + topbar brand mark (parallelogram RTX logo, title,
// subtitle, green baseline).
void paintWorkspaceChrome(HDC hdc, const RECT& client, WorkspaceMode mode, HFONT font);

enum class ButtonSkin : int {
    Tab,         // workspace tab (inactive): panel fill, dim text
    TabActive,   // workspace tab (active): green underline + dim fill
    GreenSolid,  // primary action: solid NV green, dark text
    Subtle,      // secondary action: panel2 fill, text colour, green border
    PresetBar,   // golden preset: panel fill with 3px green left bar
};

void paintOwnerDrawButton(const DRAWITEMSTRUCT& dis, const wchar_t* caption, ButtonSkin skin,
                          HFONT font, bool hovered);

// Timeline painting is self-contained in the child window proc.
void paintNleTimeline(HWND hwnd, const RECT& client, const NleTimeline& state, HFONT font);

}  // namespace rtx::gui

#endif  // RTX_GUI_WORKSTATION_CANVAS_H
