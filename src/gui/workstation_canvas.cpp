// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
//
// Stage 2 (Track B) workstation canvas implementation.  See
// workstation_canvas.h for the design contract.  All painting is plain GDI
// (no dcomp.dll, no themes beyond the dark-mode qualifiers the GUI target
// already links); the four-track timeline is a GDI DIB double-buffered child
// window so drag repainting never flickers.

#include "workstation_canvas.h"

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windowsx.h>
#include <commctrl.h>
#include <uxtheme.h>

#include "rtx/i18n.h"
#include "rtx/io.h"
#include "rtx/logger.h"

#include "shim_probe.h"

#include <algorithm>
#include <cwchar>
#include <utility>

namespace rtx::gui {
namespace {

constexpr int kTrackCount = 4;
constexpr wchar_t kTrackLabels[kTrackCount][4] = {L"V2", L"V1", L"A1", L"S1"};

constexpr wchar_t kNleTimelineClass[] = L"RTXNleTimelineWnd";

struct NleWindowState {
    NleTimeline* timeline = nullptr;
    HFONT font = nullptr;
    bool dragging = false;
};

NleWindowState* nleState(HWND hwnd) {
    return reinterpret_cast<NleWindowState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

void fillRect(HDC hdc, const RECT& rc, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(hdc, &rc, brush);
    DeleteObject(brush);
}

void frameRect(HDC hdc, const RECT& rc, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    FrameRect(hdc, &rc, brush);
    DeleteObject(brush);
}

void drawTextDim(HDC hdc, const RECT& rc, const wchar_t* text, COLORREF color, HFONT font,
                 UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE) {
    HFONT old = (font != nullptr) ? (HFONT)SelectObject(hdc, font) : nullptr;
    SetTextColor(hdc, color);
    SetBkMode(hdc, TRANSPARENT);
    DrawTextW(hdc, text, -1, const_cast<LPRECT>(&rc), flags);
    if (old != nullptr) {
        SelectObject(hdc, old);
    }
}

// Parallelogram clip-path brand mark, mirroring webui/app.css .brand-mark
// polygon(0 0, 100% 0, 82% 100%, 0 100%).
void drawBrandMark(HDC hdc, int x, int y, int w, int h) {
    POINT pts[4] = {
        {x, y},
        {x + w, y},
        {x + w - (h / 3), y + h},
        {x, y + h},
    };
    HBRUSH brush = CreateSolidBrush(kClrNvGreen);
    HBRUSH old = (HBRUSH)SelectObject(hdc, brush);
    SetPolyFillMode(hdc, WINDING);
    Polygon(hdc, pts, 4);
    SelectObject(hdc, old);
    DeleteObject(brush);
}

int timelineRulerHeight(int dpi) { return MulDiv(24, dpi, 96); }
int timelineLaneHeight(int dpi) { return MulDiv(44, dpi, 96); }

RECT laneRect(const RECT& area, int track, int dpi) {
    const int ruler = timelineRulerHeight(dpi);
    const int lane = timelineLaneHeight(dpi);
    RECT rc{area.left, area.top + ruler + track * lane, area.right,
            area.top + ruler + (track + 1) * lane};
    return rc;
}

int64_t timelineXToMs(const RECT& area, const NleTimeline& state, int x, int dpi) {
    const int width = area.right - area.left - MulDiv(56, dpi, 96);
    if (width <= 0 || state.duration_ms <= 0) {
        return 0;
    }
    const int64_t offset = MulDiv(56, dpi, 96);
    int64_t pos = (static_cast<int64_t>(x - area.left - offset) * state.duration_ms) / width;
    return std::clamp(pos, int64_t{0}, state.duration_ms);
}

int timelineMsToX(const RECT& area, const NleTimeline& state, int64_t ms, int dpi) {
    const int width = area.right - area.left - MulDiv(56, dpi, 96);
    if (width <= 0 || state.duration_ms <= 0) {
        return area.left + MulDiv(56, dpi, 96);
    }
    const int64_t offset = MulDiv(56, dpi, 96);
    return static_cast<int>(area.left + offset + ms * width / state.duration_ms);
}

LRESULT CALLBACK nleTimelineProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    NleWindowState* state = nleState(hwnd);
    switch (msg) {
        case WM_CREATE: {
            auto* owned = new NleWindowState{};
            owned->timeline = &sharedNleTimeline();
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(owned));
            return 0;
        }
        case WM_SETFONT:
            if (state != nullptr) {
                state->font = reinterpret_cast<HFONT>(wParam);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        case WM_GETFONT:
            return reinterpret_cast<LRESULT>(state != nullptr ? state->font : nullptr);
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            RECT rc{};
            GetClientRect(hwnd, &rc);
            PAINTSTRUCT ps{};
            HDC hdc = BeginPaint(hwnd, &ps);
            paintNleTimeline(hwnd, rc, state != nullptr ? *state->timeline : sharedNleTimeline(),
                             state != nullptr ? state->font : nullptr);
            (void)hdc;
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_LBUTTONDOWN:
            if (state != nullptr) {
                state->dragging = true;
                SetCapture(hwnd);
                RECT rc{};
                GetClientRect(hwnd, &rc);
                const int dpi = GetDpiForWindow(hwnd);
                state->timeline->playhead_ms =
                    timelineXToMs(rc, *state->timeline, GET_X_LPARAM(lParam), dpi);
                SendMessageW(GetParent(hwnd), WM_APP_TIMELINE_SEEK, 0,
                             static_cast<LPARAM>(state->timeline->playhead_ms));
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        case WM_MOUSEMOVE:
            if (state != nullptr && state->dragging) {
                RECT rc{};
                GetClientRect(hwnd, &rc);
                const int dpi = GetDpiForWindow(hwnd);
                state->timeline->playhead_ms =
                    timelineXToMs(rc, *state->timeline, GET_X_LPARAM(lParam), dpi);
                SendMessageW(GetParent(hwnd), WM_APP_TIMELINE_SEEK, 0,
                             static_cast<LPARAM>(state->timeline->playhead_ms));
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        case WM_LBUTTONUP:
            if (state != nullptr) {
                state->dragging = false;
                ReleaseCapture();
            }
            return 0;
        case WM_DESTROY:
            delete state;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            return 0;
        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

}  // namespace

const wchar_t* chromeText(ChromeText id) noexcept {
    // Pure single-language chrome (RW-6 fix): every entry follows
    // rtx::currentLanguage() so Ctrl+L / the EN-中文 button flips the whole
    // workstation instantly.  No zh+en mixed strings.
    const bool zh = (rtx::currentLanguage() == rtx::Language::Zh);
    switch (id) {
        case ChromeText::WsEncoder: return zh ? L"1 · 转码器" : L"1 · Encoder";
        case ChromeText::WsPlayer: return zh ? L"2 · 播放器" : L"2 · Player";
        case ChromeText::WsNle: return zh ? L"3 · 剪辑台" : L"3 · NLE";
        case ChromeText::PresetsTitle: return zh ? L"工厂预设" : L"Golden Presets";
        case ChromeText::PresetMaster: return zh ? L"大师版 Master" : L"Master Preset";
        case ChromeText::PresetRife: return zh ? L"深度补帧 RIFE" : L"RIFE 60fps";
        case ChromeText::PresetEsports: return zh ? L"电竞 120 FPS" : L"Esports 120";
        case ChromeText::PresetCompat: return zh ? L"兼容 HEVC" : L"HEVC Compat";
        case ChromeText::PresetNative: return zh ? L"原生直通" : L"Native Pass";
        case ChromeText::QueueTitle: return zh ? L"批量转码队列" : L"Batch Queue";
        case ChromeText::QueueAdd: return zh ? L"+ 添加文件" : L"+ Add File";
        case ChromeText::QueueRemove: return zh ? L"- 移除选中" : L"- Remove";
        case ChromeText::QueueClear: return zh ? L"清空队列" : L"Clear Queue";
        case ChromeText::BinTitle: return zh ? L"素材箱" : L"Media Bin";
        case ChromeText::MarkIn: return zh ? L"I 标记入点" : L"Mark In (I)";
        case ChromeText::MarkOut: return zh ? L"O 标记出点" : L"Mark Out (O)";
        case ChromeText::Razor: return zh ? L"C 剃刀分割" : L"Razor (C)";
        case ChromeText::StepBack: return L"<< -1f";
        case ChromeText::StepFwd: return L"+1f >>";
        case ChromeText::SendEncoder: return zh ? L"🚀 导出序列至转码器" : L"🚀 Export to Encoder";
        case ChromeText::Probe: return zh ? L"硬件自检" : L"Probe GPU";
        case ChromeText::LangButton: return zh ? L"English" : L"中文";
        case ChromeText::Play: return zh ? L"▶ 播放" : L"▶ Play";
        case ChromeText::Pause: return zh ? L"⏸ 暂停" : L"⏸ Pause";
        case ChromeText::FullscreenHint: return zh ? L"全屏: F11 / 双击 / Esc 退出"
                                                   : L"Fullscreen: F11 / double-click / Esc to exit";
        case ChromeText::TimelineTitle:
            return zh ? L"多轨时间轴 (V2/V1/A1/S1 · Flicks 705,600,000 Hz)"
                      : L"Timeline (V2/V1/A1/S1 · Flicks 705,600,000 Hz)";
        case ChromeText::Subtitle: return L"NVIDIA RTX Studio Native Workstation · C++26 SSOT";
        default: return L"";
    }
}

// Quick-play playback state (RW-4) + its owner-draw caption (RW-6).
bool g_guiPlaying = false;

void setGuiPlaying(bool playing) noexcept { g_guiPlaying = playing; }
bool guiIsPlaying() noexcept { return g_guiPlaying; }

const wchar_t* playbackCaption() noexcept {
    return chromeText(g_guiPlaying ? ChromeText::Pause : ChromeText::Play);
}

bool applyGoldenPreset(std::string_view preset_id, rtx::TranscodeProfile& profile) {
    // Rebased from the spec's golden table: every preset keeps the 10-bit HDR
    // pipeline and the SSOT defaults for anything it does not override.
    if (preset_id == "master") {
        profile.codec = "av1";
        profile.preset = 5;
        profile.resolution = "4k";
        profile.vsr_enabled = true;
        profile.vsr_quality = 4;
        profile.fps_mode = "60-nvofa";
        profile.peak_nits = 1000;
        return true;
    }
    if (preset_id == "rife") {
        profile.codec = "av1";
        profile.preset = 6;
        profile.resolution = "4k";
        profile.vsr_quality = 4;
        profile.fps_mode = "60-rife";
        return true;
    }
    if (preset_id == "esports") {
        profile.codec = "av1";
        profile.preset = 4;
        profile.resolution = "4k";
        profile.vsr_quality = 4;
        profile.fps_mode = "120-nvofa";
        return true;
    }
    if (preset_id == "compat") {
        profile.codec = "hevc";
        profile.preset = 5;
        profile.resolution = "2k";
        profile.vsr_quality = 3;
        profile.fps_mode = "60-nvofa";
        return true;
    }
    if (preset_id == "native") {
        profile.codec = "av1";
        profile.preset = 6;
        profile.resolution = "native";
        profile.vsr_enabled = false;
        profile.vsr_quality = 4;
        profile.fps_mode = "original";
        return true;
    }
    return false;
}

std::vector<std::pair<std::string, ChromeText>> goldenPresetList() {
    return {
        {"master", ChromeText::PresetMaster},
        {"rife", ChromeText::PresetRife},
        {"esports", ChromeText::PresetEsports},
        {"compat", ChromeText::PresetCompat},
        {"native", ChromeText::PresetNative},
    };
}

void paintWorkspaceChrome(HDC hdc, const RECT& client, WorkspaceMode mode, HFONT font) {
    fillRect(hdc, client, kClrBg);

    // Topbar: carbon background with the parallelogram brand + titles.
    const int dpi = GetDpiForWindow(WindowFromDC(hdc));
    RECT topbar{client.left, client.top, client.right, client.top + MulDiv(52, dpi, 96)};
    fillRect(hdc, topbar, kClrPanel);

    const int markH = MulDiv(28, dpi, 96);
    const int markY = topbar.top + (topbar.bottom - topbar.top - markH) / 2;
    drawBrandMark(hdc, topbar.left + MulDiv(12, dpi, 96), markY, MulDiv(56, dpi, 96), markH);

    const int titleX = topbar.left + MulDiv(80, dpi, 96);
    HFONT old = (font != nullptr) ? (HFONT)SelectObject(hdc, font) : nullptr;
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, kClrText);
    TextOutW(hdc, titleX, topbar.top + MulDiv(6, dpi, 96), L"CineRTX", 7);
    RECT subRc{titleX, topbar.top + MulDiv(26, dpi, 96), topbar.right, topbar.bottom};
    SetTextColor(hdc, kClrTextDim);
    DrawTextW(hdc, chromeText(ChromeText::Subtitle), -1, &subRc, DT_LEFT | DT_SINGLELINE);
    if (old != nullptr) {
        SelectObject(hdc, old);
    }

    // Mode accent baseline under the topbar.
    RECT baseline{client.left, topbar.bottom, client.right, topbar.bottom + 2};
    fillRect(hdc, baseline, kClrNvGreenDim);
    (void)mode;

    // Left/centre/right panel seams per workspace.
    RECT body{client.left, baseline.bottom, client.right, client.bottom};
    if (body.bottom > body.top) {
        RECT rightPanel{body.right - MulDiv(380, dpi, 96), body.top, body.right, body.bottom};
        fillRect(hdc, rightPanel, kClrPanel);
        RECT seam{rightPanel.left - 1, body.top, rightPanel.left, body.bottom};
        fillRect(hdc, seam, kClrLine);
    }
}

void paintOwnerDrawButton(const DRAWITEMSTRUCT& dis, const wchar_t* caption, ButtonSkin skin,
                          HFONT font, bool hovered) {
    const RECT rc = dis.rcItem;
    const bool active = (dis.itemState & ODS_SELECTED) != 0;
    switch (skin) {
        case ButtonSkin::Tab:
        case ButtonSkin::TabActive: {
            const bool isActive = (skin == ButtonSkin::TabActive) || active;
            fillRect(dis.hDC, rc, isActive ? kClrPanel2 : kClrPanel);
            const int dpi = GetDpiForWindow(dis.hwndItem);
            RECT text{rc.left, rc.top, rc.right, rc.bottom - MulDiv(3, dpi, 96)};
            drawTextDim(dis.hDC, text, caption, isActive ? kClrNvGreen : kClrTextDim, font,
                        DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            if (isActive) {
                RECT underline{rc.left + MulDiv(8, dpi, 96), rc.bottom - MulDiv(3, dpi, 96),
                               rc.right - MulDiv(8, dpi, 96), rc.bottom};
                fillRect(dis.hDC, underline, kClrNvGreen);
            }
            frameRect(dis.hDC, rc, kClrLine);
            break;
        }
        case ButtonSkin::GreenSolid: {
            fillRect(dis.hDC, rc, active ? kClrNvGreenHover : (hovered ? kClrNvGreenHover : kClrNvGreen));
            drawTextDim(dis.hDC, rc, caption, kClrOnAccent, font,
                        DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            break;
        }
        case ButtonSkin::Subtle: {
            fillRect(dis.hDC, rc, active ? kClrCtrl : kClrPanel2);
            drawTextDim(dis.hDC, rc, caption, hovered ? kClrNvGreenHover : kClrText, font,
                        DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            frameRect(dis.hDC, rc, kClrLineStrong);
            break;
        }
        case ButtonSkin::PresetBar: {
            fillRect(dis.hDC, rc, active ? kClrCtrl : kClrPanel2);
            const int dpi = GetDpiForWindow(dis.hwndItem);
            RECT bar{rc.left, rc.top, rc.left + MulDiv(3, dpi, 96), rc.bottom};
            fillRect(dis.hDC, bar, active ? kClrNvGreenHover : kClrNvGreen);
            RECT text{rc.left + MulDiv(10, dpi, 96), rc.top, rc.right, rc.bottom};
            drawTextDim(dis.hDC, text, caption, active ? kClrNvGreen : kClrText, font,
                        DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            frameRect(dis.hDC, rc, kClrLine);
            break;
        }
    }
}

void paintNleTimeline(HWND hwnd, const RECT& client, const NleTimeline& state, HFONT font) {
    // GDI double buffer: compose on a memory DIB, blit once.
    HDC window_dc = GetDC(hwnd);
    HDC mem = CreateCompatibleDC(window_dc);
    HBITMAP dib = CreateCompatibleBitmap(window_dc, client.right - client.left,
                                         client.bottom - client.top);
    HBITMAP old_bmp = (HBITMAP)SelectObject(mem, dib);
    const int dpi = GetDpiForWindow(hwnd);

    fillRect(mem, client, kClrPanel);

    // Ruler with second ticks.
    RECT ruler{client.left, client.top, client.right, client.top + timelineRulerHeight(dpi)};
    fillRect(mem, ruler, kClrPanel2);
    RECT trackHeader{client.left, client.top, client.left + MulDiv(56, dpi, 96), client.bottom};
    fillRect(mem, trackHeader, kClrPanel2);

    const int lanes_top = ruler.bottom;
    for (int track = 0; track < kTrackCount; ++track) {
        RECT lane = laneRect(client, track, dpi);
        if (lane.top > client.bottom) {
            break;
        }
        fillRect(mem, lane, (track % 2 == 0) ? kClrPanel : kClrPanel2);
        RECT seam{client.left, lane.bottom - 1, client.right, lane.bottom};
        fillRect(mem, seam, kClrLine);
        RECT label{trackHeader.left, lane.top, trackHeader.right, lane.bottom};
        drawTextDim(mem, label, kTrackLabels[track],
                    track == 1 ? kClrNvGreen : kClrTextDim, font,
                    DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    // Lane area for clip rendering (starts after the track header).
    RECT area{client.left + MulDiv(56, dpi, 96), client.top, client.right, client.bottom};

    // IN/OUT region shading.
    if (state.in_ms >= 0 && state.out_ms > state.in_ms) {
        const int x0 = timelineMsToX(area, state, state.in_ms, dpi);
        const int x1 = timelineMsToX(area, state, state.out_ms, dpi);
        RECT io{x0, lanes_top, x1, client.bottom};
        fillRect(mem, io, kClrNvGreenDim);
    }

    // Clips.
    for (size_t i = 0; i < state.clips.size(); ++i) {
        const NleClip& clip = state.clips[static_cast<int>(i)];
        if (clip.track < 0 || clip.track >= kTrackCount) {
            continue;
        }
        RECT lane = laneRect(client, clip.track, dpi);
        const int x0 = timelineMsToX(area, state, clip.start_ms, dpi);
        const int x1 = timelineMsToX(area, state, clip.start_ms + clip.duration_ms, dpi);
        RECT clip_rc{x0, lane.top + MulDiv(6, dpi, 96), std::max(x1, x0 + 2),
                     lane.bottom - MulDiv(6, dpi, 96)};
        const bool selected = (static_cast<int>(i) == state.selected);
        fillRect(mem, clip_rc, selected ? kClrNvGreenDim : kClrCtrl);
        frameRect(mem, clip_rc, selected ? kClrNvGreen : kClrLineStrong);
        RECT text{clip_rc.left + MulDiv(6, dpi, 96), clip_rc.top, clip_rc.right, clip_rc.bottom};
        drawTextDim(mem, text, clip.name.c_str(), kClrText, font,
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }

    // Second ticks on the ruler.
    if (state.duration_ms > 0) {
        for (int64_t ms = 0; ms <= state.duration_ms; ms += 5000) {
            const int x = timelineMsToX(area, state, ms, dpi);
            RECT tick{x, ruler.bottom - MulDiv(6, dpi, 96), x + 1, ruler.bottom};
            fillRect(mem, tick, kClrLineStrong);
        }
    }

    // Playhead.
    const int phx = timelineMsToX(area, state, state.playhead_ms, dpi);
    RECT playhead{phx - 1, client.top, phx + 1, client.bottom};
    fillRect(mem, playhead, kClrNvGreen);

    BitBlt(window_dc, client.left, client.top, client.right - client.left,
           client.bottom - client.top, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old_bmp);
    DeleteObject(dib);
    DeleteDC(mem);
    ReleaseDC(hwnd, window_dc);
}

NleTimeline& sharedNleTimeline() {
    static NleTimeline timeline;
    return timeline;
}

bool registerNleTimelineWindowClass(HINSTANCE instance) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = nleTimelineProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kNleTimelineClass;
    if (RegisterClassExW(&wc) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// SSOT session state + 19-parameter profile <-> UI binding (spec Task 1).
// ---------------------------------------------------------------------------

rtx::TranscodeProfile& guiProfile() {
    static rtx::TranscodeProfile profile{};
    return profile;
}

namespace {
bool g_guiTranscoding = false;
WorkspaceMode g_activeWorkspace = WorkspaceMode::Encoder;
}  // namespace

void setGuiTranscoding(bool busy) noexcept { g_guiTranscoding = busy; }
bool guiIsTranscoding() noexcept { return g_guiTranscoding; }
void setActiveWorkspace(WorkspaceMode mode) noexcept { g_activeWorkspace = mode; }

// ---------------------------------------------------------------------------
// CPU-side media probing (RW-2 fix): real width/height/duration straight from
// rtx_core's statically linked FFmpeg demuxer — no GPU involvement needed.
// (probeMediaFile / formatVsrScale / activeVsrTarget live inline in the
// header so the unit tests need no GDI link.)
// ---------------------------------------------------------------------------

// Hardware probe button: reuses the CLI's versioned C-ABI shim probe (the GUI
// never resolves a NVIDIA symbol directly).
void runHardwareProbe(HWND hwnd) {
    rtx::cli::ShimProbeResult probe = rtx::cli::probeShim();
    const std::string human = rtx::cli::formatProbeHuman(probe);
    rtx::logInfo("hardware probe: result=" + std::to_string(probe.result) + " loaded=" +
                 std::string(probe.loaded ? "true" : "false"));
    MessageBoxA(hwnd, human.c_str(), "NVIDIA shim probe", MB_ICONINFORMATION);
}

// ---------------------------------------------------------------------------
// NLE workstation actions (bin / timecode / I-O / razor / export).
// ---------------------------------------------------------------------------

void refreshBinListbox(HWND hwnd) {
    SendDlgItemMessageW(hwnd, IDC_LST_BIN, LB_RESETCONTENT, 0, 0);
    const auto& timeline = sharedNleTimeline();
    for (const auto& clip : timeline.clips) {
        SendDlgItemMessageW(hwnd, IDC_LST_BIN, LB_ADDSTRING, 0,
                            reinterpret_cast<LPARAM>(clip.name.c_str()));
    }
}

void addFilesToBin(HWND hwnd, const std::vector<std::wstring>& paths) {
    auto& timeline = sharedNleTimeline();
    int64_t end_ms = 0;
    for (const auto& clip : timeline.clips) {
        end_ms = std::max(end_ms, clip.start_ms + clip.duration_ms);
    }
    for (const std::wstring& path : paths) {
        NleClip clip;
        clip.name = std::filesystem::path(path).filename().wstring();
        clip.path = path;
        clip.start_ms = end_ms;
        // RW-2 fix: real clip duration probed through the static FFmpeg
        // demuxer; kAssumedClipMs is only the probe-failure fallback.
        const ProbedMedia media = probeMediaFile(path);
        clip.duration_ms = media.ok ? media.duration_ms : kAssumedClipMs;
        clip.track = 1;  // V1
        timeline.clips.push_back(clip);
        end_ms += clip.duration_ms;
    }
    timeline.duration_ms = std::max<int64_t>(timeline.duration_ms, end_ms);
    refreshBinListbox(hwnd);
    InvalidateRect(GetDlgItem(hwnd, IDC_NLE_TIMELINE), nullptr, FALSE);
    rtx::logInfo("media bin now holds " + std::to_string(timeline.clips.size()) + " clip(s)");
}

void updateNleTimecode(HWND hwnd) {
    const auto& timeline = sharedNleTimeline();
    SetDlgItemTextW(hwnd, IDC_LBL_TIMECODE,
                    formatFlicksTimecode(timeline.playhead_ms, timeline.duration_ms).c_str());
}

void onNleMarkIn(HWND hwnd) {
    auto& timeline = sharedNleTimeline();
    timeline.in_ms = timeline.playhead_ms;
    if (timeline.out_ms >= 0 && timeline.out_ms <= timeline.in_ms) {
        timeline.out_ms = -1;
    }
    InvalidateRect(GetDlgItem(hwnd, IDC_NLE_TIMELINE), nullptr, FALSE);
    rtx::logInfo("NLE mark IN at " + std::to_string(timeline.in_ms) + " ms");
}

void onNleMarkOut(HWND hwnd) {
    auto& timeline = sharedNleTimeline();
    timeline.out_ms = timeline.playhead_ms;
    if (timeline.in_ms >= 0 && timeline.out_ms <= timeline.in_ms) {
        timeline.in_ms = -1;
    }
    InvalidateRect(GetDlgItem(hwnd, IDC_NLE_TIMELINE), nullptr, FALSE);
    rtx::logInfo("NLE mark OUT at " + std::to_string(timeline.out_ms) + " ms");
}

void onNleRazor(HWND hwnd) {
    auto& timeline = sharedNleTimeline();
    if (timeline.selected < 0 || timeline.selected >= static_cast<int>(timeline.clips.size())) {
        return;
    }
    NleClip clip = timeline.clips[timeline.selected];
    const int64_t cut = timeline.playhead_ms;
    if (cut <= clip.start_ms + 100 || cut >= clip.start_ms + clip.duration_ms - 100) {
        return;
    }
    NleClip right = clip;
    right.start_ms = cut;
    right.duration_ms = clip.start_ms + clip.duration_ms - cut;
    clip.duration_ms = cut - clip.start_ms;
    timeline.clips[timeline.selected] = clip;
    timeline.clips.insert(timeline.clips.begin() + timeline.selected + 1, right);
    InvalidateRect(GetDlgItem(hwnd, IDC_NLE_TIMELINE), nullptr, FALSE);
    rtx::logInfo("NLE razor cut at " + std::to_string(cut) + " ms");
}

bool exportSelectedClipToEncoder(HWND hwnd) {
    const LRESULT sel = SendDlgItemMessageW(hwnd, IDC_LST_BIN, LB_GETCURSEL, 0, 0);
    const auto& timeline = sharedNleTimeline();
    if (sel >= 0 && sel < static_cast<LRESULT>(timeline.clips.size())) {
        const std::wstring& path = timeline.clips[static_cast<size_t>(sel)].path;
        SetDlgItemTextW(hwnd, IDC_EDT_INPUT, path.c_str());
        SetDlgItemTextW(hwnd, IDC_EDT_OUTPUT, L"");
        autoFillOutputPath(hwnd);
        rtx::logInfo("NLE clip sent to the encoder workstation");
        return true;
    }
    return false;
}

// Ctrl+L / EN-中文 button: full-interface instant bilingual refresh (RW-6).
// Every SSOT label comes from trW(); every chrome caption is re-read from
// chromeText() at owner-draw paint time, so a pure invalidate suffices there.
void refreshUiLanguage(HWND hwnd) {
    SetWindowTextW(hwnd, rtx::trW(rtx::StrId::AppTitle));
    SetDlgItemTextW(hwnd, IDC_LBL_INPUT, rtx::trW(rtx::StrId::ParamInput));
    SetDlgItemTextW(hwnd, IDC_LBL_OUTPUT, rtx::trW(rtx::StrId::ParamOutput));
    SetDlgItemTextW(hwnd, IDC_LBL_CODEC, rtx::trW(rtx::StrId::ParamCodec));
    SetDlgItemTextW(hwnd, IDC_LBL_PRESET, rtx::trW(rtx::StrId::ParamPreset));
    SetDlgItemTextW(hwnd, IDC_LBL_RC, rtx::trW(rtx::StrId::ParamRateControl));
    SetDlgItemTextW(hwnd, IDC_LBL_BITRATE, rtx::trW(rtx::StrId::ParamBitrate));
    SetDlgItemTextW(hwnd, IDC_LBL_QP, rtx::trW(rtx::StrId::ParamCq));
    SetDlgItemTextW(hwnd, IDC_LBL_RES, rtx::trW(rtx::StrId::ParamResolution));
    SetDlgItemTextW(hwnd, IDC_LBL_GEOM, rtx::trW(rtx::StrId::ParamAspect));
    SetDlgItemTextW(hwnd, IDC_CHK_VSR, rtx::trW(rtx::StrId::ParamVsr));
    SetDlgItemTextW(hwnd, IDC_LBL_VSR_QUAL, rtx::trW(rtx::StrId::ParamVsrQuality));
    SetDlgItemTextW(hwnd, IDC_LBL_PEAK_NITS, rtx::trW(rtx::StrId::ParamPeakNits));
    SetDlgItemTextW(hwnd, IDC_LBL_FPS, rtx::trW(rtx::StrId::WuiParamFps));
    SetDlgItemTextW(hwnd, IDC_LBL_RIFE, rtx::trW(rtx::StrId::WuiParamRifeEngine));
    SetDlgItemTextW(hwnd, IDC_LBL_GPU, rtx::trW(rtx::StrId::WuiParamGpu));
    SetDlgItemTextW(hwnd, IDC_LBL_FRAMES, rtx::trW(rtx::StrId::WuiParamFrames));
    SetDlgItemTextW(hwnd, IDC_CHK_AUDIO, rtx::trW(rtx::StrId::ParamAudioCopy));
    SetDlgItemTextW(hwnd, IDC_CHK_HDR10PLUS, rtx::trW(rtx::StrId::WuiParamHdr10plus));
    SetDlgItemTextW(hwnd, IDC_LBL_LUT, rtx::trW(rtx::StrId::WuiParamLut));
    SetDlgItemTextW(hwnd, IDC_BTN_BROWSE_IN, rtx::trW(rtx::StrId::BtnBrowse));
    SetDlgItemTextW(hwnd, IDC_BTN_BROWSE_OUT, rtx::trW(rtx::StrId::BtnBrowse));
    SetDlgItemTextW(hwnd, IDC_BTN_BROWSE_LUT, rtx::trW(rtx::StrId::BtnBrowse));
    SetDlgItemTextW(hwnd, IDC_BTN_BROWSE_RIFE, rtx::trW(rtx::StrId::BtnBrowse));
    if (!guiIsTranscoding()) {
        SetDlgItemTextW(hwnd, IDC_LBL_STATUS, rtx::trW(rtx::StrId::StatusReady));
    }
    SetDlgItemTextW(hwnd, IDC_LBL_TELEMETRY,
                    (std::wstring(rtx::trW(rtx::StrId::LangToggleHint)) + L"  |  " +
                     chromeText(ChromeText::FullscreenHint))
                        .c_str());
    populateComboBoxes(hwnd);
    // Owner-draw chrome re-reads chromeText() at paint time: force repaints.
    InvalidateRect(GetDlgItem(hwnd, IDC_TAB_WS_ENCODER), nullptr, FALSE);
    InvalidateRect(GetDlgItem(hwnd, IDC_TAB_WS_PLAYER), nullptr, FALSE);
    InvalidateRect(GetDlgItem(hwnd, IDC_TAB_WS_NLE), nullptr, FALSE);
    InvalidateRect(GetDlgItem(hwnd, IDC_BTN_PROBE), nullptr, FALSE);
    InvalidateRect(GetDlgItem(hwnd, IDC_BTN_LANG), nullptr, FALSE);
    for (const int id : kEncoderIds) {
        InvalidateRect(GetDlgItem(hwnd, id), nullptr, FALSE);
    }
    for (const int id : kPlayerIds) {
        InvalidateRect(GetDlgItem(hwnd, id), nullptr, FALSE);
    }
    for (const int id : kNleIds) {
        InvalidateRect(GetDlgItem(hwnd, id), nullptr, FALSE);
    }
    updateNleTimecode(hwnd);
    // RW-6 runtime evidence: the applied UI language is observable in
    // DebugView so the interaction harness can assert instant switching.
    rtx::logInfo(std::string("ui language -> ") + rtx::languageTag(rtx::currentLanguage()));
    InvalidateRect(hwnd, nullptr, TRUE);
}

rtx::TranscodeProfile collectProfileFromUi(HWND hwnd) {
    rtx::TranscodeProfile profile = guiProfile();
    wchar_t buf[MAX_PATH] = {};
    auto text = [&](int id) {
        GetDlgItemTextW(hwnd, id, buf, MAX_PATH);
        return std::wstring(buf);
    };
    auto sel = [&](int id) { return ComboBox_GetCurSel(GetDlgItem(hwnd, id)); };
    auto checked = [&](int id) { return Button_GetCheck(GetDlgItem(hwnd, id)) == BST_CHECKED; };

    profile.input = wideToUtf8(text(IDC_EDT_INPUT));
    profile.output = wideToUtf8(text(IDC_EDT_OUTPUT));

    constexpr const char* kCodecs[] = {"av1", "hevc", "av1_cpu", "hevc_cpu"};
    const int codecIdx = std::clamp(sel(IDC_CMB_CODEC), 0, 3);
    profile.codec = kCodecs[codecIdx];

    profile.preset = std::clamp(sel(IDC_CMB_PRESET), 0, 6) + 1;

    constexpr const char* kRcModes[] = {"cq", "vbr", "cbr", "qp", "crf"};
    const int rcIdx = std::clamp(sel(IDC_CMB_RC), 0, 4);
    profile.rate_control = kRcModes[rcIdx];

    profile.bitrate_kbps = _wtoi(text(IDC_EDT_BITRATE).c_str());
    profile.qp = _wtoi(text(IDC_EDT_QP).c_str());
    profile.resolution = (sel(IDC_CMB_RES) == 1) ? "2k"
                         : (sel(IDC_CMB_RES) == 2) ? "fhd"
                         : (sel(IDC_CMB_RES) == 3) ? "native" : "4k";
    profile.geometry = (sel(IDC_CMB_GEOM) == 1) ? "pillarbox" : "auto";
    profile.vsr_enabled = checked(IDC_CHK_VSR);
    profile.vsr_quality = std::clamp(sel(IDC_CMB_VSR_QUAL), 0, 3) + 1;
    profile.peak_nits = _wtoi(text(IDC_EDT_PEAK_NITS).c_str());
    constexpr const wchar_t* kFpsModes[] = {
        L"original", L"60-nvofa", L"120-nvofa", L"60-rife", L"120-rife"
    };
    const int fpsIdx = std::clamp(sel(IDC_CMB_FPS), 0, 4);
    profile.fps_mode = wideToUtf8(kFpsModes[fpsIdx]);
    profile.rife_engine = wideToUtf8(text(IDC_EDT_RIFE_ENGINE));
    profile.copy_audio = checked(IDC_CHK_AUDIO);
    profile.hdr10plus = checked(IDC_CHK_HDR10PLUS);
    profile.lut = wideToUtf8(text(IDC_EDT_LUT));
    profile.gpu = _wtoi(text(IDC_EDT_GPU).c_str());
    profile.frames = _wtoi(text(IDC_EDT_FRAMES).c_str());
    return profile;
}

void applyProfileToUi(HWND hwnd, const rtx::TranscodeProfile& profile) {
    auto setEdit = [&](int id, const std::wstring& value) {
        SetDlgItemTextW(hwnd, id, value.c_str());
    };
    auto setSel = [&](int id, int index) {
        ComboBox_SetCurSel(GetDlgItem(hwnd, id), index);
    };
    setEdit(IDC_EDT_INPUT, utf8ToWide(profile.input));
    setEdit(IDC_EDT_OUTPUT, utf8ToWide(profile.output));

    int codecSel = 0;
    if (profile.codec == "hevc") codecSel = 1;
    else if (profile.codec == "av1_cpu") codecSel = 2;
    else if (profile.codec == "hevc_cpu") codecSel = 3;
    setSel(IDC_CMB_CODEC, codecSel);

    setSel(IDC_CMB_PRESET, std::clamp(profile.preset - 1, 0, 6));

    int rcSel = 0; // default "cq"
    if (profile.rate_control == "vbr") rcSel = 1;
    else if (profile.rate_control == "cbr") rcSel = 2;
    else if (profile.rate_control == "qp") rcSel = 3;
    else if (profile.rate_control == "crf") rcSel = 4;
    setSel(IDC_CMB_RC, rcSel);

    setEdit(IDC_EDT_BITRATE, std::to_wstring(profile.bitrate_kbps));
    setEdit(IDC_EDT_QP, std::to_wstring(profile.qp));
    setSel(IDC_CMB_RES, profile.resolution == "2k" ? 1
                        : profile.resolution == "fhd" ? 2
                        : profile.resolution == "native" ? 3 : 0);
    setSel(IDC_CMB_GEOM, profile.geometry == "pillarbox" ? 1 : 0);
    Button_SetCheck(GetDlgItem(hwnd, IDC_CHK_VSR),
                    profile.vsr_enabled ? BST_CHECKED : BST_UNCHECKED);
    setSel(IDC_CMB_VSR_QUAL, std::clamp(profile.vsr_quality - 1, 0, 3));
    setEdit(IDC_EDT_PEAK_NITS, std::to_wstring(profile.peak_nits));
    constexpr const wchar_t* kFpsModes[] = {
        L"original", L"60-nvofa", L"120-nvofa", L"60-rife", L"120-rife"
    };
    int fpsSel = 1;
    for (int i = 0; i < 5; ++i) {
        if (utf8ToWide(profile.fps_mode) == kFpsModes[i]) {
            fpsSel = i;
            break;
        }
    }
    setSel(IDC_CMB_FPS, fpsSel);
    setEdit(IDC_EDT_RIFE_ENGINE, utf8ToWide(profile.rife_engine));
    Button_SetCheck(GetDlgItem(hwnd, IDC_CHK_AUDIO),
                    profile.copy_audio ? BST_CHECKED : BST_UNCHECKED);
    Button_SetCheck(GetDlgItem(hwnd, IDC_CHK_HDR10PLUS),
                    profile.hdr10plus ? BST_CHECKED : BST_UNCHECKED);
    setEdit(IDC_EDT_LUT, utf8ToWide(profile.lut));
    setEdit(IDC_EDT_GPU, std::to_wstring(profile.gpu));
    setEdit(IDC_EDT_FRAMES, std::to_wstring(profile.frames));
}

// Output path derivation is 100% SSOT (Bug 2 fix): no suffix concatenation.
void autoFillOutputPath(HWND hwnd) {
    wchar_t inPath[MAX_PATH] = {};
    GetDlgItemTextW(hwnd, IDC_EDT_INPUT, inPath, MAX_PATH);
    if (inPath[0] == L'\0') return;
    wchar_t outPath[MAX_PATH] = {};
    GetDlgItemTextW(hwnd, IDC_EDT_OUTPUT, outPath, MAX_PATH);
    rtx::TranscodeProfile& profile = guiProfile();
    profile.input = wideToUtf8(inPath);
    profile.output = wideToUtf8(outPath);
    profile.output = rtx::deriveOutputPath(profile.input, profile.codec, profile.output,
                                           profile.auto_output);
    SetDlgItemTextW(hwnd, IDC_EDT_OUTPUT, utf8ToWide(profile.output).c_str());
}

// Dropdown option labels come exclusively from the 113-entry i18n SSOT table
// (Bug 5 fix): zero hardcoded bilingual ternaries, zero hardcoded options.
void populateComboBoxes(HWND hwnd) {
    auto refill = [&](int id, std::initializer_list<rtx::StrId> options, int defaultSel) {
        HWND h = GetDlgItem(hwnd, id);
        int sel = ComboBox_GetCurSel(h);
        if (sel < 0) sel = defaultSel;
        ComboBox_ResetContent(h);
        for (rtx::StrId option : options) {
            ComboBox_AddString(h, rtx::trW(option));
        }
        ComboBox_SetCurSel(h, sel);
    };
    refill(IDC_CMB_CODEC,
           {rtx::StrId::OptCodecAv1, rtx::StrId::OptCodecHevc,
            rtx::StrId::OptCodecAv1Cpu, rtx::StrId::OptCodecHevcCpu}, 0);
    refill(IDC_CMB_PRESET,
           {rtx::StrId::OptPresetP1, rtx::StrId::OptPresetP2, rtx::StrId::OptPresetP3,
            rtx::StrId::OptPresetP4, rtx::StrId::OptPresetP5, rtx::StrId::OptPresetP6,
            rtx::StrId::OptPresetP7}, 4);
    refill(IDC_CMB_RC,
           {rtx::StrId::OptRcCq, rtx::StrId::OptRcVbr, rtx::StrId::OptRcCbr,
            rtx::StrId::OptRcQp, rtx::StrId::OptRcCrf}, 0);
    refill(IDC_CMB_RES,
           {rtx::StrId::OptRes4k, rtx::StrId::OptRes2k, rtx::StrId::OptResFhd,
            rtx::StrId::OptResNative}, 0);
    refill(IDC_CMB_GEOM, {rtx::StrId::OptGeomAuto, rtx::StrId::OptGeomPillar}, 0);
    refill(IDC_CMB_VSR_QUAL,
           {rtx::StrId::OptVsrQ1, rtx::StrId::OptVsrQ2, rtx::StrId::OptVsrQ3,
            rtx::StrId::OptVsrQ4}, 3);
    refill(IDC_CMB_FPS,
           {rtx::StrId::OptFpsOriginal, rtx::StrId::OptFps60Nvofa, rtx::StrId::OptFps120Nvofa,
            rtx::StrId::OptFps60Rife, rtx::StrId::OptFps120Rife}, 1);
}

void syncControlStates(HWND hwnd) {
    const int rcSel = ComboBox_GetCurSel(GetDlgItem(hwnd, IDC_CMB_RC));
    // rcSel: 0=cq, 1=vbr, 2=cbr, 3=qp, 4=crf
    const bool usesQualityTarget = (rcSel == 0 || rcSel == 3 || rcSel == 4);
    const bool usesBitrateTarget = (rcSel == 1 || rcSel == 2);
    EnableWindow(GetDlgItem(hwnd, IDC_EDT_BITRATE), usesBitrateTarget && !guiIsTranscoding());
    EnableWindow(GetDlgItem(hwnd, IDC_EDT_QP), usesQualityTarget && !guiIsTranscoding());

    const int fpsSel = ComboBox_GetCurSel(GetDlgItem(hwnd, IDC_CMB_FPS));
    const bool rifeMode = (fpsSel == 3 || fpsSel == 4);
    EnableWindow(GetDlgItem(hwnd, IDC_EDT_RIFE_ENGINE), rifeMode && !guiIsTranscoding());
    EnableWindow(GetDlgItem(hwnd, IDC_BTN_BROWSE_RIFE), rifeMode && !guiIsTranscoding());

    const bool vsrChecked = (Button_GetCheck(GetDlgItem(hwnd, IDC_CHK_VSR)) == BST_CHECKED);
    EnableWindow(GetDlgItem(hwnd, IDC_CMB_VSR_QUAL), vsrChecked && !guiIsTranscoding());

    // Bug 1 fix: Peak Nits is always bound to the SSOT profile (10-bit HDR
    // pipeline) and always editable while idle.
    EnableWindow(GetDlgItem(hwnd, IDC_EDT_PEAK_NITS), !guiIsTranscoding());
    EnableWindow(GetDlgItem(hwnd, IDC_CHK_HDR10PLUS), !guiIsTranscoding());
    EnableWindow(GetDlgItem(hwnd, IDC_EDT_LUT), !guiIsTranscoding());
    EnableWindow(GetDlgItem(hwnd, IDC_BTN_BROWSE_LUT), !guiIsTranscoding());
    EnableWindow(GetDlgItem(hwnd, IDC_CMB_FPS), !guiIsTranscoding());
    EnableWindow(GetDlgItem(hwnd, IDC_BTN_START), !guiIsTranscoding());
    EnableWindow(GetDlgItem(hwnd, IDC_BTN_STOP), guiIsTranscoding());
}

void applyGoldenPresetById(HWND hwnd, const std::string& preset_id) {
    if (!applyGoldenPreset(preset_id, guiProfile())) {
        return;
    }
    applyProfileToUi(hwnd, guiProfile());
    syncControlStates(hwnd);
    const rtx::TranscodeProfile& profile = guiProfile();
    rtx::logInfo("golden preset applied: " + preset_id + " (codec=" + profile.codec +
                 " preset=" + std::to_string(profile.preset) + " res=" + profile.resolution +
                 " fps=" + profile.fps_mode + ")");
}

bool paintCommandButton(const DRAWITEMSTRUCT* dis, HFONT font) {
    using rtx::trW;
    const int id = static_cast<int>(dis->CtlID);
    const wchar_t* caption = nullptr;
    ButtonSkin skin = ButtonSkin::Subtle;
    switch (id) {
        case IDC_TAB_WS_ENCODER: caption = chromeText(ChromeText::WsEncoder);
            skin = (g_activeWorkspace == WorkspaceMode::Encoder) ? ButtonSkin::TabActive : ButtonSkin::Tab; break;
        case IDC_TAB_WS_PLAYER: caption = chromeText(ChromeText::WsPlayer);
            skin = (g_activeWorkspace == WorkspaceMode::Player) ? ButtonSkin::TabActive : ButtonSkin::Tab; break;
        case IDC_TAB_WS_NLE: caption = chromeText(ChromeText::WsNle);
            skin = (g_activeWorkspace == WorkspaceMode::Nle) ? ButtonSkin::TabActive : ButtonSkin::Tab; break;
        case IDC_BTN_START: caption = trW(rtx::StrId::ButtonStart); skin = ButtonSkin::GreenSolid; break;
        case IDC_BTN_STOP: caption = trW(rtx::StrId::ButtonStop); break;
        case IDC_BTN_PROBE: caption = chromeText(ChromeText::Probe); break;
        case IDC_BTN_LANG: caption = chromeText(ChromeText::LangButton); break;
        case IDC_BTN_BROWSE_IN:
        case IDC_BTN_BROWSE_OUT:
        case IDC_BTN_BROWSE_LUT:
        case IDC_BTN_BROWSE_RIFE: caption = trW(rtx::StrId::BtnBrowse); break;
        case IDC_BTN_PRESET_MASTER: caption = chromeText(ChromeText::PresetMaster); skin = ButtonSkin::PresetBar; break;
        case IDC_BTN_PRESET_RIFE: caption = chromeText(ChromeText::PresetRife); skin = ButtonSkin::PresetBar; break;
        case IDC_BTN_PRESET_ESPORTS: caption = chromeText(ChromeText::PresetEsports); skin = ButtonSkin::PresetBar; break;
        case IDC_BTN_PRESET_COMPAT: caption = chromeText(ChromeText::PresetCompat); skin = ButtonSkin::PresetBar; break;
        case IDC_BTN_PRESET_NATIVE: caption = chromeText(ChromeText::PresetNative); skin = ButtonSkin::PresetBar; break;
        case IDC_BTN_QUEUE_ADD: caption = chromeText(ChromeText::QueueAdd); break;
        case IDC_BTN_QUEUE_REMOVE: caption = chromeText(ChromeText::QueueRemove); break;
        case IDC_BTN_QUEUE_CLEAR: caption = chromeText(ChromeText::QueueClear); break;
        case IDC_BTN_MARK_IN: caption = chromeText(ChromeText::MarkIn); break;
        case IDC_BTN_MARK_OUT: caption = chromeText(ChromeText::MarkOut); break;
        case IDC_BTN_RAZOR: caption = chromeText(ChromeText::Razor); break;
        case IDC_BTN_STEP_BACK: caption = chromeText(ChromeText::StepBack); break;
        case IDC_BTN_STEP_FWD: caption = chromeText(ChromeText::StepFwd); break;
        case IDC_BTN_PLAYER_PLAY: caption = playbackCaption(); skin = ButtonSkin::GreenSolid; break;
        case IDC_BTN_NLE_EXPORT: caption = chromeText(ChromeText::SendEncoder); skin = ButtonSkin::GreenSolid; break;
        default: return false;
    }
    paintOwnerDrawButton(*dis, caption, skin, font, false);
    return true;
}

// ---------------------------------------------------------------------------
// Control factory: every child control except the realtime viewport (which
// the shell creates last so it z-orders above).  Owner-draw chrome buttons
// carry no static captions — the painter reads chromeText()/trW() at paint
// time, so a language toggle is a pure invalidate.
// ---------------------------------------------------------------------------
void createWorkstationControls(HWND hwnd, HFONT font, HFONT font_bold) {
    (void)font_bold;
    auto createCtrl = [&](const wchar_t* cls, const wchar_t* txt, DWORD style, int id) -> HWND {
        HWND h = CreateWindowExW(0, cls, txt, WS_CHILD | WS_VISIBLE | style, 0, 0, 0, 0,
                                 hwnd, (HMENU)(intptr_t)id, GetModuleHandleW(nullptr), nullptr);
        SendMessageW(h, WM_SETFONT, (WPARAM)font, TRUE);
        SetWindowTheme(h, L"DarkMode_Explorer", nullptr);
        return h;
    };
    auto createLabel = [&](int id, const wchar_t* txt) {
        (void)createCtrl(L"STATIC", txt, 0, id);
    };
    auto createEdit = [&](int id, const wchar_t* txt, bool number = false) {
        createCtrl(L"EDIT", txt, WS_BORDER | (number ? ES_NUMBER : ES_AUTOHSCROLL), id);
    };
    auto createCombo = [&](int id) {
        createCtrl(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL, id);
    };
    auto createOwnerBtn = [&](int id, const wchar_t* txt) {
        createCtrl(L"BUTTON", txt, BS_OWNERDRAW, id);
    };
    auto createCheck = [&](int id, const wchar_t* txt) {
        createCtrl(L"BUTTON", txt, BS_AUTOCHECKBOX, id);
    };

    // Inspector (19 SSOT parameters + actions).
    createLabel(IDC_LBL_INPUT, L"Input:");
    createEdit(IDC_EDT_INPUT, L"");
    createOwnerBtn(IDC_BTN_BROWSE_IN, L"...");
    createLabel(IDC_LBL_OUTPUT, L"Output:");
    createEdit(IDC_EDT_OUTPUT, L"");
    createOwnerBtn(IDC_BTN_BROWSE_OUT, L"...");
    createLabel(IDC_LBL_CODEC, L"Codec:");
    createCombo(IDC_CMB_CODEC);
    createLabel(IDC_LBL_PRESET, L"Preset:");
    createCombo(IDC_CMB_PRESET);
    createLabel(IDC_LBL_RC, L"Rate Control:");
    createCombo(IDC_CMB_RC);
    createLabel(IDC_LBL_BITRATE, L"Bitrate:");
    createEdit(IDC_EDT_BITRATE, L"0", true);
    createLabel(IDC_LBL_QP, L"QP:");
    createEdit(IDC_EDT_QP, L"22", true);
    createLabel(IDC_LBL_RES, L"Resolution:");
    createCombo(IDC_CMB_RES);
    createLabel(IDC_LBL_GEOM, L"Aspect:");
    createCombo(IDC_CMB_GEOM);
    createCheck(IDC_CHK_VSR, L"Enable RTX VSR");
    Button_SetCheck(GetDlgItem(hwnd, IDC_CHK_VSR), BST_CHECKED);
    createLabel(IDC_LBL_VSR_QUAL, L"Lvl:");
    createCombo(IDC_CMB_VSR_QUAL);
    createLabel(IDC_LBL_PEAK_NITS, L"Peak Nits:");
    createEdit(IDC_EDT_PEAK_NITS, L"1000", true);
    createLabel(IDC_LBL_FPS, L"Target FPS:");
    createCombo(IDC_CMB_FPS);
    createLabel(IDC_LBL_RIFE, L"RIFE Engine:");
    createEdit(IDC_EDT_RIFE_ENGINE, L"");
    createOwnerBtn(IDC_BTN_BROWSE_RIFE, L"...");
    createLabel(IDC_LBL_GPU, L"GPU:");
    createEdit(IDC_EDT_GPU, L"0", true);
    createLabel(IDC_LBL_FRAMES, L"Frames:");
    createEdit(IDC_EDT_FRAMES, L"0", true);
    createCheck(IDC_CHK_AUDIO, L"Audio Passthrough");
    Button_SetCheck(GetDlgItem(hwnd, IDC_CHK_AUDIO), BST_CHECKED);
    createCheck(IDC_CHK_HDR10PLUS, L"HDR10+ (ST 2094-40)");
    createLabel(IDC_LBL_LUT, L"3D LUT:");
    createEdit(IDC_EDT_LUT, L"");
    createOwnerBtn(IDC_BTN_BROWSE_LUT, L"...");
    createOwnerBtn(IDC_BTN_START, L"Start Transcode");
    createOwnerBtn(IDC_BTN_STOP, L"Stop");
    EnableWindow(GetDlgItem(hwnd, IDC_BTN_STOP), FALSE);
    HWND hProgress = createCtrl(PROGRESS_CLASSW, L"", 0, IDC_PROGRESS);
    SendMessageW(hProgress, PBM_SETRANGE32, 0, 1000);
    createLabel(IDC_LBL_STATUS, L"Ready");
    createLabel(IDC_LBL_TELEMETRY, L"");

    // Centre: preview scrub slider (Encoder preview + Player transport).
    HWND hScrub = createCtrl(TRACKBAR_CLASSW, L"", TBS_HORZ, IDC_TRK_SCRUB);
    SendMessageW(hScrub, TBM_SETRANGE, TRUE, MAKELPARAM(0, 30000));
    SendMessageW(hScrub, TBM_SETPOS, TRUE, 0);

    // Left rail: golden presets + batch queue.
    for (const int id : {IDC_BTN_PRESET_MASTER, IDC_BTN_PRESET_RIFE,
                         IDC_BTN_PRESET_ESPORTS, IDC_BTN_PRESET_COMPAT,
                         IDC_BTN_PRESET_NATIVE}) {
        createOwnerBtn(id, L"");
    }
    createCtrl(L"LISTBOX", L"", WS_BORDER | LBS_NOINTEGRALHEIGHT | WS_VSCROLL, IDC_LST_QUEUE);
    createOwnerBtn(IDC_BTN_QUEUE_ADD, L"+");
    createOwnerBtn(IDC_BTN_QUEUE_REMOVE, L"-");
    createOwnerBtn(IDC_BTN_QUEUE_CLEAR, L"x");

    // Topbar: 3-mode tabs + probe + language.
    createOwnerBtn(IDC_TAB_WS_ENCODER, L"");
    createOwnerBtn(IDC_TAB_WS_PLAYER, L"");
    createOwnerBtn(IDC_TAB_WS_NLE, L"");
    createOwnerBtn(IDC_BTN_PROBE, L"");
    createOwnerBtn(IDC_BTN_LANG, chromeText(ChromeText::LangButton));

    // Player transport.
    createOwnerBtn(IDC_BTN_STEP_BACK, L"<<");
    createOwnerBtn(IDC_BTN_STEP_FWD, L">>");
    createOwnerBtn(IDC_BTN_PLAYER_PLAY, playbackCaption());
    createOwnerBtn(IDC_BTN_NLE_EXPORT, L"");
    createLabel(IDC_LBL_VSR_OSD, L"RTX VSR 1.00x");

    // NLE workstation.
    createCtrl(L"LISTBOX", L"", WS_BORDER | LBS_NOTIFY | WS_VSCROLL, IDC_LST_BIN);
    createLabel(IDC_LBL_TIMECODE, L"SMPTE 00:00:00:00 | 0 Flicks (705,600,000 Hz)");
    createOwnerBtn(IDC_BTN_MARK_IN, L"I");
    createOwnerBtn(IDC_BTN_MARK_OUT, L"O");
    createOwnerBtn(IDC_BTN_RAZOR, L"C");
    HINSTANCE instance = GetModuleHandleW(nullptr);
    (void)registerNleTimelineWindowClass(instance);
    HWND hTimeline = CreateWindowExW(0, L"RTXNleTimelineWnd", L"",
                                     WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 0, 0, 0, 0, hwnd,
                                     (HMENU)(intptr_t)IDC_NLE_TIMELINE, instance, nullptr);
    SendMessageW(hTimeline, WM_SETFONT, (WPARAM)font, TRUE);
}

}  // namespace rtx::gui
