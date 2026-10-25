// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
//
// Immediate-mode Win32 GUI for the single-exe build.  The window owns no child
// controls: every frame is composed into one off-screen bitmap and blitted in a
// single BitBlt.  The transcode worker and the message pump only share the
// cache-line aligned telemetry block below.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#ifndef WINVER
#define WINVER 0x0A00
#endif

#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <shellapi.h>

#if defined(small)
#undef small
#endif

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "rtx/hw_transcode.h"
#include "rtx/nvidia_shim.h"
#include "rtx/types.h"

int rtxCliMain(int argc, char** argv);

namespace {

constexpr UINT kTelemetryTimer = 1;
constexpr UINT kExitTimer = 2;
constexpr UINT kMsgDone = WM_APP + 1;
constexpr UINT kMsgHardware = WM_APP + 2;
constexpr UINT kMsgAutostart = WM_APP + 3;

struct alignas(64) TranscodeTelemetry {
    std::atomic<uint32_t> running{0};
    std::atomic<uint32_t> progressPermille{0};
    std::atomic<uint32_t> fpsTimes10{0};
    std::atomic<uint32_t> cancelRequested{0};
    std::atomic<uint32_t> zeroCopy{0};  // 0 unknown, 1 confirmed, 2 host copies
    std::atomic<uint64_t> framesDecoded{0};
    std::atomic<uint64_t> framesEncoded{0};
    std::atomic<uint64_t> totalFrames{0};
    std::atomic<uint64_t> elapsedMs{0};
    std::atomic<uint64_t> d2hBytes{0};
    std::atomic<uint64_t> h2dBytes{0};
};

static_assert(sizeof(TranscodeTelemetry) % 64 == 0,
              "telemetry must not share a cache line with unrelated state");

TranscodeTelemetry g_telemetry;
std::atomic<bool> g_cancel{false};
std::atomic<HWND> g_window{nullptr};
bool g_selfTest = false;
bool g_dirty = true;

struct WorkerResult {
    bool ok = false;
    bool cancelled = false;
    bool zeroCopy = false;
    uint64_t d2h = 0;
    uint64_t h2d = 0;
    uint64_t frames = 0;
    double elapsedMs = 0.0;
    std::wstring title;
    std::wstring detail;
};

std::mutex g_resultMutex;
WorkerResult g_result;
std::mutex g_gpuMutex;
std::wstring g_gpuText = L"拖入视频后检测 GPU / NVENC";
bool g_hardwareProbeStarted = false;
bool g_startupRecorded = false;
bool g_headlessPaint = false;
UINT g_forcedDpi = 0;
double g_mainEntryMs = 0.0;
double g_windowCreatedMs = 0.0;
double g_updateMs = 0.0;
double g_paintBufferMs = 0.0;
double g_paintDrawMs = 0.0;
double g_paintBlitMs = 0.0;

std::wstring g_inputPath;
std::wstring g_outputPath;
std::wstring g_autostartPath;
std::wstring g_status = L"将 MP4 / MKV 拖入窗口，然后开始硬件转码。";
int g_codecIndex = 0;   // 0 = HEVC, 1 = H.264
int g_presetIndex = 1;  // P5, P6, P7
constexpr int kPresets[] = {5, 6, 7};

constexpr COLORREF kBg = RGB(18, 18, 18);
constexpr COLORREF kCard = RGB(30, 30, 30);
constexpr COLORREF kCard2 = RGB(24, 28, 32);
constexpr COLORREF kAccent = RGB(118, 185, 0);
constexpr COLORREF kAccentDim = RGB(72, 112, 8);
constexpr COLORREF kText = RGB(232, 236, 239);
constexpr COLORREF kMuted = RGB(154, 164, 175);
constexpr COLORREF kBorder = RGB(44, 50, 56);
constexpr COLORREF kDanger = RGB(190, 46, 38);
constexpr COLORREF kBarBg = RGB(42, 48, 54);

struct Theme {
    UINT dpi = 96;
    HFONT title = nullptr;
    HFONT section = nullptr;
    HFONT body = nullptr;
    HFONT small = nullptr;
    HFONT mono = nullptr;
    HBRUSH bg = nullptr;
    HBRUSH card = nullptr;
    HBRUSH card2 = nullptr;
    HBRUSH accent = nullptr;
    HBRUSH accentDim = nullptr;
    HBRUSH danger = nullptr;
    HBRUSH barBg = nullptr;
    HPEN border = nullptr;
    HDC mem = nullptr;
    HBITMAP bitmap = nullptr;
    HBITMAP oldBitmap = nullptr;
    int width = 0;
    int height = 0;
};

Theme g_theme;

struct Zones {
    RECT drop{};
    RECT codec{};
    RECT preset{};
    RECT start{};

    bool hit(const RECT& r, POINT p) {
        return p.x >= r.left && p.x < r.right && p.y >= r.top && p.y < r.bottom;
    }
};

Zones g_zones;

int S(int value) {
    return MulDiv(value, static_cast<int>(g_theme.dpi), 96);
}

HFONT createFont(int pixelHeight, int weight, const wchar_t* face, DWORD pitch) {
    return CreateFontW(-S(pixelHeight), 0, 0, 0, weight, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, pitch, face);
}

void destroyTheme() {
    if (g_theme.mem != nullptr) {
        if (g_theme.oldBitmap != nullptr) {
            SelectObject(g_theme.mem, g_theme.oldBitmap);
        }
        if (g_theme.bitmap != nullptr) {
            DeleteObject(g_theme.bitmap);
        }
        DeleteDC(g_theme.mem);
    }
    g_theme.mem = nullptr;
    g_theme.bitmap = nullptr;
    g_theme.oldBitmap = nullptr;
    for (HFONT* font : {&g_theme.title, &g_theme.section, &g_theme.body, &g_theme.small,
                        &g_theme.mono}) {
        if (*font != nullptr) {
            DeleteObject(*font);
            *font = nullptr;
        }
    }
    for (HBRUSH* brush : {&g_theme.bg, &g_theme.card, &g_theme.card2, &g_theme.accent,
                          &g_theme.accentDim, &g_theme.danger, &g_theme.barBg}) {
        if (*brush != nullptr) {
            DeleteObject(*brush);
            *brush = nullptr;
        }
    }
    if (g_theme.border != nullptr) {
        DeleteObject(g_theme.border);
        g_theme.border = nullptr;
    }
}

void createTheme(UINT dpi) {
    destroyTheme();
    g_theme.dpi = dpi;
    g_theme.title = createFont(25, FW_BOLD, L"Microsoft YaHei UI", DEFAULT_PITCH);
    g_theme.section = createFont(15, FW_SEMIBOLD, L"Microsoft YaHei UI", DEFAULT_PITCH);
    g_theme.body = createFont(14, FW_NORMAL, L"Microsoft YaHei UI", DEFAULT_PITCH);
    g_theme.small = createFont(12, FW_NORMAL, L"Microsoft YaHei UI", DEFAULT_PITCH);
    g_theme.mono = createFont(13, FW_NORMAL, L"Consolas", FIXED_PITCH | FF_MODERN);
    g_theme.bg = CreateSolidBrush(kBg);
    g_theme.card = CreateSolidBrush(kCard);
    g_theme.card2 = CreateSolidBrush(kCard2);
    g_theme.accent = CreateSolidBrush(kAccent);
    g_theme.accentDim = CreateSolidBrush(kAccentDim);
    g_theme.danger = CreateSolidBrush(kDanger);
    g_theme.barBg = CreateSolidBrush(kBarBg);
    g_theme.border = CreatePen(PS_SOLID, 1, kBorder);
}

void ensureBackBuffer(int width, int height) {
    if (width <= 0 || height <= 0) {
        return;
    }
    if (g_theme.mem != nullptr && g_theme.width == width && g_theme.height == height) {
        return;
    }
    if (g_theme.mem != nullptr) {
        SelectObject(g_theme.mem, g_theme.oldBitmap);
        DeleteObject(g_theme.bitmap);
        DeleteDC(g_theme.mem);
    }
    HDC screen = GetDC(nullptr);
    g_theme.mem = CreateCompatibleDC(screen);
    g_theme.bitmap = CreateCompatibleBitmap(screen, width, height);
    ReleaseDC(nullptr, screen);
    g_theme.oldBitmap = static_cast<HBITMAP>(SelectObject(g_theme.mem, g_theme.bitmap));
    g_theme.width = width;
    g_theme.height = height;
}

void fillRect(HDC dc, const RECT& rect, HBRUSH brush) {
    FillRect(dc, &rect, brush);
}

void card(HDC dc, const RECT& rect, int radius, HBRUSH fill) {
    HGDIOBJ oldBrush = SelectObject(dc, fill);
    HGDIOBJ oldPen = SelectObject(dc, g_theme.border);
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, radius, radius);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
}

void text(HDC dc, const std::wstring& value, RECT rect, HFONT font, COLORREF color,
          UINT flags) {
    SelectObject(dc, font);
    SetTextColor(dc, color);
    SetBkMode(dc, TRANSPARENT);
    DrawTextW(dc, value.c_str(), static_cast<int>(value.size()), &rect,
              flags | DT_NOPREFIX);
}

std::wstring utf8ToWide(const std::string& value) {
    if (value.empty()) {
        return {};
    }
    const int size = MultiByteToWideChar(CP_UTF8, 0, value.data(),
                                         static_cast<int>(value.size()), nullptr, 0);
    if (size <= 0) {
        return {};
    }
    std::wstring result(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                        result.data(), size);
    return result;
}

std::string wideToUtf8(const std::wstring& value) {
    if (value.empty()) {
        return {};
    }
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(),
                                         static_cast<int>(value.size()), nullptr, 0,
                                         nullptr, nullptr);
    if (size <= 0) {
        return {};
    }
    std::string result(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                        result.data(), size, nullptr, nullptr);
    return result;
}

std::wstring fileName(const std::wstring& path) {
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

std::wstring directOutputPath(const std::wstring& input) {
    const size_t slash = input.find_last_of(L"\\/");
    size_t dot = input.find_last_of(L'.');
    if (dot == std::wstring::npos || (slash != std::wstring::npos && dot < slash)) {
        dot = input.size();
    }
    return input.substr(0, dot) + L".remaster.mp4";
}

std::wstring formatFps(uint32_t times10) {
    wchar_t buffer[64] = {};
    std::swprintf(buffer, 64, L"%.1f FPS", static_cast<double>(times10) / 10.0);
    return buffer;
}

std::wstring formatSeconds(uint64_t milliseconds) {
    wchar_t buffer[64] = {};
    std::swprintf(buffer, 64, L"%.2f s", static_cast<double>(milliseconds) / 1000.0);
    return buffer;
}

std::wstring formatCount(uint64_t value, uint64_t total) {
    wchar_t buffer[96] = {};
    if (total > 0) {
        std::swprintf(buffer, 96, L"%llu / %llu", static_cast<unsigned long long>(value),
                      static_cast<unsigned long long>(total));
    } else {
        std::swprintf(buffer, 96, L"%llu", static_cast<unsigned long long>(value));
    }
    return buffer;
}

void setStatus(const std::wstring& value) {
    g_status = value;
    g_dirty = true;
}

void enableDarkTitleBar(HWND hwnd) {
    BOOL dark = TRUE;
    if (FAILED(DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark)))) {
        (void)DwmSetWindowAttribute(hwnd, 19, &dark, sizeof(dark));
    }
    SendMessageW(hwnd, WM_NCACTIVATE, FALSE, 0);
    SendMessageW(hwnd, WM_NCACTIVATE, TRUE, 0);
}

bool environmentFlag(const wchar_t* name) {
    wchar_t value[8] = {};
    return GetEnvironmentVariableW(name, value, 8) > 0 && value[0] == L'1';
}

void startHardwareProbe() {
    if (g_hardwareProbeStarted) {
        return;
    }
    g_hardwareProbeStarted = true;
    std::thread([]() {
        std::wstring text;
        rtx::NvidiaShim shim;
        std::string error;
        if (!shim.open(&error)) {
            text = L"rtx_shim.dll 未就绪：" + utf8ToWide(error);
        } else {
            RtxDiagnostics diagnostics{};
            if (shim.probe(&diagnostics) == RTX_OK && diagnostics.device.name[0] != '\0') {
                wchar_t buffer[256] = {};
                std::swprintf(buffer, 256, L"%ls · SM %d.%d · CUDA %u.%u",
                              utf8ToWide(diagnostics.device.name).c_str(),
                              diagnostics.device.compute_major,
                              diagnostics.device.compute_minor,
                              diagnostics.device.cuda_version / 1000,
                              (diagnostics.device.cuda_version % 1000) / 10);
                text = buffer;
            } else {
                text = L"CUDA 设备不可用：" + utf8ToWide(diagnostics.summary);
            }
        }
        {
            std::lock_guard<std::mutex> lock(g_gpuMutex);
            g_gpuText = text;
        }
        if (HWND window = g_window.load()) {
            PostMessageW(window, kMsgHardware, 0, 0);
        }
    }).detach();
}

double millisecondsSinceCreation() {
    FILETIME creation{};
    FILETIME exitTime{};
    FILETIME kernel{};
    FILETIME user{};
    ULARGE_INTEGER started{};
    if (GetProcessTimes(GetCurrentProcess(), &creation, &exitTime, &kernel, &user)) {
        started.LowPart = creation.dwLowDateTime;
        started.HighPart = creation.dwHighDateTime;
    }
    FILETIME now{};
    GetSystemTimePreciseAsFileTime(&now);
    ULARGE_INTEGER ended{};
    ended.LowPart = now.dwLowDateTime;
    ended.HighPart = now.dwHighDateTime;
    return started.QuadPart != 0
               ? static_cast<double>(ended.QuadPart - started.QuadPart) / 10000.0
               : 0.0;
}

void writeStartupLog(HWND hwnd, bool painted) {
    wchar_t logPath[1024] = {};
    if (GetEnvironmentVariableW(L"RTX_GUI_STARTUP_LOG", logPath, 1024) > 0) {
        const double elapsedMs = millisecondsSinceCreation();

        RECT client{};
        GetClientRect(hwnd, &client);
        char line[512] = {};
        const int length =
            painted ? std::snprintf(
                          line, sizeof(line),
                          "startup_ms=%.3f\r\ndpi=%u\r\nclient=%dx%d\r\nheadless=%d\r\n"
                          "phases=main:%.2f,create:%.2f,update:%.2f\r\n"
                          "paint=buffer:%.2f,draw:%.2f,blit:%.2f\r\n",
                          elapsedMs, g_theme.dpi, static_cast<int>(client.right - client.left),
                          static_cast<int>(client.bottom - client.top), g_headlessPaint ? 1 : 0,
                          g_mainEntryMs, g_windowCreatedMs, g_updateMs,
                          g_paintBufferMs, g_paintDrawMs, g_paintBlitMs)
                    : std::snprintf(
                          line, sizeof(line),
                          "startup_ms=NA\r\npainted=0\r\ndpi=%u\r\nclient=%dx%d\r\nvisible=%d\r\n",
                          g_theme.dpi, static_cast<int>(client.right - client.left),
                          static_cast<int>(client.bottom - client.top),
                          IsWindowVisible(hwnd) ? 1 : 0);
        HANDLE file = CreateFileW(logPath, GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                                  CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE) {
            DWORD written = 0;
            WriteFile(file, line, static_cast<DWORD>(length), &written, nullptr);
            CloseHandle(file);
        }
    }
}

void recordStartup(HWND hwnd) {
    if (g_startupRecorded) {
        return;
    }
    g_startupRecorded = true;
    writeStartupLog(hwnd, true);
}

Zones computeLayout(int width, int* headerBottom) {
    const int margin = S(24);
    Zones zones;
    int top = margin;
    if (headerBottom != nullptr) {
        *headerBottom = top + S(58);
    }
    top += S(58) + S(16);
    zones.drop = RECT{margin, top, width - margin, top + S(88)};
    top = zones.drop.bottom + S(16);

    const int gap = S(12);
    const int codecWidth = S(190);
    const int presetWidth = S(150);
    zones.codec = RECT{margin, top, margin + codecWidth, top + S(46)};
    zones.preset = RECT{zones.codec.right + gap, top,
                        zones.codec.right + gap + presetWidth, top + S(46)};
    zones.start = RECT{zones.preset.right + gap, top, width - margin, top + S(46)};
    return zones;
}

void drawFrame(HDC dc, int width, int height) {
    const RECT full{0, 0, width, height};
    fillRect(dc, full, g_theme.bg);

    int headerBottom = 0;
    const Zones zones = computeLayout(width, &headerBottom);
    g_zones = zones;
    const int margin = S(24);

    RECT accent{margin, margin + S(4), margin + S(4), margin + S(48)};
    fillRect(dc, accent, g_theme.accent);
    text(dc, L"RTX 零拷贝转码引擎",
         RECT{margin + S(16), margin - S(2), width - margin, margin + S(34)},
         g_theme.title, kText, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    std::wstring gpu;
    {
        std::lock_guard<std::mutex> lock(g_gpuMutex);
        gpu = g_gpuText;
    }
    text(dc, gpu, RECT{margin + S(17), margin + S(32), width - margin, headerBottom},
         g_theme.small, kMuted, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
    text(dc, L"Win32 · 单 exe · 无托管运行时",
         RECT{width - S(300), margin, width - margin, margin + S(24)}, g_theme.small,
         kMuted, DT_RIGHT | DT_SINGLELINE | DT_VCENTER);

    card(dc, zones.drop, S(10), g_theme.card);
    text(dc, L"输入视频", RECT{zones.drop.left + S(16), zones.drop.top + S(9),
                              zones.drop.right - S(16), zones.drop.top + S(28)},
         g_theme.small, kAccent, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    if (g_inputPath.empty()) {
        text(dc, L"将 MP4 / MKV 文件拖入窗口",
             RECT{zones.drop.left + S(16), zones.drop.top + S(30), zones.drop.right - S(16),
                  zones.drop.top + S(60)},
             g_theme.section, kText, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        text(dc, L"NVDEC 解码 · NVENC HEVC / H.264 编码 · 保持源分辨率与位深",
             RECT{zones.drop.left + S(16), zones.drop.top + S(59), zones.drop.right - S(16),
                  zones.drop.top + S(82)},
             g_theme.small, kMuted, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    } else {
        text(dc, g_inputPath,
             RECT{zones.drop.left + S(16), zones.drop.top + S(29), zones.drop.right - S(16),
                  zones.drop.top + S(53)},
             g_theme.body, kText, DT_PATH_ELLIPSIS | DT_SINGLELINE | DT_VCENTER);
        text(dc, L"输出  " + fileName(g_outputPath),
             RECT{zones.drop.left + S(16), zones.drop.top + S(55), zones.drop.right - S(16),
                  zones.drop.top + S(79)},
             g_theme.small, kMuted, DT_END_ELLIPSIS | DT_SINGLELINE | DT_VCENTER);
    }

    card(dc, zones.codec, S(9), g_theme.card2);
    text(dc, g_codecIndex == 0 ? L"编码  HEVC" : L"编码  H.264", zones.codec,
         g_theme.body, kText, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
    card(dc, zones.preset, S(9), g_theme.card2);
    wchar_t presetLabel[32] = {};
    std::swprintf(presetLabel, 32, L"预设  P%d", kPresets[g_presetIndex]);
    text(dc, presetLabel, zones.preset, g_theme.body, kText,
         DT_CENTER | DT_SINGLELINE | DT_VCENTER);

    const bool running = g_telemetry.running.load(std::memory_order_relaxed) != 0;
    fillRect(dc, zones.start, running ? g_theme.danger : g_theme.accent);
    text(dc, running ? L"停止转码" : (g_inputPath.empty() ? L"拖入文件后开始" : L"开始硬件转码"),
         zones.start, g_theme.section, RGB(255, 255, 255),
         DT_CENTER | DT_SINGLELINE | DT_VCENTER);

    const int top = zones.start.bottom + S(16);
    const RECT progressCard{margin, top, width - margin, height - margin};
    if (progressCard.bottom > progressCard.top + S(80)) {
        card(dc, progressCard, S(10), g_theme.card);
        text(dc, L"任务遥测",
             RECT{progressCard.left + S(16), progressCard.top + S(9),
                  progressCard.right - S(16), progressCard.top + S(31)},
             g_theme.section, kText, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        text(dc, L"WDDM 异步 · 零拷贝主循环",
             RECT{progressCard.left + S(200), progressCard.top + S(9),
                  progressCard.right - S(16), progressCard.top + S(31)},
             g_theme.small, kMuted, DT_RIGHT | DT_SINGLELINE | DT_VCENTER);

        const RECT barBg{progressCard.left + S(16), progressCard.top + S(43),
                         progressCard.right - S(16), progressCard.top + S(53)};
        card(dc, barBg, S(5), g_theme.barBg);
        const uint32_t permille = g_telemetry.progressPermille.load(std::memory_order_relaxed);
        const uint64_t total = g_telemetry.totalFrames.load(std::memory_order_relaxed);
        RECT fill = barBg;
        if (permille > 0) {
            fill.right = barBg.left +
                         static_cast<int>((barBg.right - barBg.left) * permille / 1000);
            if (fill.right - fill.left < S(6)) {
                fill.right = fill.left + S(6);
            }
            card(dc, fill, S(5), g_theme.accent);
        } else if (running && total == 0) {
            const int span = barBg.right - barBg.left;
            const int segment = span / 4;
            const int offset = static_cast<int>((GetTickCount64() / 6) % (span + segment));
            fill.left = barBg.left + offset - segment;
            fill.right = fill.left + segment;
            if (fill.left < barBg.left) {
                fill.left = barBg.left;
            }
            if (fill.right > barBg.right) {
                fill.right = barBg.right;
            }
            if (fill.right > fill.left) {
                card(dc, fill, S(5), g_theme.accentDim);
            }
        }

        wchar_t progressLabel[64] = {};
        if (total > 0) {
            std::swprintf(progressLabel, 64, L"进度  %.1f%%",
                          static_cast<double>(permille) / 10.0);
        } else if (running) {
            std::swprintf(progressLabel, 64, L"进度  处理中");
        } else {
            std::swprintf(progressLabel, 64, L"进度  —");
        }
        text(dc, progressLabel,
             RECT{progressCard.left + S(16), progressCard.top + S(57),
                  progressCard.right - S(16), progressCard.top + S(77)},
             g_theme.small, kMuted, DT_LEFT | DT_SINGLELINE | DT_VCENTER);

        const uint64_t encoded = g_telemetry.framesEncoded.load(std::memory_order_relaxed);
        const uint64_t elapsed = g_telemetry.elapsedMs.load(std::memory_order_relaxed);
        const uint32_t fps = g_telemetry.fpsTimes10.load(std::memory_order_relaxed);
        const uint32_t zeroCopy = g_telemetry.zeroCopy.load(std::memory_order_relaxed);
        const int column = (progressCard.right - progressCard.left - S(48)) / 4;
        const int statTop = progressCard.top + S(84);
        const wchar_t* labels[4] = {L"吞吐", L"帧数", L"耗时", L"零拷贝"};
        std::wstring values[4];
        values[0] = running || fps > 0 ? formatFps(fps) : L"—";
        values[1] = formatCount(encoded, total);
        values[2] = elapsed > 0 ? formatSeconds(elapsed) : L"—";
        if (zeroCopy == 1) {
            values[3] = L"D2H 0 B · H2D 0 B";
        } else if (zeroCopy == 2) {
            values[3] = L"检测到主机拷贝";
        } else {
            values[3] = running ? L"运行中" : L"—";
        }
        for (int index = 0; index < 4; ++index) {
            const int left = progressCard.left + S(16) + column * index;
            text(dc, labels[index], RECT{left, statTop, left + column - S(8), statTop + S(18)},
                 g_theme.small, kMuted, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
            text(dc, values[index],
                 RECT{left, statTop + S(18), left + column - S(8), statTop + S(42)},
                 index == 3 ? g_theme.mono : g_theme.section,
                 index == 3 && zeroCopy == 1 ? kAccent : kText,
                 DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
        }

        text(dc, g_status,
             RECT{progressCard.left + S(16), progressCard.top + S(133),
                  progressCard.right - S(16), progressCard.bottom - S(10)},
             g_theme.body, kMuted, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
    }
}

void paint(HWND hwnd, HDC target) {
    RECT client{};
    GetClientRect(hwnd, &client);
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;
    const double started = millisecondsSinceCreation();
    ensureBackBuffer(width, height);
    if (g_theme.mem == nullptr) {
        return;
    }
    const double buffered = millisecondsSinceCreation();
    drawFrame(g_theme.mem, width, height);
    const double drawn = millisecondsSinceCreation();
    BitBlt(target, 0, 0, width, height, g_theme.mem, 0, 0, SRCCOPY);
    const double blitted = millisecondsSinceCreation();
    if (!g_startupRecorded) {
        g_paintBufferMs = buffered - started;
        g_paintDrawMs = drawn - buffered;
        g_paintBlitMs = blitted - drawn;
    }
}

struct JobSnapshot {
    std::wstring input;
    std::wstring output;
    int codecIndex = 0;
    int preset = 5;
};

void writeResultLog(const WorkerResult& result, const JobSnapshot& job) {
    wchar_t path[1024] = {};
    if (GetEnvironmentVariableW(L"RTX_GUI_RESULT_LOG", path, 1024) == 0) {
        return;
    }
    const std::string title = wideToUtf8(result.title);
    const std::string detail = wideToUtf8(result.detail);
    const std::string input = wideToUtf8(job.input);
    const std::string output = wideToUtf8(job.output);
    char line[4096] = {};
    const int length = std::snprintf(
        line, sizeof(line),
        "ok=%d\r\ncancelled=%d\r\nframes=%llu\r\nelapsed_ms=%.3f\r\nzero_copy=%d\r\n"
        "d2h_bytes=%llu\r\nh2d_bytes=%llu\r\ninput=%s\r\noutput=%s\r\ntitle=%s\r\n"
        "detail=%s\r\n",
        result.ok ? 1 : 0, result.cancelled ? 1 : 0,
        static_cast<unsigned long long>(result.frames), result.elapsedMs, result.zeroCopy,
        static_cast<unsigned long long>(result.d2h), static_cast<unsigned long long>(result.h2d),
        input.c_str(), output.c_str(), title.c_str(), detail.c_str());
    HANDLE file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(file, line, static_cast<DWORD>(length), &written, nullptr);
        CloseHandle(file);
    }
}

void runJob(JobSnapshot job) {
    rtx::HwTranscodeOptions options;
    options.input_path = wideToUtf8(job.input);
    options.output_path = wideToUtf8(job.output);
    options.codec = job.codecIndex == 0 ? rtx::VideoCodec::HEVC : rtx::VideoCodec::H264;
    options.bit_depth = 0;
    options.preset = job.preset;
    options.hdr = true;
    options.cancel = &g_cancel;
    options.progress = [](const rtx::HwTranscodeProgress& progress) {
        g_telemetry.framesDecoded.store(progress.frames_decoded, std::memory_order_relaxed);
        g_telemetry.framesEncoded.store(progress.frames_encoded, std::memory_order_relaxed);
        g_telemetry.totalFrames.store(progress.total_frames, std::memory_order_relaxed);
        g_telemetry.elapsedMs.store(static_cast<uint64_t>(progress.elapsed_ms),
                                    std::memory_order_relaxed);
        if (progress.total_frames > 0) {
            const uint64_t permille =
                static_cast<uint64_t>(progress.frames_decoded) * 1000 /
                static_cast<uint64_t>(progress.total_frames);
            g_telemetry.progressPermille.store(
                static_cast<uint32_t>(permille > 1000 ? 1000 : permille),
                std::memory_order_relaxed);
        }
    };

    const auto result = rtx::runNvidiaContainerTranscode(options);
    WorkerResult completed;
    if (result.ok()) {
        const rtx::HwTranscodeReport& report = result.value();
        completed.ok = true;
        completed.zeroCopy = report.zero_copy_main_loop;
        completed.d2h = report.decoder_device_to_host_bytes;
        completed.h2d = report.encoder_host_to_device_bytes;
        completed.frames = static_cast<uint64_t>(report.frames_encoded);
        completed.elapsedMs = report.elapsed_ms;
        wchar_t title[256] = {};
        const double fps = report.elapsed_ms > 0
                               ? static_cast<double>(report.frames_encoded) * 1000.0 /
                                     report.elapsed_ms
                               : 0.0;
        std::swprintf(title, 256, L"完成 · %lld 帧 · %.1f FPS · %ls",
                      static_cast<long long>(report.frames_encoded), fps,
                      report.zero_copy_main_loop ? L"零拷贝确认" : L"存在主机拷贝");
        completed.title = title;
        completed.detail = L"输出 " + job.output;
        g_telemetry.progressPermille.store(1000, std::memory_order_relaxed);
        g_telemetry.zeroCopy.store(report.zero_copy_main_loop ? 1 : 2,
                                   std::memory_order_relaxed);
        g_telemetry.d2hBytes.store(report.decoder_device_to_host_bytes,
                                   std::memory_order_relaxed);
        g_telemetry.h2dBytes.store(report.encoder_host_to_device_bytes,
                                   std::memory_order_relaxed);
    } else if (result.code() == rtx::ErrorCode::State && g_cancel.load()) {
        completed.cancelled = true;
        completed.title = L"任务已取消";
        completed.detail = L"输入文件未被修改";
    } else {
        completed.title = L"转码失败";
        completed.detail = utf8ToWide(result.error().message);
        if (!result.error().detail.empty()) {
            completed.detail += L"：" + utf8ToWide(result.error().detail);
        }
        completed.detail += L"（输出 " + job.output + L"）";
    }

    {
        std::lock_guard<std::mutex> lock(g_resultMutex);
        g_result = completed;
    }
    writeResultLog(completed, job);
    g_telemetry.running.store(0, std::memory_order_relaxed);
    if (HWND window = g_window.load()) {
        PostMessageW(window, kMsgDone, 0, 0);
    }
}

void startJob(HWND hwnd) {
    if (g_telemetry.running.load() != 0) {
        return;
    }
    if (g_inputPath.empty()) {
        setStatus(L"请先将 MP4 / MKV 文件拖入窗口。");
        InvalidateRect(hwnd, nullptr, FALSE);
        return;
    }
    g_outputPath = directOutputPath(g_inputPath);
    if (g_outputPath == g_inputPath) {
        setStatus(L"输入与输出路径相同，请更换文件。");
        InvalidateRect(hwnd, nullptr, FALSE);
        return;
    }
    startHardwareProbe();
    g_cancel.store(false);
    g_telemetry.running.store(1, std::memory_order_relaxed);
    g_telemetry.progressPermille.store(0, std::memory_order_relaxed);
    g_telemetry.fpsTimes10.store(0, std::memory_order_relaxed);
    g_telemetry.cancelRequested.store(0, std::memory_order_relaxed);
    g_telemetry.zeroCopy.store(0, std::memory_order_relaxed);
    g_telemetry.framesDecoded.store(0, std::memory_order_relaxed);
    g_telemetry.framesEncoded.store(0, std::memory_order_relaxed);
    g_telemetry.totalFrames.store(0, std::memory_order_relaxed);
    g_telemetry.elapsedMs.store(0, std::memory_order_relaxed);
    g_telemetry.d2hBytes.store(0, std::memory_order_relaxed);
    g_telemetry.h2dBytes.store(0, std::memory_order_relaxed);
    setStatus(L"正在初始化 NVDEC / NVENC 并启动异步转码…");

    JobSnapshot job;
    job.input = g_inputPath;
    job.output = g_outputPath;
    job.codecIndex = g_codecIndex;
    job.preset = kPresets[g_presetIndex];
    try {
        std::thread(runJob, std::move(job)).detach();
    } catch (...) {
        g_telemetry.running.store(0, std::memory_order_relaxed);
        setStatus(L"无法创建转码工作线程。");
    }
    InvalidateRect(hwnd, nullptr, FALSE);
}

void cancelJob() {
    if (g_telemetry.running.load() == 0) {
        return;
    }
    g_cancel.store(true);
    g_telemetry.cancelRequested.store(1, std::memory_order_relaxed);
    setStatus(L"正在停止转码，等待当前帧完成…");
}

void setInput(const std::wstring& path) {
    g_inputPath = path;
    g_outputPath = directOutputPath(path);
    setStatus(L"已就绪。点击“开始硬件转码”启动后台任务。");
}

void updateFps() {
    static ULONGLONG lastTick = 0;
    static uint64_t lastFrames = 0;
    if (g_telemetry.running.load() == 0) {
        lastTick = 0;
        lastFrames = 0;
        return;
    }
    const ULONGLONG now = GetTickCount64();
    const uint64_t frames = g_telemetry.framesEncoded.load(std::memory_order_relaxed);
    if (lastTick == 0) {
        lastTick = now;
        lastFrames = frames;
        return;
    }
    const ULONGLONG elapsed = now - lastTick;
    if (elapsed >= 400 && frames >= lastFrames) {
        const uint32_t fpsTimes10 =
            static_cast<uint32_t>((frames - lastFrames) * 10000 / elapsed);
        g_telemetry.fpsTimes10.store(fpsTimes10, std::memory_order_relaxed);
        lastTick = now;
        lastFrames = frames;
    }
}

void showCompletedResult() {
    WorkerResult result;
    {
        std::lock_guard<std::mutex> lock(g_resultMutex);
        result = g_result;
    }
    if (result.title.empty()) {
        return;
    }
    if (result.cancelled) {
        setStatus(result.title);
    } else {
        setStatus(result.title + L" · " + result.detail);
    }
    g_dirty = true;
}

LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_CREATE: {
            g_window.store(hwnd);
            createTheme(g_forcedDpi != 0 ? g_forcedDpi : GetDpiForWindow(hwnd));
            enableDarkTitleBar(hwnd);
            DragAcceptFiles(hwnd, TRUE);
            SetTimer(hwnd, kTelemetryTimer, 16, nullptr);
            return 0;
        }
        case WM_DPICHANGED: {
            createTheme(g_forcedDpi != 0 ? g_forcedDpi : HIWORD(wParam));
            const RECT* suggested = reinterpret_cast<const RECT*>(lParam);
            SetWindowPos(hwnd, nullptr, suggested->left, suggested->top,
                         suggested->right - suggested->left,
                         suggested->bottom - suggested->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            g_dirty = true;
            return 0;
        }
        case WM_SIZE:
            ensureBackBuffer(LOWORD(lParam), HIWORD(lParam));
            g_dirty = true;
            return 0;
        case WM_GETMINMAXINFO: {
            auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
            info->ptMinTrackSize.x = S(760);
            info->ptMinTrackSize.y = S(500);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT paintStruct{};
            HDC dc = BeginPaint(hwnd, &paintStruct);
            paint(hwnd, dc);
            EndPaint(hwnd, &paintStruct);
            recordStartup(hwnd);
            return 0;
        }
        case WM_DROPFILES: {
            HDROP drop = reinterpret_cast<HDROP>(wParam);
            const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
            if (count > 0 && g_telemetry.running.load() == 0) {
                wchar_t path[32768] = {};
                if (DragQueryFileW(drop, 0, path, 32768) > 0) {
                    const DWORD attributes = GetFileAttributesW(path);
                    if (attributes != INVALID_FILE_ATTRIBUTES &&
                        (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
                        setInput(path);
                        startHardwareProbe();
                    } else {
                        setStatus(L"请拖入视频文件，而不是文件夹。");
                    }
                }
            } else if (count > 0) {
                setStatus(L"当前任务尚未结束，完成后再更换输入。");
            }
            DragFinish(drop);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        case WM_LBUTTONDOWN: {
            POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            if (g_zones.hit(g_zones.start, point)) {
                if (g_telemetry.running.load() != 0) {
                    cancelJob();
                } else {
                    startJob(hwnd);
                }
            } else if (g_telemetry.running.load() == 0 && g_zones.hit(g_zones.codec, point)) {
                g_codecIndex = (g_codecIndex + 1) % 2;
                g_dirty = true;
            } else if (g_telemetry.running.load() == 0 && g_zones.hit(g_zones.preset, point)) {
                g_presetIndex = (g_presetIndex + 1) % 3;
                g_dirty = true;
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        case WM_SETCURSOR:
            if (LOWORD(lParam) == HTCLIENT) {
                POINT point{};
                GetCursorPos(&point);
                ScreenToClient(hwnd, &point);
                if (g_zones.hit(g_zones.start, point) || g_zones.hit(g_zones.codec, point) ||
                    g_zones.hit(g_zones.preset, point)) {
                    SetCursor(LoadCursorW(nullptr, IDC_HAND));
                    return TRUE;
                }
            }
            break;
        case WM_TIMER:
            if (wParam == kTelemetryTimer) {
                updateFps();
                if (g_telemetry.running.load() != 0) {
                    InvalidateRect(hwnd, nullptr, FALSE);
                } else if (g_dirty) {
                    g_dirty = false;
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
                return 0;
            }
            if (wParam == kExitTimer) {
                KillTimer(hwnd, kExitTimer);
                if (!g_startupRecorded) {
                    g_startupRecorded = true;
                    writeStartupLog(hwnd, false);
                }
                DestroyWindow(hwnd);
                return 0;
            }
            break;
        case kMsgDone:
            showCompletedResult();
            InvalidateRect(hwnd, nullptr, FALSE);
            if (environmentFlag(L"RTX_GUI_EXIT_ON_DONE")) {
                PostMessageW(hwnd, WM_CLOSE, 0, 0);
            }
            return 0;
        case kMsgAutostart:
            if (!g_autostartPath.empty()) {
                setInput(g_autostartPath);
                startJob(hwnd);
            }
            return 0;
        case kMsgHardware:
            g_dirty = true;
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_CLOSE:
            if (g_telemetry.running.load() != 0) {
                cancelJob();
                return 0;
            }
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            KillTimer(hwnd, kTelemetryTimer);
            KillTimer(hwnd, kExitTimer);
            g_window.store(nullptr);
            g_cancel.store(true);
            destroyTheme();
            PostQuitMessage(0);
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

int attachParentConsole() {
    const HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    const DWORD outputType =
        (output != nullptr && output != INVALID_HANDLE_VALUE) ? GetFileType(output) : FILE_TYPE_UNKNOWN;
    const HANDLE error = GetStdHandle(STD_ERROR_HANDLE);
    const DWORD errorType =
        (error != nullptr && error != INVALID_HANDLE_VALUE) ? GetFileType(error) : FILE_TYPE_UNKNOWN;
    const bool outputRedirected = outputType == FILE_TYPE_PIPE || outputType == FILE_TYPE_DISK;
    const bool errorRedirected = errorType == FILE_TYPE_PIPE || errorType == FILE_TYPE_DISK;
    if (!outputRedirected || !errorRedirected) {
        if (!AttachConsole(ATTACH_PARENT_PROCESS) && !AllocConsole()) {
            return -1;
        }
        SetConsoleOutputCP(CP_UTF8);
        SetConsoleCP(CP_UTF8);
        if (!outputRedirected) {
            std::freopen("CONOUT$", "w", stdout);
        }
        if (!errorRedirected) {
            std::freopen("CONOUT$", "w", stderr);
        }
        std::freopen("CONIN$", "r", stdin);
    }
    return 0;
}

}  // namespace

extern "C" int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    g_mainEntryMs = millisecondsSinceCreation();
    g_selfTest = environmentFlag(L"RTX_GUI_SELFTEST");
    wchar_t dpiValue[16] = {};
    if (GetEnvironmentVariableW(L"RTX_GUI_FORCE_DPI", dpiValue, 16) > 0) {
        const long parsed = std::wcstol(dpiValue, nullptr, 10);
        if (parsed >= 72 && parsed <= 480) {
            g_forcedDpi = static_cast<UINT>(parsed);
        }
    }
    wchar_t autostartValue[1024] = {};
    if (GetEnvironmentVariableW(L"RTX_GUI_AUTOSTART", autostartValue, 1024) > 0) {
        g_autostartPath = autostartValue;
    }

    bool hasCliArguments = false;
    bool forceGui = false;
    int argumentCount = 0;
    std::vector<std::string> cliArguments;
    if (LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount)) {
        for (int index = 0; index < argumentCount; ++index) {
            if (index > 0 && std::wcscmp(arguments[index], L"--gui") == 0) {
                forceGui = true;
                continue;
            }
            if (index > 0) {
                hasCliArguments = true;
            }
            const int length = WideCharToMultiByte(CP_UTF8, 0, arguments[index], -1,
                                                   nullptr, 0, nullptr, nullptr);
            std::string narrow(length > 0 ? static_cast<size_t>(length) : 0, '\0');
            if (length > 1) {
                WideCharToMultiByte(CP_UTF8, 0, arguments[index], -1, narrow.data(),
                                    length, nullptr, nullptr);
                narrow.resize(static_cast<size_t>(length - 1));
            } else {
                narrow.clear();
            }
            cliArguments.push_back(std::move(narrow));
        }
        LocalFree(arguments);
    }
    if (hasCliArguments && !forceGui) {
        if (attachParentConsole() != 0) {
            return 2;
        }
        std::vector<char*> cliPointers;
        cliPointers.reserve(cliArguments.size() + 1);
        for (std::string& argument : cliArguments) {
            cliPointers.push_back(argument.data());
        }
        cliPointers.push_back(nullptr);
        const int exitCode = rtxCliMain(static_cast<int>(cliArguments.size()), cliPointers.data());
        std::fflush(stdout);
        std::fflush(stderr);
        FreeConsole();
        return exitCode;
    }

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = windowProc;
    windowClass.hInstance = instance;
    windowClass.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = nullptr;
    windowClass.lpszClassName = L"RtxTranscodeSingleExe";
    windowClass.hIconSm = windowClass.hIcon;
    if (RegisterClassExW(&windowClass) == 0) {
        return 1;
    }

    const UINT dpi = g_forcedDpi != 0 ? g_forcedDpi : GetDpiForSystem();
    RECT windowRect{0, 0, MulDiv(920, dpi, 96), MulDiv(600, dpi, 96)};
    AdjustWindowRectExForDpi(&windowRect, WS_OVERLAPPEDWINDOW, FALSE, 0, dpi);
    HWND hwnd = CreateWindowExW(WS_EX_ACCEPTFILES, windowClass.lpszClassName,
                                L"RTX 零拷贝转码引擎",
                                WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                                CW_USEDEFAULT, CW_USEDEFAULT,
                                windowRect.right - windowRect.left,
                                windowRect.bottom - windowRect.top, nullptr, nullptr,
                                instance, nullptr);
    if (hwnd == nullptr) {
        return 1;
    }
    g_windowCreatedMs = millisecondsSinceCreation();
    const int showState =
        (showCommand == SW_SHOWDEFAULT || showCommand == SW_HIDE) ? SW_SHOWNORMAL : showCommand;
    ShowWindow(hwnd, showState);
    g_updateMs = millisecondsSinceCreation();
    UpdateWindow(hwnd);
    if (!g_startupRecorded) {
        RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ERASE);
    }
    if (g_selfTest && !g_startupRecorded) {
        if (HDC dc = GetDC(hwnd)) {
            g_headlessPaint = true;
            paint(hwnd, dc);
            ReleaseDC(hwnd, dc);
            recordStartup(hwnd);
        }
    }
    if (g_selfTest) {
        UINT holdMs = 3000;
        wchar_t holdValue[16] = {};
        if (GetEnvironmentVariableW(L"RTX_GUI_SELFTEST_MS", holdValue, 16) > 0) {
            const long parsed = std::wcstol(holdValue, nullptr, 10);
            if (parsed >= 1000 && parsed <= 600000) {
                holdMs = static_cast<UINT>(parsed);
            }
        }
        SetTimer(hwnd, kExitTimer, holdMs, nullptr);
    }
    if (!g_autostartPath.empty()) {
        PostMessageW(hwnd, kMsgAutostart, 0, 0);
    }

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}
