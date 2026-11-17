// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
//
// Stage 2 (Track B): NVIDIA Signature Green "3+1" workstation GUI.
//   Encoder (ME 转码) | Player (VLC 播放) | NLE (PR 非线编) | borderless
//   fullscreen quick-play via file association.
//
// SSOT zero-duplication law (spec Task 1): the GUI hand-writes NO ranges, NO
// output suffix concatenation, NO RIFE engine probing, NO bilingual ternaries
// for SSOT-labelled strings.  Everything routes through rtx::TranscodeProfile,
// rtx::validateAndNormalizeProfile, rtx::profileToHwOptions,
// rtx::deriveOutputPath, rtx::resolveDefaultRifeEnginePath (via the schema)
// and rtx::trW(rtx::StrId::...).

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

#include "rtx/hw_transcode.h"
#include "rtx/job_queue.h"
#include "rtx/string_utils.h"
#include "rtx/i18n.h"
#include "rtx/logger.h"
#include "rtx/param_schema.h"
#include "rtx/runtime_env.h"
#include "rtx/types.h"

#include "preview_viewport.h"
#include "workstation_canvas.h"

#include "shim_probe.h"

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <uxtheme.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "dwmapi.lib")

int rtxCliMain(int argc, char** argv);

namespace {

// The shell consumes the canvas module's control ids and SSOT binding surface.
using namespace rtx::gui;

// Control ids live in workstation_canvas.h (rtx::gui::ControlId).

constexpr UINT WM_APP_PROGRESS      = WM_APP + 10;
constexpr UINT WM_APP_DONE          = WM_APP + 11;
constexpr UINT WM_APP_PREVIEW       = WM_APP + 12;
constexpr UINT WM_APP_QUEUE_UPDATED = WM_APP + 13;
constexpr UINT WM_APP_TOGGLE_LANG   = WM_APP + 20;
// WM_APP_SPLIT_DRAG comes from src/gui/preview_viewport.h.
// WM_APP_TIMELINE_SEEK comes from src/gui/workstation_canvas.h.

constexpr int kPreviewWidth = 960;
constexpr int kPreviewHeight = 540;
// Baseline source geometry for dynamic VSR until the engine reports the real
// per-clip dimensions (needs a GPU host; the pure calc is unit tested at the
// spec's reference values).
constexpr int kBaselineSrcWidth = 1920;
constexpr int kBaselineSrcHeight = 1080;

struct WorkstationApp {
    std::atomic<bool> cancelRequested{false};
    std::atomic<float> splitRatio{0.5f};
    std::thread workerThread;
    std::thread scrubThread;
    std::atomic<bool> previewBusy{false};

    rtx::gui::WorkspaceMode workspace = rtx::gui::WorkspaceMode::Encoder;
    bool fullscreen = false;
    bool inResizeDrag = false;
    bool isPlaying = false;
    DWORD prevStyle = 0;
    WINDOWPLACEMENT prevPlacement{};
    std::wstring quickPlayFile;
    int srcWidth = kBaselineSrcWidth;
    int srcHeight = kBaselineSrcHeight;

    rtx::EncoderJobQueue jobQueue{};

    std::mutex previewMutex;
    std::vector<uint8_t> previewHost;
    uint32_t previewPitch = 0;
    bool previewDirty = false;
    bool previewEnabled = true;
    std::wstring lastErrorMessage;

    HBRUSH hbrBg = nullptr;
    HBRUSH hbrCtrl = nullptr;
    HFONT hFont = nullptr;
    HFONT hFontBold = nullptr;
    std::optional<rtx::gui::PreviewViewport> previewViewport;
};

WorkstationApp g_app;

// ---------------------------------------------------------------------------
// Workspace layout: every control gets its rectangle from ONE function, per
// workspace mode (Encoder / Player / NLE) and fullscreen overlay.
// ---------------------------------------------------------------------------

void showGroup(HWND hwnd, const int* ids, size_t count, bool visible) {
    for (size_t i = 0; i < count; ++i) {
        ShowWindow(GetDlgItem(hwnd, ids[i]), visible ? SW_SHOW : SW_HIDE);
    }
}

void applyWorkspaceLayout(HWND hwnd) {
    RECT rc{};
    GetClientRect(hwnd, &rc);
    const int width = rc.right - rc.left;
    const int height = rc.bottom - rc.top;
    const int dpi = GetDpiForWindow(hwnd);
    const auto u = [dpi](int v) { return MulDiv(v, dpi, 96); };

    const bool fullscreen = g_app.fullscreen;
    const int topbarH = u(52);
    const int bodyTop = topbarH + 2;
    const int bodyH = height - bodyTop;

    const bool isEncoder = (g_app.workspace == rtx::gui::WorkspaceMode::Encoder);
    const bool isPlayer = (g_app.workspace == rtx::gui::WorkspaceMode::Player);
    const bool isNle = (g_app.workspace == rtx::gui::WorkspaceMode::Nle);
    const bool showInspector = (isEncoder || isNle) && !fullscreen;
    const bool showEncoderRail = isEncoder && !fullscreen;
    const bool showPlayer = isPlayer && !fullscreen;
    const bool showNle = isNle && !fullscreen;

    showGroup(hwnd, kInspectorIds, std::size(kInspectorIds), showInspector);
    showGroup(hwnd, kEncoderIds, std::size(kEncoderIds), showEncoderRail);
    showGroup(hwnd, kPlayerIds, std::size(kPlayerIds), showPlayer || (isPlayer && fullscreen));
    showGroup(hwnd, kNleIds, std::size(kNleIds), showNle);
    ShowWindow(GetDlgItem(hwnd, IDC_TRK_SCRUB), (showPlayer || (isEncoder && !fullscreen)) ? SW_SHOW : SW_HIDE);
    // Topbar tabs always visible except in fullscreen (Esc / F11 exits).
    ShowWindow(GetDlgItem(hwnd, IDC_TAB_WS_ENCODER), fullscreen ? SW_HIDE : SW_SHOW);
    ShowWindow(GetDlgItem(hwnd, IDC_TAB_WS_PLAYER), fullscreen ? SW_HIDE : SW_SHOW);
    ShowWindow(GetDlgItem(hwnd, IDC_TAB_WS_NLE), fullscreen ? SW_HIDE : SW_SHOW);
    ShowWindow(GetDlgItem(hwnd, IDC_BTN_PROBE), fullscreen ? SW_HIDE : SW_SHOW);
    ShowWindow(GetDlgItem(hwnd, IDC_BTN_LANG), fullscreen ? SW_HIDE : SW_SHOW);

    if (fullscreen) {
        if (g_app.previewViewport.has_value()) {
            g_app.previewViewport->setBounds(0, 0, width, height);
        }
        if (isPlayer) {
            MoveWindow(GetDlgItem(hwnd, IDC_TRK_SCRUB), u(8), height - u(40), width - u(16), u(26), TRUE);
            MoveWindow(GetDlgItem(hwnd, IDC_LBL_VSR_OSD), u(8), u(8), width - u(16), u(22), TRUE);
            MoveWindow(GetDlgItem(hwnd, IDC_BTN_STEP_BACK), u(8), height - u(72), u(64), u(26), TRUE);
            MoveWindow(GetDlgItem(hwnd, IDC_BTN_STEP_FWD), u(80), height - u(72), u(64), u(26), TRUE);
            MoveWindow(GetDlgItem(hwnd, IDC_BTN_PLAYER_PLAY), u(152), height - u(72), u(84), u(26), TRUE);
        }
        return;
    }

    // Tabs on the topbar — start at u(380) so the RTX parallelogram brand mark
    // (x = 12..68) and the "CineRTX" title (x = 80..360) stay fully visible
    // (RW-5 fix: three-section topbar like the WebUI brand | switcher | right).
    const int tabStartX = u(380);
    const int tabW = u(110);
    const int tabH = u(30);
    MoveWindow(GetDlgItem(hwnd, IDC_TAB_WS_ENCODER), tabStartX, u(11), tabW, tabH, TRUE);
    MoveWindow(GetDlgItem(hwnd, IDC_TAB_WS_PLAYER), tabStartX + tabW + u(6), u(11), tabW, tabH, TRUE);
    MoveWindow(GetDlgItem(hwnd, IDC_TAB_WS_NLE), tabStartX + (tabW + u(6)) * 2, u(11), tabW, tabH, TRUE);
    MoveWindow(GetDlgItem(hwnd, IDC_BTN_PROBE), width - u(220), u(14), u(100), u(24), TRUE);
    MoveWindow(GetDlgItem(hwnd, IDC_BTN_LANG), width - u(112), u(14), u(100), u(24), TRUE);

    if (isEncoder) {
        // Left rail: golden presets + batch queue.
        const int railW = u(224);
        int y = bodyTop + u(10);
        MoveWindow(GetDlgItem(hwnd, IDC_BTN_PRESET_MASTER), u(12), y, railW - u(24), u(30), TRUE);
        y += u(34);
        MoveWindow(GetDlgItem(hwnd, IDC_BTN_PRESET_RIFE), u(12), y, railW - u(24), u(30), TRUE);
        y += u(34);
        MoveWindow(GetDlgItem(hwnd, IDC_BTN_PRESET_ESPORTS), u(12), y, railW - u(24), u(30), TRUE);
        y += u(34);
        MoveWindow(GetDlgItem(hwnd, IDC_BTN_PRESET_COMPAT), u(12), y, railW - u(24), u(30), TRUE);
        y += u(34);
        MoveWindow(GetDlgItem(hwnd, IDC_BTN_PRESET_NATIVE), u(12), y, railW - u(24), u(30), TRUE);
        y += u(44);
        const int queueBtnW = (railW - u(24) - u(12)) / 3;
        MoveWindow(GetDlgItem(hwnd, IDC_BTN_QUEUE_ADD), u(12), y, queueBtnW, u(26), TRUE);
        MoveWindow(GetDlgItem(hwnd, IDC_BTN_QUEUE_REMOVE), u(12) + queueBtnW + u(6), y, queueBtnW, u(26), TRUE);
        MoveWindow(GetDlgItem(hwnd, IDC_BTN_QUEUE_CLEAR), u(12) + (queueBtnW + u(6)) * 2, y, queueBtnW, u(26), TRUE);
        y += u(30);
        MoveWindow(GetDlgItem(hwnd, IDC_LST_QUEUE), u(12), y, railW - u(24),
                   std::max(u(60), height - y - u(12)), TRUE);

        // Centre: viewport + scrub slider.
        const int centerX = railW;
        const int centerW = width - railW - u(380);
        const int vpH = bodyH - u(120);
        if (g_app.previewViewport.has_value()) {
            g_app.previewViewport->setBounds(centerX, bodyTop, std::max(u(120), centerW), std::max(u(80), vpH));
        }
        MoveWindow(GetDlgItem(hwnd, IDC_TRK_SCRUB), centerX, bodyTop + vpH + u(8),
                   std::max(u(120), centerW), u(26), TRUE);
    } else if (isPlayer) {
        const int vpH = bodyH - u(110);
        if (g_app.previewViewport.has_value()) {
            g_app.previewViewport->setBounds(0, bodyTop, width, std::max(u(80), vpH));
        }
        MoveWindow(GetDlgItem(hwnd, IDC_TRK_SCRUB), u(8), bodyTop + vpH + u(8), width - u(16), u(26), TRUE);
        const int btnY = bodyTop + vpH + u(40);
        MoveWindow(GetDlgItem(hwnd, IDC_BTN_STEP_BACK), u(8), btnY, u(72), u(28), TRUE);
        MoveWindow(GetDlgItem(hwnd, IDC_BTN_STEP_FWD), u(88), btnY, u(72), u(28), TRUE);
        MoveWindow(GetDlgItem(hwnd, IDC_BTN_PLAYER_PLAY), u(168), btnY, u(110), u(28), TRUE);
        MoveWindow(GetDlgItem(hwnd, IDC_BTN_NLE_EXPORT), u(286), btnY, u(200), u(28), TRUE);
        MoveWindow(GetDlgItem(hwnd, IDC_LBL_VSR_OSD), u(502), btnY + u(4), width - u(514), u(22), TRUE);
    } else {
        // NLE: media bin | program monitor + timecode + I/O razor | timeline.
        const int binW = u(220);
        const int timelineH = u(230);
        const int monitorX = binW;
        const int monitorW = width - binW - u(380);
        const int monitorH = bodyH - timelineH - u(40);
        MoveWindow(GetDlgItem(hwnd, IDC_LST_BIN), u(8), bodyTop + u(8), binW - u(16),
                   bodyH - timelineH - u(24), TRUE);
        if (g_app.previewViewport.has_value()) {
            g_app.previewViewport->setBounds(monitorX, bodyTop, std::max(u(120), monitorW),
                                         std::max(u(80), monitorH));
        }
        const int toolY = bodyTop + monitorH + u(8);
        MoveWindow(GetDlgItem(hwnd, IDC_LBL_TIMECODE), monitorX, toolY,
                   std::max(u(200), monitorW - u(300)), u(22), TRUE);
        MoveWindow(GetDlgItem(hwnd, IDC_BTN_MARK_IN), monitorX + std::max(u(200), monitorW - u(300)), toolY, u(90), u(24), TRUE);
        MoveWindow(GetDlgItem(hwnd, IDC_BTN_MARK_OUT), monitorX + std::max(u(200), monitorW - u(300)) + u(94), toolY, u(90), u(24), TRUE);
        MoveWindow(GetDlgItem(hwnd, IDC_BTN_RAZOR), monitorX + std::max(u(200), monitorW - u(300)) + u(188), toolY, u(110), u(24), TRUE);
        MoveWindow(GetDlgItem(hwnd, IDC_NLE_TIMELINE), u(4), height - timelineH - u(4), width - u(8), timelineH, TRUE);
    }

    if (showInspector) {
        // Right inspector: the complete 19-parameter SSOT stack.
        const int panelW = u(380);
        const int panelX = width - panelW;
        const int lblW = u(120);
        const int rowH = u(26);
        const int gap = u(2);
        int y = bodyTop + u(10);
        auto row = [&](int labelId, int ctrlId, int extraId = -1, int extraW = 0) {
            MoveWindow(GetDlgItem(hwnd, labelId), panelX + u(8), y + u(4), lblW, rowH, TRUE);
            int ctrlW = panelW - lblW - u(24) - extraW;
            MoveWindow(GetDlgItem(hwnd, ctrlId), panelX + u(8) + lblW, y, ctrlW, rowH, TRUE);
            if (extraId >= 0) {
                MoveWindow(GetDlgItem(hwnd, extraId), panelX + panelW - u(16) - extraW, y,
                           extraW, rowH, TRUE);
            }
            y += rowH + gap;
        };
        row(IDC_LBL_INPUT, IDC_EDT_INPUT, IDC_BTN_BROWSE_IN, u(56));
        row(IDC_LBL_OUTPUT, IDC_EDT_OUTPUT, IDC_BTN_BROWSE_OUT, u(56));
        row(IDC_LBL_CODEC, IDC_CMB_CODEC);
        row(IDC_LBL_PRESET, IDC_CMB_PRESET);
        row(IDC_LBL_RC, IDC_CMB_RC);
        row(IDC_LBL_BITRATE, IDC_EDT_BITRATE);
        row(IDC_LBL_QP, IDC_EDT_QP);
        row(IDC_LBL_RES, IDC_CMB_RES);
        row(IDC_LBL_GEOM, IDC_CMB_GEOM);
        // VSR enable + quality share one row (Bug 1 fix: peak nits gets its own row below).
        MoveWindow(GetDlgItem(hwnd, IDC_CHK_VSR), panelX + u(8), y + u(4), u(110), rowH, TRUE);
        MoveWindow(GetDlgItem(hwnd, IDC_LBL_VSR_QUAL), panelX + u(8) + u(110), y + u(4), u(50), rowH, TRUE);
        MoveWindow(GetDlgItem(hwnd, IDC_CMB_VSR_QUAL), panelX + u(8) + u(160), y,
                   panelW - u(160) - u(24), rowH, TRUE);
        y += rowH + gap;
        row(IDC_LBL_PEAK_NITS, IDC_EDT_PEAK_NITS);
        row(IDC_LBL_FPS, IDC_CMB_FPS);
        row(IDC_LBL_RIFE, IDC_EDT_RIFE_ENGINE, IDC_BTN_BROWSE_RIFE, u(56));
        row(IDC_LBL_GPU, IDC_EDT_GPU);
        row(IDC_LBL_FRAMES, IDC_EDT_FRAMES);
        MoveWindow(GetDlgItem(hwnd, IDC_CHK_AUDIO), panelX + u(8), y + u(4), panelW / 2 - u(12), rowH, TRUE);
        MoveWindow(GetDlgItem(hwnd, IDC_CHK_HDR10PLUS), panelX + panelW / 2 + u(4), y + u(4),
                   panelW / 2 - u(12), rowH, TRUE);
        y += rowH + gap;
        row(IDC_LBL_LUT, IDC_EDT_LUT, IDC_BTN_BROWSE_LUT, u(56));
        y += u(6);
        MoveWindow(GetDlgItem(hwnd, IDC_BTN_START), panelX + u(8), y, (panelW - u(28)) / 2, u(32), TRUE);
        MoveWindow(GetDlgItem(hwnd, IDC_BTN_STOP), panelX + u(12) + (panelW - u(28)) / 2, y,
                   (panelW - u(28)) / 2, u(32), TRUE);
        y += u(36);
        MoveWindow(GetDlgItem(hwnd, IDC_PROGRESS), panelX + u(8), y, panelW - u(16), u(14), TRUE);
        y += u(18);
        MoveWindow(GetDlgItem(hwnd, IDC_LBL_STATUS), panelX + u(8), y, panelW - u(16), u(18), TRUE);
        y += u(20);
        MoveWindow(GetDlgItem(hwnd, IDC_LBL_TELEMETRY), panelX + u(8), y, panelW - u(16), u(18), TRUE);
    }
}

void updateTabStates(HWND hwnd) {
    InvalidateRect(GetDlgItem(hwnd, IDC_TAB_WS_ENCODER), nullptr, FALSE);
    InvalidateRect(GetDlgItem(hwnd, IDC_TAB_WS_PLAYER), nullptr, FALSE);
    InvalidateRect(GetDlgItem(hwnd, IDC_TAB_WS_NLE), nullptr, FALSE);
}

void commitViewportVsrScale(HWND hwnd);

void setWorkspace(HWND hwnd, rtx::gui::WorkspaceMode mode) {
    if (g_app.workspace == mode && !g_app.fullscreen) {
        return;
    }
    g_app.workspace = mode;
    rtx::gui::setActiveWorkspace(mode);
    applyWorkspaceLayout(hwnd);
    updateTabStates(hwnd);
    commitViewportVsrScale(hwnd);  // RW-3 timing (2): workspace relayout
    const char* names[] = {"encoder", "player", "nle"};
    rtx::logInfo(std::string("workspace -> ") + names[static_cast<int>(mode)]);
}

// ---------------------------------------------------------------------------
// Borderless fullscreen quick-play (the "+1" workstation form).
// ---------------------------------------------------------------------------
void setBorderlessFullscreen(HWND hwnd, bool fullscreen) {
    if (fullscreen == g_app.fullscreen) {
        return;
    }
    g_app.fullscreen = fullscreen;
    if (fullscreen) {
        g_app.prevStyle = static_cast<DWORD>(GetWindowLongW(hwnd, GWL_STYLE));
        g_app.prevPlacement.length = sizeof(WINDOWPLACEMENT);
        GetWindowPlacement(hwnd, &g_app.prevPlacement);
        SetWindowLongW(hwnd, GWL_STYLE, static_cast<LONG>(WS_POPUP | WS_VISIBLE | WS_CLIPCHILDREN));
        const HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi{};
        mi.cbSize = sizeof(mi);
        GetMonitorInfoW(monitor, &mi);
        SetWindowPos(hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top,
                     mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top,
                     SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        rtx::logInfo("borderless fullscreen entered (quick-play)");
    } else {
        SetWindowLongW(hwnd, GWL_STYLE, static_cast<LONG>(g_app.prevStyle));
        SetWindowPlacement(hwnd, &g_app.prevPlacement);
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
        rtx::logInfo("borderless fullscreen exited");
    }
    applyWorkspaceLayout(hwnd);
    commitViewportVsrScale(hwnd);  // RW-3 timing (1): fullscreen toggle never sends WM_EXITSIZEMOVE
}

// ---------------------------------------------------------------------------
// Viewport-driven dynamic VSR scaling (RW-3): four recompute timings —
// fullscreen toggle, workspace switch, new media probe, and mouse release
// (WM_EXITSIZEMOVE).  Drag (WM_ENTERSIZEMOVE..WM_EXITSIZEMOVE) never recomputes.
// ---------------------------------------------------------------------------
void commitViewportVsrScale(HWND hwnd) {
    if (!g_app.previewViewport.has_value()) {
        return;
    }
    RECT vp{};
    if (!GetClientRect(g_app.previewViewport->hwnd(), &vp) || vp.right <= 0 || vp.bottom <= 0) {
        return;
    }
    const rtx::gui::DynamicVsrTarget target =
        rtx::gui::computeViewportVsrTarget(g_app.srcWidth, g_app.srcHeight, vp.right, vp.bottom);
    rtx::gui::storeActiveVsrTarget(target);
    g_app.previewViewport->resizeSwapChain(vp.right, vp.bottom);
    const std::wstring scaleStr = rtx::gui::formatVsrScale(static_cast<float>(target.scale));
    wchar_t osd[160];
    if (target.vsr_active) {
        _snwprintf_s(osd, _countof(osd), _TRUNCATE, L"RTX VSR %ls -> %d x %d",
                     scaleStr.c_str(), target.target_width, target.target_height);
    } else {
        _snwprintf_s(osd, _countof(osd), _TRUNCATE, L"RTX VSR bypass (scale %ls <= 1.00x)",
                     scaleStr.c_str());
    }
    SetDlgItemTextW(hwnd, IDC_LBL_VSR_OSD, osd);
    rtx::logInfo("recomputed dynamic VSR scale: " + wideToUtf8(osd) +
                 " (viewport " + std::to_string(vp.right) + "x" + std::to_string(vp.bottom) + ")");
}

// RW-2 fix: one probe helper for every media-loading entry point.  Feeds the
// real source geometry into the dynamic VSR calc and the real duration into
// the NLE timeline + scrub range, then refreshes the scale immediately.
void applyProbedMedia(HWND hwnd, const std::filesystem::path& path) {
    const rtx::gui::ProbedMedia media = rtx::gui::probeMediaFile(path);
    if (media.ok) {
        g_app.srcWidth = media.width;
        g_app.srcHeight = media.height;
        auto& timeline = rtx::gui::sharedNleTimeline();
        timeline.duration_ms = media.duration_ms;
        SendMessageW(GetDlgItem(hwnd, IDC_TRK_SCRUB), TBM_SETRANGEMAX, TRUE,
                     static_cast<LPARAM>(media.duration_ms));
        rtx::logInfo("media probed: " + wideToUtf8(path.wstring()) + " " +
                     std::to_string(media.width) + "x" + std::to_string(media.height) + " " +
                     std::to_string(media.duration_ms) + " ms");
    }
    commitViewportVsrScale(hwnd);
}

// ---------------------------------------------------------------------------
// Transcode actions.  The single collect path validates through the SSOT
// schema; HwTranscodeOptions come from rtx::profileToHwOptions only.
// ---------------------------------------------------------------------------

bool collectOptions(HWND hwnd, rtx::HwTranscodeOptions& out) {
    guiProfile() = collectProfileFromUi(hwnd);
    auto validation = rtx::validateAndNormalizeProfile(guiProfile(), false);
    if (!validation) {
        const std::string detail = validation.error().toString();
        rtx::logError("profile validation failed: " + detail);
        SetDlgItemTextW(hwnd, IDC_LBL_STATUS, utf8ToWide(detail).c_str());
        return false;
    }
    // validateAndNormalizeProfile already derives the SSOT output path into
    // guiProfile().output (Bug 2 fix lives entirely in the schema).
    SetDlgItemTextW(hwnd, IDC_EDT_OUTPUT, utf8ToWide(guiProfile().output).c_str());
    out = rtx::profileToHwOptions(guiProfile());
    g_app.splitRatio.store(0.5f, std::memory_order_relaxed);
    out.split_ratio = &g_app.splitRatio;
    if (g_app.previewEnabled) {
        out.preview = [](const void* data, std::uint32_t pitch) {
            std::lock_guard<std::mutex> lock(g_app.previewMutex);
            const size_t bytes = static_cast<size_t>(pitch) * kPreviewHeight;
            g_app.previewHost.assign(static_cast<const uint8_t*>(data),
                                 static_cast<const uint8_t*>(data) + bytes);
            g_app.previewPitch = pitch;
            g_app.previewDirty = true;
        };
    }
    out.cancel = &g_app.cancelRequested;
    return true;
}

void refreshQueueListbox(HWND hwnd);

void onStartTranscode(HWND hwnd) {
    if (guiIsTranscoding()) return;

    guiProfile() = collectProfileFromUi(hwnd);
    wchar_t inPath[MAX_PATH] = {};
    GetDlgItemTextW(hwnd, IDC_EDT_INPUT, inPath, MAX_PATH);

    auto snapshots = g_app.jobQueue.snapshot();
    bool hasPending = false;
    for (const auto& job : snapshots) {
        if (job.status == rtx::JobStatus::Pending) {
            hasPending = true;
            rtx::TranscodeProfile updated = guiProfile();
            if (!job.inputs.empty()) {
                updated.input = wideToUtf8(job.inputs.front().wstring());
            }
            if (updated.output.empty() || updated.auto_output) {
                updated.output = rtx::deriveOutputPath(updated.input, updated.codec, {}, true);
            }
            (void)g_app.jobQueue.updatePendingJob(job.job_id, updated);
        }
    }

    if (!hasPending && inPath[0] != L'\0' && std::filesystem::exists(inPath)) {
        rtx::TranscodeProfile profile = guiProfile();
        profile.input = wideToUtf8(inPath);
        if (profile.output.empty()) {
            profile.output = rtx::deriveOutputPath(profile.input, profile.codec, {}, true);
        }
        (void)g_app.jobQueue.enqueueJob({std::filesystem::path(inPath)}, profile,
                                        std::filesystem::path(inPath).parent_path());
        hasPending = true;
    }

    refreshQueueListbox(hwnd);
    snapshots = g_app.jobQueue.snapshot();

    if (!hasPending) {
        MessageBoxW(hwnd, rtx::trW(rtx::StrId::StatusDropFileOnly),
                    rtx::trW(rtx::StrId::AppTitle), MB_ICONWARNING);
        return;
    }

    g_app.cancelRequested.store(false, std::memory_order_relaxed);
    setGuiTranscoding(true);
    syncControlStates(hwnd);
    SendMessageW(GetDlgItem(hwnd, IDC_PROGRESS), PBM_SETPOS, 0, 0);
    SetDlgItemTextW(hwnd, IDC_LBL_STATUS, rtx::trW(rtx::StrId::StatusWorking));
    rtx::logInfo("job queue start: " + std::to_string(snapshots.size()) + " job(s)");

    g_app.jobQueue.startWorker();
}

void onStopTranscode(HWND hwnd) {
    if (!guiIsTranscoding()) return;
    g_app.cancelRequested.store(true, std::memory_order_release);
    g_app.jobQueue.skipCurrentJob();
    SetDlgItemTextW(hwnd, IDC_LBL_STATUS, rtx::trW(rtx::StrId::StatusStopping));
}

void onBrowseInput(HWND hwnd) {
    wchar_t fileName[MAX_PATH] = {};
    OPENFILENAMEW ofn{
        .lStructSize = sizeof(OPENFILENAMEW),
        .hwndOwner = hwnd,
        .lpstrFilter = L"Video Files (*.mp4;*.mkv;*.mov;*.ts)\0*.mp4;*.mkv;*.mov;*.ts\0All Files (*.*)\0*.*\0",
        .lpstrFile = fileName,
        .nMaxFile = MAX_PATH,
        .Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST
    };
    if (GetOpenFileNameW(&ofn)) {
        SetDlgItemTextW(hwnd, IDC_EDT_INPUT, fileName);
        SetDlgItemTextW(hwnd, IDC_EDT_OUTPUT, L"");
        autoFillOutputPath(hwnd);
        // RW-2 entry point 1/5: probe the picked media, refresh VSR.
        applyProbedMedia(hwnd, std::filesystem::path(fileName));
    }
}

void onBrowseOutput(HWND hwnd) {
    wchar_t fileName[MAX_PATH] = {};
    OPENFILENAMEW ofn{
        .lStructSize = sizeof(OPENFILENAMEW),
        .hwndOwner = hwnd,
        .lpstrFilter = L"MP4 Video (*.mp4)\0*.mp4\0Matroska (*.mkv)\0*.mkv\0All Files (*.*)\0*.*\0",
        .lpstrFile = fileName,
        .nMaxFile = MAX_PATH,
        .Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST
    };
    if (GetSaveFileNameW(&ofn)) {
        SetDlgItemTextW(hwnd, IDC_EDT_OUTPUT, fileName);
    }
}

void onBrowseLut(HWND hwnd) {
    wchar_t fileName[MAX_PATH] = {};
    OPENFILENAMEW ofn{
        .lStructSize = sizeof(OPENFILENAMEW),
        .hwndOwner = hwnd,
        .lpstrFilter = L"3D LUT (*.cube)\0*.cube\0All Files (*.*)\0*.*\0",
        .lpstrFile = fileName,
        .nMaxFile = MAX_PATH,
        .Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST
    };
    if (GetOpenFileNameW(&ofn)) {
        SetDlgItemTextW(hwnd, IDC_EDT_LUT, fileName);
    }
}

void onBrowseRife(HWND hwnd) {
    wchar_t fileName[MAX_PATH] = {};
    OPENFILENAMEW ofn{
        .lStructSize = sizeof(OPENFILENAMEW),
        .hwndOwner = hwnd,
        .lpstrFilter = L"TensorRT Engine (*.engine)\0*.engine\0All Files (*.*)\0*.*\0",
        .lpstrFile = fileName,
        .nMaxFile = MAX_PATH,
        .Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST
    };
    if (GetOpenFileNameW(&ofn)) {
        SetDlgItemTextW(hwnd, IDC_EDT_RIFE_ENGINE, fileName);
    }
}

void onQueueAdd(HWND hwnd) {
    wchar_t files[16384] = {};
    OPENFILENAMEW ofn{
        .lStructSize = sizeof(OPENFILENAMEW),
        .hwndOwner = hwnd,
        .lpstrFilter = L"Video Files (*.mp4;*.mkv;*.mov;*.ts)\0*.mp4;*.mkv;*.mov;*.ts\0All Files (*.*)\0*.*\0",
        .lpstrFile = files,
        .nMaxFile = 16384,
        .Flags = OFN_ALLOWMULTISELECT | OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST
    };
    if (!GetOpenFileNameW(&ofn)) {
        return;
    }
    const std::wstring dir(files);
    std::vector<std::filesystem::path> inputs;
    wchar_t* cursor = files + dir.size() + 1;
    if (*cursor == L'\0') {
        inputs.push_back(std::filesystem::path(dir));
    } else {
        while (*cursor != L'\0') {
            inputs.push_back(std::filesystem::path(dir) / cursor);
            cursor += wcslen(cursor) + 1;
        }
    }
    if (!inputs.empty()) {
        guiProfile() = collectProfileFromUi(hwnd);
        rtx::TranscodeProfile profile = guiProfile();
        profile.input = wideToUtf8(inputs.front().wstring());
        if (profile.output.empty()) {
            profile.output = rtx::deriveOutputPath(profile.input, profile.codec, {}, true);
        }
        (void)g_app.jobQueue.enqueueJob(inputs, profile, inputs.front().parent_path());
        refreshQueueListbox(hwnd);
        applyProbedMedia(hwnd, inputs.front());
    }
}

void onQueueRemove(HWND hwnd) {
    const LRESULT sel = SendDlgItemMessageW(hwnd, IDC_LST_QUEUE, LB_GETCURSEL, 0, 0);
    if (sel < 0) return;
    const auto snapshots = g_app.jobQueue.snapshot();
    if (static_cast<size_t>(sel) < snapshots.size()) {
        (void)g_app.jobQueue.removeJob(snapshots[static_cast<size_t>(sel)].job_id);
        refreshQueueListbox(hwnd);
    }
}

void onQueueClear(HWND hwnd) {
    g_app.jobQueue.clearCompleted();
    refreshQueueListbox(hwnd);
}

// ---------------------------------------------------------------------------
// Scrub preview (single-frame remaster at the slider position).
// ---------------------------------------------------------------------------
void runScrubPreviewAsync(HWND hwnd) {
    if (guiIsTranscoding()) {
        return;
    }
    wchar_t inPath[MAX_PATH] = {};
    GetDlgItemTextW(hwnd, IDC_EDT_INPUT, inPath, MAX_PATH);
    if (inPath[0] == L'\0' || !std::filesystem::exists(inPath)) {
        return;
    }
    const LRESULT pos = SendMessageW(GetDlgItem(hwnd, IDC_TRK_SCRUB), TBM_GETPOS, 0, 0);
    const LRESULT range_min = SendMessageW(GetDlgItem(hwnd, IDC_TRK_SCRUB), TBM_GETRANGEMIN, 0, 0);
    const LRESULT range_max = SendMessageW(GetDlgItem(hwnd, IDC_TRK_SCRUB), TBM_GETRANGEMAX, 0, 0);
    const int64_t duration_ms = rtx::gui::sharedNleTimeline().duration_ms;
    const int64_t seek_ms =
        range_max > range_min
            ? (static_cast<int64_t>(pos) - range_min) * duration_ms / (range_max - range_min)
            : 0;

    if (g_app.scrubThread.joinable()) {
        return;  // one scrub at a time; the slider re-fires on release
    }
    g_app.previewBusy.store(true, std::memory_order_relaxed);
    g_app.scrubThread = std::thread([hwnd, inPathW = std::wstring(inPath), seek_ms]() {
        rtx::HwTranscodeOptions opt;
        if (!collectOptions(hwnd, opt)) {
            g_app.previewBusy.store(false, std::memory_order_relaxed);
            return;
        }
        opt.input_path = inPathW;
        opt.preview = [](const void* data, std::uint32_t pitch) {
            std::lock_guard<std::mutex> lock(g_app.previewMutex);
            const size_t bytes = static_cast<size_t>(pitch) * kPreviewHeight;
            g_app.previewHost.assign(static_cast<const uint8_t*>(data),
                                 static_cast<const uint8_t*>(data) + bytes);
            g_app.previewPitch = pitch;
            g_app.previewDirty = true;
        };
        auto result = rtx::runScrubPreview(opt, seek_ms);
        (void)result;
        g_app.previewBusy.store(false, std::memory_order_relaxed);
    });
}

// RW-4: playback/pause toggle shared by IDC_BTN_PLAYER_PLAY and VK_SPACE.
void togglePlayback(HWND hwnd) {
    g_app.isPlaying = !g_app.isPlaying;
    rtx::gui::setGuiPlaying(g_app.isPlaying);
    InvalidateRect(GetDlgItem(hwnd, IDC_BTN_PLAYER_PLAY), nullptr, FALSE);
    rtx::logInfo(std::string("playback -> ") + (g_app.isPlaying ? "playing" : "paused"));
}

void refreshQueueListbox(HWND hwnd) {
    SendDlgItemMessageW(hwnd, IDC_LST_QUEUE, LB_RESETCONTENT, 0, 0);
    const auto snapshots = g_app.jobQueue.snapshot();
    for (const auto& job : snapshots) {
        std::wstring statusTag = L"[Pending] ";
        if (job.status == rtx::JobStatus::Running) {
            statusTag = L"[Running " + std::to_wstring(static_cast<int>(job.total_job_pct)) + L"%] ";
        } else if (job.status == rtx::JobStatus::Completed) {
            statusTag = L"[Done] ";
        } else if (job.status == rtx::JobStatus::Failed) {
            statusTag = L"[Failed] ";
        } else if (job.status == rtx::JobStatus::Cancelled) {
            statusTag = L"[Cancelled] ";
        }

        std::wstring label = statusTag + (job.inputs.empty() ? L"" : job.inputs.front().wstring()) +
                             L" [" + utf8ToWide(job.profile.codec) + L"/" +
                             utf8ToWide(job.profile.rate_control) + L"/QP" +
                             std::to_wstring(job.profile.qp) + L"]";
        SendDlgItemMessageW(hwnd, IDC_LST_QUEUE, LB_ADDSTRING, 0,
                            reinterpret_cast<LPARAM>(label.c_str()));
    }
}

// Owner-draw dispatch: maps control ids to skin + caption.


LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            g_app.hbrBg = CreateSolidBrush(rtx::gui::kClrPanel);
            g_app.hbrCtrl = CreateSolidBrush(rtx::gui::kClrCtrl);

            g_app.hFont = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                  DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                  CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            g_app.hFontBold = CreateFontW(-14, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                      DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                      CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

            BOOL dark = TRUE;
            DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));

            rtx::gui::createWorkstationControls(hwnd, g_app.hFont, g_app.hFontBold);

            // Realtime A/B monitoring viewport (created last to z-order above).
            auto viewport = rtx::gui::PreviewViewport::create(hwnd, kPreviewWidth, kPreviewHeight);
            if (viewport.has_value()) {
                g_app.previewViewport.emplace(std::move(viewport.value()));
                g_app.previewEnabled = true;
            } else {
                g_app.previewEnabled = false;
            }

            DragAcceptFiles(hwnd, TRUE);
            RegisterHotKey(hwnd, 1, MOD_CONTROL, 'L');
            populateComboBoxes(hwnd);
            applyProfileToUi(hwnd, guiProfile());
            refreshUiLanguage(hwnd);
            syncControlStates(hwnd);
            applyWorkspaceLayout(hwnd);
            g_app.jobQueue.setEventCallback([hwnd](const rtx::JobQueueEvent& evt) {
                if (evt.kind == rtx::JobQueueEvent::Kind::JobProgress) {
                    int permille = std::clamp(static_cast<int>(evt.snapshot.total_job_pct * 10.0), 0, 1000);
                    int fps10 = static_cast<int>(evt.snapshot.current_fps * 10.0);
                    PostMessageW(hwnd, WM_APP_PROGRESS, static_cast<WPARAM>(permille), static_cast<LPARAM>(fps10));
                } else {
                    PostMessageW(hwnd, WM_APP_QUEUE_UPDATED, static_cast<WPARAM>(evt.kind), static_cast<LPARAM>(evt.job_id));
                }
            });
            // RW-4: playback transport clock (gated on g_app.isPlaying).
            SetTimer(hwnd, 3, 66, nullptr);
            return 0;
        }

        case WM_SIZE:
            applyWorkspaceLayout(hwnd);
            // RW-3 timing (4b): a maximize/restore that never went through the
            // drag loop must still recompute the dynamic VSR target.
            if (!g_app.inResizeDrag && (wParam == SIZE_MAXIMIZED || wParam == SIZE_RESTORED)) {
                commitViewportVsrScale(hwnd);
            }
            return 0;

        case WM_ENTERSIZEMOVE:
            g_app.inResizeDrag = true;
            return 0;

        case WM_EXITSIZEMOVE:
            g_app.inResizeDrag = false;
            commitViewportVsrScale(hwnd);  // dynamic VSR recompute on mouse release only
            return 0;

        case WM_GETMINMAXINFO: {
            auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
            const UINT dpi = GetDpiForWindow(hwnd);
            mmi->ptMinTrackSize = {MulDiv(1120, static_cast<int>(dpi), 96),
                                   MulDiv(760, static_cast<int>(dpi), 96)};
            return 0;
        }

        case WM_DROPFILES: {
            HDROP hDrop = reinterpret_cast<HDROP>(wParam);
            const UINT fileCount = DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);
            std::vector<std::wstring> paths;
            for (UINT i = 0; i < fileCount; ++i) {
                wchar_t path[MAX_PATH] = {};
                if (DragQueryFileW(hDrop, i, path, MAX_PATH) > 0) {
                    paths.emplace_back(path);
                }
            }
            DragFinish(hDrop);
            guiProfile() = collectProfileFromUi(hwnd);
            for (const auto& p : paths) {
                rtx::TranscodeProfile profile = guiProfile();
                profile.input = wideToUtf8(p);
                profile.output = rtx::deriveOutputPath(profile.input, profile.codec, {}, true);
                (void)g_app.jobQueue.enqueueJob({std::filesystem::path(p)}, profile, std::filesystem::path(p).parent_path());
            }
            refreshQueueListbox(hwnd);
            if (!paths.empty()) {
                SetDlgItemTextW(hwnd, IDC_EDT_INPUT, paths.front().c_str());
                SetDlgItemTextW(hwnd, IDC_EDT_OUTPUT, L"");
                autoFillOutputPath(hwnd);
            }
            addFilesToBin(hwnd, paths);
            // RW-2 entry point 2/5: probe the dropped media for real geometry
            // + duration, then refresh the dynamic VSR target immediately.
            if (!paths.empty()) {
                applyProbedMedia(hwnd, std::filesystem::path(paths.front()));
            }
            if (g_app.workspace != rtx::gui::WorkspaceMode::Encoder) {
                runScrubPreviewAsync(hwnd);  // Player/NLE: drop = load into monitor
            }
            return 0;
        }

        case WM_APP_SPLIT_DRAG: {
            const UINT mouseMsg = static_cast<UINT>(wParam);
            if (mouseMsg == WM_LBUTTONDBLCLK) {
                setBorderlessFullscreen(hwnd, !g_app.fullscreen);
                return 0;
            }
            const int x = GET_X_LPARAM(lParam);
            RECT rc;
            if (g_app.previewViewport.has_value() && GetClientRect(g_app.previewViewport->hwnd(), &rc) &&
                rc.right > 0) {
                const float ratio = std::clamp(static_cast<float>(x) / static_cast<float>(rc.right), 0.0f, 1.0f);
                if (mouseMsg != WM_LBUTTONUP) {
                    g_app.splitRatio.store(ratio, std::memory_order_relaxed);
                }
                g_app.previewViewport->setSplitRatio(ratio);
            }
            return 0;
        }

        case rtx::gui::WM_APP_TIMELINE_SEEK: {
            updateNleTimecode(hwnd);
            InvalidateRect(GetDlgItem(hwnd, IDC_NLE_TIMELINE), nullptr, FALSE);
            return 0;
        }

        case WM_HSCROLL: {
            if (reinterpret_cast<HWND>(lParam) == GetDlgItem(hwnd, IDC_TRK_SCRUB)) {
                runScrubPreviewAsync(hwnd);
            }
            return 0;
        }

        case WM_APP_PREVIEW: {
            std::vector<uint8_t> frame;
            uint32_t pitch = 0;
            {
                std::lock_guard<std::mutex> lock(g_app.previewMutex);
                if (!g_app.previewDirty || g_app.previewHost.empty()) {
                    return 0;
                }
                g_app.previewDirty = false;
                frame = g_app.previewHost;
                pitch = g_app.previewPitch;
            }
            if (g_app.previewViewport.has_value()) {
                g_app.previewViewport->presentFromHostOrShared(frame.data(), pitch);
            }
            return 0;
        }

        case WM_DRAWITEM:
            if (rtx::gui::paintCommandButton(reinterpret_cast<const DRAWITEMSTRUCT*>(lParam), g_app.hFont)) {
                return TRUE;
            }
            break;

        case WM_COMMAND: {
            int id = LOWORD(wParam);
            int code = HIWORD(wParam);
            if (code == BN_CLICKED) {
                switch (id) {
                    case IDC_BTN_START: onStartTranscode(hwnd); return 0;
                    case IDC_BTN_STOP: onStopTranscode(hwnd); return 0;
                    case IDC_BTN_BROWSE_IN: onBrowseInput(hwnd); return 0;
                    case IDC_BTN_BROWSE_OUT: onBrowseOutput(hwnd); return 0;
                    case IDC_BTN_BROWSE_LUT: onBrowseLut(hwnd); return 0;
                    case IDC_BTN_BROWSE_RIFE: onBrowseRife(hwnd); return 0;
                    case IDC_TAB_WS_ENCODER: setWorkspace(hwnd, rtx::gui::WorkspaceMode::Encoder); return 0;
                    case IDC_TAB_WS_PLAYER: setWorkspace(hwnd, rtx::gui::WorkspaceMode::Player); return 0;
                    case IDC_TAB_WS_NLE: setWorkspace(hwnd, rtx::gui::WorkspaceMode::Nle); return 0;
                    case IDC_BTN_PRESET_MASTER: applyGoldenPresetById(hwnd, "master"); return 0;
                    case IDC_BTN_PRESET_RIFE: applyGoldenPresetById(hwnd, "rife"); return 0;
                    case IDC_BTN_PRESET_ESPORTS: applyGoldenPresetById(hwnd, "esports"); return 0;
                    case IDC_BTN_PRESET_COMPAT: applyGoldenPresetById(hwnd, "compat"); return 0;
                    case IDC_BTN_PRESET_NATIVE: applyGoldenPresetById(hwnd, "native"); return 0;
                    case IDC_BTN_QUEUE_ADD: onQueueAdd(hwnd); return 0;
                    case IDC_BTN_QUEUE_REMOVE: onQueueRemove(hwnd); return 0;
                    case IDC_BTN_QUEUE_CLEAR: onQueueClear(hwnd); return 0;
                    case IDC_BTN_MARK_IN: onNleMarkIn(hwnd); return 0;
                    case IDC_BTN_MARK_OUT: onNleMarkOut(hwnd); return 0;
                    case IDC_BTN_RAZOR: onNleRazor(hwnd); return 0;
                    case IDC_BTN_PLAYER_PLAY:
                        // RW-4: transport button toggles the playback loop.
                        togglePlayback(hwnd);
                        return 0;
                    case IDC_BTN_STEP_BACK:
                    case IDC_BTN_STEP_FWD: {
                        const LRESULT pos = SendMessageW(GetDlgItem(hwnd, IDC_TRK_SCRUB), TBM_GETPOS, 0, 0);
                        // One frame at the 24 fps film base (~41.7 ms).
                        const LRESULT step = (id == IDC_BTN_STEP_BACK) ? -1000 / 24 : 1000 / 24;
                        SendMessageW(GetDlgItem(hwnd, IDC_TRK_SCRUB), TBM_SETPOS, TRUE,
                                     static_cast<LPARAM>(std::max<LRESULT>(0, pos + step)));
                        runScrubPreviewAsync(hwnd);
                        return 0;
                    }
                    case IDC_BTN_NLE_EXPORT: if (rtx::gui::exportSelectedClipToEncoder(hwnd)) { setWorkspace(hwnd, rtx::gui::WorkspaceMode::Encoder); } return 0;
                    case IDC_BTN_PROBE: runHardwareProbe(hwnd); return 0;
                    case IDC_BTN_LANG: rtx::toggleLanguage(); refreshUiLanguage(hwnd); return 0;
                    case IDC_CHK_VSR: syncControlStates(hwnd); return 0;
                    default: break;
                }
            } else if (code == CBN_SELCHANGE) {
                if (id == IDC_CMB_CODEC || id == IDC_CMB_RC || id == IDC_CMB_FPS) {
                    syncControlStates(hwnd);
                }
            } else if (code == LBN_SELCHANGE && id == IDC_LST_BIN) {
                const LRESULT sel = SendDlgItemMessageW(hwnd, IDC_LST_BIN, LB_GETCURSEL, 0, 0);
                auto& timeline = rtx::gui::sharedNleTimeline();
                if (sel >= 0 && sel < static_cast<LRESULT>(timeline.clips.size())) {
                    timeline.selected = static_cast<int>(sel);
                    SetDlgItemTextW(hwnd, IDC_EDT_INPUT,
                                    timeline.clips[static_cast<size_t>(sel)].path.c_str());
                    SetDlgItemTextW(hwnd, IDC_EDT_OUTPUT, L"");
                    autoFillOutputPath(hwnd);
                    InvalidateRect(GetDlgItem(hwnd, IDC_NLE_TIMELINE), nullptr, FALSE);
                }
                return 0;
            }
            return 0;
        }

        case WM_HOTKEY:
        case WM_APP_TOGGLE_LANG: {
            rtx::toggleLanguage();
            refreshUiLanguage(hwnd);
            return 0;
        }

        case WM_SYSKEYDOWN: {
            const bool altDown = (lParam & (1 << 29)) != 0;
            if (wParam == '1' && altDown) { setWorkspace(hwnd, rtx::gui::WorkspaceMode::Encoder); return 0; }
            if (wParam == '2' && altDown) { setWorkspace(hwnd, rtx::gui::WorkspaceMode::Player); return 0; }
            if (wParam == '3' && altDown) { setWorkspace(hwnd, rtx::gui::WorkspaceMode::Nle); return 0; }
            if (wParam == VK_RETURN && altDown) { setBorderlessFullscreen(hwnd, !g_app.fullscreen); return 0; }
            if (wParam == VK_F11) { setBorderlessFullscreen(hwnd, !g_app.fullscreen); return 0; }
            break;
        }

        case WM_KEYDOWN: {
            if (wParam == VK_F11) {
                setBorderlessFullscreen(hwnd, !g_app.fullscreen);
                return 0;
            }
            if (wParam == VK_ESCAPE && g_app.fullscreen) {
                setBorderlessFullscreen(hwnd, false);
                return 0;
            }
            if (wParam == VK_SPACE) {
                // RW-4: space toggles quick-play playback/pause.
                togglePlayback(hwnd);
                return 0;
            }
            if ((wParam == 'L' || wParam == 'l') &&
                ((GetKeyState(VK_CONTROL) < 0) || ((GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0))) {
                rtx::toggleLanguage();
                refreshUiLanguage(hwnd);
                return 0;
            }
            break;
        }

        case WM_APP_PROGRESS: {
            int permille = static_cast<int>(wParam);
            double fps = static_cast<double>(lParam) / 10.0;
            SendMessageW(GetDlgItem(hwnd, IDC_PROGRESS), PBM_SETPOS, permille, 0);
            wchar_t buf[192];
            const auto snapshots = g_app.jobQueue.snapshot();
            size_t batch_total = snapshots.size();
            size_t batch_index = 0;
            for (size_t i = 0; i < snapshots.size(); ++i) {
                if (snapshots[i].status == rtx::JobStatus::Running) {
                    batch_index = i;
                    break;
                }
            }
            if (batch_total > 1) {
                _snwprintf_s(buf, _countof(buf), _TRUNCATE,
                             L"Batch %zu/%zu  |  Progress: %.1f%%  |  Speed: %.1f FPS",
                             batch_index + 1, batch_total,
                             static_cast<double>(permille) / 10.0, fps);
            } else {
                _snwprintf_s(buf, _countof(buf), _TRUNCATE,
                             L"Progress: %.1f%%  |  Speed: %.1f FPS",
                             static_cast<double>(permille) / 10.0, fps);
            }
            SetDlgItemTextW(hwnd, IDC_LBL_TELEMETRY, buf);
            return 0;
        }

        case WM_APP_QUEUE_UPDATED: {
            refreshQueueListbox(hwnd);
            if (!g_app.jobQueue.isWorkerRunning()) {
                setGuiTranscoding(false);
                syncControlStates(hwnd);
                const auto snapshots = g_app.jobQueue.snapshot();
                const rtx::EncoderJob* lastFailed = nullptr;
                bool anyCancelled = false;
                for (const auto& j : snapshots) {
                    if (j.status == rtx::JobStatus::Failed) lastFailed = &j;
                    else if (j.status == rtx::JobStatus::Cancelled) anyCancelled = true;
                }
                if (lastFailed != nullptr) {
                    SetDlgItemTextW(hwnd, IDC_LBL_STATUS, rtx::trW(rtx::StrId::StatusFailed));
                    std::wstring detail = utf8ToWide(lastFailed->error_message.empty() ? "unknown error" : lastFailed->error_message);
                    std::wstring box_text = std::wstring(rtx::trW(rtx::StrId::StatusFailed)) + L": " + detail;
                    rtx::logError("transcode failed: " + lastFailed->error_message);
                    MessageBoxW(hwnd, box_text.c_str(), rtx::trW(rtx::StrId::AppTitle), MB_ICONERROR);
                } else if (anyCancelled) {
                    SetDlgItemTextW(hwnd, IDC_LBL_STATUS, rtx::trW(rtx::StrId::StatusCancelled));
                } else {
                    SendMessageW(GetDlgItem(hwnd, IDC_PROGRESS), PBM_SETPOS, 1000, 0);
                    SetDlgItemTextW(hwnd, IDC_LBL_STATUS, rtx::trW(rtx::StrId::StatusDonePrefix));
                }
            }
            return 0;
        }

        case WM_APP_DONE: {
            bool ok = (wParam == 1);
            setGuiTranscoding(false);
            syncControlStates(hwnd);
            if (g_app.workerThread.joinable()) {
                g_app.workerThread.join();
            }
            if (ok) {
                SendMessageW(GetDlgItem(hwnd, IDC_PROGRESS), PBM_SETPOS, 1000, 0);
                SetDlgItemTextW(hwnd, IDC_LBL_STATUS, rtx::trW(rtx::StrId::StatusDonePrefix));
                MessageBoxW(hwnd, rtx::trW(rtx::StrId::StatusDonePrefix),
                            rtx::trW(rtx::StrId::AppTitle), MB_ICONINFORMATION);
            } else {
                SetDlgItemTextW(hwnd, IDC_LBL_STATUS, rtx::trW(rtx::StrId::StatusFailed));
                std::wstring err_text = std::wstring(rtx::trW(rtx::StrId::StatusFailed)) + L": " +
                                        (g_app.lastErrorMessage.empty() ? L"unknown error" : g_app.lastErrorMessage);
                rtx::logError("transcode failed: " + wideToUtf8(g_app.lastErrorMessage));
                MessageBoxW(hwnd, err_text.c_str(), rtx::trW(rtx::StrId::AppTitle), MB_ICONERROR);
            }
            return 0;
        }

        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc{};
            GetClientRect(hwnd, &rc);
            rtx::gui::paintWorkspaceChrome(hdc, rc, g_app.workspace, g_app.hFont);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_CTLCOLORSTATIC: {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            SetTextColor(hdc, rtx::gui::kClrText);
            SetBkColor(hdc, rtx::gui::kClrPanel);
            return reinterpret_cast<LRESULT>(g_app.hbrBg);
        }

        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX:
        case WM_CTLCOLORBTN: {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            SetTextColor(hdc, rtx::gui::kClrText);
            SetBkColor(hdc, rtx::gui::kClrCtrl);
            return reinterpret_cast<LRESULT>(g_app.hbrCtrl);
        }

        case WM_TIMER: {
            if (wParam == 2) {
                bool dirty = false;
                {
                    std::lock_guard<std::mutex> lock(g_app.previewMutex);
                    dirty = g_app.previewDirty;
                }
                if (dirty) {
                    SendMessageW(hwnd, WM_APP_PREVIEW, 0, 0);
                }
            } else if (wParam == 3 && g_app.isPlaying) {
                // RW-4: the playback loop advances the transport every 66 ms,
                // loops at the end, and drives the next single-frame decode
                // only when the previous scrub finished (zero backlog).
                HWND track = GetDlgItem(hwnd, IDC_TRK_SCRUB);
                const LRESULT pos = SendMessageW(track, TBM_GETPOS, 0, 0);
                const LRESULT maxPos = SendMessageW(track, TBM_GETRANGEMAX, 0, 0);
                LRESULT next = pos + 66;
                if (maxPos > 0 && next >= maxPos) {
                    next = 0;  // reached the tail: loop
                    rtx::logInfo("playback loop restart (end of media)");
                }
                SendMessageW(track, TBM_SETPOS, TRUE, next);
                rtx::gui::sharedNleTimeline().playhead_ms = next;
                rtx::gui::updateNleTimecode(hwnd);
                InvalidateRect(GetDlgItem(hwnd, IDC_NLE_TIMELINE), nullptr, FALSE);
                if (!g_app.previewBusy.load(std::memory_order_relaxed)) {
                    runScrubPreviewAsync(hwnd);
                }
            }
            return 0;
        }

        case WM_DESTROY:
            UnregisterHotKey(hwnd, 1);
            KillTimer(hwnd, 2);
            KillTimer(hwnd, 3);
            if (g_app.scrubThread.joinable()) {
                g_app.scrubThread.join();
            }
            if (g_app.workerThread.joinable()) {
                g_app.cancelRequested.store(true, std::memory_order_release);
                g_app.workerThread.join();
            }
            g_app.previewViewport.reset();
            if (g_app.hbrBg) DeleteObject(g_app.hbrBg);
            if (g_app.hbrCtrl) DeleteObject(g_app.hbrCtrl);
            if (g_app.hFont) DeleteObject(g_app.hFont);
            if (g_app.hFontBold) DeleteObject(g_app.hFontBold);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR /*commandLine*/, int showCommand) {
    rtx::initRuntimeEnvironment();

    INITCOMMONCONTROLSEX icce{
        .dwSize = sizeof(INITCOMMONCONTROLSEX),
        .dwICC = ICC_STANDARD_CLASSES | ICC_PROGRESS_CLASS | ICC_UPDOWN_CLASS | ICC_BAR_CLASSES
    };
    InitCommonControlsEx(&icce);

    // Stage 2 "+1" routing: a single existing-file argument launches borderless
    // fullscreen quick-play; every other argument set stays with the CLI.
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::wstring quickPlayFile;
    if (argv != nullptr && argc == 2 && argv[1][0] != L'-' &&
        std::filesystem::exists(argv[1])) {
        quickPlayFile = argv[1];
    } else if (argv != nullptr && argc > 1) {
        std::vector<std::string> cliArgs;
        cliArgs.reserve(static_cast<size_t>(argc));
        for (int i = 0; i < argc; ++i) {
            cliArgs.push_back(wideToUtf8(argv[i]));
        }
        LocalFree(argv);

        std::vector<char*> cliPtrs;
        cliPtrs.reserve(cliArgs.size() + 1);
        for (auto& s : cliArgs) {
            cliPtrs.push_back(s.data());
        }
        cliPtrs.push_back(nullptr);
        return rtxCliMain(static_cast<int>(cliArgs.size()), cliPtrs.data());
    }
    if (argv != nullptr) {
        LocalFree(argv);
    }

    if (!rtx::gui::registerNleTimelineWindowClass(instance)) {
        return 1;
    }
    WNDCLASSEXW wc{
        .cbSize = sizeof(WNDCLASSEXW),
        .style = CS_HREDRAW | CS_VREDRAW,
        .lpfnWndProc = windowProc,
        .hInstance = instance,
        .hIcon = LoadIconW(nullptr, IDI_APPLICATION),
        .hCursor = LoadCursorW(nullptr, IDC_ARROW),
        .hbrBackground = nullptr,
        .lpszClassName = L"CineRtxNativeGui",
        .hIconSm = LoadIconW(nullptr, IDI_APPLICATION)
    };
    if (RegisterClassExW(&wc) == 0) {
        return 1;
    }

    const UINT dpi = GetDpiForSystem();
    RECT wr{0, 0, MulDiv(1220, dpi, 96), MulDiv(860, dpi, 96)};
    AdjustWindowRectExForDpi(&wr, WS_OVERLAPPEDWINDOW, FALSE, 0, dpi);

    HWND hwnd = CreateWindowExW(
        WS_EX_ACCEPTFILES,
        wc.lpszClassName,
        rtx::trW(rtx::StrId::AppTitle),
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT,
        wr.right - wr.left, wr.bottom - wr.top,
        nullptr, nullptr, instance, nullptr
    );
    if (hwnd == nullptr) {
        return 1;
    }

    ShowWindow(hwnd, showCommand == SW_HIDE ? SW_SHOWNORMAL : showCommand);
    UpdateWindow(hwnd);

    SetTimer(hwnd, 2, 66, nullptr);

    if (!quickPlayFile.empty()) {
        g_app.quickPlayFile = quickPlayFile;
        SetDlgItemTextW(hwnd, IDC_EDT_INPUT, quickPlayFile.c_str());
        SetDlgItemTextW(hwnd, IDC_EDT_OUTPUT, L"");
        autoFillOutputPath(hwnd);
        setWorkspace(hwnd, rtx::gui::WorkspaceMode::Player);
        setBorderlessFullscreen(hwnd, true);
        // RW-2 entry point 5/5: probe the quick-play media for real geometry
        // + duration, then start the continuous playback loop (RW-4).
        applyProbedMedia(hwnd, std::filesystem::path(quickPlayFile));
        g_app.isPlaying = true;
        rtx::gui::setGuiPlaying(true);
        InvalidateRect(GetDlgItem(hwnd, IDC_BTN_PLAYER_PLAY), nullptr, FALSE);
        rtx::logInfo("quick-play launch: " + wideToUtf8(quickPlayFile) + " playback started");
        runScrubPreviewAsync(hwnd);
    }

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (msg.message == WM_KEYDOWN && (msg.wParam == 'L' || msg.wParam == 'l') &&
            ((GetKeyState(VK_CONTROL) < 0) || ((GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0))) {
            rtx::toggleLanguage();
            refreshUiLanguage(hwnd);
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}