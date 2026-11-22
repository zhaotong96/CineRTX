// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors

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

#include "rtx/job_queue.h"
#include "rtx/string_utils.h"
#include "rtx/i18n.h"
#include "rtx/logger.h"
#include "rtx/param_schema.h"
#include "rtx/ngx_remaster.h"
#include "rtx/playback_session.h"
#include "rtx/runtime_env.h"
#include "rtx/raii_owners.h"
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
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "dwmapi.lib")

int rtxCliMain(int argc, char** argv);

namespace {
using namespace rtx::gui;

constexpr UINT WM_APP_PROGRESS      = WM_APP + 10;
constexpr UINT WM_APP_QUEUE_UPDATED = WM_APP + 13;

constexpr int kPreviewWidth = 960;
constexpr int kPreviewHeight = 540;

constexpr int kTabIds[] = {IDC_TAB_WS_ENCODER, IDC_TAB_WS_PLAYER, IDC_TAB_WS_NLE};
constexpr int kQueueBtnIds[] = {IDC_BTN_QUEUE_ADD, IDC_BTN_QUEUE_REMOVE, IDC_BTN_QUEUE_CLEAR};
using InspectorRow = std::tuple<int, int, int, int>;
constexpr InspectorRow kInspectorRows[] = {
    {IDC_LBL_INPUT, IDC_EDT_INPUT, IDC_BTN_BROWSE_IN, 56},
    {IDC_LBL_OUTPUT, IDC_EDT_OUTPUT, IDC_BTN_BROWSE_OUT, 56},
    {IDC_LBL_CODEC, IDC_CMB_CODEC, -1, 0},
    {IDC_LBL_PRESET, IDC_CMB_PRESET, -1, 0},
    {IDC_LBL_RC, IDC_CMB_RC, -1, 0},
    {IDC_LBL_BITRATE, IDC_EDT_BITRATE, -1, 0},
    {IDC_LBL_QP, IDC_EDT_QP, -1, 0},
    {IDC_LBL_RES, IDC_CMB_RES, -1, 0},
    {IDC_LBL_GEOM, IDC_CMB_GEOM, -1, 0},
    {IDC_LBL_PEAK_NITS, IDC_EDT_PEAK_NITS, -1, 0},
    {IDC_LBL_FPS, IDC_CMB_FPS, -1, 0},
    {IDC_LBL_RIFE, IDC_EDT_RIFE_ENGINE, IDC_BTN_BROWSE_RIFE, 56},
    {IDC_LBL_GPU, IDC_EDT_GPU, -1, 0},
    {IDC_LBL_FRAMES, IDC_EDT_FRAMES, -1, 0},
    {IDC_LBL_LUT, IDC_EDT_LUT, IDC_BTN_BROWSE_LUT, 56},
};

[[nodiscard]] rtx::UniqueFont makeUiFont(bool bold) noexcept {
    return rtx::UniqueFont{CreateFontW(-14, 0, 0, 0, bold ? FW_BOLD : FW_NORMAL, FALSE, FALSE,
                                       FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                       CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                       DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI")};
}
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
    const bool showInspector = (isEncoder || g_app.workspace == rtx::gui::WorkspaceMode::Nle) && !fullscreen;
    const bool showPlayer = isPlayer && !fullscreen;

    const std::tuple<const int*, size_t, bool> kGroups[] = {
        {kInspectorIds, std::size(kInspectorIds), showInspector},
        {kEncoderIds, std::size(kEncoderIds), isEncoder && !fullscreen},
        {kPlayerIds, std::size(kPlayerIds), showPlayer || (isPlayer && fullscreen)},
        {kNleIds, std::size(kNleIds), (g_app.workspace == rtx::gui::WorkspaceMode::Nle) && !fullscreen},
    };
    for (const auto& [ids, count, visible] : kGroups) {
        showGroup(hwnd, ids, count, visible);
    }
    ShowWindow(GetDlgItem(hwnd, IDC_TRK_SCRUB), (showPlayer || isEncoder) ? SW_SHOW : SW_HIDE);
    for (int id : {IDC_TAB_WS_ENCODER, IDC_TAB_WS_PLAYER, IDC_TAB_WS_NLE, IDC_BTN_PROBE, IDC_BTN_LANG}) {
        ShowWindow(GetDlgItem(hwnd, id), fullscreen ? SW_HIDE : SW_SHOW);
    }
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
    const int tabStartX = u(380);
    const int tabW = u(110);
    for (size_t i = 0; i < 3; ++i) {
        MoveWindow(GetDlgItem(hwnd, kTabIds[i]), tabStartX + static_cast<int>(i) * (tabW + u(6)),
                   u(11), tabW, u(30), TRUE);
    }
    MoveWindow(GetDlgItem(hwnd, IDC_BTN_PROBE), width - u(220), u(14), u(100), u(24), TRUE);
    MoveWindow(GetDlgItem(hwnd, IDC_BTN_LANG), width - u(112), u(14), u(100), u(24), TRUE);

    if (isEncoder) {
        const int railW = u(224);
        const int railBtnW = railW - u(24);
        int y = bodyTop + u(10);
        for (int id : {IDC_BTN_PRESET_MASTER, IDC_BTN_PRESET_RIFE, IDC_BTN_PRESET_ESPORTS,
                       IDC_BTN_PRESET_COMPAT, IDC_BTN_PRESET_NATIVE}) {
            MoveWindow(GetDlgItem(hwnd, id), u(12), y, railBtnW, u(30), TRUE);
            y += u(34);
        }
        y += u(10);
        const int queueBtnW = (railBtnW - u(12)) / 3;
        for (size_t i = 0; i < 3; ++i) {
            MoveWindow(GetDlgItem(hwnd, kQueueBtnIds[i]),
                       u(12) + static_cast<int>(i) * (queueBtnW + u(6)), y, queueBtnW, u(26), TRUE);
        }
        y += u(30);
        MoveWindow(GetDlgItem(hwnd, IDC_LST_QUEUE), u(12), y, railBtnW,
                   std::max(u(60), height - y - u(12)), TRUE);

        const int centerW = width - railW - u(380);
        const int vpH = bodyH - u(120);
        if (g_app.previewViewport.has_value()) {
            g_app.previewViewport->setBounds(railW, bodyTop, std::max(u(120), centerW), std::max(u(80), vpH));
        }
        MoveWindow(GetDlgItem(hwnd, IDC_TRK_SCRUB), railW, bodyTop + vpH + u(8),
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
        const int binW = u(220);
        const int timelineH = u(230);
        const int monitorW = width - binW - u(380);
        const int monitorH = bodyH - timelineH - u(40);
        MoveWindow(GetDlgItem(hwnd, IDC_LST_BIN), u(8), bodyTop + u(8), binW - u(16),
                   bodyH - timelineH - u(24), TRUE);
        if (g_app.previewViewport.has_value()) {
            g_app.previewViewport->setBounds(binW, bodyTop, std::max(u(120), monitorW),
                                         std::max(u(80), monitorH));
        }
        const int toolY = bodyTop + monitorH + u(8);
        const int timecodeW = std::max(u(200), monitorW - u(300));
        MoveWindow(GetDlgItem(hwnd, IDC_LBL_TIMECODE), binW, toolY, timecodeW, u(22), TRUE);
        MoveWindow(GetDlgItem(hwnd, IDC_BTN_MARK_IN), binW + timecodeW, toolY, u(90), u(24), TRUE);
        MoveWindow(GetDlgItem(hwnd, IDC_BTN_MARK_OUT), binW + timecodeW + u(94), toolY, u(90), u(24), TRUE);
        MoveWindow(GetDlgItem(hwnd, IDC_BTN_RAZOR), binW + timecodeW + u(188), toolY, u(110), u(24), TRUE);
        MoveWindow(GetDlgItem(hwnd, IDC_NLE_TIMELINE), u(4), height - timelineH - u(4), width - u(8), timelineH, TRUE);
    }
    if (showInspector) {
        const int panelW = u(380);
        const int panelX = width - panelW;
        const int lblW = u(120);
        const int rowH = u(26);
        const int gap = u(2);
        int y = bodyTop + u(10);
        auto row = [&](int labelId, int ctrlId, int extraId = -1, int extraW = 0) {
            MoveWindow(GetDlgItem(hwnd, labelId), panelX + u(8), y + u(4), lblW, rowH, TRUE);
            MoveWindow(GetDlgItem(hwnd, ctrlId), panelX + u(8) + lblW, y,
                       panelW - lblW - u(24) - extraW, rowH, TRUE);
            if (extraId >= 0) {
                MoveWindow(GetDlgItem(hwnd, extraId), panelX + panelW - u(16) - extraW, y,
                           extraW, rowH, TRUE);
            }
            y += rowH + gap;
        };
        for (const auto& [labelId, ctrlId, extraId, extraW] : kInspectorRows) {
            row(labelId, ctrlId, extraId, extraW != 0 ? u(extraW) : 0);
        }
        MoveWindow(GetDlgItem(hwnd, IDC_CHK_VSR), panelX + u(8), y + u(4), u(110), rowH, TRUE);
        MoveWindow(GetDlgItem(hwnd, IDC_LBL_VSR_QUAL), panelX + u(8) + u(110), y + u(4), u(50), rowH, TRUE);
        MoveWindow(GetDlgItem(hwnd, IDC_CMB_VSR_QUAL), panelX + u(8) + u(160), y,
                   panelW - u(160) - u(24), rowH, TRUE);
        y += rowH + gap;
        MoveWindow(GetDlgItem(hwnd, IDC_CHK_AUDIO), panelX + u(8), y + u(4), panelW / 2 - u(12), rowH, TRUE);
        MoveWindow(GetDlgItem(hwnd, IDC_CHK_HDR10PLUS), panelX + panelW / 2 + u(4), y + u(4),
                   panelW / 2 - u(12), rowH, TRUE);
        y += rowH + gap + u(6);
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
    for (int id : kTabIds) {
        InvalidateRect(GetDlgItem(hwnd, id), nullptr, FALSE);
    }
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
    commitViewportVsrScale(hwnd);  // RW-3 timing (2)
    static const char* names[] = {"encoder", "player", "nle"};
    rtx::logInfo(std::string("workspace -> ") + names[static_cast<int>(mode)]);
}
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
    } else {
        SetWindowLongW(hwnd, GWL_STYLE, static_cast<LONG>(g_app.prevStyle));
        SetWindowPlacement(hwnd, &g_app.prevPlacement);
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    }
    applyWorkspaceLayout(hwnd);
    commitViewportVsrScale(hwnd);  // RW-3 timing (1)
}
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
    target.vsr_active ? _snwprintf_s(osd, _countof(osd), _TRUNCATE, L"RTX VSR %ls -> %d x %d",
                                     scaleStr.c_str(), target.target_width, target.target_height)
                      : _snwprintf_s(osd, _countof(osd), _TRUNCATE, L"RTX VSR bypass (scale %ls <= 1.00x)",
                                     scaleStr.c_str());
    SetDlgItemTextW(hwnd, IDC_LBL_VSR_OSD, osd);
    rtx::logInfo("VSR: " + wideToUtf8(osd) + " (viewport " + std::to_string(vp.right) + "x" +
                 std::to_string(vp.bottom) + ")");
}
void applyProbedMedia(HWND hwnd, const std::filesystem::path& path) {
    const rtx::gui::ProbedMedia media = rtx::gui::probeMediaFile(path);
    if (media.ok) {
        g_app.srcWidth = media.width;
        g_app.srcHeight = media.height;
        g_app.mediaDurationFlicks = media.duration_flicks;
        const int64_t duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            rtx::Flicks(media.duration_flicks)).count();
        SendMessageW(GetDlgItem(hwnd, IDC_TRK_SCRUB), TBM_SETRANGEMAX, TRUE,
                     static_cast<LPARAM>(duration_ms));
        rtx::logInfo("media probed: " + wideToUtf8(path.wstring()) + " " +
                     std::to_string(media.width) + "x" + std::to_string(media.height) + " " +
                     std::to_string(duration_ms) + " ms");
    }
    commitViewportVsrScale(hwnd);
}
void refreshQueueListbox(HWND hwnd);

void onStartTranscode(HWND hwnd) {
    if (guiIsTranscoding()) return;

    guiProfile() = collectProfileFromUi(hwnd);
    wchar_t inPath[MAX_PATH] = {};
    GetDlgItemTextW(hwnd, IDC_EDT_INPUT, inPath, MAX_PATH);
    bool hasPending = false;
    for (const auto& job : g_app.jobQueue.snapshot()) {
        if (job.status != rtx::JobStatus::Pending) {
            continue;
        }
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
    g_app.jobQueue.startWorker();
}
void onStopTranscode(HWND hwnd) {
    if (!guiIsTranscoding()) return;
    g_app.cancelRequested.store(true, std::memory_order_release);
    g_app.jobQueue.skipCurrentJob();
    SetDlgItemTextW(hwnd, IDC_LBL_STATUS, rtx::trW(rtx::StrId::StatusStopping));
}
using BrowseSpec = std::tuple<int, int, const wchar_t*, DWORD, bool, bool>;
constexpr BrowseSpec kBrowseSpecs[] = {
    {IDC_BTN_BROWSE_IN, IDC_EDT_INPUT,
     L"Video Files (*.mp4;*.mkv;*.mov;*.ts)\0*.mp4;*.mkv;*.mov;*.ts\0All Files (*.*)\0*.*\0",
     OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST, false, true},
    {IDC_BTN_BROWSE_OUT, IDC_EDT_OUTPUT,
     L"MP4 Video (*.mp4)\0*.mp4\0Matroska (*.mkv)\0*.mkv\0All Files (*.*)\0*.*\0",
     OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST, true, false},
    {IDC_BTN_BROWSE_LUT, IDC_EDT_LUT, L"3D LUT (*.cube)\0*.cube\0All Files (*.*)\0*.*\0",
     OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST, false, false},
    {IDC_BTN_BROWSE_RIFE, IDC_EDT_RIFE_ENGINE,
     L"TensorRT Engine (*.engine)\0*.engine\0All Files (*.*)\0*.*\0",
     OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST, false, false},
};

bool runFileDialog(HWND hwnd, const wchar_t* filter, DWORD flags, wchar_t* buffer, UINT capacity,
                   bool save = false) {
    OPENFILENAMEW ofn{
        .lStructSize = sizeof(OPENFILENAMEW),
        .hwndOwner = hwnd,
        .lpstrFilter = filter,
        .lpstrFile = buffer,
        .nMaxFile = capacity,
        .Flags = flags,
    };
    return save ? GetSaveFileNameW(&ofn) != FALSE : GetOpenFileNameW(&ofn) != FALSE;
}
void onBrowse(HWND hwnd, const BrowseSpec& spec) {
    const auto& [btn_id, ctrl, filter, flags, save, probe] = spec;
    (void)btn_id;
    wchar_t fileName[MAX_PATH] = {};
    if (!runFileDialog(hwnd, filter, flags, fileName, MAX_PATH, save)) {
        return;
    }
    SetDlgItemTextW(hwnd, ctrl, fileName);
    if (ctrl == IDC_EDT_INPUT) {
        SetDlgItemTextW(hwnd, IDC_EDT_OUTPUT, L"");
        autoFillOutputPath(hwnd);
    }
    if (probe) {
        applyProbedMedia(hwnd, std::filesystem::path(fileName));
    }
}
void onQueueAdd(HWND hwnd) {
    wchar_t files[16384] = {};
    if (!runFileDialog(hwnd, L"Video Files (*.mp4;*.mkv;*.mov;*.ts)\0*.mp4;*.mkv;*.mov;*.ts\0All Files (*.*)\0*.*\0",
                       OFN_ALLOWMULTISELECT | OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST,
                       files, 16384)) {
        return;
    }
    const std::wstring dir(files);
    std::vector<std::filesystem::path> inputs;
    wchar_t* cursor = files + dir.size() + 1;
    if (*cursor == L'\0') {
        inputs.push_back(std::filesystem::path(dir));
    } else {
        for (; *cursor != L'\0'; cursor += wcslen(cursor) + 1) {
            inputs.push_back(std::filesystem::path(dir) / cursor);
        }
    }
    if (inputs.empty()) {
        return;
    }
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
void onNleExport(HWND hwnd) {
    if (!g_app.project.clips.empty()) {
        rtx::gui::onNleExportSequence(hwnd);
    }
}
void onToggleLanguage(HWND hwnd) {
    rtx::toggleLanguage();
    refreshUiLanguage(hwnd);
}
void syncControlStatesHwnd(HWND hwnd) { syncControlStates(hwnd); }

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
void refreshQueueListbox(HWND hwnd) {
    SendDlgItemMessageW(hwnd, IDC_LST_QUEUE, LB_RESETCONTENT, 0, 0);
    for (const auto& job : g_app.jobQueue.snapshot()) {
        const wchar_t* tag = L"[Pending] ";
        if (job.status == rtx::JobStatus::Completed) tag = L"[Done] ";
        else if (job.status == rtx::JobStatus::Failed) tag = L"[Failed] ";
        else if (job.status == rtx::JobStatus::Cancelled) tag = L"[Cancelled] ";
        const std::wstring label = (job.status == rtx::JobStatus::Running)
            ? (L"[Running " + std::to_wstring(static_cast<int>(job.total_job_pct)) + L"%] ")
            : std::wstring(tag);
        std::wstring label2 = label + (job.inputs.empty() ? L"" : job.inputs.front().wstring()) +
                             L" [" + utf8ToWide(job.profile.codec) + L"/" +
                             utf8ToWide(job.profile.rate_control) + L"/QP" +
                             std::to_wstring(job.profile.qp) + L"]";
        SendDlgItemMessageW(hwnd, IDC_LST_QUEUE, LB_ADDSTRING, 0,
                            reinterpret_cast<LPARAM>(label2.c_str()));
    }
}
LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            g_app.hbrBg = rtx::UniqueBrush{CreateSolidBrush(rtx::gui::kClrPanel)};
            g_app.hbrCtrl = rtx::UniqueBrush{CreateSolidBrush(rtx::gui::kClrCtrl)};
            g_app.hFont = makeUiFont(false);
            g_app.hFontBold = makeUiFont(true);
            BOOL dark = TRUE;
            DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
            rtx::gui::createWorkstationControls(hwnd, g_app.hFont.get(), g_app.hFontBold.get());
            auto viewport = rtx::gui::PreviewViewport::create(hwnd, kPreviewWidth, kPreviewHeight);
            if (viewport.has_value()) {
                g_app.previewViewport.emplace(std::move(viewport.value()));
            }
            DragAcceptFiles(hwnd, TRUE);
            RegisterHotKey(hwnd, 1, MOD_CONTROL, 'L');
            installTranscodeBorrow();
            populateComboBoxes(hwnd);
            applyProfileToUi(hwnd, guiProfile());
            refreshUiLanguage(hwnd);
            syncControlStates(hwnd);
            applyWorkspaceLayout(hwnd);
            g_app.jobQueue.setEventCallback([hwnd](const rtx::JobQueueEvent& evt) {
                if (evt.kind == rtx::JobQueueEvent::Kind::JobProgress) {
                    int permille = std::clamp(static_cast<int>(evt.snapshot.total_job_pct * 10.0), 0, 1000);
                    PostMessageW(hwnd, WM_APP_PROGRESS, static_cast<WPARAM>(permille),
                                 static_cast<LPARAM>(static_cast<int>(evt.snapshot.current_fps * 10.0)));
                } else {
                    PostMessageW(hwnd, WM_APP_QUEUE_UPDATED, static_cast<WPARAM>(evt.kind), static_cast<LPARAM>(evt.job_id));
                }
            });
            SetTimer(hwnd, 2, 33, nullptr);  // 30 Hz 遥测 + 零回读直呈（66ms 假播已拔除）
            return 0;
        }
        case WM_SIZE:
            applyWorkspaceLayout(hwnd);
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
            std::vector<std::filesystem::path> paths;
            for (UINT i = 0, count = DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0); i < count; ++i) {
                wchar_t path[MAX_PATH] = {};
                if (DragQueryFileW(hDrop, i, path, MAX_PATH) > 0) {
                    paths.emplace_back(path);
                }
            }
            DragFinish(hDrop);
            guiProfile() = collectProfileFromUi(hwnd);
            std::vector<std::wstring> wide_paths;
            wide_paths.reserve(paths.size());
            for (const auto& p : paths) {
                wide_paths.emplace_back(p.wstring());
                rtx::TranscodeProfile profile = guiProfile();
                profile.input = wideToUtf8(p.wstring());
                profile.output = rtx::deriveOutputPath(profile.input, profile.codec, {}, true);
                (void)g_app.jobQueue.enqueueJob({p}, profile, p.parent_path());
            }
            refreshQueueListbox(hwnd);
            if (!wide_paths.empty()) {
                SetDlgItemTextW(hwnd, IDC_EDT_INPUT, wide_paths.front().c_str());
                SetDlgItemTextW(hwnd, IDC_EDT_OUTPUT, L"");
                autoFillOutputPath(hwnd);
                addFilesToBin(hwnd, wide_paths);
                applyProbedMedia(hwnd, paths.front());
                if (g_app.workspace != rtx::gui::WorkspaceMode::Encoder) {
                    startPlaybackSession(hwnd, paths.front());  // Player/NLE: 零回读监看
                }
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
        case rtx::gui::WM_APP_NLE_EXPORT_DONE: {
            std::unique_ptr<std::wstring> status(reinterpret_cast<std::wstring*>(lParam));
            setGuiTranscoding(false);
            syncControlStates(hwnd);
            if (status != nullptr) {
                SetDlgItemTextW(hwnd, IDC_LBL_STATUS, status->c_str());
            }
            return 0;
        }
        case WM_HSCROLL: {
            if (reinterpret_cast<HWND>(lParam) == GetDlgItem(hwnd, IDC_TRK_SCRUB)) {
                seekPlaybackFromSlider(hwnd);
            }
            return 0;
        }
        case WM_APP_PREVIEW: {
            presentPlaybackFrame(hwnd);
            return 0;
        }
        case WM_DRAWITEM:
            if (rtx::gui::paintCommandButton(reinterpret_cast<const DRAWITEMSTRUCT*>(lParam), g_app.hFont.get())) {
                return TRUE;
            }
            break;

        case WM_COMMAND: {
            int id = LOWORD(wParam);
            int code = HIWORD(wParam);
            if (code == BN_CLICKED) {
                for (const BrowseSpec& b : kBrowseSpecs) {
                    if (id == std::get<0>(b)) {
                        onBrowse(hwnd, b);
                        return 0;
                    }
                }
                switch (id) {
                    case IDC_BTN_START: onStartTranscode(hwnd); return 0;
                    case IDC_BTN_STOP: onStopTranscode(hwnd); return 0;
                    case IDC_BTN_QUEUE_ADD: onQueueAdd(hwnd); return 0;
                    case IDC_BTN_QUEUE_REMOVE: onQueueRemove(hwnd); return 0;
                    case IDC_BTN_QUEUE_CLEAR: onQueueClear(hwnd); return 0;
                    case IDC_BTN_MARK_IN: onNleMarkIn(hwnd); return 0;
                    case IDC_BTN_MARK_OUT: onNleMarkOut(hwnd); return 0;
                    case IDC_BTN_RAZOR: onNleRazor(hwnd); return 0;
                    case IDC_BTN_PLAYER_PLAY: togglePlayback(hwnd); return 0;
                    case IDC_BTN_NLE_EXPORT: onNleExport(hwnd); return 0;
                    case IDC_BTN_PROBE: runHardwareProbe(hwnd); return 0;
                    case IDC_BTN_LANG: onToggleLanguage(hwnd); return 0;
                    case IDC_CHK_VSR: syncControlStates(hwnd); return 0;
                    case IDC_TAB_WS_ENCODER: setWorkspace(hwnd, rtx::gui::WorkspaceMode::Encoder); return 0;
                    case IDC_TAB_WS_PLAYER: setWorkspace(hwnd, rtx::gui::WorkspaceMode::Player); return 0;
                    case IDC_TAB_WS_NLE: setWorkspace(hwnd, rtx::gui::WorkspaceMode::Nle); return 0;
                    case IDC_BTN_PRESET_MASTER: applyGoldenPresetById(hwnd, "master"); return 0;
                    case IDC_BTN_PRESET_RIFE: applyGoldenPresetById(hwnd, "rife"); return 0;
                    case IDC_BTN_PRESET_ESPORTS: applyGoldenPresetById(hwnd, "esports"); return 0;
                    case IDC_BTN_PRESET_COMPAT: applyGoldenPresetById(hwnd, "compat"); return 0;
                    case IDC_BTN_PRESET_NATIVE: applyGoldenPresetById(hwnd, "native"); return 0;
                    case IDC_BTN_STEP_BACK: stepPlayback(hwnd, -1); return 0;
                    case IDC_BTN_STEP_FWD: stepPlayback(hwnd, +1); return 0;
                    default: break;
                }
            } else if (code == CBN_SELCHANGE) {
                if (id == IDC_CMB_CODEC || id == IDC_CMB_RC || id == IDC_CMB_FPS) {
                    syncControlStates(hwnd);
                }
            } else if (code == LBN_SELCHANGE && id == IDC_LST_BIN) {
                const LRESULT sel = SendDlgItemMessageW(hwnd, IDC_LST_BIN, LB_GETCURSEL, 0, 0);
                if (sel >= 0 && sel < static_cast<LRESULT>(g_app.project.clips.size())) {
                    const auto& clip = g_app.project.clips[static_cast<size_t>(sel)];
                    g_app.project.selected_clip_id = clip.id;
                    if (const auto* asset = g_app.project.asset(clip.asset_id)) {
                        SetDlgItemTextW(hwnd, IDC_EDT_INPUT, utf8ToWide(asset->path).c_str());
                        SetDlgItemTextW(hwnd, IDC_EDT_OUTPUT, L"");
                        autoFillOutputPath(hwnd);
                    }
                    InvalidateRect(GetDlgItem(hwnd, IDC_NLE_TIMELINE), nullptr, FALSE);
                }
                return 0;
            }
            return 0;
        }
        case WM_HOTKEY: {
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
            const bool ctrlDown = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            const bool shiftDown = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            if (ctrlDown && wParam == 'Z') {
                if (shiftDown) {
                    rtx::gui::onNleRedo(hwnd);
                } else {
                    rtx::gui::onNleUndo(hwnd);
                }
                return 0;
            }
            if (ctrlDown && wParam == 'Y') {
                rtx::gui::onNleRedo(hwnd);
                return 0;
            }
            if (wParam == VK_DELETE && g_app.workspace == rtx::gui::WorkspaceMode::Nle) {
                rtx::gui::onNleDeleteSelected(hwnd);
                return 0;
            }
            if (wParam == VK_F11) {
                setBorderlessFullscreen(hwnd, !g_app.fullscreen);
                return 0;
            }
            if (wParam == VK_ESCAPE && g_app.fullscreen) {
                setBorderlessFullscreen(hwnd, false);
                return 0;
            }
            if (wParam == VK_SPACE) {
                togglePlayback(hwnd);
                return 0;
            }
            break;
        }
        case WM_APP_PROGRESS: {
            const int permille = static_cast<int>(wParam);
            const double fps = static_cast<double>(lParam) / 10.0;
            SendMessageW(GetDlgItem(hwnd, IDC_PROGRESS), PBM_SETPOS, permille, 0);
            const auto snapshots = g_app.jobQueue.snapshot();
            size_t batch_index = 0;
            for (size_t i = 0; i < snapshots.size(); ++i) {
                if (snapshots[i].status == rtx::JobStatus::Running) {
                    batch_index = i;
                    break;
                }
            }
            wchar_t buf[192];
            if (snapshots.size() > 1) {
                _snwprintf_s(buf, _countof(buf), _TRUNCATE,
                             L"Batch %zu/%zu  |  Progress: %.1f%%  |  Speed: %.1f FPS",
                             batch_index + 1, snapshots.size(),
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
            if (g_app.jobQueue.isWorkerRunning()) {
                return 0;
            }
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
                rtx::logError("transcode failed: " + lastFailed->error_message);
                MessageBoxW(hwnd, (std::wstring(rtx::trW(rtx::StrId::StatusFailed)) + L": " + detail).c_str(),
                            rtx::trW(rtx::StrId::AppTitle), MB_ICONERROR);
            } else if (anyCancelled) {
                SetDlgItemTextW(hwnd, IDC_LBL_STATUS, rtx::trW(rtx::StrId::StatusCancelled));
            } else {
                SendMessageW(GetDlgItem(hwnd, IDC_PROGRESS), PBM_SETPOS, 1000, 0);
                SetDlgItemTextW(hwnd, IDC_LBL_STATUS, rtx::trW(rtx::StrId::StatusDonePrefix));
            }
            return 0;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc{};
            GetClientRect(hwnd, &rc);
            rtx::gui::paintWorkspaceChrome(hdc, rc, g_app.workspace, g_app.hFont.get());
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_CTLCOLORSTATIC: {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            SetTextColor(hdc, rtx::gui::kClrText);
            SetBkColor(hdc, rtx::gui::kClrPanel);
            return reinterpret_cast<LRESULT>(g_app.hbrBg.get());
        }
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX:
        case WM_CTLCOLORBTN: {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            SetTextColor(hdc, rtx::gui::kClrText);
            SetBkColor(hdc, rtx::gui::kClrCtrl);
            return reinterpret_cast<LRESULT>(g_app.hbrCtrl.get());
        }
        case WM_TIMER: {
            if (wParam == 2) {
                presentPlaybackFrame(hwnd);
                presentTranscodePreview(hwnd);
                if (g_app.playbackSession) {
                    const rtx::TelemetrySnapshot snap = g_app.playbackSession->telemetry();
                    const int64_t playhead_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        rtx::Flicks(snap.playhead_flicks)).count();
                    HWND track = GetDlgItem(hwnd, IDC_TRK_SCRUB);
                    const LRESULT maxPos = SendMessageW(track, TBM_GETRANGEMAX, 0, 0);
                    if (maxPos > 0) {
                        SendMessageW(track, TBM_SETPOS, TRUE,
                                     static_cast<LPARAM>(std::clamp<int64_t>(playhead_ms, 0, maxPos)));
                    }
                    g_app.project.playhead_flicks = snap.playhead_flicks;
                    rtx::gui::updateNleTimecode(hwnd);
                    InvalidateRect(GetDlgItem(hwnd, IDC_NLE_TIMELINE), nullptr, FALSE);
                }
            }
            return 0;
        }
        case WM_DESTROY:
            UnregisterHotKey(hwnd, 1);
            KillTimer(hwnd, 2);
            if (g_app.exportThread.joinable()) {
                g_app.exportThread.request_stop();
                g_app.exportThread.join();
            }
            stopPlaybackSession();
            g_app.jobQueue.skipCurrentJob();
            g_app.transcodeBorrow = nullptr;
            g_app.previewViewport.reset();
            g_app.hbrBg.reset();
            g_app.hbrCtrl.reset();
            g_app.hFont.reset();
            g_app.hFontBold.reset();
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
    if (!rtx::gui::registerTimelineCanvasWindowClass(instance)) {
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

    if (!quickPlayFile.empty()) {
            SetDlgItemTextW(hwnd, IDC_EDT_INPUT, quickPlayFile.c_str());
        SetDlgItemTextW(hwnd, IDC_EDT_OUTPUT, L"");
        autoFillOutputPath(hwnd);
        setWorkspace(hwnd, rtx::gui::WorkspaceMode::Player);
        setBorderlessFullscreen(hwnd, true);
        applyProbedMedia(hwnd, std::filesystem::path(quickPlayFile));
        if (!startPlaybackSession(hwnd, std::filesystem::path(quickPlayFile))) {
            g_app.isPlaying = false;
            rtx::gui::setGuiPlaying(false);
            rtx::logError("quick-play launch failed: " + wideToUtf8(quickPlayFile));
        } else {
            InvalidateRect(GetDlgItem(hwnd, IDC_BTN_PLAYER_PLAY), nullptr, FALSE);
            rtx::logInfo("quick-play launch: " + wideToUtf8(quickPlayFile) + " playback started");
        }
    }
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}