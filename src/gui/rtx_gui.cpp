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

#include "rtx/hw_transcode.h"
#include "rtx/i18n.h"
#include "rtx/runtime_env.h"
#include "rtx/types.h"

#include "preview_viewport.h"

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
    IDC_CHK_TRUEHDR,
    IDC_LBL_PEAK_NITS,
    IDC_EDT_PEAK_NITS,
    IDC_CHK_AUDIO,
    IDC_BTN_START,
    IDC_BTN_STOP,
    IDC_PROGRESS,
    IDC_LBL_STATUS,
    IDC_LBL_TELEMETRY,
    // Phase 7 realtime monitoring + colour science + batch queue.
    IDC_EDT_LUT_PATH,
    IDC_BTN_BROWSE_LUT,
    IDC_CHK_HDR10PLUS,
    IDC_TRK_SCRUB,
    IDC_LST_QUEUE,
};

constexpr UINT WM_APP_PROGRESS    = WM_APP + 10;
constexpr UINT WM_APP_DONE        = WM_APP + 11;
constexpr UINT WM_APP_PREVIEW     = WM_APP + 12;
constexpr UINT WM_APP_TOGGLE_LANG = WM_APP + 20;
// WM_APP_SPLIT_DRAG comes from src/gui/preview_viewport.h.

constexpr int kPreviewWidth = 960;
constexpr int kPreviewHeight = 540;

std::atomic<bool> g_cancelRequested{false};
std::atomic<float> g_splitRatio{0.5f};
std::thread g_workerThread;
std::thread g_scrubThread;
bool g_isTranscoding = false;

// Phase 7 (Task 7-C2): multi-file batch queue (drag N files, transcode in
// order with per-file progress).
std::vector<std::wstring> g_batchQueue;
size_t g_batchIndex = 0;
std::mutex g_batchMutex;

// Phase 7 realtime monitoring staging: the transcode thread copies the latest
// A/B preview frame here; the GUI thread presents it through the viewport.
std::mutex g_previewMutex;
std::vector<uint8_t> g_previewHost;
uint32_t g_previewPitch = 0;
bool g_previewDirty = false;
bool g_previewEnabled = true;
std::wstring g_lastErrorMessage;

HBRUSH g_hbrBg = nullptr;
HBRUSH g_hbrCtrl = nullptr;
HFONT  g_hFont = nullptr;
HFONT  g_hFontBold = nullptr;

std::optional<rtx::gui::PreviewViewport> g_previewViewport;

void guiLog(const wchar_t* fmt, ...) {
    wchar_t buf[1024];
    va_list args;
    va_start(args, fmt);
    _vsnwprintf_s(buf, _countof(buf), _TRUNCATE, fmt, args);
    va_end(args);
    OutputDebugStringW(buf);
}

std::wstring utf8ToWide(const std::string& str) {
    if (str.empty()) return {};
    int size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.size()), nullptr, 0);
    std::wstring out(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.size()), out.data(), size);
    return out;
}

std::string wideToUtf8(const std::wstring& wstr) {
    if (wstr.empty()) return {};
    int size = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), static_cast<int>(wstr.size()), nullptr, 0, nullptr, nullptr);
    std::string out(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), static_cast<int>(wstr.size()), out.data(), size, nullptr, nullptr);
    return out;
}

void autoFillOutputPath(HWND hwnd) {
    wchar_t inPath[MAX_PATH] = {};
    GetDlgItemTextW(hwnd, IDC_EDT_INPUT, inPath, MAX_PATH);
    if (inPath[0] == L'\0') return;

    wchar_t outPath[MAX_PATH] = {};
    GetDlgItemTextW(hwnd, IDC_EDT_OUTPUT, outPath, MAX_PATH);
    if (outPath[0] != L'\0') return;

    int codecIdx = ComboBox_GetCurSel(GetDlgItem(hwnd, IDC_CMB_CODEC));
    const wchar_t* suffix = (codecIdx == 1) ? L"_remaster_hevc.mp4"
                          : (codecIdx == 2) ? L"_remaster_h264.mp4"
                                            : L"_remaster_av1.mp4";

    std::filesystem::path in(inPath);
    std::filesystem::path out = in.parent_path() / (in.stem().wstring() + suffix);
    SetDlgItemTextW(hwnd, IDC_EDT_OUTPUT, out.c_str());
}

void populateComboBoxes(HWND hwnd) {
    const bool isZh = (rtx::currentLanguage() == rtx::Language::Zh);

    // Codec
    HWND hCodec = GetDlgItem(hwnd, IDC_CMB_CODEC);
    int selCodec = ComboBox_GetCurSel(hCodec);
    if (selCodec < 0) selCodec = 0;
    ComboBox_ResetContent(hCodec);
    ComboBox_AddString(hCodec, isZh ? L"AV1 10-bit (默认推荐)" : L"AV1 10-bit (Default)");
    ComboBox_AddString(hCodec, L"HEVC Main10 (H.265)");
    ComboBox_AddString(hCodec, L"H.264 High (8-bit SDR)");
    ComboBox_SetCurSel(hCodec, selCodec);

    // Preset
    HWND hPreset = GetDlgItem(hwnd, IDC_CMB_PRESET);
    int selPreset = ComboBox_GetCurSel(hPreset);
    if (selPreset < 0) selPreset = 4; // P5
    ComboBox_ResetContent(hPreset);
    ComboBox_AddString(hPreset, L"P1 (Fastest / 最快)");
    ComboBox_AddString(hPreset, L"P2 (Faster)");
    ComboBox_AddString(hPreset, L"P3 (Fast)");
    ComboBox_AddString(hPreset, L"P4 (Medium)");
    ComboBox_AddString(hPreset, isZh ? L"P5 (平衡默认 / Balanced)" : L"P5 (Balanced / Default)");
    ComboBox_AddString(hPreset, L"P6 (Better Quality)");
    ComboBox_AddString(hPreset, isZh ? L"P7 (最高画质 / High Quality)" : L"P7 (Highest Quality)");
    ComboBox_SetCurSel(hPreset, selPreset);

    // Rate Control
    HWND hRc = GetDlgItem(hwnd, IDC_CMB_RC);
    int selRc = ComboBox_GetCurSel(hRc);
    if (selRc < 0) selRc = 0;
    ComboBox_ResetContent(hRc);
    ComboBox_AddString(hRc, isZh ? L"CBR (恒定码率)" : L"CBR (Constant Bitrate)");
    ComboBox_AddString(hRc, isZh ? L"VBR (动态码率)" : L"VBR (Variable Bitrate)");
    ComboBox_AddString(hRc, isZh ? L"Constant QP (固定量化)" : L"Constant QP");
    ComboBox_SetCurSel(hRc, selRc);

    // Resolution
    HWND hRes = GetDlgItem(hwnd, IDC_CMB_RES);
    int selRes = ComboBox_GetCurSel(hRes);
    if (selRes < 0) selRes = 0;
    ComboBox_ResetContent(hRes);
    ComboBox_AddString(hRes, isZh ? L"4K UHD (3840x2160 / 2160x3840 自适应)" : L"4K UHD (3840x2160 / 2160x3840 Auto)");
    ComboBox_AddString(hRes, L"2K QHD (2560x1440)");
    ComboBox_AddString(hRes, L"FHD (1920x1080)");
    ComboBox_AddString(hRes, isZh ? L"保持原画分辨率 (Native)" : L"Native (Source Resolution)");
    ComboBox_SetCurSel(hRes, selRes);

    // Orientation / Geometry
    HWND hGeom = GetDlgItem(hwnd, IDC_CMB_GEOM);
    int selGeom = ComboBox_GetCurSel(hGeom);
    if (selGeom < 0) selGeom = 0;
    ComboBox_ResetContent(hGeom);
    ComboBox_AddString(hGeom, isZh ? L"自动竖屏检测旋转 (9:16 / 16:9 智能适配)" : L"Auto Direct (9:16 Portrait / 16:9 Landscape)");
    ComboBox_AddString(hGeom, isZh ? L"横屏填充黑边居中 (Pillarbox 3840x2160)" : L"Pillarbox (3840x2160 Center Pad)");
    ComboBox_SetCurSel(hGeom, selGeom);

    // VSR Quality
    HWND hVsr = GetDlgItem(hwnd, IDC_CMB_VSR_QUAL);
    int selVsr = ComboBox_GetCurSel(hVsr);
    if (selVsr < 0) selVsr = 1; // Level 2
    ComboBox_ResetContent(hVsr);
    ComboBox_AddString(hVsr, L"Level 1 (Basic)");
    ComboBox_AddString(hVsr, isZh ? L"Level 2 (推荐默认)" : L"Level 2 (Default)");
    ComboBox_AddString(hVsr, L"Level 3 (High)");
    ComboBox_AddString(hVsr, L"Level 4 (Ultra Quality)");
    ComboBox_SetCurSel(hVsr, selVsr);
}

void updateUiLanguage(HWND hwnd) {
    SetWindowTextW(hwnd, rtx::trW(rtx::StrId::AppTitle));
    SetDlgItemTextW(hwnd, IDC_LBL_INPUT, rtx::trW(rtx::StrId::ParamInput));
    SetDlgItemTextW(hwnd, IDC_BTN_BROWSE_IN, rtx::trW(rtx::StrId::BtnBrowse));
    SetDlgItemTextW(hwnd, IDC_LBL_OUTPUT, rtx::trW(rtx::StrId::ParamOutput));
    SetDlgItemTextW(hwnd, IDC_BTN_BROWSE_OUT, rtx::trW(rtx::StrId::BtnBrowse));
    SetDlgItemTextW(hwnd, IDC_LBL_CODEC, rtx::trW(rtx::StrId::ParamCodec));
    SetDlgItemTextW(hwnd, IDC_LBL_PRESET, rtx::trW(rtx::StrId::ParamPreset));
    SetDlgItemTextW(hwnd, IDC_LBL_RC, rtx::trW(rtx::StrId::ParamRateControl));
    SetDlgItemTextW(hwnd, IDC_LBL_BITRATE, rtx::trW(rtx::StrId::ParamBitrate));
    SetDlgItemTextW(hwnd, IDC_LBL_QP, rtx::trW(rtx::StrId::ParamCq));
    SetDlgItemTextW(hwnd, IDC_LBL_RES, rtx::trW(rtx::StrId::ParamResolution));
    SetDlgItemTextW(hwnd, IDC_LBL_GEOM, rtx::trW(rtx::StrId::ParamAspect));
    SetDlgItemTextW(hwnd, IDC_CHK_VSR, rtx::trW(rtx::StrId::ParamVsr));
    SetDlgItemTextW(hwnd, IDC_LBL_VSR_QUAL, rtx::trW(rtx::StrId::ParamVsrQuality));
    SetDlgItemTextW(hwnd, IDC_CHK_TRUEHDR, rtx::trW(rtx::StrId::ParamTrueHdr));
    SetDlgItemTextW(hwnd, IDC_LBL_PEAK_NITS, rtx::trW(rtx::StrId::ParamPeakNits));
    SetDlgItemTextW(hwnd, IDC_CHK_AUDIO, rtx::trW(rtx::StrId::ParamAudioCopy));
    SetDlgItemTextW(hwnd, IDC_BTN_START, rtx::trW(rtx::StrId::ButtonStart));
    SetDlgItemTextW(hwnd, IDC_BTN_STOP, rtx::trW(rtx::StrId::ButtonStop));
    if (!g_isTranscoding) {
        SetDlgItemTextW(hwnd, IDC_LBL_STATUS, rtx::trW(rtx::StrId::StatusReady));
    }
    populateComboBoxes(hwnd);
}

void syncControlStates(HWND hwnd) {
    const int codecIdx = ComboBox_GetCurSel(GetDlgItem(hwnd, IDC_CMB_CODEC));
    const bool isH264 = (codecIdx == 2);
    if (isH264) {
        Button_SetCheck(GetDlgItem(hwnd, IDC_CHK_TRUEHDR), BST_UNCHECKED);
        EnableWindow(GetDlgItem(hwnd, IDC_CHK_TRUEHDR), FALSE);
        EnableWindow(GetDlgItem(hwnd, IDC_EDT_PEAK_NITS), FALSE);
        EnableWindow(GetDlgItem(hwnd, IDC_CHK_HDR10PLUS), FALSE);
        EnableWindow(GetDlgItem(hwnd, IDC_EDT_LUT_PATH), FALSE);
    } else {
        EnableWindow(GetDlgItem(hwnd, IDC_CHK_TRUEHDR), !g_isTranscoding);
        const bool hdrChecked = (Button_GetCheck(GetDlgItem(hwnd, IDC_CHK_TRUEHDR)) == BST_CHECKED);
        EnableWindow(GetDlgItem(hwnd, IDC_EDT_PEAK_NITS), hdrChecked && !g_isTranscoding);
        EnableWindow(GetDlgItem(hwnd, IDC_CHK_HDR10PLUS), hdrChecked && !g_isTranscoding);
        EnableWindow(GetDlgItem(hwnd, IDC_EDT_LUT_PATH), !g_isTranscoding);
    }

    const bool vsrChecked = (Button_GetCheck(GetDlgItem(hwnd, IDC_CHK_VSR)) == BST_CHECKED);
    EnableWindow(GetDlgItem(hwnd, IDC_CMB_VSR_QUAL), vsrChecked && !g_isTranscoding);
}

// Maps the client rectangle onto the Phase 7 layout: preview region on top,
// scrub slider, parameter grid, action row and the batch queue listbox.
void layoutControls(HWND hwnd) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    const int width = rc.right - rc.left;
    const int pad = 8;
    const int rowH = 24;
    const int gapY = 4;
    const int btnW = 80;
    const int lblW = 110;

    int y = pad;

    // Row 1: Input Path
    MoveWindow(GetDlgItem(hwnd, IDC_LBL_INPUT), pad, y + 4, lblW, rowH, TRUE);
    MoveWindow(GetDlgItem(hwnd, IDC_EDT_INPUT), pad + lblW, y, width - pad * 2 - lblW - btnW - 8, rowH, TRUE);
    MoveWindow(GetDlgItem(hwnd, IDC_BTN_BROWSE_IN), width - pad - btnW, y, btnW, rowH, TRUE);
    y += rowH + gapY;

    // Row 2: Output Path
    MoveWindow(GetDlgItem(hwnd, IDC_LBL_OUTPUT), pad, y + 4, lblW, rowH, TRUE);
    MoveWindow(GetDlgItem(hwnd, IDC_EDT_OUTPUT), pad + lblW, y, width - pad * 2 - lblW - btnW - 8, rowH, TRUE);
    MoveWindow(GetDlgItem(hwnd, IDC_BTN_BROWSE_OUT), width - pad - btnW, y, btnW, rowH, TRUE);
    y += rowH + gapY + 4;

    // Phase 7: realtime A/B preview region + timeline scrub slider.
    const int previewH = 240;
    if (g_previewViewport.has_value()) {
        g_previewViewport->setBounds(pad, y, width - pad * 2, previewH);
    }
    y += previewH + gapY;
    MoveWindow(GetDlgItem(hwnd, IDC_TRK_SCRUB), pad, y, width - pad * 2, 26, TRUE);
    y += 26 + gapY;

    // 2-Column Parameter Grid
    const int colW = (width - pad * 3) / 2;
    const int col2X = pad * 2 + colW;
    const int gridH = rowH + 4;

    // Grid Row 1: Codec (Left) vs Preset (Right)
    MoveWindow(GetDlgItem(hwnd, IDC_LBL_CODEC), pad, y + 4, lblW, rowH, TRUE);
    MoveWindow(GetDlgItem(hwnd, IDC_CMB_CODEC), pad + lblW, y, colW - lblW, rowH, TRUE);
    MoveWindow(GetDlgItem(hwnd, IDC_LBL_PRESET), col2X, y + 4, lblW, rowH, TRUE);
    MoveWindow(GetDlgItem(hwnd, IDC_CMB_PRESET), col2X + lblW, y, colW - lblW, rowH, TRUE);
    y += gridH;

    // Grid Row 2: Rate Control (Left) vs QP (Right)
    MoveWindow(GetDlgItem(hwnd, IDC_LBL_RC), pad, y + 4, lblW, rowH, TRUE);
    MoveWindow(GetDlgItem(hwnd, IDC_CMB_RC), pad + lblW, y, colW - lblW, rowH, TRUE);
    MoveWindow(GetDlgItem(hwnd, IDC_LBL_QP), col2X, y + 4, lblW, rowH, TRUE);
    MoveWindow(GetDlgItem(hwnd, IDC_EDT_QP), col2X + lblW, y, colW - lblW, rowH, TRUE);
    y += gridH;

    // Grid Row 3: Bitrate (Left) vs Resolution (Right)
    MoveWindow(GetDlgItem(hwnd, IDC_LBL_BITRATE), pad, y + 4, lblW, rowH, TRUE);
    MoveWindow(GetDlgItem(hwnd, IDC_EDT_BITRATE), pad + lblW, y, colW - lblW, rowH, TRUE);
    MoveWindow(GetDlgItem(hwnd, IDC_LBL_RES), col2X, y + 4, lblW, rowH, TRUE);
    MoveWindow(GetDlgItem(hwnd, IDC_CMB_RES), col2X + lblW, y, colW - lblW, rowH, TRUE);
    y += gridH;

    // Grid Row 4: Orientation / Aspect (Left) vs Audio Copy (Right)
    MoveWindow(GetDlgItem(hwnd, IDC_LBL_GEOM), pad, y + 4, lblW, rowH, TRUE);
    MoveWindow(GetDlgItem(hwnd, IDC_CMB_GEOM), pad + lblW, y, colW - lblW, rowH, TRUE);
    MoveWindow(GetDlgItem(hwnd, IDC_CHK_AUDIO), col2X, y + 4, colW, rowH, TRUE);
    y += gridH;

    // Grid Row 5: VSR Check & Quality (Left) vs TrueHDR & Peak Nits (Right)
    MoveWindow(GetDlgItem(hwnd, IDC_CHK_VSR), pad, y + 4, lblW + 20, rowH, TRUE);
    MoveWindow(GetDlgItem(hwnd, IDC_CMB_VSR_QUAL), pad + lblW + 25, y, colW - lblW - 25, rowH, TRUE);
    MoveWindow(GetDlgItem(hwnd, IDC_CHK_TRUEHDR), col2X, y + 4, lblW + 20, rowH, TRUE);
    MoveWindow(GetDlgItem(hwnd, IDC_EDT_PEAK_NITS), col2X + lblW + 25, y, colW - lblW - 25, rowH, TRUE);
    y += gridH;

    // Grid Row 6: HDR10+ (Left) vs 3D LUT path + browse (Right)
    MoveWindow(GetDlgItem(hwnd, IDC_CHK_HDR10PLUS), pad, y + 4, lblW + 20, rowH, TRUE);
    MoveWindow(GetDlgItem(hwnd, IDC_EDT_LUT_PATH), col2X, y, colW - btnW - 6, rowH, TRUE);
    MoveWindow(GetDlgItem(hwnd, IDC_BTN_BROWSE_LUT), col2X + colW - btnW, y, btnW, rowH, TRUE);
    y += gridH + 6;

    // Action Row: Start & Stop buttons
    const int actionBtnW = 140;
    const int actionBtnH = 30;
    MoveWindow(GetDlgItem(hwnd, IDC_BTN_START), pad, y, actionBtnW, actionBtnH, TRUE);
    MoveWindow(GetDlgItem(hwnd, IDC_BTN_STOP), pad + actionBtnW + 12, y, actionBtnW, actionBtnH, TRUE);
    y += actionBtnH + 8;

    // Progress Bar
    MoveWindow(GetDlgItem(hwnd, IDC_PROGRESS), pad, y, width - pad * 2, 16, TRUE);
    y += 16 + 6;

    // Status Label & Telemetry Label
    MoveWindow(GetDlgItem(hwnd, IDC_LBL_STATUS), pad, y, width - pad * 2, 18, TRUE);
    y += 20;
    MoveWindow(GetDlgItem(hwnd, IDC_LBL_TELEMETRY), pad, y, width - pad * 2, 18, TRUE);
    y += 20;

    // Phase 7 (Task 7-C2): multi-file batch queue status list.
    MoveWindow(GetDlgItem(hwnd, IDC_LST_QUEUE), pad, y, width - pad * 2,
               std::max(60L, static_cast<long>(rc.bottom) - rc.top - y - pad), TRUE);
}

// Collects the transcode options from the current dialog state.  The batch
// worker overrides input/output per queued file.
rtx::HwTranscodeOptions collectOptions(HWND hwnd) {
    rtx::HwTranscodeOptions opt;

    wchar_t outPath[MAX_PATH] = {};
    GetDlgItemTextW(hwnd, IDC_EDT_OUTPUT, outPath, MAX_PATH);
    opt.output_path = outPath;

    // Codec
    int codecIdx = ComboBox_GetCurSel(GetDlgItem(hwnd, IDC_CMB_CODEC));
    opt.codec = (codecIdx == 1) ? rtx::VideoCodec::HEVC
              : (codecIdx == 2) ? rtx::VideoCodec::H264
                                : rtx::VideoCodec::AV1;

    // Preset
    opt.preset = std::clamp(ComboBox_GetCurSel(GetDlgItem(hwnd, IDC_CMB_PRESET)) + 1, 1, 7);

    // Rate Control & Bitrate & QP
    opt.rate_control = std::clamp(ComboBox_GetCurSel(GetDlgItem(hwnd, IDC_CMB_RC)), 0, 2);
    wchar_t bitrateBuf[32] = {};
    GetDlgItemTextW(hwnd, IDC_EDT_BITRATE, bitrateBuf, 32);
    opt.bitrate_kbps = _wtoi(bitrateBuf);

    wchar_t qpBuf[32] = {};
    GetDlgItemTextW(hwnd, IDC_EDT_QP, qpBuf, 32);
    opt.qp = std::clamp(_wtoi(qpBuf), 0, 51);

    // Resolution
    int resIdx = ComboBox_GetCurSel(GetDlgItem(hwnd, IDC_CMB_RES));
    if (resIdx == 1) {
        opt.target_width = 2560;
        opt.target_height = 1440;
    } else if (resIdx == 2) {
        opt.target_width = 1920;
        opt.target_height = 1080;
    } else if (resIdx == 3) {
        opt.target_width = 0;
        opt.target_height = 0;
    } else {
        opt.target_width = 3840;
        opt.target_height = 2160;
    }

    // VSR
    opt.vsr_enabled = (Button_GetCheck(GetDlgItem(hwnd, IDC_CHK_VSR)) == BST_CHECKED);
    opt.vsr_quality = std::clamp(ComboBox_GetCurSel(GetDlgItem(hwnd, IDC_CMB_VSR_QUAL)) + 1, 1, 4);

    // HDR & Peak Nits (Phase 6.1 微瑕 1: the two controls were created but the
    // collected options never carried them into HwTranscodeOptions).
    opt.hdr = (Button_GetCheck(GetDlgItem(hwnd, IDC_CHK_TRUEHDR)) == BST_CHECKED);
    wchar_t nitsBuf[32] = {};
    GetDlgItemTextW(hwnd, IDC_EDT_PEAK_NITS, nitsBuf, 32);
    const int parsedNits = _wtoi(nitsBuf);
    opt.peak_nits = std::clamp(parsedNits > 0 ? parsedNits : 1000, 400, 4000);

    // Orientation / Geometry & Audio Passthrough
    opt.geometry_mode = std::clamp(ComboBox_GetCurSel(GetDlgItem(hwnd, IDC_CMB_GEOM)), 0, 1);
    opt.copy_audio = (Button_GetCheck(GetDlgItem(hwnd, IDC_CHK_AUDIO)) == BST_CHECKED);

    // Phase 7: colour science (HDR10+ dynamic metadata + .cube 3D LUT).
    opt.hdr10plus = (Button_GetCheck(GetDlgItem(hwnd, IDC_CHK_HDR10PLUS)) == BST_CHECKED) && opt.hdr;
    wchar_t lutPath[MAX_PATH] = {};
    GetDlgItemTextW(hwnd, IDC_EDT_LUT_PATH, lutPath, MAX_PATH);
    opt.lut_path = lutPath;

    // Phase 7: realtime A/B preview + shared split ratio.
    g_splitRatio.store(0.5f, std::memory_order_relaxed);
    opt.split_ratio = &g_splitRatio;
    if (g_previewEnabled) {
        opt.preview = [](const void* data, std::uint32_t pitch) {
            std::lock_guard<std::mutex> lock(g_previewMutex);
            const size_t bytes = static_cast<size_t>(pitch) * kPreviewHeight;
            g_previewHost.assign(static_cast<const uint8_t*>(data),
                                 static_cast<const uint8_t*>(data) + bytes);
            g_previewPitch = pitch;
            g_previewDirty = true;
        };
    }
    opt.cancel = &g_cancelRequested;
    return opt;
}

void onStartTranscode(HWND hwnd) {
    if (g_isTranscoding) return;

    wchar_t inPath[MAX_PATH] = {};
    GetDlgItemTextW(hwnd, IDC_EDT_INPUT, inPath, MAX_PATH);

    // Phase 7 (Task 7-C2): the batch queue fills from WM_DROPFILES; when the
    // queue is empty the current input path is the single job.
    {
        std::lock_guard<std::mutex> lock(g_batchMutex);
        if (g_batchQueue.empty() && inPath[0] != L'\0') {
            g_batchQueue.push_back(inPath);
        }
        g_batchIndex = 0;
    }
    {
        std::lock_guard<std::mutex> lock(g_batchMutex);
        if (g_batchQueue.empty()) {
            MessageBoxW(hwnd, rtx::trW(rtx::StrId::StatusDropFileOnly), rtx::trW(rtx::StrId::AppTitle), MB_ICONWARNING);
            return;
        }
        if (!std::filesystem::exists(g_batchQueue.front())) {
            MessageBoxW(hwnd, rtx::trW(rtx::StrId::StatusDropFileOnly), rtx::trW(rtx::StrId::AppTitle), MB_ICONWARNING);
            return;
        }
    }

    rtx::HwTranscodeOptions opt = collectOptions(hwnd);
    autoFillOutputPath(hwnd);
    wchar_t outPath[MAX_PATH] = {};
    GetDlgItemTextW(hwnd, IDC_EDT_OUTPUT, outPath, MAX_PATH);
    opt.output_path = outPath;

    g_cancelRequested.store(false, std::memory_order_relaxed);

    g_isTranscoding = true;
    EnableWindow(GetDlgItem(hwnd, IDC_BTN_START), FALSE);
    EnableWindow(GetDlgItem(hwnd, IDC_BTN_STOP), TRUE);
    SendMessageW(GetDlgItem(hwnd, IDC_PROGRESS), PBM_SETPOS, 0, 0);
    SetDlgItemTextW(hwnd, IDC_LBL_STATUS, rtx::trW(rtx::StrId::StatusWorking));

    guiLog(L"[CineRTX GUI] Starting transcode: %ls -> %ls (codec=%d)\n", inPath, outPath, static_cast<int>(opt.codec));

    opt.progress = [hwnd](const rtx::HwTranscodeProgress& p) {
        int pct = 0;
        if (p.total_frames > 0) {
            pct = static_cast<int>(static_cast<double>(p.frames_encoded) * 1000.0 / p.total_frames);
        }
        int fps10 = static_cast<int>((p.elapsed_ms > 0 ? (p.frames_encoded * 10000.0 / p.elapsed_ms) : 0.0));
        PostMessageW(hwnd, WM_APP_PROGRESS, static_cast<WPARAM>(pct), static_cast<LPARAM>(fps10));
    };

    if (g_workerThread.joinable()) {
        g_workerThread.join();
    }

    g_workerThread = std::thread([hwnd, opt]() {
        // Phase 7 (Task 7-C2): sequential batch loop with per-file output
        // naming (<stem>_remaster_av1.mp4) and live total progress.
        std::vector<std::wstring> queue;
        {
            std::lock_guard<std::mutex> lock(g_batchMutex);
            queue = g_batchQueue;
        }
        for (size_t index = 0; index < queue.size(); ++index) {
            if (g_cancelRequested.load(std::memory_order_relaxed)) {
                break;
            }
            rtx::HwTranscodeOptions job = opt;
            job.input_path = queue[index];
            if (queue.size() > 1) {
                std::filesystem::path in(job.input_path);
                const wchar_t* suffix = (job.codec == rtx::VideoCodec::HEVC) ? L"_remaster_hevc.mp4"
                                      : (job.codec == rtx::VideoCodec::H264) ? L"_remaster_h264.mp4"
                                                                             : L"_remaster_av1.mp4";
                job.output_path = in.parent_path() / (in.stem().wstring() + suffix);
            }
            auto result = rtx::runNvidiaRemasterTranscode(job);
            const bool ok = result.has_value();
            PostMessageW(hwnd, WM_APP_PROGRESS, ok ? 1000 : 0, 0);
            if (!ok && g_cancelRequested.load(std::memory_order_relaxed)) {
                break;  // cancelled: stop the queue silently
            }
            if (!ok) {
                g_lastErrorMessage = utf8ToWide(result.error().toString());
                guiLog(L"[CineRTX GUI] Transcode failed: %ls\n", g_lastErrorMessage.c_str());
                PostMessageW(hwnd, WM_APP_DONE, 0, 0);
                return;
            }
        }
        PostMessageW(hwnd, WM_APP_DONE, 1, 0);
    });
}

void onStopTranscode(HWND hwnd) {
    if (!g_isTranscoding) return;
    g_cancelRequested.store(true, std::memory_order_release);
    SetDlgItemTextW(hwnd, IDC_LBL_STATUS, rtx::trW(rtx::StrId::StatusStopping));
    EnableWindow(GetDlgItem(hwnd, IDC_BTN_STOP), FALSE);
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
        SetDlgItemTextW(hwnd, IDC_EDT_LUT_PATH, fileName);
    }
}

// Phase 7 (Task 7-C2): timeline scrubbing.  A detached 1-frame remaster
// publishes the A/B preview for the frame at the slider position.
void runScrubPreviewAsync(HWND hwnd) {
    if (g_isTranscoding) {
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
    const int64_t duration_ms = 30000;  // default scrub span until a file is analysed
    const int64_t seek_ms =
        range_max > range_min
            ? (static_cast<int64_t>(pos) - range_min) * duration_ms / (range_max - range_min)
            : 0;

    if (g_scrubThread.joinable()) {
        return;  // one scrub at a time; the slider re-fires on release
    }
    rtx::gui::PreviewViewport* viewport = g_previewViewport.has_value()
                                              ? &*g_previewViewport
                                              : nullptr;
    (void)viewport;
    g_scrubThread = std::thread([hwnd, inPathW = std::wstring(inPath), seek_ms]() {
        rtx::HwTranscodeOptions opt = collectOptions(hwnd);
        opt.input_path = inPathW;
        opt.preview = [](const void* data, std::uint32_t pitch) {
            std::lock_guard<std::mutex> lock(g_previewMutex);
            const size_t bytes = static_cast<size_t>(pitch) * kPreviewHeight;
            g_previewHost.assign(static_cast<const uint8_t*>(data),
                                 static_cast<const uint8_t*>(data) + bytes);
            g_previewPitch = pitch;
            g_previewDirty = true;
        };
        auto result = rtx::runScrubPreview(opt, seek_ms);
        (void)result;
    });
}

LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            g_hbrBg = CreateSolidBrush(RGB(24, 24, 24));
            g_hbrCtrl = CreateSolidBrush(RGB(36, 36, 36));

            g_hFont = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                  DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                  CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            g_hFontBold = CreateFontW(-14, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                      DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                      CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

            BOOL dark = TRUE;
            DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));

            // Create Controls
            auto createCtrl = [&](const wchar_t* cls, const wchar_t* txt, DWORD style, int id) -> HWND {
                HWND h = CreateWindowExW(0, cls, txt, WS_CHILD | WS_VISIBLE | style, 0, 0, 0, 0, hwnd, (HMENU)(intptr_t)id, GetModuleHandleW(nullptr), nullptr);
                SendMessageW(h, WM_SETFONT, (WPARAM)g_hFont, TRUE);
                SetWindowTheme(h, L"DarkMode_Explorer", nullptr);
                return h;
            };

            createCtrl(L"STATIC", L"Input:", 0, IDC_LBL_INPUT);
            createCtrl(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL, IDC_EDT_INPUT);
            createCtrl(L"BUTTON", L"Browse...", BS_PUSHBUTTON, IDC_BTN_BROWSE_IN);

            createCtrl(L"STATIC", L"Output:", 0, IDC_LBL_OUTPUT);
            createCtrl(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL, IDC_EDT_OUTPUT);
            createCtrl(L"BUTTON", L"Browse...", BS_PUSHBUTTON, IDC_BTN_BROWSE_OUT);

            createCtrl(L"STATIC", L"Codec:", 0, IDC_LBL_CODEC);
            createCtrl(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL, IDC_CMB_CODEC);

            createCtrl(L"STATIC", L"Preset:", 0, IDC_LBL_PRESET);
            createCtrl(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL, IDC_CMB_PRESET);

            createCtrl(L"STATIC", L"Rate Control:", 0, IDC_LBL_RC);
            createCtrl(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL, IDC_CMB_RC);

            createCtrl(L"STATIC", L"Bitrate:", 0, IDC_LBL_BITRATE);
            createCtrl(L"EDIT", L"0", WS_BORDER | ES_NUMBER, IDC_EDT_BITRATE);

            createCtrl(L"STATIC", L"QP:", 0, IDC_LBL_QP);
            createCtrl(L"EDIT", L"24", WS_BORDER | ES_NUMBER, IDC_EDT_QP);

            createCtrl(L"STATIC", L"Resolution:", 0, IDC_LBL_RES);
            createCtrl(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL, IDC_CMB_RES);

            createCtrl(L"STATIC", L"Aspect:", 0, IDC_LBL_GEOM);
            createCtrl(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL, IDC_CMB_GEOM);

            createCtrl(L"BUTTON", L"Enable RTX VSR", BS_AUTOCHECKBOX, IDC_CHK_VSR);
            Button_SetCheck(GetDlgItem(hwnd, IDC_CHK_VSR), BST_CHECKED);

            createCtrl(L"STATIC", L"VSR Quality:", 0, IDC_LBL_VSR_QUAL);
            createCtrl(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL, IDC_CMB_VSR_QUAL);

            createCtrl(L"BUTTON", L"Enable TrueHDR", BS_AUTOCHECKBOX, IDC_CHK_TRUEHDR);
            Button_SetCheck(GetDlgItem(hwnd, IDC_CHK_TRUEHDR), BST_CHECKED);

            createCtrl(L"STATIC", L"Peak Nits:", 0, IDC_LBL_PEAK_NITS);
            createCtrl(L"EDIT", L"1000", WS_BORDER | ES_NUMBER, IDC_EDT_PEAK_NITS);

            createCtrl(L"BUTTON", L"Audio Passthrough", BS_AUTOCHECKBOX, IDC_CHK_AUDIO);
            Button_SetCheck(GetDlgItem(hwnd, IDC_CHK_AUDIO), BST_CHECKED);

            // Phase 7: colour science + realtime monitoring controls.
            createCtrl(L"BUTTON", L"HDR10+ (ST 2094-40)", BS_AUTOCHECKBOX, IDC_CHK_HDR10PLUS);
            createCtrl(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL, IDC_EDT_LUT_PATH);
            createCtrl(L"BUTTON", L"LUT...", BS_PUSHBUTTON, IDC_BTN_BROWSE_LUT);
            createCtrl(TRACKBAR_CLASSW, L"", TBS_HORZ | TBS_AUTOTICKS, IDC_TRK_SCRUB);
            SendMessageW(GetDlgItem(hwnd, IDC_TRK_SCRUB), TBM_SETRANGE, TRUE,
                         MAKELPARAM(0, 30000));
            SendMessageW(GetDlgItem(hwnd, IDC_TRK_SCRUB), TBM_SETPOS, TRUE, 0);

            createCtrl(L"LISTBOX", L"", WS_BORDER | LBS_NOINTEGRALHEIGHT | WS_VSCROLL, IDC_LST_QUEUE);

            HWND hStart = createCtrl(L"BUTTON", L"Start Transcode", BS_DEFPUSHBUTTON, IDC_BTN_START);
            SendMessageW(hStart, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);

            HWND hStop = createCtrl(L"BUTTON", L"Stop", BS_PUSHBUTTON, IDC_BTN_STOP);
            EnableWindow(hStop, FALSE);

            HWND hProg = createCtrl(PROGRESS_CLASSW, L"", 0, IDC_PROGRESS);
            SendMessageW(hProg, PBM_SETRANGE32, 0, 1000);

            createCtrl(L"STATIC", L"Ready", 0, IDC_LBL_STATUS);
            createCtrl(L"STATIC", L"FPS: - | Frames: 0 | Ctrl+L: 切换语言", 0, IDC_LBL_TELEMETRY);

            // Phase 7 (Task 7-C1): the realtime A/B monitoring viewport backs
            // the preview region (created after the controls so it z-orders
            // above the STATIC placeholder).
            auto viewport = rtx::gui::PreviewViewport::create(hwnd, kPreviewWidth, kPreviewHeight);
            if (viewport.has_value()) {
                g_previewViewport.emplace(std::move(viewport.value()));
                g_previewEnabled = true;
            } else {
                g_previewEnabled = false;
            }

            DragAcceptFiles(hwnd, TRUE);
            RegisterHotKey(hwnd, 1, MOD_CONTROL, 'L');
            updateUiLanguage(hwnd);
            syncControlStates(hwnd);
            return 0;
        }

        case WM_SIZE:
            layoutControls(hwnd);
            return 0;

        case WM_DROPFILES: {
            // Phase 7 (Task 7-C2): multi-file batch queue.
            HDROP hDrop = reinterpret_cast<HDROP>(wParam);
            const UINT fileCount = DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);
            {
                std::lock_guard<std::mutex> lock(g_batchMutex);
                g_batchQueue.clear();
                for (UINT i = 0; i < fileCount; ++i) {
                    wchar_t path[MAX_PATH] = {};
                    if (DragQueryFileW(hDrop, i, path, MAX_PATH) > 0) {
                        g_batchQueue.emplace_back(path);
                    }
                }
                g_batchIndex = 0;
            }
            if (!g_batchQueue.empty()) {
                SetDlgItemTextW(hwnd, IDC_EDT_INPUT, g_batchQueue.front().c_str());
                SetDlgItemTextW(hwnd, IDC_EDT_OUTPUT, L"");
                autoFillOutputPath(hwnd);
                SendDlgItemMessageW(hwnd, IDC_LST_QUEUE, LB_RESETCONTENT, 0, 0);
                for (const std::wstring& item : g_batchQueue) {
                    SendDlgItemMessageW(hwnd, IDC_LST_QUEUE, LB_ADDSTRING, 0,
                                        reinterpret_cast<LPARAM>(item.c_str()));
                }
            }
            DragFinish(hDrop);
            return 0;
        }

        case WM_APP_SPLIT_DRAG: {
            // Phase 7 A/B split dragging forwarded by the preview child window.
            const UINT mouseMsg = static_cast<UINT>(wParam);
            const int x = GET_X_LPARAM(lParam);
            RECT rc;
            if (g_previewViewport.has_value() && GetClientRect(g_previewViewport->hwnd(), &rc) &&
                rc.right > 0) {
                const float ratio = std::clamp(static_cast<float>(x) / static_cast<float>(rc.right), 0.0f, 1.0f);
                if (mouseMsg != WM_LBUTTONUP) {
                    g_splitRatio.store(ratio, std::memory_order_relaxed);
                }
                g_previewViewport->setSplitRatio(ratio);
            }
            return 0;
        }

        case WM_HSCROLL: {
            if (reinterpret_cast<HWND>(lParam) == GetDlgItem(hwnd, IDC_TRK_SCRUB)) {
                runScrubPreviewAsync(hwnd);
            }
            return 0;
        }

        case WM_APP_PREVIEW: {
            // GUI-thread present of the latest A/B preview frame.
            std::vector<uint8_t> frame;
            uint32_t pitch = 0;
            {
                std::lock_guard<std::mutex> lock(g_previewMutex);
                if (!g_previewDirty || g_previewHost.empty()) {
                    return 0;
                }
                g_previewDirty = false;
                frame = g_previewHost;
                pitch = g_previewPitch;
            }
            if (g_previewViewport.has_value()) {
                g_previewViewport->presentFromHostOrShared(frame.data(), pitch);
            }
            return 0;
        }

        case WM_COMMAND: {
            int id = LOWORD(wParam);
            int code = HIWORD(wParam);

            if (code == BN_CLICKED) {
                if (id == IDC_BTN_START) {
                    onStartTranscode(hwnd);
                } else if (id == IDC_BTN_STOP) {
                    onStopTranscode(hwnd);
                } else if (id == IDC_BTN_BROWSE_IN) {
                    onBrowseInput(hwnd);
                } else if (id == IDC_BTN_BROWSE_OUT) {
                    onBrowseOutput(hwnd);
                } else if (id == IDC_BTN_BROWSE_LUT) {
                    onBrowseLut(hwnd);
                } else if (id == IDC_CHK_VSR || id == IDC_CHK_TRUEHDR) {
                    syncControlStates(hwnd);
                }
            } else if (code == CBN_SELCHANGE) {
                if (id == IDC_CMB_CODEC) {
                    syncControlStates(hwnd);
                }
            }
            return 0;
        }

        case WM_HOTKEY:
        case WM_APP_TOGGLE_LANG: {
            rtx::Language next = (rtx::currentLanguage() == rtx::Language::Zh) ? rtx::Language::En : rtx::Language::Zh;
            rtx::setLanguage(next);
            updateUiLanguage(hwnd);
            return 0;
        }

        case WM_KEYDOWN: {
            if ((wParam == 'L' || wParam == 'l') &&
                ((GetKeyState(VK_CONTROL) < 0) || ((GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0))) {
                rtx::Language next = (rtx::currentLanguage() == rtx::Language::Zh) ? rtx::Language::En : rtx::Language::Zh;
                rtx::setLanguage(next);
                updateUiLanguage(hwnd);
                return 0;
            }
            break;
        }

        case WM_APP_PROGRESS: {
            int permille = static_cast<int>(wParam);
            double fps = static_cast<double>(lParam) / 10.0;
            SendMessageW(GetDlgItem(hwnd, IDC_PROGRESS), PBM_SETPOS, permille, 0);

            wchar_t buf[192];
            size_t batch_total = 0;
            size_t batch_index = 0;
            {
                std::lock_guard<std::mutex> lock(g_batchMutex);
                batch_total = g_batchQueue.size();
                batch_index = g_batchIndex;
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

        case WM_APP_DONE: {
            bool ok = (wParam == 1);
            g_isTranscoding = false;
            EnableWindow(GetDlgItem(hwnd, IDC_BTN_START), TRUE);
            EnableWindow(GetDlgItem(hwnd, IDC_BTN_STOP), FALSE);

            if (g_workerThread.joinable()) {
                g_workerThread.join();
            }

            if (ok) {
                SendMessageW(GetDlgItem(hwnd, IDC_PROGRESS), PBM_SETPOS, 1000, 0);
                SetDlgItemTextW(hwnd, IDC_LBL_STATUS, rtx::trW(rtx::StrId::StatusDonePrefix));
                MessageBoxW(hwnd, L"Transcode successfully completed!", rtx::trW(rtx::StrId::AppTitle), MB_ICONINFORMATION);
            } else {
                SetDlgItemTextW(hwnd, IDC_LBL_STATUS, rtx::trW(rtx::StrId::StatusFailed));
                std::wstring msg = L"Transcode failed: " + (g_lastErrorMessage.empty() ? L"unknown error" : g_lastErrorMessage);
                MessageBoxW(hwnd, msg.c_str(), rtx::trW(rtx::StrId::AppTitle), MB_ICONERROR);
            }
            return 0;
        }

        case WM_CTLCOLORSTATIC: {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            SetTextColor(hdc, RGB(225, 230, 235));
            SetBkColor(hdc, RGB(24, 24, 24));
            return reinterpret_cast<LRESULT>(g_hbrBg);
        }

        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX:
        case WM_CTLCOLORBTN: {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            SetTextColor(hdc, RGB(240, 245, 250));
            SetBkColor(hdc, RGB(36, 36, 36));
            return reinterpret_cast<LRESULT>(g_hbrCtrl);
        }

        case WM_TIMER: {
            if (wParam == 2) {
                // Present the latest preview frame if the transcode thread
                // staged one (the preview callback itself runs off-thread).
                bool dirty = false;
                {
                    std::lock_guard<std::mutex> lock(g_previewMutex);
                    dirty = g_previewDirty;
                }
                if (dirty) {
                    SendMessageW(hwnd, WM_APP_PREVIEW, 0, 0);
                }
            }
            return 0;
        }

        case WM_DESTROY:
            UnregisterHotKey(hwnd, 1);
            KillTimer(hwnd, 2);
            if (g_scrubThread.joinable()) {
                g_scrubThread.join();
            }
            if (g_workerThread.joinable()) {
                g_cancelRequested.store(true, std::memory_order_release);
                g_workerThread.join();
            }
            g_previewViewport.reset();
            if (g_hbrBg) DeleteObject(g_hbrBg);
            if (g_hbrCtrl) DeleteObject(g_hbrCtrl);
            if (g_hFont) DeleteObject(g_hFont);
            if (g_hFontBold) DeleteObject(g_hFontBold);
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
    if (argv != nullptr && argc > 1) {
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
    RECT wr{0, 0, MulDiv(960, dpi, 96), MulDiv(780, dpi, 96)};
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

    // Phase 7: the preview staging poll runs at ~15 Hz (well below the 60 Hz
    // UI budget) so the transcode thread is never blocked by a present.
    SetTimer(hwnd, 2, 66, nullptr);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (msg.message == WM_KEYDOWN && (msg.wParam == 'L' || msg.wParam == 'l') &&
            ((GetKeyState(VK_CONTROL) < 0) || ((GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0))) {
            rtx::Language next = (rtx::currentLanguage() == rtx::Language::Zh) ? rtx::Language::En : rtx::Language::Zh;
            rtx::setLanguage(next);
            updateUiLanguage(hwnd);
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}
