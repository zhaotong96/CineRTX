// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors

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
#include <format>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <string_view>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

#include "rtx/io.h"
#include "rtx/job_queue.h"
#include "rtx/ngx_remaster.h"
#include "rtx/nle_project.h"
#include "rtx/param_schema.h"
#include "rtx/playback_session.h"
#include "rtx/raii_owners.h"
#include "rtx/render_graph.h"
#include "rtx/time_model.h"

#include "preview_viewport.h"

namespace rtx::gui {

struct WorkstationApp;

enum class WorkspaceMode : int {
    Encoder = 0,
    Player = 1,
    Nle = 2,
};
inline constexpr int kWorkspaceModeCount = 3;

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

// Phase 12 (Wave 12.2-B2): tuple alias (target_width, target_height, scale, vsr_active).
using DynamicVsrTarget = std::tuple<int, int, double, bool>;

using ViewportVsrTarget = DynamicVsrTarget;

[[nodiscard]] inline ViewportVsrTarget& active_vsr_target() noexcept {
    static ViewportVsrTarget target{0, 0, 1.0, false};
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

inline constexpr rtx::Flicks kDefaultTimelineSpan =
    std::chrono::duration_cast<rtx::Flicks>(std::chrono::seconds(30));

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

[[nodiscard]] inline std::wstring formatFlicksTimecode(int64_t playhead_flicks,
                                                       int64_t duration_flicks) {
    const int64_t clamped = std::clamp(playhead_flicks, int64_t{0},
                                       duration_flicks > 0 ? duration_flicks : playhead_flicks);
    const std::string tc = rtx::formatSmpteTimecode(clamped, rtx::kDefaultTimecodeFormat);
    return std::format(L"SMPTE {} | {} Flicks (706,305,600,000 Hz)", utf8ToWide(tc), static_cast<long long>(clamped));
}

// Phase 12 (Wave 12.2-B2): tuple alias (width, height, duration_flicks, ok).
using ProbedMedia = std::tuple<int, int, int64_t, bool>;

[[nodiscard]] inline ProbedMedia probeMediaFile(const std::filesystem::path& path) noexcept {
    int width = 1920;
    int height = 1080;
    int64_t duration_flicks = kDefaultTimelineSpan.count();
    bool ok = false;
    if (path.empty()) return {width, height, duration_flicks, ok};
    auto demux = rtx::makeFfmpegDemuxer();
    if (demux == nullptr) return {width, height, duration_flicks, ok};
    auto info = demux->open(path);
    if (info.has_value()) {
        const rtx::MediaInfo& media = info.value();
        if (media.width > 0 && media.height > 0) {
            width = media.width;
            height = media.height;
        }
        if (media.duration_ms > 0) {
            duration_flicks = std::chrono::duration_cast<rtx::Flicks>(
                std::chrono::milliseconds(media.duration_ms)).count();
        }
        ok = true;
    }
    return {width, height, duration_flicks, ok};
}
[[nodiscard]] inline std::wstring formatVsrScale(float scale) noexcept {
    std::wstring s = std::format(L"{:.4f}", static_cast<double>(scale));
    const size_t dot = s.find(L'.');
    if (dot != std::wstring::npos) {
        while (s.size() > dot + 3 && s.back() == L'0') {
            s.pop_back();
        }
    }
    s += L"x";
    return s;
}

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
[[nodiscard]] bool applyGoldenPreset(std::string_view preset_id, rtx::TranscodeProfile& profile);
void applyGoldenPresetById(HWND hwnd, const std::string& preset_id);
[[nodiscard]] std::vector<std::pair<std::string, ChromeText>> goldenPresetList();
[[nodiscard]] const rtx::TranscodeProfile& guiProfile(const WorkstationApp& app) noexcept;
[[nodiscard]] rtx::TranscodeProfile& guiProfile(WorkstationApp& app) noexcept;

void setGuiTranscoding(WorkstationApp& app, bool busy) noexcept;
[[nodiscard]] bool guiIsTranscoding(const WorkstationApp& app) noexcept;
void setActiveWorkspace(WorkstationApp& app, WorkspaceMode mode) noexcept;
void setGuiPlaying(WorkstationApp& app, bool playing) noexcept;
[[nodiscard]] bool guiIsPlaying(const WorkstationApp& app) noexcept;
[[nodiscard]] const wchar_t* playbackCaption(const WorkstationApp& app) noexcept;

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
    IDC_TAB_WS_ENCODER,
    IDC_TAB_WS_PLAYER,
    IDC_TAB_WS_NLE,
    IDC_BTN_PROBE,
    IDC_BTN_LANG,
    IDC_BTN_PRESET_MASTER,
    IDC_BTN_PRESET_RIFE,
    IDC_BTN_PRESET_ESPORTS,
    IDC_BTN_PRESET_COMPAT,
    IDC_BTN_PRESET_NATIVE,
    IDC_BTN_QUEUE_ADD,
    IDC_BTN_QUEUE_REMOVE,
    IDC_BTN_QUEUE_CLEAR,
    IDC_LST_BIN,
    IDC_LBL_TIMECODE,
    IDC_BTN_MARK_IN,
    IDC_BTN_MARK_OUT,
    IDC_BTN_RAZOR,
    IDC_NLE_TIMELINE,
    IDC_BTN_STEP_BACK,
    IDC_BTN_STEP_FWD,
    IDC_BTN_NLE_EXPORT,
    IDC_LBL_VSR_OSD,
};

inline constexpr int IDC_BTN_PLAYER_PLAY = 2109;

[[nodiscard]] rtx::TranscodeProfile collectProfileFromUi(HWND hwnd);
void applyProfileToUi(HWND hwnd, const rtx::TranscodeProfile& profile);
void autoFillOutputPath(HWND hwnd);
void populateComboBoxes(HWND hwnd);
void syncControlStates(HWND hwnd);

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

void refreshUiLanguage(HWND hwnd);
void runHardwareProbe(HWND hwnd);
bool paintCommandButton(const DRAWITEMSTRUCT* dis, HFONT font);
void createWorkstationControls(HWND hwnd, HFONT font, HFONT font_bold);

void refreshBinListbox(HWND hwnd);
void addFilesToBin(HWND hwnd, const std::vector<std::wstring>& paths);
void updateNleTimecode(HWND hwnd);
void onNleMarkIn(HWND hwnd);
void onNleMarkOut(HWND hwnd);
void onNleRazor(HWND hwnd);
void onNleDeleteSelected(HWND hwnd);
void onNleUndo(HWND hwnd);
void onNleRedo(HWND hwnd);
void onNleExportSequence(HWND hwnd);

constexpr UINT WM_APP_TIMELINE_SEEK = WM_APP + 40;
constexpr UINT WM_APP_NLE_EXPORT_DONE = WM_APP + 41;
constexpr UINT WM_APP_PREVIEW = WM_APP + 12;

constexpr int kBaselineSrcWidth = 1920;
constexpr int kBaselineSrcHeight = 1080;

// @rtx-type-justification
// Category: Domain-Model
// Why-Std-Fails: Win32 GUI 根聚合宿主，按值持有 NleProject/EditStacks，并跨转码队列、导出线程、播放会话与 DirectComposition 视口协调严格逆序析构。
// Invariant: 成员声明序即析构逆序——previewViewport 最先排空析构，exportThread 与 playbackSession 显式 join 后再释放 playbackSink 与 GDI 资源。
struct WorkstationApp {
    std::atomic<bool> cancelRequested{false};
    std::atomic<bool> transcoding{false};
    std::atomic<bool> playing{false};
    std::atomic<float> splitRatio{0.5f};

    WorkspaceMode workspace = WorkspaceMode::Encoder;
    bool fullscreen = false;
    bool inResizeDrag = false;
    bool isPlaying = false;
    bool timelineDragging = false;
    DWORD prevStyle = 0;
    WINDOWPLACEMENT prevPlacement{};
    int srcWidth = kBaselineSrcWidth;
    int srcHeight = kBaselineSrcHeight;
    int64_t mediaDurationFlicks = kDefaultTimelineSpan.count();

    rtx::TranscodeProfile profile{};
    rtx::NleProject project{};
    rtx::EditStacks history{};
    rtx::EncoderJobQueue jobQueue{};
    uint64_t selectedJobId = 0;
    std::unordered_set<uint64_t> activeSyncIds;
    std::jthread exportThread;

    std::optional<rtx::VideoFrameSink> playbackSink;          // 先声明，逆序最后析构
    std::unique_ptr<rtx::MediaPlaybackSession> playbackSession;  // 后声明，逆序先析构（join 工作线程）
    rtx::BorrowCallback playbackBorrow;
    rtx::BorrowCallback transcodeBorrow;
    rtx::MediaInfo playbackInfo{};
    std::uint32_t playbackLastSlot = 0xFFFFFFFFu;
    std::uint32_t transcodeLastSlot = 0xFFFFFFFFu;  // 30 Hz 轮询去重

    HFONT timelineFont = nullptr;
    rtx::UniqueBrush hbrBg;
    rtx::UniqueBrush hbrCtrl;
    rtx::UniqueFont hFont;
    rtx::UniqueFont hFontBold;
    std::optional<PreviewViewport> previewViewport;
};

// Phase 12 (HYG-P12-05): Zero mutable global app singleton; WorkstationApp is owned by wWinMain
// and bound to top-level / child window GWLP_USERDATA in WM_NCCREATE (cleared in WM_NCDESTROY).
[[nodiscard]] inline WorkstationApp* appFromHwnd(HWND hwnd) noexcept {
    return hwnd ? reinterpret_cast<WorkstationApp*>(rtx::sys::getWindowLongPtrW(hwnd, GWLP_USERDATA)) : nullptr;
}
[[nodiscard]] inline WorkstationApp* appFromControl(HWND hwnd) noexcept {
    if (auto* direct = appFromHwnd(hwnd)) return direct;
    HWND root = hwnd ? rtx::sys::getAncestor(hwnd, GA_ROOT) : nullptr;
    return root ? appFromHwnd(root) : nullptr;
}
[[nodiscard]] inline WorkstationApp* getWindowApp(HWND hwnd) noexcept {
    return appFromControl(hwnd);
}
[[nodiscard]] inline WorkstationApp& requireApp(HWND hwnd) noexcept {
    return *appFromControl(hwnd);
}

void installTranscodeBorrow(WorkstationApp& app);
void stopPlaybackSession(WorkstationApp& app) noexcept;
bool startPlaybackSession(HWND hwnd, const std::filesystem::path& path);
void togglePlayback(HWND hwnd);
void stepPlayback(HWND hwnd, int direction);
void seekPlaybackFromSlider(HWND hwnd);
void presentSlotToViewport(HWND hwnd, rtx::BorrowCallback& borrow, std::uint32_t& last_slot,
                           std::uint32_t src_width, std::uint32_t src_height, bool input_is_pq,
                           float max_nits);
// Doc 48: Pure stateless helpers for dynamic transcode preview geometry and dual-side HDR10/HLG color flags (+0 named types).
[[nodiscard]] inline std::pair<std::uint32_t, std::uint32_t> derivePreviewDimensions(
    std::uint64_t resource_bytes, std::uint32_t pitch,
    std::uint32_t hint_width = 0u, std::uint32_t hint_height = 0u) noexcept {
    if (pitch < 4u || (pitch % 4u) != 0u || resource_bytes < static_cast<std::uint64_t>(pitch)) {
        return {0u, 0u};
    }
    const std::uint32_t max_w = pitch / 4u;
    const std::uint32_t buf_h = static_cast<std::uint32_t>(resource_bytes / static_cast<std::uint64_t>(pitch));
    if (buf_h == 0u || max_w == 0u) {
        return {0u, 0u};
    }
    if (hint_width > 0u && hint_width <= max_w && hint_height == buf_h) {
        return {hint_width, buf_h};
    }
    return {max_w, buf_h};
}

[[nodiscard]] inline std::tuple<std::uint32_t, float, float> computeTranscodePreviewColorParams(
    const rtx::MediaInfo& src_info, const rtx::TranscodeProfile& prof,
    float dst_max_nits = 0.0f) noexcept {
    const auto [src_primaries, src_transfer, src_matrix, src_range] = src_info.color;
    (void)src_matrix; (void)src_range;
    std::uint32_t color_flags = 0u;
    float left_max_nits = 100.0f;
    float right_max_nits = 100.0f;

    if (src_transfer == rtx::TransferFunction::SMPTE2084) {
        color_flags |= (kPreviewLeftPq | kPreviewLeftBt2020);
        left_max_nits = (src_info.hdr.cll.first > 0)
            ? static_cast<float>(src_info.hdr.cll.first)
            : 1000.0f;
    } else if (src_transfer == rtx::TransferFunction::HLG) {
        color_flags |= (kPreviewLeftHlg | kPreviewLeftBt2020);
        left_max_nits = 1000.0f;
    } else if (src_primaries == rtx::ColorPrimaries::BT2020) {
        color_flags |= kPreviewLeftBt2020;
    }

    if (prof.peak_nits > 0) {
        color_flags |= (kPreviewRightPq | kPreviewRightBt2020);
        const float peak = (dst_max_nits > 0.0f)
            ? dst_max_nits
            : static_cast<float>(prof.peak_nits);
        right_max_nits = std::max(peak, 203.0f);
    } else if (src_transfer == rtx::TransferFunction::SMPTE2084) {
        color_flags |= (kPreviewRightPq | kPreviewRightBt2020);
        right_max_nits = std::max(left_max_nits, 203.0f);
    } else if (src_transfer == rtx::TransferFunction::HLG) {
        color_flags |= (kPreviewRightHlg | kPreviewRightBt2020);
        right_max_nits = 1000.0f;
    }
    return {color_flags, left_max_nits, right_max_nits};
}

// Doc 58 (Task 2.1): Single source of truth for IDC_CMB_FPS index (0..4) <-> fps_mode (+0 named types).
[[nodiscard]] inline std::string_view fpsModeStringFromIndex(int index) noexcept {
    constexpr std::string_view kFpsModeStrings[5] = {
        "original", "60-nvofa", "120-nvofa", "60-rife", "120-rife"
    };
    return kFpsModeStrings[std::clamp(index, 0, 4)];
}

[[nodiscard]] inline int fpsModeIndexFromString(std::string_view fps_mode) noexcept {
    if (fps_mode == "original") return 0;
    if (fps_mode == "60-nvofa") return 1;
    if (fps_mode == "120-nvofa") return 2;
    if (fps_mode == "60-rife") return 3;
    if (fps_mode == "120-rife") return 4;
    return 1;
}

void presentPlaybackFrame(HWND hwnd);
void presentTranscodePreview(HWND hwnd);

[[nodiscard]] bool registerTimelineCanvasWindowClass(HINSTANCE instance);

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

void paintTimelineCanvas(HWND hwnd, const RECT& client, const rtx::NleProject& project, HFONT font);


#if defined(_WIN32)
// Wave 4-A: Pure GDI word-wrap flags & stateless height measurement (zero static DLL / zero new types).
inline constexpr UINT kWrapDrawFlags = DT_LEFT | DT_TOP | DT_WORDBREAK | DT_EDITCONTROL | DT_NOPREFIX;

[[nodiscard]] inline int measureWrappedTextHeight(HDC hdc, HFONT font,
                                                  std::wstring_view text,
                                                  int max_width) noexcept {
    if (!hdc || text.empty() || max_width <= 0) return 0;
    HGDIOBJ prev = font ? rtx::sys::selectObject(hdc, font) : nullptr;
    RECT rc{0, 0, max_width, 0};
    rtx::sys::drawTextW(hdc, text.data(), static_cast<int>(text.size()), &rc, kWrapDrawFlags | DT_CALCRECT);
    if (prev) rtx::sys::selectObject(hdc, prev);
    return rc.bottom - rc.top;
}
#endif


// Wave D-1 & D-2 (80_ Task D-1/D-2): Zero-UiWidgetNode stateless hit-test and
// contract-based horizontal DAG color-space validation (+0 named struct/class types).
[[nodiscard]] constexpr bool hitTestRect(float x, float y, float w, float h,
                                         float mouse_x, float mouse_y) noexcept {
    return mouse_x >= x && mouse_x <= x + w && mouse_y >= y && mouse_y <= y + h;
}

enum class DagColorContract : uint8_t {
    SdrBt709_8Bit = 0,
    SdrBt709_10Bit = 1,
    HdrBt2020Pq_10Bit = 2,
    HdrBt2020Hlg_10Bit = 3,
};

// Tuple: (node_id, node_name, input_contract, output_contract)
using DagNodeContract = std::tuple<uint32_t, const char*, DagColorContract, DagColorContract>;
// Tuple: (is_valid, auto_tonemap_available, conflicting_node_name)
using DagEdgeValidation = std::tuple<bool, bool, const char*>;

[[nodiscard]] constexpr DagEdgeValidation validateDagEdge(const DagNodeContract& src,
                                                          const DagNodeContract& dst) noexcept {
    const DagColorContract src_out = std::get<3>(src);
    const DagColorContract dst_in  = std::get<2>(dst);
    const char* dst_name = std::get<1>(dst) ? std::get<1>(dst) : "";
    if (src_out == dst_in) {
        return {true, false, ""};
    }
    if ((src_out == DagColorContract::HdrBt2020Pq_10Bit ||
         src_out == DagColorContract::HdrBt2020Hlg_10Bit) &&
        (dst_in == DagColorContract::SdrBt709_8Bit ||
         dst_in == DagColorContract::SdrBt709_10Bit)) {
        // Auto ToneMap (BT.2020 -> BT.709) node can be inserted without implicit color overwrite.
        return {false, true, dst_name};
    }
    return {false, false, dst_name};
}

}  // namespace rtx::gui

#endif  // RTX_GUI_WORKSTATION_CANVAS_H
