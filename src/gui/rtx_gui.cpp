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
#include <format>
#include <windowsx.h>
#include <shobjidl.h>

#include "rtx/sys_dynload.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <iostream>
#include <cwchar>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

int rtxCliMain(int argc, char** argv);

namespace {
using namespace rtx::gui;

constexpr UINT WM_APP_PROGRESS      = WM_APP + 10;
constexpr UINT WM_APP_QUEUE_UPDATED = WM_APP + 13;

constexpr UINT kTbmGetRangeMax = WM_USER + 2;
constexpr UINT kTbmSetPos      = WM_USER + 5;
constexpr UINT kTbmSetRangeMax = WM_USER + 8;
constexpr UINT kPbmSetPos      = WM_USER + 2;
constexpr DWORD kIccInitFlags  = 0x00004000u | 0x00000020u | 0x00000010u | 0x00000004u;

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
    return rtx::UniqueFont{rtx::sys::createFontW(-14, 0, 0, 0, bold ? FW_BOLD : FW_NORMAL, FALSE, FALSE,
                                       FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                       CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                       DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI")};
}
void showGroup(HWND hwnd, const int* ids, size_t count, bool visible) {
    for (size_t i = 0; i < count; ++i) {
        rtx::sys::showWindow(rtx::sys::getDlgItem(hwnd, ids[i]), visible ? SW_SHOW : SW_HIDE);
    }
}
void applyWorkspaceLayout(HWND hwnd) {
    RECT rc{};
    rtx::sys::getClientRect(hwnd, &rc);
    const int width = rc.right - rc.left;
    const int height = rc.bottom - rc.top;
    const int dpi = rtx::sys::getDpiForWindow(hwnd);
    const auto u = [dpi](int v) { return MulDiv(v, dpi, 96); };
    const auto place = [hwnd](int id, int x, int y, int w, int h) {
        rtx::sys::moveWindow(rtx::sys::getDlgItem(hwnd, id), x, y, w, h, TRUE);
    };

    auto& app = rtx::gui::requireApp(hwnd);
    const bool fullscreen = app.fullscreen;
    const int topbarH = u(52);
    const int bodyTop = topbarH + 2;
    const int bodyH = height - bodyTop;
    const bool isEncoder = (app.workspace == rtx::gui::WorkspaceMode::Encoder);
    const bool isPlayer = (app.workspace == rtx::gui::WorkspaceMode::Player);
    const bool isNle = (app.workspace == rtx::gui::WorkspaceMode::Nle);
    const bool showInspector = (isEncoder || isNle) && !fullscreen;
    const bool showPlayer = isPlayer && !fullscreen;

    const std::tuple<const int*, size_t, bool> kGroups[] = {
        {kInspectorIds, std::size(kInspectorIds), showInspector},
        {kEncoderIds, std::size(kEncoderIds), isEncoder && !fullscreen},
        {kPlayerIds, std::size(kPlayerIds), showPlayer || (isPlayer && fullscreen)},
        {kNleIds, std::size(kNleIds), isNle && !fullscreen},
    };
    for (const auto& [ids, count, visible] : kGroups) showGroup(hwnd, ids, count, visible);
    rtx::sys::showWindow(rtx::sys::getDlgItem(hwnd, IDC_TRK_SCRUB), (showPlayer || isEncoder) ? SW_SHOW : SW_HIDE);
    for (int id : {IDC_TAB_WS_ENCODER, IDC_TAB_WS_PLAYER, IDC_TAB_WS_NLE, IDC_BTN_PROBE, IDC_BTN_LANG}) {
        rtx::sys::showWindow(rtx::sys::getDlgItem(hwnd, id), fullscreen ? SW_HIDE : SW_SHOW);
    }
    if (fullscreen) {
        if (app.previewViewport.has_value()) app.previewViewport->setBounds(0, 0, width, height);
        if (isPlayer) {
            place(IDC_TRK_SCRUB, u(8), height - u(40), width - u(16), u(26));
            place(IDC_LBL_VSR_OSD, u(8), u(8), width - u(16), u(22));
            place(IDC_BTN_STEP_BACK, u(8), height - u(72), u(64), u(26));
            place(IDC_BTN_STEP_FWD, u(80), height - u(72), u(64), u(26));
            place(IDC_BTN_PLAYER_PLAY, u(152), height - u(72), u(84), u(26));
        }
        return;
    }
    const int tabStartX = u(380), tabW = u(110);
    for (size_t i = 0; i < 3; ++i) place(kTabIds[i], tabStartX + static_cast<int>(i) * (tabW + u(6)), u(11), tabW, u(30));
    place(IDC_BTN_PROBE, width - u(220), u(14), u(100), u(24));
    place(IDC_BTN_LANG, width - u(112), u(14), u(100), u(24));

    if (isEncoder) {
        const int railW = u(224), railBtnW = railW - u(24);
        int y = bodyTop + u(10);
        for (int id : {IDC_BTN_PRESET_MASTER, IDC_BTN_PRESET_RIFE, IDC_BTN_PRESET_ESPORTS,
                       IDC_BTN_PRESET_COMPAT, IDC_BTN_PRESET_NATIVE}) {
            place(id, u(12), y, railBtnW, u(30));
            y += u(34);
        }
        y += u(10);
        const int queueBtnW = (railBtnW - u(12)) / 3;
        for (size_t i = 0; i < 3; ++i) place(kQueueBtnIds[i], u(12) + static_cast<int>(i) * (queueBtnW + u(6)), y, queueBtnW, u(26));
        y += u(30);
        place(IDC_LST_QUEUE, u(12), y, railBtnW, std::max(u(60), height - y - u(12)));
        const int centerW = width - railW - u(380), vpH = bodyH - u(120);
        if (app.previewViewport.has_value()) app.previewViewport->setBounds(railW, bodyTop, std::max(u(120), centerW), std::max(u(80), vpH));
        place(IDC_TRK_SCRUB, railW, bodyTop + vpH + u(8), std::max(u(120), centerW), u(26));
    } else if (isPlayer) {
        const int vpH = bodyH - u(110), btnY = bodyTop + vpH + u(40);
        if (app.previewViewport.has_value()) app.previewViewport->setBounds(0, bodyTop, width, std::max(u(80), vpH));
        place(IDC_TRK_SCRUB, u(8), bodyTop + vpH + u(8), width - u(16), u(26));
        place(IDC_BTN_STEP_BACK, u(8), btnY, u(72), u(28));
        place(IDC_BTN_STEP_FWD, u(88), btnY, u(72), u(28));
        place(IDC_BTN_PLAYER_PLAY, u(168), btnY, u(110), u(28));
        place(IDC_BTN_NLE_EXPORT, u(286), btnY, u(200), u(28));
        place(IDC_LBL_VSR_OSD, u(502), btnY + u(4), width - u(514), u(22));
    } else {
        const int binW = u(220), timelineH = u(230);
        const int monitorW = width - binW - u(380), monitorH = bodyH - timelineH - u(40);
        place(IDC_LST_BIN, u(8), bodyTop + u(8), binW - u(16), bodyH - timelineH - u(24));
        if (app.previewViewport.has_value()) app.previewViewport->setBounds(binW, bodyTop, std::max(u(120), monitorW), std::max(u(80), monitorH));
        const int toolY = bodyTop + monitorH + u(8), timecodeW = std::max(u(200), monitorW - u(300));
        place(IDC_LBL_TIMECODE, binW, toolY, timecodeW, u(22));
        place(IDC_BTN_MARK_IN, binW + timecodeW, toolY, u(90), u(24));
        place(IDC_BTN_MARK_OUT, binW + timecodeW + u(94), toolY, u(90), u(24));
        place(IDC_BTN_RAZOR, binW + timecodeW + u(188), toolY, u(110), u(24));
        place(IDC_NLE_TIMELINE, u(4), height - timelineH - u(4), width - u(8), timelineH);
    }
    if (showInspector) {
        const int panelW = u(380), panelX = width - panelW, lblW = u(120), rowH = u(26), gap = u(2);
        const int comboDropH = u(260);
        const auto isComboBox = [](int id) noexcept {
            return id == IDC_CMB_CODEC || id == IDC_CMB_PRESET || id == IDC_CMB_RC ||
                   id == IDC_CMB_RES || id == IDC_CMB_GEOM || id == IDC_CMB_FPS ||
                   id == IDC_CMB_VSR_QUAL;
        };
        int y = bodyTop + u(10);
        HDC measDc = rtx::sys::getDC(hwnd);
        HDWP dwp = rtx::sys::beginDeferWindowPos(static_cast<int>(std::size(kInspectorRows) * 3));
        const auto deferPlace = [&](int id, int x, int yp, int w, int h) {
            HWND ch = rtx::sys::getDlgItem(hwnd, id);
            if (!ch) return;
            if (dwp) {
                dwp = rtx::sys::deferWindowPos(dwp, ch, nullptr, x, yp, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
            } else {
                rtx::sys::moveWindow(ch, x, yp, w, h, TRUE);
            }
        };
        for (const auto& [labelId, ctrlId, extraId, extraW_raw] : kInspectorRows) {
            const int extraW = extraW_raw != 0 ? u(extraW_raw) : 0;
            const int ctrlH = isComboBox(ctrlId) ? comboDropH : rowH;
            wchar_t lblBuf[128]{};
            const int lblLen = rtx::sys::getWindowTextW(rtx::sys::getDlgItem(hwnd, labelId), lblBuf, 128);
            const int wrapH = (measDc && lblLen > 0)
                ? rtx::gui::measureWrappedTextHeight(measDc, rtx::gui::requireApp(hwnd).hFont.get(),
                                                     std::wstring_view(lblBuf, static_cast<size_t>(lblLen)), lblW)
                : 0;
            const int dynRowH = std::min(std::max(rowH, wrapH + u(4)), u(40));
            deferPlace(labelId, panelX + u(8), y + u(2), lblW, dynRowH);
            deferPlace(ctrlId, panelX + u(8) + lblW, y, panelW - lblW - u(24) - extraW, ctrlH);
            if (extraId >= 0) deferPlace(extraId, panelX + panelW - u(16) - extraW, y, extraW, rowH);
            y += dynRowH + gap;
        }
        if (dwp) rtx::sys::endDeferWindowPos(dwp);
        if (measDc) rtx::sys::releaseDC(hwnd, measDc);
        const int maxFooterTop = std::max(bodyTop + u(10), height - u(150));
        if (y > maxFooterTop) y = maxFooterTop;
        place(IDC_CHK_VSR, panelX + u(8), y + u(4), u(110), rowH);
        place(IDC_LBL_VSR_QUAL, panelX + u(8) + u(110), y + u(4), u(50), rowH);
        place(IDC_CMB_VSR_QUAL, panelX + u(8) + u(160), y, panelW - u(160) - u(24), comboDropH);
        y += rowH + gap;
        place(IDC_CHK_AUDIO, panelX + u(8), y + u(4), panelW / 2 - u(12), rowH);
        place(IDC_CHK_HDR10PLUS, panelX + panelW / 2 + u(4), y + u(4), panelW / 2 - u(12), rowH);
        y += rowH + gap + u(4);
        const int btnY = std::min(y, std::max(bodyTop + u(10), height - u(88)));
        place(IDC_BTN_START, panelX + u(8), btnY, (panelW - u(28)) / 2, u(32));
        place(IDC_BTN_STOP, panelX + u(12) + (panelW - u(28)) / 2, btnY, (panelW - u(28)) / 2, u(32));
        y = btnY + u(36);
        place(IDC_PROGRESS, panelX + u(8), y, panelW - u(16), u(14));
        y += u(16);
        place(IDC_LBL_STATUS, panelX + u(8), y, panelW - u(16), u(16));
        y += u(18);
        place(IDC_LBL_TELEMETRY, panelX + u(8), y, panelW - u(16), u(16));
    }
}
void updateTabStates(HWND hwnd) {
    for (int id : kTabIds) {
        rtx::sys::invalidateRect(rtx::sys::getDlgItem(hwnd, id), nullptr, FALSE);
    }
}
void commitViewportVsrScale(HWND hwnd);

void setWorkspace(HWND hwnd, rtx::gui::WorkspaceMode mode) {
    if (rtx::gui::requireApp(hwnd).workspace == mode && !rtx::gui::requireApp(hwnd).fullscreen) {
        return;
    }
    rtx::gui::requireApp(hwnd).workspace = mode;
    rtx::gui::setActiveWorkspace(rtx::gui::requireApp(hwnd), mode);
    applyWorkspaceLayout(hwnd);
    updateTabStates(hwnd);
    commitViewportVsrScale(hwnd);  // RW-3 timing (2)
    if (mode == rtx::gui::WorkspaceMode::Player && !rtx::gui::requireApp(hwnd).playbackSession) {
        wchar_t inPath[1024]{};
        rtx::sys::getDlgItemTextW(hwnd, IDC_EDT_INPUT, inPath, static_cast<int>(std::size(inPath)));
        if (inPath[0] != L'\0') {
            const std::filesystem::path input_path(inPath);
            if (std::filesystem::exists(input_path) && startPlaybackSession(hwnd, input_path)) {
                rtx::sys::invalidateRect(rtx::sys::getDlgItem(hwnd, IDC_BTN_PLAYER_PLAY), nullptr, FALSE);
            }
        }
    }
    static const char* names[] = {"encoder", "player", "nle"};
    rtx::logInfo(std::string("workspace -> ") + names[static_cast<int>(mode)]);
}
void setBorderlessFullscreen(HWND hwnd, bool fullscreen) {
    if (fullscreen == rtx::gui::requireApp(hwnd).fullscreen) {
        return;
    }
    rtx::gui::requireApp(hwnd).fullscreen = fullscreen;
    if (fullscreen) {
        rtx::gui::requireApp(hwnd).prevStyle = static_cast<DWORD>(rtx::sys::getWindowLongW(hwnd, GWL_STYLE));
        rtx::gui::requireApp(hwnd).prevPlacement.length = sizeof(WINDOWPLACEMENT);
        rtx::sys::getWindowPlacement(hwnd, &rtx::gui::requireApp(hwnd).prevPlacement);
        rtx::sys::setWindowLongW(hwnd, GWL_STYLE, static_cast<LONG>(WS_POPUP | WS_VISIBLE | WS_CLIPCHILDREN));
        const HMONITOR monitor = rtx::sys::monitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi{};
        mi.cbSize = sizeof(mi);
        rtx::sys::getMonitorInfoW(monitor, &mi);
        rtx::sys::setWindowPos(hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top,
                     mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top,
                     SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    } else {
        rtx::sys::setWindowLongW(hwnd, GWL_STYLE, static_cast<LONG>(rtx::gui::requireApp(hwnd).prevStyle));
        rtx::sys::setWindowPlacement(hwnd, &rtx::gui::requireApp(hwnd).prevPlacement);
        rtx::sys::setWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    }
    applyWorkspaceLayout(hwnd);
    commitViewportVsrScale(hwnd);  // RW-3 timing (1)
}
void commitViewportVsrScale(HWND hwnd) {
    if (!rtx::gui::requireApp(hwnd).previewViewport.has_value()) {
        return;
    }
    RECT vp{};
    if (!rtx::sys::getClientRect(rtx::gui::requireApp(hwnd).previewViewport->hwnd(), &vp) || vp.right <= 0 || vp.bottom <= 0) {
        return;
    }
    const rtx::gui::DynamicVsrTarget target =
        rtx::gui::computeViewportVsrTarget(rtx::gui::requireApp(hwnd).srcWidth, rtx::gui::requireApp(hwnd).srcHeight, vp.right, vp.bottom);
    rtx::gui::storeActiveVsrTarget(target);
    const auto [target_w, target_h, scale, vsr_active] = target;
    rtx::gui::requireApp(hwnd).previewViewport->resizeSwapChain(vp.right, vp.bottom);
    const std::wstring scaleStr = rtx::gui::formatVsrScale(static_cast<float>(scale));
    const std::wstring osd = vsr_active
        ? std::format(L"RTX VSR {} -> {} x {}", scaleStr, target_w, target_h)
        : std::format(L"RTX VSR bypass (scale {} <= 1.00x)", scaleStr);
    rtx::sys::setDlgItemTextW(hwnd, IDC_LBL_VSR_OSD, osd.c_str());
    rtx::logInfo("VSR: " + wideToUtf8(osd) + " (viewport " + std::to_string(vp.right) + "x" +
                 std::to_string(vp.bottom) + ")");
}
void applyProbedMedia(HWND hwnd, const std::filesystem::path& path) {
    auto& app = rtx::gui::requireApp(hwnd);
    if (!path.empty()) {
        if (auto demux = rtx::makeFfmpegDemuxer()) {
            if (auto probed = demux->open(path); probed.has_value()) {
                app.playbackInfo = probed.value();
            }
        }
    }
    const auto [media_w, media_h, media_dur, media_ok] = rtx::gui::probeMediaFile(path);
    if (media_ok) {
        app.srcWidth = media_w;
        app.srcHeight = media_h;
        app.mediaDurationFlicks = media_dur;
        const int64_t duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            rtx::Flicks(media_dur)).count();
        rtx::sys::sendMessageW(rtx::sys::getDlgItem(hwnd, IDC_TRK_SCRUB), kTbmSetRangeMax, TRUE,
                     static_cast<LPARAM>(duration_ms));
        rtx::logInfo("media probed: " + wideToUtf8(path.wstring()) + " " +
                     std::to_string(media_w) + "x" + std::to_string(media_h) + " " +
                     std::to_string(duration_ms) + " ms");
    }
    commitViewportVsrScale(hwnd);
}
void refreshQueueListbox(HWND hwnd);

// Doc 58 (Task 1): Sync current UI profile onto Pending jobs tracked in activeSyncIds only.
void syncActivePendingJobs(HWND hwnd) {
    auto& app = rtx::gui::requireApp(hwnd);
    if (guiIsTranscoding(app) || app.activeSyncIds.empty()) return;
    const rtx::TranscodeProfile uiProf = collectProfileFromUi(hwnd);
    guiProfile(app) = uiProf;
    bool updated = false;
    for (const auto& job : app.jobQueue.snapshot()) {
        const auto& [jid, jname, jin, jeo, jod, jprof, jst, jfi, jfp, jtp, jfps, jel, jem, jfr] = job;
        (void)jname;(void)jeo;(void)jod;(void)jprof;(void)jfi;(void)jfp;(void)jtp;(void)jfps;(void)jel;(void)jem;(void)jfr;
        if (jst == rtx::JobStatus::Pending && app.activeSyncIds.count(jid) > 0u) {
            rtx::TranscodeProfile synced = uiProf;
            if (!jin.empty()) {
                synced.input = wideToUtf8(jin.front().wstring());
            }
            synced.output = rtx::deriveOutputPath(synced.input, synced.codec, {}, true);
            if (app.jobQueue.updatePendingJob(jid, synced).has_value()) {
                updated = true;
            }
        }
    }
    if (updated) refreshQueueListbox(hwnd);
}

void onStartTranscode(HWND hwnd) {
    auto& app = rtx::gui::requireApp(hwnd);
    if (guiIsTranscoding(app)) return;

    wchar_t inPath[1024]{};
    wchar_t outPath[1024]{};
    rtx::sys::getDlgItemTextW(hwnd, IDC_EDT_INPUT, inPath, static_cast<int>(std::size(inPath)));
    rtx::sys::getDlgItemTextW(hwnd, IDC_EDT_OUTPUT, outPath, static_cast<int>(std::size(outPath)));
    if (inPath[0] != L'\0' && outPath[0] == L'\0') {
        autoFillOutputPath(hwnd);
        rtx::sys::getDlgItemTextW(hwnd, IDC_EDT_OUTPUT, outPath, static_cast<int>(std::size(outPath)));
    }
    guiProfile(app) = collectProfileFromUi(hwnd);

    // Doc 58 (Task 1): Sync activeSyncIds Pending snapshots and auto-enqueue unqueued IDC_EDT_INPUT path.
    syncActivePendingJobs(hwnd);
    bool hasPending = false;
    bool inputAlreadyQueued = false;
    const std::string uiInputUtf8 = (inPath[0] != L'\0') ? wideToUtf8(inPath) : std::string{};
    for (const auto& job : app.jobQueue.snapshot()) {
        const auto& [jid, jname, jin, jeo, jod, jprof, jst, jfi, jfp, jtp, jfps, jel, jem, jfr] = job;
        (void)jid;(void)jname;(void)jeo;(void)jod;(void)jprof;(void)jfi;(void)jfp;(void)jtp;(void)jfps;(void)jel;(void)jem;(void)jfr;
        if (jst == rtx::JobStatus::Pending) {
            hasPending = true;
            if (!uiInputUtf8.empty() && !jin.empty() && wideToUtf8(jin.front().wstring()) == uiInputUtf8) {
                inputAlreadyQueued = true;
            }
        }
    }
    if (!hasPending || (!uiInputUtf8.empty() && !inputAlreadyQueued && app.activeSyncIds.empty())) {
        if (inPath[0] == L'\0') {
            rtx::sys::messageBoxW(hwnd, rtx::trW(rtx::StrId::StatusDropFileOnly),
                        rtx::trW(rtx::StrId::AppTitle), MB_ICONWARNING);
            return;
        }
        const std::filesystem::path input_file(inPath);
        if (!std::filesystem::exists(input_file)) {
            const std::wstring msg = std::wstring(L"Input file not found: ") + inPath;
            rtx::logError("onStartTranscode: input file missing: " + wideToUtf8(inPath));
            rtx::sys::messageBoxW(hwnd, msg.c_str(), rtx::trW(rtx::StrId::AppTitle), MB_ICONERROR);
            return;
        }
        rtx::TranscodeProfile profile = guiProfile(app);
        profile.input = wideToUtf8(input_file.wstring());
        if (outPath[0] != L'\0') {
            profile.output = wideToUtf8(outPath);
        }
        if (profile.output.empty()) {
            profile.output = rtx::deriveOutputPath(profile.input, profile.codec, {}, true);
            rtx::sys::setDlgItemTextW(hwnd, IDC_EDT_OUTPUT, utf8ToWide(profile.output).c_str());
        }
        auto enq = app.jobQueue.enqueueJob({input_file}, profile, input_file.parent_path());
        if (!enq.has_value()) {
            rtx::logError("onStartTranscode: enqueueJob failed: " + enq.error().message);
            rtx::sys::messageBoxW(hwnd, utf8ToWide(enq.error().message).c_str(),
                        rtx::trW(rtx::StrId::AppTitle), MB_ICONERROR);
            return;
        }
        app.selectedJobId = enq.value();
        app.activeSyncIds = {enq.value()};
        hasPending = true;
    }
    // Doc 58 (Task 2.3): Pre-flight RIFE contract validation (zero silent fallback to 60-nvofa).
    std::wstring rifeBanner;
    for (const auto& job : app.jobQueue.snapshot()) {
        const auto& [jid, jname, jin, jeo, jod, jprof, jst, jfi, jfp, jtp, jfps, jel, jem, jfr] = job;
        (void)jid;(void)jname;(void)jin;(void)jeo;(void)jod;(void)jfi;(void)jfp;(void)jtp;(void)jfps;(void)jel;(void)jem;(void)jfr;
        if (jst == rtx::JobStatus::Pending && (jprof.fps_mode == "60-rife" || jprof.fps_mode == "120-rife")) {
            rtx::TranscodeProfile check = jprof;
            auto v = rtx::validateAndNormalizeProfile(check, false);
            const rtx::HwTranscodeOptions opts = rtx::profileToHwOptions(check);
            const int expectedFps = (jprof.fps_mode == "120-rife") ? 120 : 60;
            if (!v.has_value() || opts.target_fps.num != expectedFps || opts.rife_engine_path.empty()) {
                const std::wstring errMsg = L"RIFE configuration validation failed (aborting without NVOFA fallback).";
                rtx::logError("onStartTranscode: RIFE contract validation failed");
                rtx::sys::messageBoxW(hwnd, errMsg.c_str(), rtx::trW(rtx::StrId::AppTitle), MB_ICONERROR);
                return;
            }
            rifeBanner = L"Engine: RIFE | Target FPS: " + std::to_wstring(expectedFps) + L" | Status: Ready";
        }
    }
    refreshQueueListbox(hwnd);

    app.cancelRequested.store(false, std::memory_order_relaxed);
    setGuiTranscoding(app, true);
    syncControlStates(hwnd);
    rtx::sys::sendMessageW(rtx::sys::getDlgItem(hwnd, IDC_PROGRESS), kPbmSetPos, 0, 0);
    rtx::sys::setDlgItemTextW(hwnd, IDC_LBL_STATUS,
                    rifeBanner.empty() ? rtx::trW(rtx::StrId::StatusWorking) : rifeBanner.c_str());
    app.jobQueue.startWorker();
}
void onStopTranscode(HWND hwnd) {
    if (!guiIsTranscoding(rtx::gui::requireApp(hwnd))) return;
    rtx::gui::requireApp(hwnd).cancelRequested.store(true, std::memory_order_release);
    rtx::gui::requireApp(hwnd).jobQueue.skipCurrentJob();
    rtx::sys::setDlgItemTextW(hwnd, IDC_LBL_STATUS, rtx::trW(rtx::StrId::StatusStopping));
}
using BrowseSpec = std::tuple<int, int, const wchar_t*, bool, bool>;
constexpr BrowseSpec kBrowseSpecs[] = {
    {IDC_BTN_BROWSE_IN, IDC_EDT_INPUT,
     L"All Supported Media (*.mp4;*.mkv;*.mov;*.webm;*.avi;*.ts;*.m2ts;*.flv;*.mxf;*.wmv;*.asf;*.mpg;*.mpeg;*.vob;*.rmvb;*.ivf)\0*.mp4;*.mkv;*.mov;*.webm;*.avi;*.ts;*.m2ts;*.flv;*.mxf;*.wmv;*.asf;*.mpg;*.mpeg;*.vob;*.rmvb;*.ivf\0All Files (*.*)\0*.*\0",
     false, true},
    {IDC_BTN_BROWSE_OUT, IDC_EDT_OUTPUT,
     L"MP4 Video (*.mp4)\0*.mp4\0Matroska (*.mkv)\0*.mkv\0All Files (*.*)\0*.*\0",
     true, false},
    {IDC_BTN_BROWSE_LUT, IDC_EDT_LUT, L"3D LUT (*.cube)\0*.cube\0All Files (*.*)\0*.*\0",
     false, false},
    {IDC_BTN_BROWSE_RIFE, IDC_EDT_RIFE_ENGINE,
     L"TensorRT Engine (*.engine)\0*.engine\0All Files (*.*)\0*.*\0",
     false, false},
};

std::vector<std::filesystem::path> runComFileDialog(HWND hwnd, const wchar_t* filter,
                                                    bool save, bool multi) {
    std::vector<std::filesystem::path> results;
    std::unique_ptr<void, rtx::FnDeleter<&rtx::sys::coUninitializeGuard>> com;
    if (SUCCEEDED(rtx::sys::coInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE))) {
        com.reset(reinterpret_cast<void*>(1));
    }
    std::vector<COMDLG_FILTERSPEC> specs;
    if (filter != nullptr) {
        const wchar_t* cur = filter;
        while (*cur != L'\0') {
            const wchar_t* label = cur;
            cur += std::wcslen(cur) + 1;
            if (*cur == L'\0') break;
            const wchar_t* pattern = cur;
            cur += std::wcslen(cur) + 1;
            specs.push_back(COMDLG_FILTERSPEC{label, pattern});
        }
    }
    auto collectItem = [&](IShellItem* item) {
        if (item == nullptr) return;
        PWSTR raw_path = nullptr;
        if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &raw_path)) && raw_path != nullptr) {
            results.emplace_back(raw_path);
            rtx::sys::coTaskMemFree(raw_path);
        }
    };
    if (save) {
        IFileSaveDialog* raw = nullptr;
        if (FAILED(rtx::sys::coCreateInstance(__uuidof(FileSaveDialog), nullptr, CLSCTX_INPROC_SERVER,
                                              __uuidof(IFileSaveDialog), reinterpret_cast<void**>(&raw)))) {
            return results;
        }
        rtx::ComPtr<IFileSaveDialog> dlg(raw);
        DWORD opts = 0;
        if (SUCCEEDED(dlg->GetOptions(&opts))) {
            (void)dlg->SetOptions(opts | FOS_OVERWRITEPROMPT | FOS_PATHMUSTEXIST | FOS_FORCEFILESYSTEM);
        }
        if (!specs.empty()) (void)dlg->SetFileTypes(static_cast<UINT>(specs.size()), specs.data());
        if (SUCCEEDED(dlg->Show(hwnd))) {
            IShellItem* raw_item = nullptr;
            if (SUCCEEDED(dlg->GetResult(&raw_item)) && raw_item != nullptr) {
                rtx::ComPtr<IShellItem> item(raw_item);
                collectItem(item.get());
            }
        }
        return results;
    }
    IFileOpenDialog* raw = nullptr;
    if (FAILED(rtx::sys::coCreateInstance(__uuidof(FileOpenDialog), nullptr, CLSCTX_INPROC_SERVER,
                                          __uuidof(IFileOpenDialog), reinterpret_cast<void**>(&raw)))) {
        return results;
    }
    rtx::ComPtr<IFileOpenDialog> dlg(raw);
    DWORD opts = 0;
    if (SUCCEEDED(dlg->GetOptions(&opts))) {
        DWORD extra = FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST | FOS_FORCEFILESYSTEM;
        if (multi) extra |= FOS_ALLOWMULTISELECT;
        (void)dlg->SetOptions(opts | extra);
    }
    if (!specs.empty()) (void)dlg->SetFileTypes(static_cast<UINT>(specs.size()), specs.data());
    if (SUCCEEDED(dlg->Show(hwnd))) {
        if (multi) {
            IShellItemArray* raw_arr = nullptr;
            if (SUCCEEDED(dlg->GetResults(&raw_arr)) && raw_arr != nullptr) {
                rtx::ComPtr<IShellItemArray> arr(raw_arr);
                DWORD count = 0;
                if (SUCCEEDED(arr->GetCount(&count))) {
                    for (DWORD i = 0; i < count; ++i) {
                        IShellItem* raw_item = nullptr;
                        if (SUCCEEDED(arr->GetItemAt(i, &raw_item)) && raw_item != nullptr) {
                            rtx::ComPtr<IShellItem> item(raw_item);
                            collectItem(item.get());
                        }
                    }
                }
            }
        } else {
            IShellItem* raw_item = nullptr;
            if (SUCCEEDED(dlg->GetResult(&raw_item)) && raw_item != nullptr) {
                rtx::ComPtr<IShellItem> item(raw_item);
                collectItem(item.get());
            }
        }
    }
    return results;
}
void onBrowse(HWND hwnd, const BrowseSpec& spec) {
    const auto& [btn_id, ctrl, filter, save, probe] = spec;
    (void)btn_id;
    const auto chosen = runComFileDialog(hwnd, filter, save, false);
    if (chosen.empty()) {
        return;
    }
    const std::wstring fileName = chosen.front().wstring();
    rtx::sys::setDlgItemTextW(hwnd, ctrl, fileName.c_str());
    if (ctrl == IDC_EDT_INPUT) {
        rtx::sys::setDlgItemTextW(hwnd, IDC_EDT_OUTPUT, L"");
        autoFillOutputPath(hwnd);
    }
    if (probe) {
        applyProbedMedia(hwnd, chosen.front());
        if (rtx::gui::requireApp(hwnd).workspace != rtx::gui::WorkspaceMode::Encoder) {
            if (startPlaybackSession(hwnd, chosen.front())) {
                rtx::sys::invalidateRect(rtx::sys::getDlgItem(hwnd, IDC_BTN_PLAYER_PLAY), nullptr, FALSE);
            }
        }
    }
}
void onQueueAdd(HWND hwnd) {
    const auto inputs = runComFileDialog(
        hwnd, L"All Supported Media (*.mp4;*.mkv;*.mov;*.webm;*.avi;*.ts;*.m2ts;*.flv;*.mxf;*.wmv;*.asf;*.mpg;*.mpeg;*.vob;*.rmvb;*.ivf)\0*.mp4;*.mkv;*.mov;*.webm;*.avi;*.ts;*.m2ts;*.flv;*.mxf;*.wmv;*.asf;*.mpg;*.mpeg;*.vob;*.rmvb;*.ivf\0All Files (*.*)\0*.*\0",
        false, true);
    if (inputs.empty()) {
        return;
    }
    guiProfile(rtx::gui::requireApp(hwnd)) = collectProfileFromUi(hwnd);
    rtx::TranscodeProfile profile = guiProfile(rtx::gui::requireApp(hwnd));
    profile.input = wideToUtf8(inputs.front().wstring());
    if (profile.output.empty()) {
        profile.output = rtx::deriveOutputPath(profile.input, profile.codec, {}, true);
    }
    auto& app = rtx::gui::requireApp(hwnd);
    if (auto enq = app.jobQueue.enqueueJob(inputs, profile, inputs.front().parent_path()); enq.has_value()) {
        app.selectedJobId = enq.value();
        app.activeSyncIds = {enq.value()};
    }
    refreshQueueListbox(hwnd);
    applyProbedMedia(hwnd, inputs.front());
}
void onNleExport(HWND hwnd) {
    if (!rtx::gui::requireApp(hwnd).project.clips.empty()) {
        rtx::gui::onNleExportSequence(hwnd);
    }
}
void onToggleLanguage(HWND hwnd) {
    rtx::toggleLanguage();
    refreshUiLanguage(hwnd);
}
void syncControlStatesHwnd(HWND hwnd) { syncControlStates(hwnd); }

void onQueueRemove(HWND hwnd) {
    const LRESULT sel = rtx::sys::sendDlgItemMessageW(hwnd, IDC_LST_QUEUE, LB_GETCURSEL, 0, 0);
    if (sel < 0) return;
    const auto snapshots = rtx::gui::requireApp(hwnd).jobQueue.snapshot();
    if (static_cast<size_t>(sel) < snapshots.size()) {
        (void)rtx::gui::requireApp(hwnd).jobQueue.removeJob(rtx::encoderJobId(snapshots[static_cast<size_t>(sel)]));
        refreshQueueListbox(hwnd);
    }
}
void onQueueClear(HWND hwnd) {
    rtx::gui::requireApp(hwnd).jobQueue.clearCompleted();
    refreshQueueListbox(hwnd);
}
void refreshQueueListbox(HWND hwnd) {
    auto& app = rtx::gui::requireApp(hwnd);
    rtx::sys::sendDlgItemMessageW(hwnd, IDC_LST_QUEUE, LB_RESETCONTENT, 0, 0);
    int selectIdx = -1;
    int rowIdx = 0;
    for (const auto& job : app.jobQueue.snapshot()) {
        const auto& [jid, jname, jinputs, jeo, jod, jprof, jst, jfi, jfp, jtp, jfps, jel, jem, jfr] = job;
        (void)jname;(void)jeo;(void)jod;(void)jfi;(void)jfp;(void)jfps;(void)jel;(void)jem;(void)jfr;
        const wchar_t* tag = L"[Pending] ";
        if (jst == rtx::JobStatus::Completed) tag = L"[Done] ";
        else if (jst == rtx::JobStatus::Failed) tag = L"[Failed] ";
        else if (jst == rtx::JobStatus::Cancelled) tag = L"[Cancelled] ";
        const std::wstring label = (jst == rtx::JobStatus::Running)
            ? (L"[Running " + std::to_wstring(static_cast<int>(jtp)) + L"%] ")
            : std::wstring(tag);
        std::wstring label2 = label + (jinputs.empty() ? L"" : jinputs.front().wstring()) +
                             L" [" + utf8ToWide(jprof.codec) + L"/" +
                             utf8ToWide(jprof.rate_control) + L"/QP" +
                             std::to_wstring(jprof.qp) + L"/" +
                             utf8ToWide(jprof.fps_mode) + L"]";
        rtx::sys::sendDlgItemMessageW(hwnd, IDC_LST_QUEUE, LB_ADDSTRING, 0,
                            reinterpret_cast<LPARAM>(label2.c_str()));
        if (jid == app.selectedJobId) selectIdx = rowIdx;
        ++rowIdx;
    }
    if (selectIdx >= 0) {
        rtx::sys::sendDlgItemMessageW(hwnd, IDC_LST_QUEUE, LB_SETCURSEL, static_cast<WPARAM>(selectIdx), 0);
    }
}
LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_NCCREATE) {
        const auto* cs = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        rtx::sys::setWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return TRUE;
    }
    if (msg == WM_NCDESTROY) {
        rtx::sys::setWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        return 0;
    }
    rtx::gui::WorkstationApp* app_ptr = rtx::gui::appFromHwnd(hwnd);
    if (!app_ptr) {
        return rtx::sys::defWindowProcW(hwnd, msg, wParam, lParam);
    }
    switch (msg) {
        case WM_CREATE: {
            rtx::gui::requireApp(hwnd).hbrBg = rtx::makeSolidBrush(rtx::gui::kClrPanel);
            rtx::gui::requireApp(hwnd).hbrCtrl = rtx::makeSolidBrush(rtx::gui::kClrCtrl);
            rtx::gui::requireApp(hwnd).hFont = makeUiFont(false);
            rtx::gui::requireApp(hwnd).hFontBold = makeUiFont(true);
            BOOL dark = TRUE;
            constexpr DWORD kDwmwaUseImmersiveDarkMode = 20;
            (void)rtx::sys::dwmSetWindowAttribute(hwnd, kDwmwaUseImmersiveDarkMode, &dark, sizeof(dark));
            rtx::gui::createWorkstationControls(hwnd, rtx::gui::requireApp(hwnd).hFont.get(), rtx::gui::requireApp(hwnd).hFontBold.get());
            auto viewport = rtx::gui::PreviewViewport::create(hwnd, kPreviewWidth, kPreviewHeight);
            if (viewport.has_value()) {
                rtx::gui::requireApp(hwnd).previewViewport.emplace(std::move(viewport.value()));
            }
            rtx::sys::dragAcceptFiles(hwnd, TRUE);
            rtx::sys::registerHotKey(hwnd, 1, MOD_CONTROL, 'L');
            installTranscodeBorrow(rtx::gui::requireApp(hwnd));
            populateComboBoxes(hwnd);
            applyProfileToUi(hwnd, guiProfile(rtx::gui::requireApp(hwnd)));
            refreshUiLanguage(hwnd);
            rtx::sys::setWindowLongPtrW(hwnd, GWL_STYLE, rtx::sys::getWindowLongPtrW(hwnd, GWL_STYLE) | WS_CLIPCHILDREN);
            syncControlStates(hwnd);
            applyWorkspaceLayout(hwnd);
            commitViewportVsrScale(hwnd);
            rtx::gui::requireApp(hwnd).jobQueue.setEventCallback([hwnd](const rtx::JobQueueEvent& evt) {
                const auto& [kind, job_id, snapshot] = evt;
                if (kind == rtx::JobQueueEventKind::JobProgress) {
                    const auto& [sjid, sjn, sji, sjeo, sjod, sjp, sjst, sjfi, sjfp, sjtp, sjfps, sjel, sjem, sjfr] = snapshot;
                    (void)sjid;(void)sjn;(void)sji;(void)sjeo;(void)sjod;(void)sjp;(void)sjst;(void)sjfi;(void)sjfp;(void)sjel;(void)sjem;(void)sjfr;
                    int permille = std::clamp(static_cast<int>(sjtp * 10.0), 0, 1000);
                    rtx::sys::postMessageW(hwnd, WM_APP_PROGRESS, static_cast<WPARAM>(permille),
                                 static_cast<LPARAM>(static_cast<int>(sjfps * 10.0)));
                } else {
                    rtx::sys::postMessageW(hwnd, WM_APP_QUEUE_UPDATED, static_cast<WPARAM>(kind), static_cast<LPARAM>(job_id));
                }
            });
            rtx::sys::setTimer(hwnd, 2, 33, nullptr);  // 30 Hz 遥测 + 零回读直呈（66ms 假播已拔除）
            return 0;
        }
        case WM_SIZE:
            if (wParam == SIZE_MINIMIZED) return 0;
            applyWorkspaceLayout(hwnd);
            if (!rtx::gui::requireApp(hwnd).inResizeDrag && (wParam == SIZE_MAXIMIZED || wParam == SIZE_RESTORED)) {
                commitViewportVsrScale(hwnd);
            }
            return 0;

        case WM_ENTERSIZEMOVE:
            rtx::gui::requireApp(hwnd).inResizeDrag = true;
            return 0;

        case WM_EXITSIZEMOVE:
            rtx::gui::requireApp(hwnd).inResizeDrag = false;
            commitViewportVsrScale(hwnd);  // dynamic VSR recompute on mouse release only
            return 0;

        case WM_GETMINMAXINFO: {
            auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
            const UINT dpi = rtx::sys::getDpiForWindow(hwnd);
            mmi->ptMinTrackSize = {MulDiv(1120, static_cast<int>(dpi), 96),
                                   MulDiv(760, static_cast<int>(dpi), 96)};
            return 0;
        }
        case WM_DROPFILES: {
            HANDLE hDrop = reinterpret_cast<HANDLE>(wParam);
            std::vector<std::filesystem::path> paths;
            for (UINT i = 0, count = rtx::sys::dragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0); i < count; ++i) {
                wchar_t path[MAX_PATH] = {};
                if (rtx::sys::dragQueryFileW(hDrop, i, path, MAX_PATH) > 0) {
                    paths.emplace_back(path);
                }
            }
            rtx::sys::dragFinish(hDrop);
            // Doc 58 (Task 1): Immediate drop-to-queue in Encoder mode with activeSyncIds targeted sync;
            // non-Encoder workspaces route directly to playback without polluting Encoder queue.
            auto& app = rtx::gui::requireApp(hwnd);
            guiProfile(app) = collectProfileFromUi(hwnd);
            std::vector<std::wstring> wide_paths;
            wide_paths.reserve(paths.size());
            for (const auto& p : paths) {
                wide_paths.emplace_back(p.wstring());
            }
            if (app.workspace == rtx::gui::WorkspaceMode::Encoder) {
                app.activeSyncIds.clear();
                uint64_t firstNewId = 0;
                for (const auto& p : paths) {
                    rtx::TranscodeProfile profile = guiProfile(app);
                    profile.input = wideToUtf8(p.wstring());
                    profile.output = rtx::deriveOutputPath(profile.input, profile.codec, {}, true);
                    if (auto enq = app.jobQueue.enqueueJob({p}, profile, p.parent_path()); enq.has_value()) {
                        if (firstNewId == 0) firstNewId = enq.value();
                        app.activeSyncIds.insert(enq.value());
                    }
                }
                if (firstNewId != 0) app.selectedJobId = firstNewId;
                refreshQueueListbox(hwnd);
            }
            if (!wide_paths.empty()) {
                rtx::sys::setDlgItemTextW(hwnd, IDC_EDT_INPUT, wide_paths.front().c_str());
                rtx::sys::setDlgItemTextW(hwnd, IDC_EDT_OUTPUT, L"");
                autoFillOutputPath(hwnd);
                addFilesToBin(hwnd, wide_paths);
                applyProbedMedia(hwnd, paths.front());
                if (rtx::gui::requireApp(hwnd).workspace != rtx::gui::WorkspaceMode::Encoder) {
                    startPlaybackSession(hwnd, paths.front());  // Player/NLE: 零回读监看
                }
            }
            return 0;
        }
        case WM_APP_SPLIT_DRAG: {
            const UINT mouseMsg = static_cast<UINT>(wParam);
            if (mouseMsg == WM_LBUTTONDBLCLK) {
                setBorderlessFullscreen(hwnd, !rtx::gui::requireApp(hwnd).fullscreen);
                return 0;
            }
            const int x = GET_X_LPARAM(lParam);
            RECT rc;
            if (rtx::gui::requireApp(hwnd).previewViewport.has_value() && rtx::sys::getClientRect(rtx::gui::requireApp(hwnd).previewViewport->hwnd(), &rc) &&
                rc.right > 0) {
                const float ratio = std::clamp(static_cast<float>(x) / static_cast<float>(rc.right), 0.0f, 1.0f);
                if (mouseMsg != WM_LBUTTONUP) {
                    rtx::gui::requireApp(hwnd).splitRatio.store(ratio, std::memory_order_relaxed);
                }
                rtx::gui::requireApp(hwnd).previewViewport->setSplitRatio(ratio);
            }
            return 0;
        }
        case rtx::gui::WM_APP_TIMELINE_SEEK: {
            updateNleTimecode(hwnd);
            rtx::sys::invalidateRect(rtx::sys::getDlgItem(hwnd, IDC_NLE_TIMELINE), nullptr, FALSE);
            return 0;
        }
        case rtx::gui::WM_APP_NLE_EXPORT_DONE: {
            std::unique_ptr<std::wstring> status(reinterpret_cast<std::wstring*>(lParam));
            setGuiTranscoding(rtx::gui::requireApp(hwnd), false);
            syncControlStates(hwnd);
            if (status != nullptr) {
                rtx::sys::setDlgItemTextW(hwnd, IDC_LBL_STATUS, status->c_str());
            }
            return 0;
        }
        case WM_HSCROLL: {
            if (reinterpret_cast<HWND>(lParam) == rtx::sys::getDlgItem(hwnd, IDC_TRK_SCRUB)) {
                seekPlaybackFromSlider(hwnd);
            }
            return 0;
        }
        case WM_APP_PREVIEW: {
            presentPlaybackFrame(hwnd);
            return 0;
        }
        case WM_DRAWITEM:
            if (rtx::gui::paintCommandButton(reinterpret_cast<const DRAWITEMSTRUCT*>(lParam), rtx::gui::requireApp(hwnd).hFont.get())) {
                return TRUE;
            }
            break;

        case WM_COMMAND: {
            int id = LOWORD(wParam);
            int code = HIWORD(wParam);
            if (code == BN_CLICKED) {
                for (const BrowseSpec& b : kBrowseSpecs) {
                    const auto& [btn_id, ctrl, filter, save, probe] = b;
                    (void)ctrl;
                    (void)filter;
                    (void)save;
                    (void)probe;
                    if (id == btn_id) {
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
                syncActivePendingJobs(hwnd);
            } else if (code == LBN_SELCHANGE && id == IDC_LST_QUEUE) {
                auto& app = rtx::gui::requireApp(hwnd);
                const LRESULT sel = rtx::sys::sendDlgItemMessageW(hwnd, IDC_LST_QUEUE, LB_GETCURSEL, 0, 0);
                const auto snaps = app.jobQueue.snapshot();
                if (sel >= 0 && static_cast<size_t>(sel) < snaps.size()) {
                    const auto& [jid, jname, jin, jeo, jod, jprof, jst, jfi, jfp, jtp, jfps, jel, jem, jfr] = snaps[static_cast<size_t>(sel)];
                    (void)jname;(void)jin;(void)jeo;(void)jod;(void)jfi;(void)jfp;(void)jtp;(void)jfps;(void)jel;(void)jem;(void)jfr;
                    app.selectedJobId = jid;
                    if (jst == rtx::JobStatus::Pending) {
                        app.activeSyncIds = {jid};
                    } else {
                        app.activeSyncIds.clear();
                    }
                    guiProfile(app) = jprof;
                    applyProfileToUi(hwnd, jprof);
                    syncControlStates(hwnd);
                }
                return 0;
            } else if (code == LBN_SELCHANGE && id == IDC_LST_BIN) {
                const LRESULT sel = rtx::sys::sendDlgItemMessageW(hwnd, IDC_LST_BIN, LB_GETCURSEL, 0, 0);
                if (sel >= 0 && sel < static_cast<LRESULT>(rtx::gui::requireApp(hwnd).project.clips.size())) {
                    const auto& [cid, caid, cnm, cln, cst, cin, cout, cg] =
                        rtx::gui::requireApp(hwnd).project.clips[static_cast<size_t>(sel)];
                    (void)cnm;(void)cln;(void)cst;(void)cin;(void)cout;(void)cg;
                    rtx::gui::requireApp(hwnd).project.selected_clip_id = cid;
                    if (const auto* asset = rtx::gui::requireApp(hwnd).project.asset(caid)) {
                        const auto& [aid, au, an, apath, adur, aw, ah, aoff] = *asset;
                        (void)aid;(void)au;(void)an;(void)adur;(void)aw;(void)ah;(void)aoff;
                        rtx::sys::setDlgItemTextW(hwnd, IDC_EDT_INPUT, utf8ToWide(apath).c_str());
                        rtx::sys::setDlgItemTextW(hwnd, IDC_EDT_OUTPUT, L"");
                        autoFillOutputPath(hwnd);
                    }
                    rtx::sys::invalidateRect(rtx::sys::getDlgItem(hwnd, IDC_NLE_TIMELINE), nullptr, FALSE);
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
            if (wParam == VK_RETURN && altDown) { setBorderlessFullscreen(hwnd, !rtx::gui::requireApp(hwnd).fullscreen); return 0; }
            if (wParam == VK_F11) { setBorderlessFullscreen(hwnd, !rtx::gui::requireApp(hwnd).fullscreen); return 0; }
            break;
        }
        case WM_KEYDOWN: {
            const bool ctrlDown = (rtx::sys::getKeyState(VK_CONTROL) & 0x8000) != 0;
            const bool shiftDown = (rtx::sys::getKeyState(VK_SHIFT) & 0x8000) != 0;
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
            if (wParam == VK_DELETE && rtx::gui::requireApp(hwnd).workspace == rtx::gui::WorkspaceMode::Nle) {
                rtx::gui::onNleDeleteSelected(hwnd);
                return 0;
            }
            if (wParam == VK_F11) {
                setBorderlessFullscreen(hwnd, !rtx::gui::requireApp(hwnd).fullscreen);
                return 0;
            }
            if (wParam == VK_ESCAPE && rtx::gui::requireApp(hwnd).fullscreen) {
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
            rtx::sys::sendMessageW(rtx::sys::getDlgItem(hwnd, IDC_PROGRESS), kPbmSetPos, permille, 0);
            const auto snapshots = rtx::gui::requireApp(hwnd).jobQueue.snapshot();
            size_t batch_index = 0;
            for (size_t i = 0; i < snapshots.size(); ++i) {
                if (rtx::encoderJobStatus(snapshots[i]) == rtx::JobStatus::Running) {
                    batch_index = i;
                    break;
                }
            }
            const std::wstring status_buf = (snapshots.size() > 1)
                ? std::format(L"Batch {}/{}  |  Progress: {:.1f}%  |  Speed: {:.1f} FPS",
                              batch_index + 1, snapshots.size(), static_cast<double>(permille) / 10.0, fps)
                : std::format(L"Progress: {:.1f}%  |  Speed: {:.1f} FPS",
                              static_cast<double>(permille) / 10.0, fps);
            rtx::sys::setDlgItemTextW(hwnd, IDC_LBL_TELEMETRY, status_buf.c_str());
            return 0;
        }
        case WM_APP_QUEUE_UPDATED: {
            refreshQueueListbox(hwnd);
            if (rtx::gui::requireApp(hwnd).jobQueue.isWorkerRunning()) {
                return 0;
            }
            setGuiTranscoding(rtx::gui::requireApp(hwnd), false);
            syncControlStates(hwnd);
            const auto snapshots = rtx::gui::requireApp(hwnd).jobQueue.snapshot();
            std::string lastFailedError;
            bool hasFailed = false;
            bool anyCancelled = false;
            for (const auto& j : snapshots) {
                const auto& [jid, jname, jin, jeo, jod, jprof, jst, jfi, jfp, jtp, jfps, jel, jem, jfr] = j;
                (void)jid;(void)jname;(void)jin;(void)jeo;(void)jod;(void)jprof;(void)jfi;(void)jfp;(void)jtp;(void)jfps;(void)jel;(void)jfr;
                if (jst == rtx::JobStatus::Failed) { hasFailed = true; lastFailedError = jem; }
                else if (jst == rtx::JobStatus::Cancelled) anyCancelled = true;
            }
            if (hasFailed) {
                rtx::sys::setDlgItemTextW(hwnd, IDC_LBL_STATUS, rtx::trW(rtx::StrId::StatusFailed));
                std::wstring detail = utf8ToWide(lastFailedError.empty() ? "unknown error" : lastFailedError);
                rtx::logError("transcode failed: " + lastFailedError);
                rtx::sys::messageBoxW(hwnd, (std::wstring(rtx::trW(rtx::StrId::StatusFailed)) + L": " + detail).c_str(),
                            rtx::trW(rtx::StrId::AppTitle), MB_ICONERROR);
            } else if (anyCancelled) {
                rtx::sys::setDlgItemTextW(hwnd, IDC_LBL_STATUS, rtx::trW(rtx::StrId::StatusCancelled));
            } else {
                rtx::sys::sendMessageW(rtx::sys::getDlgItem(hwnd, IDC_PROGRESS), kPbmSetPos, 1000, 0);
                rtx::sys::setDlgItemTextW(hwnd, IDC_LBL_STATUS, rtx::trW(rtx::StrId::StatusDonePrefix));
            }
            return 0;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC hdc = rtx::sys::beginPaint(hwnd, &ps);
            RECT rc{};
            rtx::sys::getClientRect(hwnd, &rc);
            rtx::gui::paintWorkspaceChrome(hdc, rc, rtx::gui::requireApp(hwnd).workspace, rtx::gui::requireApp(hwnd).hFont.get());
            rtx::sys::endPaint(hwnd, &ps);
            return 0;
        }
        case WM_CTLCOLORSTATIC: {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            rtx::sys::setTextColor(hdc, rtx::gui::kClrText);
            rtx::sys::setBkColor(hdc, rtx::gui::kClrPanel);
            return reinterpret_cast<LRESULT>(rtx::gui::requireApp(hwnd).hbrBg.get());
        }
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX:
        case WM_CTLCOLORBTN: {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            rtx::sys::setTextColor(hdc, rtx::gui::kClrText);
            rtx::sys::setBkColor(hdc, rtx::gui::kClrCtrl);
            return reinterpret_cast<LRESULT>(rtx::gui::requireApp(hwnd).hbrCtrl.get());
        }
        case WM_TIMER: {
            if (wParam == 2) {
                presentPlaybackFrame(hwnd);
                presentTranscodePreview(hwnd);
                if (rtx::gui::requireApp(hwnd).playbackSession) {
                    const rtx::TelemetrySnapshot snap = rtx::gui::requireApp(hwnd).playbackSession->telemetry();
                    const int64_t playhead_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        rtx::Flicks(rtx::telemetryI64(snap, rtx::TelemetrySlot::PlayheadFlicks))).count();
                    HWND track = rtx::sys::getDlgItem(hwnd, IDC_TRK_SCRUB);
                    const LRESULT maxPos = rtx::sys::sendMessageW(track, kTbmGetRangeMax, 0, 0);
                    if (maxPos > 0) {
                        rtx::sys::sendMessageW(track, kTbmSetPos, TRUE,
                                     static_cast<LPARAM>(std::clamp<int64_t>(playhead_ms, 0, maxPos)));
                    }
                    rtx::gui::requireApp(hwnd).project.playhead_flicks = rtx::telemetryI64(snap, rtx::TelemetrySlot::PlayheadFlicks);
                    rtx::gui::updateNleTimecode(hwnd);
                    rtx::sys::invalidateRect(rtx::sys::getDlgItem(hwnd, IDC_NLE_TIMELINE), nullptr, FALSE);
                }
            }
            return 0;
        }
        case WM_SETCURSOR:
            if (LOWORD(lParam) == HTERROR) {
                return TRUE;  // Wave 0-A: suppress DefWindowProc MessageBeep
            }
            break;
        case WM_SYSCHAR:
            return 0;  // Wave 0-A: absorb unmatched Alt+key chords
        case WM_DESTROY:
            rtx::sys::unregisterHotKey(hwnd, 1);
            rtx::sys::killTimer(hwnd, 2);
            if (rtx::gui::requireApp(hwnd).exportThread.joinable()) {
                rtx::gui::requireApp(hwnd).exportThread.request_stop();
                rtx::gui::requireApp(hwnd).exportThread.join();
            }
            stopPlaybackSession(rtx::gui::requireApp(hwnd));
            rtx::gui::requireApp(hwnd).jobQueue.skipCurrentJob();
            rtx::gui::requireApp(hwnd).transcodeBorrow = nullptr;
            rtx::gui::requireApp(hwnd).previewViewport.reset();
            rtx::gui::requireApp(hwnd).hbrBg.reset();
            rtx::gui::requireApp(hwnd).hbrCtrl.reset();
            rtx::gui::requireApp(hwnd).hFont.reset();
            rtx::gui::requireApp(hwnd).hFontBold.reset();
            rtx::sys::postQuitMessage(0);
            return 0;
    }
    return rtx::sys::defWindowProcW(hwnd, msg, wParam, lParam);
}
}  // namespace

namespace {
// Phase 13 (Wave 0-A / 77_ Step 2.2): Stateless /SUBSYSTEM:WINDOWS parent console binder.
// Preserves cmd.exe / pipe redirected FILE_TYPE_DISK / FILE_TYPE_PIPE handles via SetStdHandle
// and binds CONOUT$ only for unredirected console streams (zero AllocConsole flicker).
void bindParentConsoleIfNeeded() noexcept {
    HANDLE hStdOut = GetStdHandle(STD_OUTPUT_HANDLE);
    HANDLE hStdErr = GetStdHandle(STD_ERROR_HANDLE);
    const DWORD outType = (hStdOut != nullptr && hStdOut != INVALID_HANDLE_VALUE)
                              ? GetFileType(hStdOut)
                              : FILE_TYPE_UNKNOWN;
    const DWORD errType = (hStdErr != nullptr && hStdErr != INVALID_HANDLE_VALUE)
                              ? GetFileType(hStdErr)
                              : FILE_TYPE_UNKNOWN;
    const bool isOutRedirected = (outType == FILE_TYPE_DISK || outType == FILE_TYPE_PIPE);
    const bool isErrRedirected = (errType == FILE_TYPE_DISK || errType == FILE_TYPE_PIPE);

    if ((!isOutRedirected || !isErrRedirected) && AttachConsole(ATTACH_PARENT_PROCESS) != FALSE) {
        if (!isOutRedirected) {
            FILE* fpOut = nullptr;
            if (freopen_s(&fpOut, "CONOUT$", "w", stdout) == 0 && fpOut != nullptr) {
                (void)setvbuf(stdout, nullptr, _IONBF, 0);
                HANDLE hConOut = CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE,
                                             FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                             OPEN_EXISTING, 0, nullptr);
                if (hConOut != INVALID_HANDLE_VALUE) {
                    SetStdHandle(STD_OUTPUT_HANDLE, hConOut);
                }
            }
        } else {
            SetStdHandle(STD_OUTPUT_HANDLE, hStdOut);
        }
        if (!isErrRedirected) {
            FILE* fpErr = nullptr;
            if (freopen_s(&fpErr, "CONOUT$", "w", stderr) == 0 && fpErr != nullptr) {
                (void)setvbuf(stderr, nullptr, _IONBF, 0);
                HANDLE hConErr = CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE,
                                             FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                             OPEN_EXISTING, 0, nullptr);
                if (hConErr != INVALID_HANDLE_VALUE) {
                    SetStdHandle(STD_ERROR_HANDLE, hConErr);
                }
            }
        } else {
            SetStdHandle(STD_ERROR_HANDLE, hStdErr);
        }
        SetConsoleOutputCP(CP_UTF8);
        std::ios::sync_with_stdio(true);
        std::cout.clear();
        std::cerr.clear();
    }
}
}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR /*commandLine*/, int showCommand) {
    rtx::initRuntimeEnvironment();

    const std::vector<std::wstring> argv = rtx::sys::parseCommandLineW(GetCommandLineW());
    const std::span<const std::wstring> args(argv);
    std::wstring quickPlayFile;
    if (args.size() == 2 && !args[1].empty() && args[1][0] != L'-' &&
        std::filesystem::exists(args[1])) {
        quickPlayFile = args[1];
    } else if (args.size() > 1) {
        bindParentConsoleIfNeeded();
        std::vector<std::string> cliArgs;
        cliArgs.reserve(args.size());
        for (const auto& a : args) {
            cliArgs.push_back(wideToUtf8(a));
        }
        std::vector<char*> cliPtrs;
        cliPtrs.reserve(cliArgs.size() + 1);
        for (auto& arg_str : cliArgs) {
            cliPtrs.push_back(arg_str.data());
        }
        cliPtrs.push_back(nullptr);
        return rtxCliMain(static_cast<int>(cliArgs.size()), cliPtrs.data());
    }

    // Wave D-3 (80_ Task D-3): Load user32.dll / gdi32.dll ONLY after entering desktop GUI branch.
    if (!rtx::sys::ensureUser32Loaded()) {
        return 1;
    }
    (void)rtx::sys::ensureGdi32Loaded();
    const DWORD icce[2] = {sizeof(DWORD) * 2u, kIccInitFlags};
    (void)rtx::sys::initCommonControlsEx(icce);
    if (!rtx::gui::registerTimelineCanvasWindowClass(instance)) {
        return 1;
    }
    WNDCLASSEXW wc{
        .cbSize = sizeof(WNDCLASSEXW),
        .style = CS_HREDRAW | CS_VREDRAW,
        .lpfnWndProc = windowProc,
        .hInstance = instance,
        .hIcon = rtx::sys::loadIconW(nullptr, IDI_APPLICATION),
        .hCursor = rtx::sys::loadCursorW(nullptr, IDC_ARROW),
        .hbrBackground = nullptr,
        .lpszClassName = L"CineRtxNativeGui",
        .hIconSm = rtx::sys::loadIconW(nullptr, IDI_APPLICATION)
    };
    if (rtx::sys::registerClassExW(&wc) == 0) {
        return 1;
    }
    const UINT dpi = rtx::sys::getDpiForSystem();
    RECT wr{0, 0, MulDiv(1220, dpi, 96), MulDiv(860, dpi, 96)};
    rtx::sys::adjustWindowRectExForDpi(&wr, WS_OVERLAPPEDWINDOW, FALSE, 0, dpi);

    auto app_owner = std::make_unique<rtx::gui::WorkstationApp>();
    HWND hwnd = rtx::sys::createWindowExW(
        WS_EX_ACCEPTFILES,
        wc.lpszClassName,
        rtx::trW(rtx::StrId::AppTitle),
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT,
        wr.right - wr.left, wr.bottom - wr.top,
        nullptr, nullptr, instance, app_owner.get()
    );
    if (hwnd == nullptr) {
        return 1;
    }
    rtx::sys::showWindow(hwnd, showCommand == SW_HIDE ? SW_SHOWNORMAL : showCommand);
    rtx::sys::updateWindow(hwnd);

    if (!quickPlayFile.empty()) {
            rtx::sys::setDlgItemTextW(hwnd, IDC_EDT_INPUT, quickPlayFile.c_str());
        rtx::sys::setDlgItemTextW(hwnd, IDC_EDT_OUTPUT, L"");
        autoFillOutputPath(hwnd);
        setWorkspace(hwnd, rtx::gui::WorkspaceMode::Player);
        setBorderlessFullscreen(hwnd, true);
        applyProbedMedia(hwnd, std::filesystem::path(quickPlayFile));
        if (!startPlaybackSession(hwnd, std::filesystem::path(quickPlayFile))) {
            app_owner->isPlaying = false;
            rtx::gui::setGuiPlaying(*app_owner, false);
            rtx::logError("quick-play launch failed: " + wideToUtf8(quickPlayFile));
        } else {
            rtx::sys::invalidateRect(rtx::sys::getDlgItem(hwnd, IDC_BTN_PLAYER_PLAY), nullptr, FALSE);
            rtx::logInfo("quick-play launch: " + wideToUtf8(quickPlayFile) + " playback started");
        }
    }
    MSG msg{};
    while (rtx::sys::getMessageW(&msg, nullptr, 0, 0) > 0) {
        rtx::sys::translateMessage(&msg);
        rtx::sys::dispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}