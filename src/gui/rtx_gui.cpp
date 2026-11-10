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

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <uxtheme.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <memory>
#include <mutex>
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
};

constexpr UINT WM_APP_PROGRESS    = WM_APP + 10;
constexpr UINT WM_APP_DONE        = WM_APP + 11;
constexpr UINT WM_APP_TOGGLE_LANG = WM_APP + 20;

std::atomic<bool> g_cancelRequested{false};
std::thread g_workerThread;
bool g_isTranscoding = false;

HBRUSH g_hbrBg = nullptr;
HBRUSH g_hbrCtrl = nullptr;
HFONT  g_hFont = nullptr;
HFONT  g_hFontBold = nullptr;

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
    } else {
        EnableWindow(GetDlgItem(hwnd, IDC_CHK_TRUEHDR), !g_isTranscoding);
        const bool hdrChecked = (Button_GetCheck(GetDlgItem(hwnd, IDC_CHK_TRUEHDR)) == BST_CHECKED);
        EnableWindow(GetDlgItem(hwnd, IDC_EDT_PEAK_NITS), hdrChecked && !g_isTranscoding);
    }

    const bool vsrChecked = (Button_GetCheck(GetDlgItem(hwnd, IDC_CHK_VSR)) == BST_CHECKED);
    EnableWindow(GetDlgItem(hwnd, IDC_CMB_VSR_QUAL), vsrChecked && !g_isTranscoding);
}

void layoutControls(HWND hwnd) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    const int width = rc.right - rc.left;
    const int pad = 16;
    const int rowH = 26;
    const int gapY = 8;
    const int btnW = 80;
    const int lblW = 125;

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

    // 2-Column Parameter Grid
    const int colW = (width - pad * 3) / 2;
    const int col2X = pad * 2 + colW;

    // Grid Row 1: Codec (Left) vs Preset (Right)
    MoveWindow(GetDlgItem(hwnd, IDC_LBL_CODEC), pad, y + 4, lblW, rowH, TRUE);
    MoveWindow(GetDlgItem(hwnd, IDC_CMB_CODEC), pad + lblW, y, colW - lblW, rowH, TRUE);
    MoveWindow(GetDlgItem(hwnd, IDC_LBL_PRESET), col2X, y + 4, lblW, rowH, TRUE);
    MoveWindow(GetDlgItem(hwnd, IDC_CMB_PRESET), col2X + lblW, y, colW - lblW, rowH, TRUE);
    y += rowH + gapY;

    // Grid Row 2: Rate Control (Left) vs QP (Right)
    MoveWindow(GetDlgItem(hwnd, IDC_LBL_RC), pad, y + 4, lblW, rowH, TRUE);
    MoveWindow(GetDlgItem(hwnd, IDC_CMB_RC), pad + lblW, y, colW - lblW, rowH, TRUE);
    MoveWindow(GetDlgItem(hwnd, IDC_LBL_QP), col2X, y + 4, lblW, rowH, TRUE);
    MoveWindow(GetDlgItem(hwnd, IDC_EDT_QP), col2X + lblW, y, colW - lblW, rowH, TRUE);
    y += rowH + gapY;

    // Grid Row 3: Bitrate (Left) vs Resolution (Right)
    MoveWindow(GetDlgItem(hwnd, IDC_LBL_BITRATE), pad, y + 4, lblW, rowH, TRUE);
    MoveWindow(GetDlgItem(hwnd, IDC_EDT_BITRATE), pad + lblW, y, colW - lblW, rowH, TRUE);
    MoveWindow(GetDlgItem(hwnd, IDC_LBL_RES), col2X, y + 4, lblW, rowH, TRUE);
    MoveWindow(GetDlgItem(hwnd, IDC_CMB_RES), col2X + lblW, y, colW - lblW, rowH, TRUE);
    y += rowH + gapY;

    // Grid Row 4: Orientation / Aspect (Left) vs Audio Copy (Right)
    MoveWindow(GetDlgItem(hwnd, IDC_LBL_GEOM), pad, y + 4, lblW, rowH, TRUE);
    MoveWindow(GetDlgItem(hwnd, IDC_CMB_GEOM), pad + lblW, y, colW - lblW, rowH, TRUE);
    MoveWindow(GetDlgItem(hwnd, IDC_CHK_AUDIO), col2X, y + 4, colW, rowH, TRUE);
    y += rowH + gapY;

    // Grid Row 5: VSR Check & Quality
    MoveWindow(GetDlgItem(hwnd, IDC_CHK_VSR), pad, y + 4, lblW + 30, rowH, TRUE);
    MoveWindow(GetDlgItem(hwnd, IDC_CMB_VSR_QUAL), pad + lblW + 35, y, colW - lblW - 35, rowH, TRUE);
    MoveWindow(GetDlgItem(hwnd, IDC_CHK_TRUEHDR), col2X, y + 4, lblW + 30, rowH, TRUE);
    MoveWindow(GetDlgItem(hwnd, IDC_EDT_PEAK_NITS), col2X + lblW + 35, y, colW - lblW - 35, rowH, TRUE);
    y += rowH + gapY + 12;

    // Action Row: Start & Stop buttons
    const int actionBtnW = 140;
    const int actionBtnH = 34;
    MoveWindow(GetDlgItem(hwnd, IDC_BTN_START), pad, y, actionBtnW, actionBtnH, TRUE);
    MoveWindow(GetDlgItem(hwnd, IDC_BTN_STOP), pad + actionBtnW + 12, y, actionBtnW, actionBtnH, TRUE);
    y += actionBtnH + 12;

    // Progress Bar
    MoveWindow(GetDlgItem(hwnd, IDC_PROGRESS), pad, y, width - pad * 2, 20, TRUE);
    y += 20 + 8;

    // Status Label & Telemetry Label
    MoveWindow(GetDlgItem(hwnd, IDC_LBL_STATUS), pad, y, width - pad * 2, 22, TRUE);
    y += 24;
    MoveWindow(GetDlgItem(hwnd, IDC_LBL_TELEMETRY), pad, y, width - pad * 2, 22, TRUE);
}

void onStartTranscode(HWND hwnd) {
    if (g_isTranscoding) return;

    wchar_t inPath[MAX_PATH] = {};
    GetDlgItemTextW(hwnd, IDC_EDT_INPUT, inPath, MAX_PATH);
    if (inPath[0] == L'\0' || !std::filesystem::exists(inPath)) {
        MessageBoxW(hwnd, rtx::trW(rtx::StrId::StatusDropFileOnly), rtx::trW(rtx::StrId::AppTitle), MB_ICONWARNING);
        return;
    }

    autoFillOutputPath(hwnd);
    wchar_t outPath[MAX_PATH] = {};
    GetDlgItemTextW(hwnd, IDC_EDT_OUTPUT, outPath, MAX_PATH);

    rtx::HwTranscodeOptions opt;
    opt.input_path = inPath;
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

    // HDR
    opt.hdr = (Button_GetCheck(GetDlgItem(hwnd, IDC_CHK_TRUEHDR)) == BST_CHECKED);
    opt.copy_audio = (Button_GetCheck(GetDlgItem(hwnd, IDC_CHK_AUDIO)) == BST_CHECKED);

    g_cancelRequested.store(false, std::memory_order_relaxed);
    opt.cancel = &g_cancelRequested;

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
        auto result = rtx::runNvidiaRemasterTranscode(opt);
        PostMessageW(hwnd, WM_APP_DONE, result.has_value() ? 1 : 0, 0);
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

            HWND hStart = createCtrl(L"BUTTON", L"Start Transcode", BS_DEFPUSHBUTTON, IDC_BTN_START);
            SendMessageW(hStart, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);

            HWND hStop = createCtrl(L"BUTTON", L"Stop", BS_PUSHBUTTON, IDC_BTN_STOP);
            EnableWindow(hStop, FALSE);

            HWND hProg = createCtrl(PROGRESS_CLASSW, L"", 0, IDC_PROGRESS);
            SendMessageW(hProg, PBM_SETRANGE32, 0, 1000);

            createCtrl(L"STATIC", L"Ready", 0, IDC_LBL_STATUS);
            createCtrl(L"STATIC", L"FPS: - | Frames: 0 | Ctrl+L: 切换语言", 0, IDC_LBL_TELEMETRY);

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
            HDROP hDrop = reinterpret_cast<HDROP>(wParam);
            wchar_t path[MAX_PATH] = {};
            if (DragQueryFileW(hDrop, 0, path, MAX_PATH) > 0) {
                SetDlgItemTextW(hwnd, IDC_EDT_INPUT, path);
                SetDlgItemTextW(hwnd, IDC_EDT_OUTPUT, L"");
                autoFillOutputPath(hwnd);
            }
            DragFinish(hDrop);
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

            wchar_t buf[128];
            _snwprintf_s(buf, _countof(buf), _TRUNCATE, L"Progress: %.1f%%  |  Speed: %.1f FPS", static_cast<double>(permille) / 10.0, fps);
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
                MessageBoxW(hwnd, L"Transcode failed or was cancelled.", rtx::trW(rtx::StrId::AppTitle), MB_ICONERROR);
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

        case WM_DESTROY:
            UnregisterHotKey(hwnd, 1);
            if (g_workerThread.joinable()) {
                g_cancelRequested.store(true, std::memory_order_release);
                g_workerThread.join();
            }
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
    RECT wr{0, 0, MulDiv(880, dpi, 96), MulDiv(580, dpi, 96)};
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
