// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors

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

#include "rtx/flicks.h"
#include "rtx/i18n.h"
#include "rtx/string_utils.h"
#include "rtx/io.h"
#include "rtx/logger.h"
#include "rtx/ngx_remaster.h"
#include "rtx/playback_session.h"
#include "preview_viewport.h"

#include "shim_probe.h"

#include <algorithm>
#include <cwchar>
#include <utility>

namespace rtx::gui {
namespace {

constexpr int kTrackCount = static_cast<int>(rtx::kNleTrackCount);
constexpr wchar_t kTrackLabels[kTrackCount][4] = {L"V2", L"V1", L"A1", L"S1"};
constexpr wchar_t kTimelineCanvasClass[] = L"RTXTimelineCanvasWnd";

[[nodiscard]] int64_t effectiveTimelineSpan(const rtx::NleProject& project) noexcept {
    return std::max<int64_t>({project.timelineDurationFlicks(),
                              g_app.mediaDurationFlicks,
                              kDefaultTimelineSpan.count()});
}
void fillRect(HDC hdc, const RECT& rc, COLORREF color) {
    SetDCBrushColor(hdc, color);
    FillRect(hdc, &rc, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
}
void frameRect(HDC hdc, const RECT& rc, COLORREF color) {
    SetDCBrushColor(hdc, color);
    FrameRect(hdc, &rc, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
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
int64_t timelineXToFlicks(const RECT& area, const rtx::NleProject& project, int x, int dpi) {
    const int64_t span = effectiveTimelineSpan(project);
    const int offset = MulDiv(56, dpi, 96);
    const int width = area.right - area.left - offset;
    if (width <= 0 || span <= 0) {
        return 0;
    }
    const double ratio = std::clamp(
        static_cast<double>(x - area.left - offset) / static_cast<double>(width), 0.0, 1.0);
    return static_cast<int64_t>(ratio * static_cast<double>(span));
}
int timelineFlicksToX(const RECT& area, const rtx::NleProject& project, int64_t flicks, int dpi) {
    const int64_t span = effectiveTimelineSpan(project);
    const int offset = MulDiv(56, dpi, 96);
    const int width = area.right - area.left - offset;
    if (width <= 0 || span <= 0) {
        return area.left + offset;
    }
    const double ratio = std::clamp(
        static_cast<double>(flicks) / static_cast<double>(span), 0.0, 1.0);
    return area.left + offset + static_cast<int>(ratio * static_cast<double>(width));
}
LRESULT CALLBACK timelineCanvasProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_SETFONT:
            g_app.timelineFont = reinterpret_cast<HFONT>(wParam);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_GETFONT:
            return reinterpret_cast<LRESULT>(g_app.timelineFont);
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            RECT rc{};
            GetClientRect(hwnd, &rc);
            PAINTSTRUCT ps{};
            HDC hdc = BeginPaint(hwnd, &ps);
            paintTimelineCanvas(hwnd, rc, g_app.project, g_app.timelineFont);
            (void)hdc;
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_LBUTTONDOWN: {
            g_app.timelineDragging = true;
            SetCapture(hwnd);
            RECT rc{};
            GetClientRect(hwnd, &rc);
            const int dpi = GetDpiForWindow(hwnd);
            const int click_x = GET_X_LPARAM(lParam);
            const int click_y = GET_Y_LPARAM(lParam);
            const int64_t click_flicks = timelineXToFlicks(rc, g_app.project, click_x, dpi);
            g_app.project.playhead_flicks = click_flicks;

            // S-1.4：根据鼠标 (lane, x) 命中选中对应时间轴片段。
            int hit_lane = -1;
            for (int lane = 0; lane < kTrackCount; ++lane) {
                const RECT lr = laneRect(rc, lane, dpi);
                if (click_y >= lr.top && click_y < lr.bottom) {
                    hit_lane = lane;
                    break;
                }
            }
            if (hit_lane >= 0) {
                for (const rtx::TimelineClip& clip : g_app.project.clips) {
                    const int64_t clip_end = clip.start_flicks + rtx::nleClipDuration(clip);
                    if (clip.lane == hit_lane &&
                        click_flicks >= clip.start_flicks && click_flicks <= clip_end) {
                        g_app.project.selected_clip_id = clip.id;
                        break;
                    }
                }
            }
            SendMessageW(GetParent(hwnd), WM_APP_TIMELINE_SEEK, 0, 0);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        case WM_MOUSEMOVE:
            if (g_app.timelineDragging) {
                RECT rc{};
                GetClientRect(hwnd, &rc);
                const int dpi = GetDpiForWindow(hwnd);
                g_app.project.playhead_flicks =
                    timelineXToFlicks(rc, g_app.project, GET_X_LPARAM(lParam), dpi);
                SendMessageW(GetParent(hwnd), WM_APP_TIMELINE_SEEK, 0, 0);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        case WM_LBUTTONUP:
            g_app.timelineDragging = false;
            ReleaseCapture();
            return 0;
        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}
}  // namespace

const wchar_t* chromeText(ChromeText id) noexcept {
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
        case ChromeText::SendEncoder: return zh ? L"\U0001F680 导出序列至转码器" : L"\U0001F680 Export to Encoder";
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
void setGuiPlaying(bool playing) noexcept {
    g_app.playing.store(playing, std::memory_order_release);
}
bool guiIsPlaying() noexcept {
    return g_app.playing.load(std::memory_order_acquire);
}
const wchar_t* playbackCaption() noexcept {
    return chromeText(guiIsPlaying() ? ChromeText::Pause : ChromeText::Play);
}
bool applyGoldenPreset(std::string_view preset_id, rtx::TranscodeProfile& profile) {
    // {id, codec, preset, resolution, vsr_enabled (-1=keep), vsr_quality, fps_mode, peak_nits (0=keep)}
    using PresetRow = std::tuple<const char*, const char*, int, const char*, int, int, const char*, int>;
    static constexpr PresetRow kPresets[] = {
        {"master",  "av1",  5, "4k",     1,  4, "60-nvofa",  1000},
        {"rife",    "av1",  6, "4k",    -1,  4, "60-rife",      0},
        {"esports", "av1",  4, "4k",    -1,  4, "120-nvofa",    0},
        {"compat",  "hevc", 5, "2k",    -1,  3, "60-nvofa",     0},
        {"native",  "av1",  6, "native", 0,  4, "original",     0},
    };
    for (const auto& [id, codec, level, res, vsr_on, vsr_q, fps, nits] : kPresets) {
        if (preset_id == id) {
            profile.codec = codec;
            profile.preset = level;
            profile.resolution = res;
            if (vsr_on >= 0) profile.vsr_enabled = (vsr_on != 0);
            profile.vsr_quality = vsr_q;
            profile.fps_mode = fps;
            if (nits > 0) profile.peak_nits = nits;
            return true;
        }
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
    RECT baseline{client.left, topbar.bottom, client.right, topbar.bottom + 2};
    fillRect(hdc, baseline, kClrNvGreenDim);
    (void)mode;

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
void paintTimelineCanvas(HWND hwnd, const RECT& client, const rtx::NleProject& project, HFONT font) {
    HDC window_dc = GetDC(hwnd);
    HDC mem = CreateCompatibleDC(window_dc);
    HBITMAP dib = CreateCompatibleBitmap(window_dc, client.right - client.left,
                                         client.bottom - client.top);
    HBITMAP old_bmp = (HBITMAP)SelectObject(mem, dib);
    const int dpi = GetDpiForWindow(hwnd);

    fillRect(mem, client, kClrPanel);

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

    if (project.mark_in_flicks >= 0 && project.mark_out_flicks > project.mark_in_flicks) {
        const int x0 = timelineFlicksToX(client, project, project.mark_in_flicks, dpi);
        const int x1 = timelineFlicksToX(client, project, project.mark_out_flicks, dpi);
        RECT io{x0, lanes_top, x1, client.bottom};
        fillRect(mem, io, kClrNvGreenDim);
    }
    for (const rtx::TimelineClip& clip : project.clips) {
        if (clip.lane < 0 || clip.lane >= kTrackCount) {
            continue;
        }
        RECT lane = laneRect(client, clip.lane, dpi);
        const int x0 = timelineFlicksToX(client, project, clip.start_flicks, dpi);
        const int x1 = timelineFlicksToX(client, project, clip.start_flicks + rtx::nleClipDuration(clip), dpi);
        RECT clip_rc{x0, lane.top + MulDiv(6, dpi, 96), std::max(x1, x0 + 2),
                     lane.bottom - MulDiv(6, dpi, 96)};
        const bool selected = (clip.id == project.selected_clip_id);
        fillRect(mem, clip_rc, selected ? kClrNvGreenDim : kClrCtrl);
        frameRect(mem, clip_rc, selected ? kClrNvGreen : kClrLineStrong);
        RECT text{clip_rc.left + MulDiv(6, dpi, 96), clip_rc.top, clip_rc.right, clip_rc.bottom};
        const std::wstring wname = utf8ToWide(clip.name);
        drawTextDim(mem, text, wname.c_str(), kClrText, font,
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    const int64_t span = effectiveTimelineSpan(project);
    const int64_t tick_step = std::chrono::duration_cast<rtx::Flicks>(std::chrono::seconds(5)).count();
    if (span > 0 && tick_step > 0) {
        for (int64_t t = 0; t <= span; t += tick_step) {
            const int x = timelineFlicksToX(client, project, t, dpi);
            RECT tick{x, ruler.bottom - MulDiv(6, dpi, 96), x + 1, ruler.bottom};
            fillRect(mem, tick, kClrLineStrong);
        }
    }
    const int phx = timelineFlicksToX(client, project, project.playhead_flicks, dpi);
    RECT playhead{phx - 1, client.top, phx + 1, client.bottom};
    fillRect(mem, playhead, kClrNvGreen);

    BitBlt(window_dc, client.left, client.top, client.right - client.left,
           client.bottom - client.top, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old_bmp);
    DeleteObject(dib);
    DeleteDC(mem);
    ReleaseDC(hwnd, window_dc);
}
bool registerTimelineCanvasWindowClass(HINSTANCE instance) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = timelineCanvasProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kTimelineCanvasClass;
    if (RegisterClassExW(&wc) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }
    return true;
}
rtx::TranscodeProfile& guiProfile() {
    return g_app.profile;
}
void setGuiTranscoding(bool busy) noexcept {
    g_app.transcoding.store(busy, std::memory_order_release);
}
bool guiIsTranscoding() noexcept {
    return g_app.transcoding.load(std::memory_order_acquire);
}
void setActiveWorkspace(WorkspaceMode mode) noexcept {
    g_app.workspace = mode;
}

void runHardwareProbe(HWND hwnd) {
    rtx::cli::ShimProbeResult probe = rtx::cli::probeShim();
    const std::string human = rtx::cli::formatProbeHuman(probe);
    rtx::logInfo("hardware probe: result=" + std::to_string(probe.result) + " loaded=" +
                 std::string(probe.loaded ? "true" : "false"));
    MessageBoxA(hwnd, human.c_str(), "NVIDIA shim probe", MB_ICONINFORMATION);
}
void refreshBinListbox(HWND hwnd) {
    SendDlgItemMessageW(hwnd, IDC_LST_BIN, LB_RESETCONTENT, 0, 0);
    const rtx::NleProject& project = g_app.project;
    for (const rtx::TimelineClip& clip : project.clips) {
        const std::wstring wname = utf8ToWide(clip.name);
        SendDlgItemMessageW(hwnd, IDC_LST_BIN, LB_ADDSTRING, 0,
                            reinterpret_cast<LPARAM>(wname.c_str()));
    }
}
void addFilesToBin(HWND hwnd, const std::vector<std::wstring>& paths) {
    rtx::NleProject& project = g_app.project;
    int64_t end_flicks = project.timelineDurationFlicks();
    for (const std::wstring& path : paths) {
        const ProbedMedia media = probeMediaFile(path);
        const int64_t dur = media.ok ? media.duration_flicks : kDefaultTimelineSpan.count();
        const std::string name = wideToUtf8(std::filesystem::path(path).filename().wstring());
        auto asset_id = rtx::nleRegisterAsset(project, rtx::NleMediaAsset{
            .uuid = "asset-" + std::to_string(project.next_id),
            .name = name,
            .path = wideToUtf8(path),
            .duration_flicks = dur,
            .width = media.width,
            .height = media.height,
            .offline = false,
        });
        if (!asset_id.has_value()) continue;
        auto v_clip_id = rtx::nleAddClip(project, g_app.history, rtx::TimelineClip{
            .asset_id = *asset_id,
            .name = name,
            .lane = 1,  // V1 主视频轨
            .start_flicks = end_flicks,
            .in_flicks = 0,
            .out_flicks = dur,
            .gain = 1.0f,
        });
        // S-1.4：同步向 A1 主音轨插入对应音频段，严禁 A1 留空。
        (void)rtx::nleAddClip(project, g_app.history, rtx::TimelineClip{
            .asset_id = *asset_id,
            .name = name + " [A1]",
            .lane = 2,  // A1 主音轨
            .start_flicks = end_flicks,
            .in_flicks = 0,
            .out_flicks = dur,
            .gain = 1.0f,
        });
        if (v_clip_id.has_value()) {
            project.selected_clip_id = *v_clip_id;
            end_flicks += dur;
        }
    }
    refreshBinListbox(hwnd);
    InvalidateRect(GetDlgItem(hwnd, IDC_NLE_TIMELINE), nullptr, FALSE);
    rtx::logInfo("media bin now holds " + std::to_string(project.clips.size()) + " clip(s)");
}
void updateNleTimecode(HWND hwnd) {
    const rtx::NleProject& project = g_app.project;
    SetDlgItemTextW(hwnd, IDC_LBL_TIMECODE,
                    formatFlicksTimecode(project.playhead_flicks, effectiveTimelineSpan(project)).c_str());
}
void onNleMarkIn(HWND hwnd) {
    rtx::NleProject& project = g_app.project;
    project.mark_in_flicks = project.playhead_flicks;
    if (project.mark_out_flicks >= 0 && project.mark_out_flicks <= project.mark_in_flicks) {
        project.mark_out_flicks = -1;
    }
    InvalidateRect(GetDlgItem(hwnd, IDC_NLE_TIMELINE), nullptr, FALSE);
    rtx::logInfo("NLE mark IN at " + std::to_string(project.mark_in_flicks) + " flicks");
}
void onNleMarkOut(HWND hwnd) {
    rtx::NleProject& project = g_app.project;
    project.mark_out_flicks = project.playhead_flicks;
    if (project.mark_in_flicks >= 0 && project.mark_out_flicks <= project.mark_in_flicks) {
        project.mark_in_flicks = -1;
    }
    InvalidateRect(GetDlgItem(hwnd, IDC_NLE_TIMELINE), nullptr, FALSE);
    rtx::logInfo("NLE mark OUT at " + std::to_string(project.mark_out_flicks) + " flicks");
}
void onNleRazor(HWND hwnd) {
    rtx::NleProject& project = g_app.project;
    if (project.selected_clip_id == 0) {
        return;
    }
    if (auto split = rtx::nleSplitClip(project, g_app.history, project.selected_clip_id,
                                       project.playhead_flicks);
        split.has_value()) {
        refreshBinListbox(hwnd);
        InvalidateRect(GetDlgItem(hwnd, IDC_NLE_TIMELINE), nullptr, FALSE);
        rtx::logInfo("NLE razor cut at " + std::to_string(project.playhead_flicks) + " flicks");
    }
}
void onNleDeleteSelected(HWND hwnd) {
    rtx::NleProject& project = g_app.project;
    if (project.selected_clip_id == 0) {
        return;
    }
    if (rtx::nleDeleteClip(project, g_app.history, project.selected_clip_id).has_value()) {
        refreshBinListbox(hwnd);
        InvalidateRect(GetDlgItem(hwnd, IDC_NLE_TIMELINE), nullptr, FALSE);
    }
}
void onNleUndo(HWND hwnd) {
    if (rtx::nleUndo(g_app.project, g_app.history).has_value()) {
        refreshBinListbox(hwnd);
        InvalidateRect(GetDlgItem(hwnd, IDC_NLE_TIMELINE), nullptr, FALSE);
    }
}
void onNleRedo(HWND hwnd) {
    if (rtx::nleRedo(g_app.project, g_app.history).has_value()) {
        refreshBinListbox(hwnd);
        InvalidateRect(GetDlgItem(hwnd, IDC_NLE_TIMELINE), nullptr, FALSE);
    }
}
void onNleExportSequence(HWND hwnd) {
    if (guiIsTranscoding()) {
        return;
    }
    if (g_app.exportThread.joinable()) {
        g_app.exportThread.request_stop();
        g_app.exportThread.join();
    }
    const rtx::NleProject snapshot = g_app.project;
    rtx::TranscodeProfile profile = collectProfileFromUi(hwnd);
    setGuiTranscoding(true);
    syncControlStates(hwnd);
    g_app.exportThread = std::jthread([hwnd, snapshot, profile = std::move(profile)](std::stop_token stop) mutable {
        auto msg = std::make_unique<std::wstring>();
        auto compiled = rtx::compileRenderGraph(snapshot);
        auto audio_compiled = rtx::compileAudioRenderGraph(snapshot);
        if (!compiled.has_value()) {
            *msg = L"NLE Export Failed: " + utf8ToWide(compiled.error().message);
        } else if (!audio_compiled.has_value()) {
            *msg = L"NLE Audio Graph Failed: " + utf8ToWide(audio_compiled.error().message);
        } else if (stop.stop_requested()) {
            *msg = L"NLE Export Cancelled";
        } else {
            if (profile.input.empty()) {
                profile.input = compiled->front().source_path;
            }
            (void)rtx::validateAndNormalizeProfile(profile, false);
            profile.output = rtx::deriveOutputPath(profile.input, profile.codec, profile.output,
                                                   profile.auto_output);
            rtx::HwTranscodeOptions opts = rtx::profileToHwOptions(profile);
            opts.cancel = &g_app.cancelRequested;
            opts.split_ratio = &g_app.splitRatio;
            auto exported = rtx::runNvidiaRemasterRenderGraph(opts, *compiled, *audio_compiled, stop);
            if (!exported.has_value()) {
                *msg = L"NLE Export Failed: " + utf8ToWide(exported.error().message);
            } else {
                *msg = L"NLE Export Complete (" + std::to_wstring(compiled->size()) + L"V / " +
                       std::to_wstring(audio_compiled->size()) + L"A, " +
                       std::to_wstring(exported->frames_encoded) + L" frames)";
            }
        }
        if (!stop.stop_requested() && IsWindow(hwnd)) {
            if (!PostMessageW(hwnd, WM_APP_NLE_EXPORT_DONE, 0,
                              reinterpret_cast<LPARAM>(msg.get()))) {
                setGuiTranscoding(false);
            } else {
                (void)msg.release();
            }
        } else {
            setGuiTranscoding(false);
        }
    });
}
void refreshUiLanguage(HWND hwnd) {
    using rtx::StrId;
    SetWindowTextW(hwnd, rtx::trW(StrId::AppTitle));
    static constexpr std::pair<int, StrId> kLabels[] = {
        {IDC_LBL_INPUT, StrId::ParamInput},         {IDC_LBL_OUTPUT, StrId::ParamOutput},
        {IDC_LBL_CODEC, StrId::ParamCodec},         {IDC_LBL_PRESET, StrId::ParamPreset},
        {IDC_LBL_RC, StrId::ParamRateControl},      {IDC_LBL_BITRATE, StrId::ParamBitrate},
        {IDC_LBL_QP, StrId::ParamCq},               {IDC_LBL_RES, StrId::ParamResolution},
        {IDC_LBL_GEOM, StrId::ParamAspect},         {IDC_CHK_VSR, StrId::ParamVsr},
        {IDC_LBL_VSR_QUAL, StrId::ParamVsrQuality}, {IDC_LBL_PEAK_NITS, StrId::ParamPeakNits},
        {IDC_LBL_FPS, StrId::WuiParamFps},          {IDC_LBL_RIFE, StrId::WuiParamRifeEngine},
        {IDC_LBL_GPU, StrId::WuiParamGpu},          {IDC_LBL_FRAMES, StrId::WuiParamFrames},
        {IDC_CHK_AUDIO, StrId::ParamAudioCopy},     {IDC_CHK_HDR10PLUS, StrId::WuiParamHdr10plus},
        {IDC_LBL_LUT, StrId::WuiParamLut},          {IDC_BTN_BROWSE_IN, StrId::BtnBrowse},
        {IDC_BTN_BROWSE_OUT, StrId::BtnBrowse},     {IDC_BTN_BROWSE_LUT, StrId::BtnBrowse},
        {IDC_BTN_BROWSE_RIFE, StrId::BtnBrowse},
    };
    for (const auto& [id, str_id] : kLabels) {
        SetDlgItemTextW(hwnd, id, rtx::trW(str_id));
    }
    if (!guiIsTranscoding()) {
        SetDlgItemTextW(hwnd, IDC_LBL_STATUS, rtx::trW(StrId::StatusReady));
    }
    SetDlgItemTextW(hwnd, IDC_LBL_TELEMETRY,
                    (std::wstring(rtx::trW(StrId::LangToggleHint)) + L"  |  " +
                     chromeText(ChromeText::FullscreenHint))
                        .c_str());
    populateComboBoxes(hwnd);
    auto invalidateIds = [hwnd](const int* ids, size_t n) {
        for (size_t i = 0; i < n; ++i) InvalidateRect(GetDlgItem(hwnd, ids[i]), nullptr, FALSE);
    };
    static constexpr int kTopIds[] = {
        IDC_TAB_WS_ENCODER, IDC_TAB_WS_PLAYER, IDC_TAB_WS_NLE, IDC_BTN_PROBE, IDC_BTN_LANG};
    invalidateIds(kTopIds, std::size(kTopIds));
    invalidateIds(kEncoderIds, std::size(kEncoderIds));
    invalidateIds(kPlayerIds, std::size(kPlayerIds));
    invalidateIds(kNleIds, std::size(kNleIds));
    updateNleTimecode(hwnd);
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

    constexpr const char* kCodecs[] = {"av1", "hevc"};
    const int codecIdx = std::clamp(sel(IDC_CMB_CODEC), 0, 1);
    profile.codec = kCodecs[codecIdx];

    profile.preset = std::clamp(sel(IDC_CMB_PRESET), 0, 6) + 1;

    constexpr const char* kRcModes[] = {"cq", "vbr", "cbr", "qp"};
    const int rcIdx = std::clamp(sel(IDC_CMB_RC), 0, 3);
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
    setSel(IDC_CMB_CODEC, codecSel);

    setSel(IDC_CMB_PRESET, std::clamp(profile.preset - 1, 0, 6));

    int rcSel = 0; // default "cq"
    if (profile.rate_control == "vbr") rcSel = 1;
    else if (profile.rate_control == "cbr") rcSel = 2;
    else if (profile.rate_control == "qp") rcSel = 3;
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
           {rtx::StrId::OptCodecAv1, rtx::StrId::OptCodecHevc}, 0);
    refill(IDC_CMB_PRESET,
           {rtx::StrId::OptPresetP1, rtx::StrId::OptPresetP2, rtx::StrId::OptPresetP3,
            rtx::StrId::OptPresetP4, rtx::StrId::OptPresetP5, rtx::StrId::OptPresetP6,
            rtx::StrId::OptPresetP7}, 4);
    refill(IDC_CMB_RC,
           {rtx::StrId::OptRcCq, rtx::StrId::OptRcVbr, rtx::StrId::OptRcCbr,
            rtx::StrId::OptRcQp}, 0);
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
    const bool usesQualityTarget = (rcSel == 0 || rcSel == 3);
    const bool usesBitrateTarget = (rcSel == 1 || rcSel == 2);
    EnableWindow(GetDlgItem(hwnd, IDC_EDT_BITRATE), usesBitrateTarget && !guiIsTranscoding());
    EnableWindow(GetDlgItem(hwnd, IDC_EDT_QP), usesQualityTarget && !guiIsTranscoding());

    const int fpsSel = ComboBox_GetCurSel(GetDlgItem(hwnd, IDC_CMB_FPS));
    const bool rifeMode = (fpsSel == 3 || fpsSel == 4);
    EnableWindow(GetDlgItem(hwnd, IDC_EDT_RIFE_ENGINE), rifeMode && !guiIsTranscoding());
    EnableWindow(GetDlgItem(hwnd, IDC_BTN_BROWSE_RIFE), rifeMode && !guiIsTranscoding());

    const bool vsrChecked = (Button_GetCheck(GetDlgItem(hwnd, IDC_CHK_VSR)) == BST_CHECKED);
    EnableWindow(GetDlgItem(hwnd, IDC_CMB_VSR_QUAL), vsrChecked && !guiIsTranscoding());

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
            skin = (g_app.workspace == WorkspaceMode::Encoder) ? ButtonSkin::TabActive : ButtonSkin::Tab; break;
        case IDC_TAB_WS_PLAYER: caption = chromeText(ChromeText::WsPlayer);
            skin = (g_app.workspace == WorkspaceMode::Player) ? ButtonSkin::TabActive : ButtonSkin::Tab; break;
        case IDC_TAB_WS_NLE: caption = chromeText(ChromeText::WsNle);
            skin = (g_app.workspace == WorkspaceMode::Nle) ? ButtonSkin::TabActive : ButtonSkin::Tab; break;
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
void createWorkstationControls(HWND hwnd, HFONT font, HFONT font_bold) {
    (void)font_bold;
    auto createCtrl = [&](const wchar_t* cls, const wchar_t* txt, DWORD style, int id) -> HWND {
        HWND h = CreateWindowExW(0, cls, txt, WS_CHILD | WS_VISIBLE | style, 0, 0, 0, 0,
                                 hwnd, (HMENU)(intptr_t)id, GetModuleHandleW(nullptr), nullptr);
        SendMessageW(h, WM_SETFONT, (WPARAM)font, TRUE);
        SetWindowTheme(h, L"DarkMode_Explorer", nullptr);
        return h;
    };
    using CtrlSpec = std::tuple<const wchar_t*, int, const wchar_t*, DWORD>;
    constexpr DWORD kEditText = WS_BORDER | ES_AUTOHSCROLL;
    constexpr DWORD kEditNum = WS_BORDER | ES_NUMBER;
    constexpr DWORD kCombo = CBS_DROPDOWNLIST | WS_VSCROLL;
    static constexpr CtrlSpec kControls[] = {
        {L"STATIC", IDC_LBL_INPUT, L"Input:", 0},           {L"EDIT", IDC_EDT_INPUT, L"", kEditText},
        {L"BUTTON", IDC_BTN_BROWSE_IN, L"...", BS_OWNERDRAW},
        {L"STATIC", IDC_LBL_OUTPUT, L"Output:", 0},         {L"EDIT", IDC_EDT_OUTPUT, L"", kEditText},
        {L"BUTTON", IDC_BTN_BROWSE_OUT, L"...", BS_OWNERDRAW},
        {L"STATIC", IDC_LBL_CODEC, L"Codec:", 0},           {L"COMBOBOX", IDC_CMB_CODEC, L"", kCombo},
        {L"STATIC", IDC_LBL_PRESET, L"Preset:", 0},         {L"COMBOBOX", IDC_CMB_PRESET, L"", kCombo},
        {L"STATIC", IDC_LBL_RC, L"Rate Control:", 0},       {L"COMBOBOX", IDC_CMB_RC, L"", kCombo},
        {L"STATIC", IDC_LBL_BITRATE, L"Bitrate:", 0},       {L"EDIT", IDC_EDT_BITRATE, L"0", kEditNum},
        {L"STATIC", IDC_LBL_QP, L"QP:", 0},                 {L"EDIT", IDC_EDT_QP, L"22", kEditNum},
        {L"STATIC", IDC_LBL_RES, L"Resolution:", 0},        {L"COMBOBOX", IDC_CMB_RES, L"", kCombo},
        {L"STATIC", IDC_LBL_GEOM, L"Aspect:", 0},           {L"COMBOBOX", IDC_CMB_GEOM, L"", kCombo},
        {L"BUTTON", IDC_CHK_VSR, L"Enable RTX VSR", BS_AUTOCHECKBOX},
        {L"STATIC", IDC_LBL_VSR_QUAL, L"Lvl:", 0},          {L"COMBOBOX", IDC_CMB_VSR_QUAL, L"", kCombo},
        {L"STATIC", IDC_LBL_PEAK_NITS, L"Peak Nits:", 0},   {L"EDIT", IDC_EDT_PEAK_NITS, L"1000", kEditNum},
        {L"STATIC", IDC_LBL_FPS, L"Target FPS:", 0},        {L"COMBOBOX", IDC_CMB_FPS, L"", kCombo},
        {L"STATIC", IDC_LBL_RIFE, L"RIFE Engine:", 0},      {L"EDIT", IDC_EDT_RIFE_ENGINE, L"", kEditText},
        {L"BUTTON", IDC_BTN_BROWSE_RIFE, L"...", BS_OWNERDRAW},
        {L"STATIC", IDC_LBL_GPU, L"GPU:", 0},               {L"EDIT", IDC_EDT_GPU, L"0", kEditNum},
        {L"STATIC", IDC_LBL_FRAMES, L"Frames:", 0},         {L"EDIT", IDC_EDT_FRAMES, L"0", kEditNum},
        {L"BUTTON", IDC_CHK_AUDIO, L"Audio Passthrough", BS_AUTOCHECKBOX},
        {L"BUTTON", IDC_CHK_HDR10PLUS, L"HDR10+ (ST 2094-40)", BS_AUTOCHECKBOX},
        {L"STATIC", IDC_LBL_LUT, L"3D LUT:", 0},            {L"EDIT", IDC_EDT_LUT, L"", kEditText},
        {L"BUTTON", IDC_BTN_BROWSE_LUT, L"...", BS_OWNERDRAW},
        {L"BUTTON", IDC_BTN_START, L"Start Transcode", BS_OWNERDRAW},
        {L"BUTTON", IDC_BTN_STOP, L"Stop", BS_OWNERDRAW},
        {L"STATIC", IDC_LBL_STATUS, L"Ready", 0},           {L"STATIC", IDC_LBL_TELEMETRY, L"", 0},
        {L"LISTBOX", IDC_LST_QUEUE, L"", WS_BORDER | LBS_NOINTEGRALHEIGHT | WS_VSCROLL},
        {L"BUTTON", IDC_BTN_PRESET_MASTER, L"", BS_OWNERDRAW}, {L"BUTTON", IDC_BTN_PRESET_RIFE, L"", BS_OWNERDRAW},
        {L"BUTTON", IDC_BTN_PRESET_ESPORTS, L"", BS_OWNERDRAW}, {L"BUTTON", IDC_BTN_PRESET_COMPAT, L"", BS_OWNERDRAW},
        {L"BUTTON", IDC_BTN_PRESET_NATIVE, L"", BS_OWNERDRAW},
        {L"BUTTON", IDC_BTN_QUEUE_ADD, L"+", BS_OWNERDRAW}, {L"BUTTON", IDC_BTN_QUEUE_REMOVE, L"-", BS_OWNERDRAW},
        {L"BUTTON", IDC_BTN_QUEUE_CLEAR, L"x", BS_OWNERDRAW},
        {L"BUTTON", IDC_TAB_WS_ENCODER, L"", BS_OWNERDRAW}, {L"BUTTON", IDC_TAB_WS_PLAYER, L"", BS_OWNERDRAW},
        {L"BUTTON", IDC_TAB_WS_NLE, L"", BS_OWNERDRAW},     {L"BUTTON", IDC_BTN_PROBE, L"", BS_OWNERDRAW},
        {L"BUTTON", IDC_BTN_STEP_BACK, L"<<", BS_OWNERDRAW}, {L"BUTTON", IDC_BTN_STEP_FWD, L">>", BS_OWNERDRAW},
        {L"BUTTON", IDC_BTN_NLE_EXPORT, L"", BS_OWNERDRAW}, {L"STATIC", IDC_LBL_VSR_OSD, L"RTX VSR 1.00x", 0},
        {L"LISTBOX", IDC_LST_BIN, L"", WS_BORDER | LBS_NOTIFY | WS_VSCROLL},
        {L"STATIC", IDC_LBL_TIMECODE, L"SMPTE 00:00:00:00 | 0 Flicks (705,600,000 Hz)", 0},
        {L"BUTTON", IDC_BTN_MARK_IN, L"I", BS_OWNERDRAW},   {L"BUTTON", IDC_BTN_MARK_OUT, L"O", BS_OWNERDRAW},
        {L"BUTTON", IDC_BTN_RAZOR, L"C", BS_OWNERDRAW},
    };
    for (const auto& [cls, id, txt, style] : kControls) {
        (void)createCtrl(cls, txt, style, id);
    }
    (void)createCtrl(L"BUTTON", chromeText(ChromeText::LangButton), BS_OWNERDRAW, IDC_BTN_LANG);
    (void)createCtrl(L"BUTTON", playbackCaption(), BS_OWNERDRAW, IDC_BTN_PLAYER_PLAY);
    Button_SetCheck(GetDlgItem(hwnd, IDC_CHK_VSR), BST_CHECKED);
    Button_SetCheck(GetDlgItem(hwnd, IDC_CHK_AUDIO), BST_CHECKED);
    EnableWindow(GetDlgItem(hwnd, IDC_BTN_STOP), FALSE);
    HWND hProgress = createCtrl(PROGRESS_CLASSW, L"", 0, IDC_PROGRESS);
    SendMessageW(hProgress, PBM_SETRANGE32, 0, 1000);
    HWND hScrub = createCtrl(TRACKBAR_CLASSW, L"", TBS_HORZ, IDC_TRK_SCRUB);
    SendMessageW(hScrub, TBM_SETRANGE, TRUE, MAKELPARAM(0, 30000));
    SendMessageW(hScrub, TBM_SETPOS, TRUE, 0);

    HINSTANCE instance = GetModuleHandleW(nullptr);
    (void)registerTimelineCanvasWindowClass(instance);
    HWND hTimeline = CreateWindowExW(0, kTimelineCanvasClass, L"",
                                     WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 0, 0, 0, 0, hwnd,
                                     (HMENU)(intptr_t)IDC_NLE_TIMELINE, instance, nullptr);
    SendMessageW(hTimeline, WM_SETFONT, (WPARAM)font, TRUE);
}
WorkstationApp g_app;

void stopPlaybackSession() {
    g_app.playbackBorrow = nullptr;
    g_app.playbackSession.reset();  // 1. 先 join 工作线程，切断一切 sink->present() 调用
    if (g_app.previewViewport.has_value()) {
        (void)g_app.previewViewport->waitForGpuIdle(1000);
    }
    rtx::gui::drainDeferredRetirement();
    g_app.playbackSink.reset();     // 2. 线程退出且视口 GPU 队列排空后，安全释放无头桥与 slot_flags_
    g_app.playbackLastSlot = 0xFFFFFFFFu;
}
bool startPlaybackSession(HWND hwnd, const std::filesystem::path& path) {
    stopPlaybackSession();
    if (!g_app.previewViewport.has_value()) {
        return false;
    }
    auto demuxer = rtx::makeFfmpegDemuxer();
    if (!demuxer) {
        rtx::logError("playback: demuxer unavailable");
        return false;
    }
    auto info = demuxer->open(path);
    if (!info.has_value()) {
        rtx::logError("playback: demuxer open failed: " + info.error().toString());
        return false;
    }
    auto sink = rtx::createHeadlessPreviewSink(
        info.value(), [hwnd] { PostMessageW(hwnd, WM_APP_PREVIEW, 0, 0); }, g_app.playbackBorrow);
    if (!sink) {
        rtx::logError("playback: zero-readback sink unavailable (no NVIDIA GPU?)");
        return false;
    }
    rtx::PlaybackOptions options{};
    if (info.value().frame_rate.valid() && info.value().frame_rate.num > 0) {
        options.frame_rate = info.value().frame_rate;
    }
    auto session = rtx::MediaPlaybackSession::create(std::move(demuxer), info.value(), sink.get(),
                                                     options);
    if (!session.has_value()) {
        rtx::logError("playback: session create failed: " + session.error().toString());
        g_app.playbackBorrow = nullptr;
        return false;
    }
    g_app.playbackInfo = info.value();
    g_app.playbackSink = std::move(sink);
    g_app.playbackSession = std::move(session.value());
    g_app.playbackLastSlot = 0xFFFFFFFFu;
    (void)g_app.playbackSession->play();
    g_app.isPlaying = true;
    setGuiPlaying(true);
    rtx::logInfo("playback started: " + wideToUtf8(path.wstring()) + " pq=" +
                 (info.value().color.transfer == rtx::TransferFunction::SMPTE2084 ? "1" : "0"));
    return true;
}
void togglePlayback(HWND hwnd) {
    if (g_app.playbackSession) {
        if (g_app.isPlaying) {
            g_app.playbackSession->pause();
        } else {
            (void)g_app.playbackSession->play();
        }
    }
    g_app.isPlaying = !g_app.isPlaying;
    setGuiPlaying(g_app.isPlaying);
    InvalidateRect(GetDlgItem(hwnd, IDC_BTN_PLAYER_PLAY), nullptr, FALSE);
    rtx::logInfo(std::string("playback -> ") + (g_app.isPlaying ? "playing" : "paused"));
}
void stepPlayback(HWND hwnd, int direction) {
    if (g_app.playbackSession) {
        (void)g_app.playbackSession->stepFrame(direction);
        presentPlaybackFrame(hwnd);
    }
}
void seekPlaybackFromSlider(HWND hwnd) {
    if (!g_app.playbackSession) {
        return;
    }
    const LRESULT pos = SendMessageW(GetDlgItem(hwnd, IDC_TRK_SCRUB), TBM_GETPOS, 0, 0);
    const rtx::Flicks target_flicks = std::chrono::duration_cast<rtx::Flicks>(
        std::chrono::milliseconds(std::max<int64_t>(0, static_cast<int64_t>(pos))));
    (void)g_app.playbackSession->seek(target_flicks.count());
    presentPlaybackFrame(hwnd);
}
void installTranscodeBorrow() {
    g_app.transcodeBorrow = [](std::uint32_t& slot, rtx::PreviewSlotBorrow& borrow) {
        const std::uint32_t latest = rtx::NgxRemasterBridge::latestPreviewSlot();
        if (latest == slot || latest > 2) {
            return false;
        }
        void* device = nullptr;
        void* resource = nullptr;
        std::uint32_t pitch = 0;
        if (!rtx::NgxRemasterBridge::borrowActivePreview(latest, &device, &resource, &pitch) ||
            resource == nullptr) {
            return false;
        }
        borrow = {device, resource, pitch, rtx::NgxRemasterBridge::previewSlotFlags()};
        slot = latest;
        return true;
    };
}
void presentSlotToViewport(HWND hwnd, rtx::BorrowCallback& borrow, std::uint32_t& last_slot,
                           std::uint32_t src_width, std::uint32_t src_height, bool input_is_pq,
                           float max_nits) {
    (void)hwnd;
    if (!g_app.previewViewport.has_value() || !borrow) {
        return;
    }
    std::uint32_t slot = last_slot;
    rtx::PreviewSlotBorrow borrowed;
    if (!borrow(slot, borrowed)) {
        return;
    }
    auto& [device, resource, pitch, flags] = borrowed;
    if (resource == nullptr || flags == nullptr) {
        return;
    }
    g_app.previewViewport->presentShared(device, resource, pitch, src_width, src_height,
                                         input_is_pq, max_nits,
                                         PreviewSlotTicket{flags, slot});
    last_slot = slot;
}
void presentPlaybackFrame(HWND hwnd) {
    const auto& info = g_app.playbackInfo;
    presentSlotToViewport(hwnd, g_app.playbackBorrow, g_app.playbackLastSlot,
                          static_cast<std::uint32_t>(info.width),
                          static_cast<std::uint32_t>(info.height),
                          info.color.transfer == rtx::TransferFunction::SMPTE2084,
                          info.hdr.cll.max_cll > 0 ? static_cast<float>(info.hdr.cll.max_cll)
                                                   : 100.0f);
}
void presentTranscodePreview(HWND hwnd) {
    presentSlotToViewport(hwnd, g_app.transcodeBorrow, g_app.transcodeLastSlot, 960, 540,
                          /*input_is_pq=*/true, 100.0f);
}
}  // namespace rtx::gui
