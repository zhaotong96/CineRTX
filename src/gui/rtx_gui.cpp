// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
//
// Immediate-mode Win32 GUI for the single-exe build.  The window owns no child
// controls: every frame is composed into one off-screen bitmap and blitted in a
// single BitBlt.  The transcode worker and the message pump only share the
// cache-line aligned telemetry blocks below.
//
// Phase-3 additions (W7-3, W9-1, W9-2):
//   * every user-visible string comes from rtx::trW(); there is not one literal
//     CJK character left in this file;
//   * Ctrl+L flips the language atomically and repaints;
//   * a four-slot status bar (hardware / NGX / FRUC / telemetry) sits at the
//     bottom edge and is fed only by lock-free atomic reads;
//   * the DirectComposition viewport is created lazily and is opt-in
//     (RTX_GUI_ENABLE_VIEWPORT=1) because the shipping single-EXE build has no
//     video texture source until the NGX bridge hands one over.

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
#include "rtx/i18n.h"
#include "rtx/ngx_remaster.h"
#include "rtx/nvidia_shim.h"
#include "rtx/scheduler.h"
#include "rtx/types.h"

#if defined(RTX_HAVE_DCOMP_VIEWPORT)
#include "dcomp_viewport.h"
#endif

int rtxCliMain(int argc, char** argv);

namespace {

constexpr UINT kTelemetryTimer = 1;
constexpr UINT kExitTimer = 2;
constexpr UINT kMsgDone = WM_APP + 1;
constexpr UINT kMsgHardware = WM_APP + 2;
constexpr UINT kMsgAutostart = WM_APP + 3;
// The diagnostic probe posts this only when the plugin state actually changed,
// so a healthy system never wakes the paint path (FMEA-03).
constexpr UINT kMsgDiagnostics = WM_APP + 4;

// Phase-3 lock-free telemetry block (rtx::TelemetrySnapshot) plus the handful
// of job-state counters the window owns.
rtx::TelemetrySnapshot g_telemetry;

struct alignas(64) JobTelemetry {
    std::atomic<uint32_t> running{0};
    std::atomic<uint32_t> progressPermille{0};
    std::atomic<uint32_t> cancelRequested{0};
    std::atomic<uint32_t> zeroCopy{0};  // 0 unknown, 1 confirmed, 2 host copies
    std::atomic<uint64_t> totalFrames{0};
    std::atomic<uint64_t> elapsedMs{0};
};

static_assert(sizeof(JobTelemetry) % 64 == 0,
              "job telemetry must not share a cache line with unrelated state");

JobTelemetry g_job;
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
std::wstring g_gpuText;
std::wstring g_ngxText;
std::wstring g_frucText;
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
std::wstring g_status;
int g_presetIndex = 1;  // P5, P6, P7
constexpr int kPresets[] = {5, 6, 7};

#if defined(RTX_HAVE_DCOMP_VIEWPORT)
rtx::gui::DcompViewport g_viewport;
bool g_viewportRequested = false;
#endif

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
constexpr COLORREF kWarning = RGB(214, 168, 44);
constexpr COLORREF kStatusBg = RGB(14, 15, 16);

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
    HBRUSH statusBg = nullptr;
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
    RECT statusBar{};

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
                          &g_theme.accentDim, &g_theme.danger, &g_theme.barBg,
                          &g_theme.statusBg}) {
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
    g_theme.statusBg = CreateSolidBrush(kStatusBg);
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

// Every localised label goes through here: no std::wstring is built for a
// constant string, and an out-of-range id cannot crash the paint path.
void text(HDC dc, rtx::StrId id, RECT rect, HFONT font, COLORREF color, UINT flags) {
    text(dc, std::wstring(rtx::trW(id)), rect, font, color, flags);
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

std::wstring formatVram(uint64_t used, uint64_t total) {
    wchar_t buffer[96] = {};
    if (total > 0) {
        std::swprintf(buffer, 96, L"%.1f/%.1f GB", static_cast<double>(used) / 1073741824.0,
                      static_cast<double>(total) / 1073741824.0);
    } else {
        std::swprintf(buffer, 96, L"-");
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

bool componentPresent(const RtxDiagnostics& diagnostics, const char* needle) {
    for (uint32_t index = 0; index < diagnostics.component_count &&
                             index < RTX_MAX_COMPONENTS;
         ++index) {
        const RtxComponentInfo& component = diagnostics.components[index];
        if (component.present != 0 && std::strstr(component.name, needle) != nullptr) {
            return true;
        }
    }
    return false;
}

// The bridge is the authoritative capability source: the shim cannot see the
// feature DLLs because it never loads the proprietary host stub.  Opening the
// bridge here also keeps the cuInit(0)/DXGI ordering required by the remaster.
struct NgxProbe {
    bool ready = false;
    std::wstring text;
};

NgxProbe probeNgx() {
    rtx::NgxRemasterBridge bridge;
    std::string error;
    if (!bridge.open(&error)) {
        return NgxProbe{false, L"NGX: " + utf8ToWide(error)};
    }
    RtxNgxBridgeCaps caps{};
    const RtxResult result = bridge.queryCaps(&caps);
    if (result != RTX_OK) {
        return NgxProbe{false, L"NGX: query_caps error " + std::to_wstring(result)};
    }
    if (caps.vsr_available != 0 && caps.truehdr_available != 0) {
        return NgxProbe{true, L"NGX: VSR + TrueHDR Ready"};
    }
    std::wstring detail = utf8ToWide(caps.diagnostics);
    return NgxProbe{false, detail.empty() ? L"NGX: unavailable" : L"NGX: " + detail};
}

// Background health probe.  It writes the three status-bar texts under a short
// mutex and posts kMsgDiagnostics only when something changed, so a healthy
// system costs one paint per state transition rather than one per second.
void startHardwareProbe() {
    if (g_hardwareProbeStarted) {
        return;
    }
    g_hardwareProbeStarted = true;
    std::thread([]() {
        std::wstring hardware;
        std::wstring fruc = L"FRUC: unavailable";
        bool changed = false;

        // Load and query the bridge before shim.probe() calls cuInit(0), exactly
        // as the remaster path does.
        const NgxProbe ngx_probe = probeNgx();
        const std::wstring ngx = ngx_probe.text;
        g_telemetry.ngx_ready.store(ngx_probe.ready ? 1 : 0, std::memory_order_relaxed);

        rtx::NvidiaShim shim;
        std::string error;
        if (!shim.open(&error)) {
            hardware = std::wstring(rtx::trW(rtx::StrId::StatusShimMissing)) + utf8ToWide(error);
        } else {
            RtxDiagnostics diagnostics{};
            if (shim.probe(&diagnostics) == RTX_OK && diagnostics.device.name[0] != '\0') {
                wchar_t buffer[256] = {};
                std::swprintf(buffer, 256, L"%ls · SM %d.%d · %u.%u · D3D12/CUDA OK",
                              utf8ToWide(diagnostics.device.name).c_str(),
                              diagnostics.device.compute_major,
                              diagnostics.device.compute_minor,
                              diagnostics.device.driver_version / 100,
                              diagnostics.device.driver_version % 100);
                hardware = buffer;
                g_telemetry.hardware_ready.store(1, std::memory_order_relaxed);
                g_telemetry.vram_total_bytes.store(diagnostics.device.vram_bytes,
                                                   std::memory_order_relaxed);
            } else {
                hardware = std::wstring(rtx::trW(rtx::StrId::StatusGpuMissing)) +
                           utf8ToWide(diagnostics.summary);
            }
            const bool ofa = componentPresent(diagnostics, "NvOFFRUC");
            const bool rife = componentPresent(diagnostics, "TensorRT");
            if (ofa && rife) {
                fruc = L"FRUC: OFA + RIFE present (execution pending)";
            } else if (ofa) {
                fruc = L"FRUC: OFA present (execution pending)";
            } else if (rife) {
                fruc = L"FRUC: RIFE runtime present (execution pending)";
            }
            g_telemetry.active_fruc_mode.store(ofa ? 1 : (rife ? 2 : 0),
                                               std::memory_order_relaxed);
        }

        {
            std::lock_guard<std::mutex> lock(g_gpuMutex);
            changed = hardware != g_gpuText || ngx != g_ngxText || fruc != g_frucText;
            g_gpuText = hardware;
            g_ngxText = ngx;
            g_frucText = fruc;
        }
        // Debugger-only trace: the status bar ellipsizes long diagnostics, so
        // the full loader/plugin paths belong in the VS Output window.
        OutputDebugStringW((L"[CineRTX] " + hardware + L" | " + ngx + L" | " + fruc + L"\n")
                               .c_str());
        if (changed) {
            if (HWND window = g_window.load()) {
                PostMessageW(window, kMsgDiagnostics, 0, 0);
            }
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

Zones computeLayout(int width, int height, int* headerBottom) {
    const int margin = S(24);
    const int statusHeight = S(24);
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

    zones.statusBar = RECT{0, height - statusHeight, width, height};
    return zones;
}

// One status-bar cell: a muted caption above a high-contrast value.
void drawStatusSlot(HDC dc, const RECT& cell, rtx::StrId caption, const std::wstring& value,
                    COLORREF valueColor) {
    RECT captionRect{cell.left + S(10), cell.top + S(3), cell.right - S(6), cell.top + S(12)};
    RECT valueRect{cell.left + S(10), cell.top + S(11), cell.right - S(6), cell.bottom - S(2)};
    text(dc, caption, captionRect, g_theme.small, kMuted,
         DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
    text(dc, value, valueRect, g_theme.small, valueColor,
         DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
}

void drawStatusBar(HDC dc, const RECT& bar) {
    fillRect(dc, bar, g_theme.statusBg);
    const int width = bar.right - bar.left;
    const int cellWidth = width / 4;

    std::wstring hardware;
    std::wstring ngx;
    std::wstring fruc;
    {
        std::lock_guard<std::mutex> lock(g_gpuMutex);
        hardware = g_gpuText.empty() ? std::wstring(rtx::trW(rtx::StrId::StatusGpuProbing))
                                     : g_gpuText;
        ngx = g_ngxText;
        fruc = g_frucText;
    }

    const uint64_t d2h = g_telemetry.device_to_host_bytes.load(std::memory_order_relaxed);
    const uint64_t vram_used = g_telemetry.vram_used_bytes.load(std::memory_order_relaxed);
    const uint64_t vram_total = g_telemetry.vram_total_bytes.load(std::memory_order_relaxed);
    const uint32_t fps = g_telemetry.ewma_fps_x10.load(std::memory_order_relaxed);
    const uint8_t tier = g_telemetry.watermark_tier.load(std::memory_order_relaxed);

    wchar_t telemetry[192] = {};
    std::swprintf(telemetry, 192, L"D2H: %llu B · VRAM: %ls (%ls) · %ls",
                  static_cast<unsigned long long>(d2h), formatVram(vram_used, vram_total).c_str(),
                  utf8ToWide(rtx::watermarkTierName(tier)).c_str(), formatFps(fps).c_str());

    RECT cell{bar.left, bar.top, bar.left + cellWidth, bar.bottom};
    drawStatusSlot(dc, cell, rtx::StrId::StatusHardware, hardware, kText);
    cell = RECT{cell.right, bar.top, cell.right + cellWidth, bar.bottom};
    drawStatusSlot(dc, cell, rtx::StrId::StatusNgxPlugins,
                   ngx.empty() ? std::wstring(L"-") : ngx,
                   g_telemetry.ngx_ready.load(std::memory_order_relaxed) != 0 ? kText : kWarning);
    cell = RECT{cell.right, bar.top, cell.right + cellWidth, bar.bottom};
    drawStatusSlot(dc, cell, rtx::StrId::StatusFrucEngine, fruc.empty() ? std::wstring(L"-") : fruc,
                   kText);
    cell = RECT{cell.right, bar.top, bar.right, bar.bottom};
    drawStatusSlot(dc, cell, rtx::StrId::StatusVramWatermark, telemetry,
                   tier == 2 ? kDanger : (tier == 1 ? kWarning : kAccent));

    // 1 px top border and vertical separators.
    HPEN pen = CreatePen(PS_SOLID, 1, kBorder);
    HGDIOBJ old = SelectObject(dc, pen);
    MoveToEx(dc, bar.left, bar.top, nullptr);
    LineTo(dc, bar.right, bar.top);
    for (int index = 1; index < 4; ++index) {
        const int x = bar.left + cellWidth * index;
        MoveToEx(dc, x, bar.top + S(4), nullptr);
        LineTo(dc, x, bar.bottom - S(4));
    }
    SelectObject(dc, old);
    DeleteObject(pen);
}

void drawFrame(HDC dc, int width, int height) {
    const RECT full{0, 0, width, height};
    fillRect(dc, full, g_theme.bg);

    int headerBottom = 0;
    const Zones zones = computeLayout(width, height, &headerBottom);
    g_zones = zones;
    const int margin = S(24);

    RECT accent{margin, margin + S(4), margin + S(4), margin + S(48)};
    fillRect(dc, accent, g_theme.accent);
    text(dc, rtx::StrId::AppTitle,
         RECT{margin + S(16), margin - S(2), width - margin, margin + S(34)},
         g_theme.title, kText, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    std::wstring gpu;
    {
        std::lock_guard<std::mutex> lock(g_gpuMutex);
        gpu = g_gpuText.empty() ? std::wstring(rtx::trW(rtx::StrId::StatusGpuProbing)) : g_gpuText;
    }
    text(dc, gpu, RECT{margin + S(17), margin + S(32), width - margin, headerBottom},
         g_theme.small, kMuted, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
    text(dc, rtx::StrId::AppSubtitle,
         RECT{width - S(300), margin, width - margin, margin + S(24)}, g_theme.small,
         kMuted, DT_RIGHT | DT_SINGLELINE | DT_VCENTER);

    card(dc, zones.drop, S(10), g_theme.card);
    text(dc, rtx::StrId::DropTitle,
         RECT{zones.drop.left + S(16), zones.drop.top + S(9), zones.drop.right - S(16),
              zones.drop.top + S(28)},
         g_theme.small, kAccent, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    if (g_inputPath.empty()) {
        text(dc, rtx::StrId::DropHint,
             RECT{zones.drop.left + S(16), zones.drop.top + S(30), zones.drop.right - S(16),
                  zones.drop.top + S(60)},
             g_theme.section, kText, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        text(dc, rtx::StrId::DropSupported,
             RECT{zones.drop.left + S(16), zones.drop.top + S(59), zones.drop.right - S(16),
                  zones.drop.top + S(82)},
             g_theme.small, kMuted, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    } else {
        text(dc, g_inputPath,
             RECT{zones.drop.left + S(16), zones.drop.top + S(29), zones.drop.right - S(16),
                  zones.drop.top + S(53)},
             g_theme.body, kText, DT_PATH_ELLIPSIS | DT_SINGLELINE | DT_VCENTER);
        text(dc, std::wstring(rtx::trW(rtx::StrId::DropOutputPrefix)) + L"  " +
                    fileName(g_outputPath),
             RECT{zones.drop.left + S(16), zones.drop.top + S(55), zones.drop.right - S(16),
                  zones.drop.top + S(79)},
             g_theme.small, kMuted, DT_END_ELLIPSIS | DT_SINGLELINE | DT_VCENTER);
    }

    card(dc, zones.codec, S(9), g_theme.card2);
    text(dc, std::wstring(rtx::trW(rtx::StrId::CodecLabelHevc)) + L" · 4K HDR10",
         zones.codec, g_theme.body, kText, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
    card(dc, zones.preset, S(9), g_theme.card2);
    wchar_t presetLabel[64] = {};
    std::swprintf(presetLabel, 64, L"%ls  P%d", rtx::trW(rtx::StrId::PresetLabel),
                  kPresets[g_presetIndex]);
    text(dc, presetLabel, zones.preset, g_theme.body, kText,
         DT_CENTER | DT_SINGLELINE | DT_VCENTER);

    const bool running = g_job.running.load(std::memory_order_relaxed) != 0;
    fillRect(dc, zones.start, running ? g_theme.danger : g_theme.accent);
    text(dc, running ? rtx::StrId::ButtonStop
                     : (g_inputPath.empty() ? rtx::StrId::ButtonDropFirst
                                            : rtx::StrId::ButtonStart),
         zones.start, g_theme.section, RGB(255, 255, 255),
         DT_CENTER | DT_SINGLELINE | DT_VCENTER);

    const int top = zones.start.bottom + S(16);
    const RECT progressCard{margin, top, width - margin, zones.statusBar.top - S(8)};
    if (progressCard.bottom > progressCard.top + S(80)) {
        card(dc, progressCard, S(10), g_theme.card);
        text(dc, rtx::StrId::TelemetryTitle,
             RECT{progressCard.left + S(16), progressCard.top + S(9),
                  progressCard.right - S(16), progressCard.top + S(31)},
             g_theme.section, kText, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        text(dc, rtx::StrId::TelemetrySubtitle,
             RECT{progressCard.left + S(200), progressCard.top + S(9),
                  progressCard.right - S(16), progressCard.top + S(31)},
             g_theme.small, kMuted, DT_RIGHT | DT_SINGLELINE | DT_VCENTER);

        const RECT barBg{progressCard.left + S(16), progressCard.top + S(43),
                         progressCard.right - S(16), progressCard.top + S(53)};
        card(dc, barBg, S(5), g_theme.barBg);
        const uint32_t permille = g_job.progressPermille.load(std::memory_order_relaxed);
        const uint64_t total = g_job.totalFrames.load(std::memory_order_relaxed);
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

        wchar_t progressLabel[96] = {};
        if (total > 0) {
            std::swprintf(progressLabel, 96, L"%ls  %.1f%%",
                          rtx::trW(rtx::StrId::ProgressLabel),
                          static_cast<double>(permille) / 10.0);
        } else {
            std::swprintf(progressLabel, 96, L"%ls  %ls", rtx::trW(rtx::StrId::ProgressLabel),
                          rtx::trW(running ? rtx::StrId::ProgressWorking
                                           : rtx::StrId::ProgressIdle));
        }
        text(dc, progressLabel,
             RECT{progressCard.left + S(16), progressCard.top + S(57),
                  progressCard.right - S(16), progressCard.top + S(77)},
             g_theme.small, kMuted, DT_LEFT | DT_SINGLELINE | DT_VCENTER);

        const uint64_t encoded = g_telemetry.frames_encoded.load(std::memory_order_relaxed);
        const uint64_t elapsed = g_job.elapsedMs.load(std::memory_order_relaxed);
        const uint32_t fps = g_telemetry.ewma_fps_x10.load(std::memory_order_relaxed);
        const uint32_t zeroCopy = g_job.zeroCopy.load(std::memory_order_relaxed);
        const int column = (progressCard.right - progressCard.left - S(48)) / 4;
        const int statTop = progressCard.top + S(84);
        const rtx::StrId labels[4] = {rtx::StrId::StatThroughput, rtx::StrId::StatFrames,
                                      rtx::StrId::StatElapsed, rtx::StrId::StatZeroCopy};
        std::wstring values[4];
        values[0] = running || fps > 0 ? formatFps(fps) : std::wstring(rtx::trW(rtx::StrId::ValueDash));
        values[1] = formatCount(encoded, total);
        values[2] = elapsed > 0 ? formatSeconds(elapsed)
                                : std::wstring(rtx::trW(rtx::StrId::ValueDash));
        if (zeroCopy == 1) {
            values[3] = rtx::trW(rtx::StrId::ValueZeroCopyClean);
        } else if (zeroCopy == 2) {
            values[3] = rtx::trW(rtx::StrId::ValueHostCopies);
        } else {
            values[3] = running ? rtx::trW(rtx::StrId::ValueRunning)
                                : rtx::trW(rtx::StrId::ValueDash);
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

    drawStatusBar(dc, zones.statusBar);
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
    // The GUI drives the same single product chain as the CLI: 4K HDR10 HEVC
    // Main10 with VSR + TrueHDR always enabled.  There is no passthrough or
    // codec choice to keep the two front ends functionally identical.
    options.codec = rtx::VideoCodec::HEVC;
    options.target_width = 3840;
    options.target_height = 2160;
    options.vsr_quality = 2;
    options.bit_depth = 10;
    options.preset = job.preset;
    options.hdr = true;
    options.cancel = &g_cancel;
    options.progress = [](const rtx::HwTranscodeProgress& progress) {
        g_telemetry.frames_decoded.store(progress.frames_decoded, std::memory_order_relaxed);
        g_telemetry.frames_encoded.store(progress.frames_encoded, std::memory_order_relaxed);
        g_job.totalFrames.store(progress.total_frames, std::memory_order_relaxed);
        g_job.elapsedMs.store(static_cast<uint64_t>(progress.elapsed_ms),
                              std::memory_order_relaxed);
        if (progress.total_frames > 0) {
            const uint64_t permille =
                static_cast<uint64_t>(progress.frames_decoded) * 1000 /
                static_cast<uint64_t>(progress.total_frames);
            g_job.progressPermille.store(
                static_cast<uint32_t>(permille > 1000 ? 1000 : permille),
                std::memory_order_relaxed);
        }
    };

    const auto result = rtx::runNvidiaRemasterTranscode(options);
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
        std::swprintf(title, 256, L"%ls · %lld · %.1f FPS · %ls",
                      rtx::trW(rtx::StrId::StatusDonePrefix),
                      static_cast<long long>(report.frames_encoded), fps,
                      report.zero_copy_main_loop ? rtx::trW(rtx::StrId::ValueZeroCopyClean)
                                                 : rtx::trW(rtx::StrId::ValueHostCopies));
        completed.title = title;
        completed.detail = std::wstring(rtx::trW(rtx::StrId::DropOutputPrefix)) + L" " + job.output;
        g_job.progressPermille.store(1000, std::memory_order_relaxed);
        g_job.zeroCopy.store(report.zero_copy_main_loop ? 1 : 2, std::memory_order_relaxed);
        g_telemetry.device_to_host_bytes.store(report.decoder_device_to_host_bytes,
                                               std::memory_order_relaxed);
        g_telemetry.ewma_fps_x10.store(static_cast<uint32_t>(fps * 10.0),
                                       std::memory_order_relaxed);
    } else if (result.code() == rtx::ErrorCode::State && g_cancel.load()) {
        completed.cancelled = true;
        completed.title = rtx::trW(rtx::StrId::StatusCancelled);
        completed.detail = job.input;
    } else {
        completed.title = rtx::trW(rtx::StrId::StatusFailed);
        completed.detail = utf8ToWide(result.error().message);
        if (!result.error().detail.empty()) {
            completed.detail += L"：" + utf8ToWide(result.error().detail);
        }
        completed.detail += L"（" + job.output + L"）";
    }

    {
        std::lock_guard<std::mutex> lock(g_resultMutex);
        g_result = completed;
    }
    writeResultLog(completed, job);
    g_job.running.store(0, std::memory_order_relaxed);
    if (HWND window = g_window.load()) {
        PostMessageW(window, kMsgDone, 0, 0);
    }
}

void startJob(HWND hwnd) {
    if (g_job.running.load() != 0) {
        return;
    }
    if (g_inputPath.empty()) {
        setStatus(rtx::trW(rtx::StrId::DropHint));
        InvalidateRect(hwnd, nullptr, FALSE);
        return;
    }
    g_outputPath = directOutputPath(g_inputPath);
    if (g_outputPath == g_inputPath) {
        setStatus(rtx::trW(rtx::StrId::StatusSamePath));
        InvalidateRect(hwnd, nullptr, FALSE);
        return;
    }
    startHardwareProbe();
    g_cancel.store(false);
    g_job.running.store(1, std::memory_order_relaxed);
    g_job.progressPermille.store(0, std::memory_order_relaxed);
    g_job.cancelRequested.store(0, std::memory_order_relaxed);
    g_job.zeroCopy.store(0, std::memory_order_relaxed);
    g_job.totalFrames.store(0, std::memory_order_relaxed);
    g_job.elapsedMs.store(0, std::memory_order_relaxed);
    g_telemetry.reset();
    g_telemetry.frames_decoded.store(0, std::memory_order_relaxed);
    g_telemetry.frames_encoded.store(0, std::memory_order_relaxed);
    g_telemetry.ewma_fps_x10.store(0, std::memory_order_relaxed);
    g_telemetry.device_to_host_bytes.store(0, std::memory_order_relaxed);
    setStatus(rtx::trW(rtx::StrId::StatusWorking));

    JobSnapshot job;
    job.input = g_inputPath;
    job.output = g_outputPath;
    job.preset = kPresets[g_presetIndex];
    try {
        std::thread(runJob, std::move(job)).detach();
    } catch (...) {
        g_job.running.store(0, std::memory_order_relaxed);
        setStatus(rtx::trW(rtx::StrId::StatusNoThread));
    }
    InvalidateRect(hwnd, nullptr, FALSE);
}

void cancelJob() {
    if (g_job.running.load() == 0) {
        return;
    }
    g_cancel.store(true);
    g_job.cancelRequested.store(1, std::memory_order_relaxed);
    setStatus(rtx::trW(rtx::StrId::StatusStopping));
}

void setInput(const std::wstring& path) {
    g_inputPath = path;
    g_outputPath = directOutputPath(path);
    setStatus(rtx::trW(rtx::StrId::StatusReady));
}

void updateFps() {
    static ULONGLONG lastTick = 0;
    static uint64_t lastFrames = 0;
    if (g_job.running.load() == 0) {
        lastTick = 0;
        lastFrames = 0;
        return;
    }
    const ULONGLONG now = GetTickCount64();
    const uint64_t frames = g_telemetry.frames_encoded.load(std::memory_order_relaxed);
    if (lastTick == 0) {
        lastTick = now;
        lastFrames = frames;
        return;
    }
    const ULONGLONG elapsed = now - lastTick;
    if (elapsed >= 400 && frames >= lastFrames) {
        const uint32_t fpsTimes10 =
            static_cast<uint32_t>((frames - lastFrames) * 10000 / elapsed);
        g_telemetry.ewma_fps_x10.store(fpsTimes10, std::memory_order_relaxed);
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

void toggleLanguage(HWND hwnd) {
    rtx::toggleLanguage();
    // A language switch must not restart the job: only the paint path re-reads
    // the tables, and the table index is an atomic byte.
    g_dirty = true;
    InvalidateRect(hwnd, nullptr, FALSE);
}

LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_CREATE: {
            g_window.store(hwnd);
            createTheme(g_forcedDpi != 0 ? g_forcedDpi : GetDpiForWindow(hwnd));
            enableDarkTitleBar(hwnd);
            DragAcceptFiles(hwnd, TRUE);
            g_status = rtx::trW(rtx::StrId::StatusReady);
            SetTimer(hwnd, kTelemetryTimer, 16, nullptr);
            // Probe as soon as the window exists: the status bar must report
            // the real GPU/NGX state on launch, not only after the first job.
            startHardwareProbe();
#if defined(RTX_HAVE_DCOMP_VIEWPORT)
            if (g_viewportRequested) {
                RECT client{};
                GetClientRect(hwnd, &client);
                std::string error;
                if (!g_viewport.create(hwnd, static_cast<uint32_t>(client.right - client.left),
                                       static_cast<uint32_t>(client.bottom - client.top),
                                       &error)) {
                    g_status = utf8ToWide(error);
                }
            }
#endif
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
        case WM_SIZE: {
            ensureBackBuffer(LOWORD(lParam), HIWORD(lParam));
            g_dirty = true;
#if defined(RTX_HAVE_DCOMP_VIEWPORT)
            if (g_viewport.valid()) {
                std::string error;
                (void)g_viewport.resize(LOWORD(lParam), HIWORD(lParam), &error);
            }
#endif
            return 0;
        }
        case WM_GETMINMAXINFO: {
            auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
            info->ptMinTrackSize.x = S(760);
            info->ptMinTrackSize.y = S(520);
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
            if (count > 0 && g_job.running.load() == 0) {
                wchar_t path[32768] = {};
                if (DragQueryFileW(drop, 0, path, 32768) > 0) {
                    const DWORD attributes = GetFileAttributesW(path);
                    if (attributes != INVALID_FILE_ATTRIBUTES &&
                        (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
                        setInput(path);
                        startHardwareProbe();
                    } else {
                        setStatus(rtx::trW(rtx::StrId::StatusDropFileOnly));
                    }
                }
            } else if (count > 0) {
                setStatus(rtx::trW(rtx::StrId::StatusBusy));
            }
            DragFinish(drop);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        case WM_LBUTTONDOWN: {
            POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            if (g_zones.hit(g_zones.start, point)) {
                if (g_job.running.load() != 0) {
                    cancelJob();
                } else {
                    startJob(hwnd);
                }
            } else if (g_job.running.load() == 0 && g_zones.hit(g_zones.preset, point)) {
                g_presetIndex = (g_presetIndex + 1) % 3;
                g_dirty = true;
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        case WM_MOUSEMOVE: {
#if defined(RTX_HAVE_DCOMP_VIEWPORT)
            if (g_viewport.valid()) {
                (void)g_viewport.setCursor(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam),
                                           g_viewport.clientRect().right);
            }
#endif
            return 0;
        }
        case WM_KEYDOWN: {
            // Ctrl+L: atomic language flip, no restart, no re-layout.
            if (wParam == 'L' && (GetKeyState(VK_CONTROL) & 0x8000) != 0) {
                toggleLanguage(hwnd);
                return 0;
            }
            break;
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
                if (g_job.running.load() != 0) {
                    InvalidateRect(hwnd, nullptr, FALSE);
                } else if (g_dirty) {
                    g_dirty = false;
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
#if defined(RTX_HAVE_DCOMP_VIEWPORT)
                if (g_viewport.valid()) {
                    // The throttle decides; the window does not second-guess it.
                    const bool occluded = IsIconic(hwnd) == FALSE && IsWindowVisible(hwnd) == FALSE;
                    (void)g_viewport.present(static_cast<double>(GetTickCount64()) / 1000.0,
                                             occluded, IsIconic(hwnd) != FALSE);
                }
#endif
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
        case kMsgDiagnostics:
            g_dirty = true;
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_CLOSE:
            if (g_job.running.load() != 0) {
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
#if defined(RTX_HAVE_DCOMP_VIEWPORT)
            g_viewport.destroy();
#endif
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
#if defined(RTX_HAVE_DCOMP_VIEWPORT)
    g_viewportRequested = environmentFlag(L"RTX_GUI_ENABLE_VIEWPORT");
#endif
    g_gpuText = rtx::trW(rtx::StrId::StatusGpuProbing);

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
    RECT windowRect{0, 0, MulDiv(920, dpi, 96), MulDiv(620, dpi, 96)};
    AdjustWindowRectExForDpi(&windowRect, WS_OVERLAPPEDWINDOW, FALSE, 0, dpi);
    HWND hwnd = CreateWindowExW(WS_EX_ACCEPTFILES, windowClass.lpszClassName,
                                rtx::trW(rtx::StrId::AppTitle),
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
