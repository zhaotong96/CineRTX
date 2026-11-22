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

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
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

// @rtx-type-justification
// Category: Domain-Model
// Why-Std-Fails: 视口动态超分辨率计算目标领域模型：封装目标宽高、缩放比与 VSR 激活状态，std::tuple 丢失具名渲染上下文。
// Invariant: scale >= 1.0；target_width/height 必须为偶数或与源一致；vsr_active 与 scale > 1.0 严格单调。
struct DynamicVsrTarget {
    int target_width = 0;
    int target_height = 0;
    double scale = 1.0;
    bool vsr_active = false;
};

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
    const std::string tc = rtx::formatSmpteTimecode(clamped, rtx::TimecodeFormat{});
    wchar_t buf[128];
    _snwprintf_s(buf, _countof(buf), _TRUNCATE,
                 L"SMPTE %ls | %lld Flicks (705,600,000 Hz)",
                 utf8ToWide(tc).c_str(), static_cast<long long>(clamped));
    return buf;
}

// @rtx-type-justification
// Category: Domain-Model
// Why-Std-Fails: 媒体容器 CPU 静态探测结果领域模型：封装宽高、Flicks 时长与探测有效性，std::tuple 丢失具名解析语义。
// Invariant: ok 为 false 时降级为 1080p/30s 默认参数；ok 为 true 时 width/height > 0 且 duration_flicks >= 0。
struct ProbedMedia {
    int width = 1920;
    int height = 1080;
    int64_t duration_flicks = kDefaultTimelineSpan.count();
    bool ok = false;
};

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
            out.duration_flicks = std::chrono::duration_cast<rtx::Flicks>(
                std::chrono::milliseconds(media.duration_ms)).count();
        }
        out.ok = true;
    }
    return out;
}
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
[[nodiscard]] rtx::TranscodeProfile& guiProfile();

void setGuiTranscoding(bool busy) noexcept;
[[nodiscard]] bool guiIsTranscoding() noexcept;
void setActiveWorkspace(WorkspaceMode mode) noexcept;
void setGuiPlaying(bool playing) noexcept;
[[nodiscard]] bool guiIsPlaying() noexcept;
[[nodiscard]] const wchar_t* playbackCaption() noexcept;

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
    std::jthread exportThread;

    std::unique_ptr<rtx::IVideoFrameSink> playbackSink;          // 先声明，逆序最后析构
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

extern WorkstationApp g_app;

void installTranscodeBorrow();
void stopPlaybackSession();
bool startPlaybackSession(HWND hwnd, const std::filesystem::path& path);
void togglePlayback(HWND hwnd);
void stepPlayback(HWND hwnd, int direction);
void seekPlaybackFromSlider(HWND hwnd);
void presentSlotToViewport(HWND hwnd, rtx::BorrowCallback& borrow, std::uint32_t& last_slot,
                           std::uint32_t src_width, std::uint32_t src_height, bool input_is_pq,
                           float max_nits);
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

}  // namespace rtx::gui

#endif  // RTX_GUI_WORKSTATION_CANVAS_H
