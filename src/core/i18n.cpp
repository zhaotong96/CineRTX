// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
//
// Static localisation tables. Everything here is constexpr-initialised and
// lands in .rdata: no static constructor runs, no heap is touched, and the
// accessor is a single indexed load.

#include "rtx/i18n.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdio>
#include <string>
#include <string_view>

namespace rtx {
namespace {

constexpr const wchar_t* kTableW[static_cast<size_t>(Language::Count)][kStrIdCount] = {
    // English
    {
        L"CineRTX Professional GPU Remaster & NLE Engine",              // AppTitle
        L"Win32 - single EXE - no managed runtime",                     // AppSubtitle
        L"Hardware & Driver",                                           // StatusHardware
        L"NGX Plugins",                                                 // StatusNgxPlugins
        L"Frame Interpolation",                                         // StatusFrucEngine
        L"VRAM Pool Level",                                             // StatusVramWatermark
        L"Input File",                                                  // ParamInput
        L"Output File",                                                 // ParamOutput
        L"Video Codec",                                                 // ParamCodec
        L"Resolution",                                                  // ParamResolution
        L"NVENC Preset",                                                // ParamPreset
        L"Rate Control",                                                // ParamRateControl
        L"Bitrate (kbps)",                                              // ParamBitrate
        L"Constant QP",                                                 // ParamCq
        L"RTX VSR Super Resolution",                                    // ParamVsr
        L"VSR Quality",                                                 // ParamVsrQuality
        L"RTX TrueHDR (SDR->HDR10)",                                    // ParamTrueHdr
        L"Peak Luminance (nits)",                                       // ParamPeakNits
        L"Aspect & Orientation",                                        // ParamAspect
        L"Audio Stream Copy (Passthrough)",                             // ParamAudioCopy
        L"Browse...",                                                   // BtnBrowse
        L"Start hardware transcode",                                    // ButtonStart
        L"Stop transcode",                                              // ButtonStop
        L"Drop a file to begin",                                        // ButtonDropFirst
        L"Task telemetry",                                              // TelemetryTitle
        L"WDDM async - zero-copy main loop",                            // TelemetrySubtitle
        L"Progress",                                                    // ProgressLabel
        L"in progress",                                                 // ProgressWorking
        L"-",                                                           // ProgressIdle
        L"Throughput",                                                  // StatThroughput
        L"Frames",                                                      // StatFrames
        L"Elapsed",                                                     // StatElapsed
        L"Zero-copy",                                                   // StatZeroCopy
        L"D2H 0 B - H2D 0 B",                                           // ValueZeroCopyClean
        L"Host copies detected",                                        // ValueHostCopies
        L"running",                                                     // ValueRunning
        L"-",                                                           // ValueDash
        L"Probing GPU / NVENC after a file is dropped",                 // StatusGpuProbing
        L"CUDA device unavailable: ",                                   // StatusGpuMissing
        L"NVIDIA driver is not ready: ",                                // StatusShimMissing
        L"Ready. Click Start hardware transcode to launch the job.",    // StatusReady
        L"Initialising NVDEC / NVENC and starting the async job...",    // StatusWorking
        L"Stopping the transcode, waiting for the current frame...",    // StatusStopping
        L"Task cancelled",                                              // StatusCancelled
        L"Transcode failed",                                            // StatusFailed
        L"Done",                                                        // StatusDonePrefix
        L"Input and output paths are identical; choose another file.",  // StatusSamePath
        L"Drop a video file, not a folder.",                            // StatusDropFileOnly
        L"The current job has not finished; wait before changing the "
        L"input.",                                                      // StatusBusy
        L"Cannot create the transcode worker thread.",                  // StatusNoThread
        L"Ctrl+L switches language",                                    // LangToggleHint
        L"Live viewport",                                               // ViewportTitle
        L"Move the pointer to split the original and remastered frame", // ViewportSplitHint
        L"Multi-track audio",                                           // AudioMixerTitle
        L"Low",                                                         // WatermarkLow
        L"High",                                                        // WatermarkHigh
        L"Critical",                                                    // WatermarkCritical
        L"Invalid argument",                                            // ErrorInvalidArgument
        L"I/O error",                                                   // ErrorIo
        L"Out of memory",                                               // ErrorOutOfMemory
        L"Device lost",                                                 // ErrorDeviceLost
        L"self-test PASS",                                              // SelfTestPass
        L"self-test FAIL",                                              // SelfTestFail
        L"Hardware probe OK",                                           // StatusProbeOk

        // Stage 2 SSOT additions: WebUI & shared parameter labels
        L"WebUI - JSON state-driven - fully decoupled from the backend",// WuiSubtitle
        L"Transcode parameters",                                        // WuiParamsTitle
        L"Target frame rate (FRUC interpolation)",                      // WuiParamFps
        L"RIFE TensorRT engine path",                                   // WuiParamRifeEngine
        L"HDR10+ dynamic metadata (ST 2094-40)",                        // WuiParamHdr10plus
        L"Auto-fill output file name",                                  // WuiAutoOutput
        L"3D LUT (.cube, applied in pack kernel)",                      // WuiParamLut
        L"GPU device index",                                            // WuiParamGpu
        L"Frame limit (0 = whole stream)",                              // WuiParamFrames
        L"Engine",                                                      // WuiEngine
        L"Hardware probe (--probe --json)",                             // WuiProbeTitle
        L"Run probe",                                                   // WuiProbeRun
        L"Job report",                                                  // WuiReportTitle
        L"Engine log",                                                  // WuiLogTitle
        L"Show",                                                        // WuiShowLog
        L"Go",                                                          // WuiFsGo
        L"Up",                                                          // WuiFsUp
        L"Select current",                                              // WuiFsSelect
        L"State document: state.json - frontend/backend communicate exclusively in JSON (HTTP + SSE)", // WuiFoot
        L"Probe unavailable",                                           // WuiProbeUnavailable
        L"Request failed",                                              // WuiReqFailed

        // Stage 2 SSOT additions: unified dropdown option labels
        L"AV1 10-bit (Default)",                                        // OptCodecAv1
        L"HEVC Main10 (H.265)",                                         // OptCodecHevc
        L"H.264 High (8-bit SDR - Deprecated)",                         // OptCodecH264
        L"P1 (Fastest)",                                                // OptPresetP1
        L"P2 (Faster)",                                                 // OptPresetP2
        L"P3 (Fast)",                                                   // OptPresetP3
        L"P4 (Medium)",                                                 // OptPresetP4
        L"P5 (Balanced / Default)",                                     // OptPresetP5
        L"P6 (Better Quality)",                                         // OptPresetP6
        L"P7 (Highest Quality)",                                        // OptPresetP7
        L"CBR (Constant Bitrate)",                                      // OptRcCbr
        L"VBR (Variable Bitrate)",                                      // OptRcVbr
        L"Constant QP",                                                 // OptRcQp
        L"4K UHD (3840x2160 / 2160x3840 Auto)",                         // OptRes4k
        L"2K QHD (2560x1440)",                                          // OptRes2k
        L"FHD (1920x1080)",                                             // OptResFhd
        L"Native (Source Resolution)",                                  // OptResNative
        L"Auto Direct (9:16 Portrait / 16:9 Landscape)",                // OptGeomAuto
        L"Pillarbox (3840x2160 Center Pad)",                            // OptGeomPillar
        L"Level 1 (Basic)",                                             // OptVsrQ1
        L"Level 2 (Standard)",                                          // OptVsrQ2
        L"Level 3 (High)",                                              // OptVsrQ3
        L"Level 4 (Ultra Quality - Default)",                           // OptVsrQ4
        L"Original FPS (No FRUC)",                                      // OptFpsOriginal
        L"60 FPS (NVOFA Optical Flow - Default)",                       // OptFps60Nvofa
        L"120 FPS (NVOFA Optical Flow)",                                // OptFps120Nvofa
        L"60 FPS (RIFE Deep Learning - TensorRT)",                      // OptFps60Rife
        L"120 FPS (RIFE Deep Learning - TensorRT)",                     // OptFps120Rife
    },
    // Simplified Chinese
    {
        L"CineRTX 纯 GPU 专业转码与非编引擎",                            // AppTitle
        L"Win32 · 单 exe · 无托管运行时",                                // AppSubtitle
        L"硬件架构与驱动",                                               // StatusHardware
        L"NGX 增强插件",                                                 // StatusNgxPlugins
        L"AI 智能插帧引擎",                                              // StatusFrucEngine
        L"显存池安全水位",                                               // StatusVramWatermark
        L"输入文件",                                                     // ParamInput
        L"输出文件",                                                     // ParamOutput
        L"编码格式",                                                     // ParamCodec
        L"目标分辨率",                                                   // ParamResolution
        L"编码预设",                                                     // ParamPreset
        L"码率控制",                                                     // ParamRateControl
        L"目标码率 (kbps)",                                              // ParamBitrate
        L"质量 QP",                                                      // ParamCq
        L"启用 RTX VSR 超分辨率",                                        // ParamVsr
        L"VSR 质量档位",                                                 // ParamVsrQuality
        L"启用 RTX TrueHDR (SDR->HDR10)",                                // ParamTrueHdr
        L"HDR 峰值亮度 (nits)",                                          // ParamPeakNits
        L"画面画幅与旋转",                                               // ParamAspect
        L"音频流无损直通 (Stream Copy)",                                 // ParamAudioCopy
        L"浏览...",                                                      // BtnBrowse
        L"开始硬件转码",                                                 // ButtonStart
        L"停止转码",                                                     // ButtonStop
        L"准备就绪",                                                     // ButtonDropFirst
        L"任务遥测",                                                     // TelemetryTitle
        L"WDDM 异步 · 零拷贝主循环",                                     // TelemetrySubtitle
        L"进度",                                                         // ProgressLabel
        L"处理中",                                                       // ProgressWorking
        L"—",                                                            // ProgressIdle
        L"吞吐",                                                         // StatThroughput
        L"帧数",                                                         // StatFrames
        L"耗时",                                                         // StatElapsed
        L"零拷贝",                                                       // StatZeroCopy
        L"D2H 0 B · H2D 0 B",                                            // ValueZeroCopyClean
        L"检测到主机拷贝",                                               // ValueHostCopies
        L"运行中",                                                       // ValueRunning
        L"—",                                                            // ValueDash
        L"拖入视频后检测 GPU / NVENC",                                   // StatusGpuProbing
        L"CUDA 设备不可用：",                                            // StatusGpuMissing
        L"NVIDIA 驱动未就绪：",                                          // StatusShimMissing
        L"已就绪。点击“开始硬件转码”启动后台任务。",                     // StatusReady
        L"正在初始化 NVDEC / NVENC 并启动异步转码…",                     // StatusWorking
        L"正在停止转码，等待当前帧完成…",                                // StatusStopping
        L"任务已取消",                                                   // StatusCancelled
        L"转码失败",                                                     // StatusFailed
        L"完成",                                                         // StatusDonePrefix
        L"输入与输出路径相同，请更换文件。",                             // StatusSamePath
        L"请拖入视频文件，而不是文件夹。",                               // StatusDropFileOnly
        L"当前任务尚未结束，完成后再更换输入。",                         // StatusBusy
        L"无法创建转码工作线程。",                                       // StatusNoThread
        L"Ctrl+L 切换语言",                                              // LangToggleHint
        L"实时监看视口",                                                 // ViewportTitle
        L"移动鼠标可对比原画与重制画面",                                 // ViewportSplitHint
        L"多轨音频",                                                     // AudioMixerTitle
        L"低位",                                                         // WatermarkLow
        L"高位",                                                         // WatermarkHigh
        L"临界",                                                         // WatermarkCritical
        L"参数无效",                                                     // ErrorInvalidArgument
        L"读写错误",                                                     // ErrorIo
        L"显存不足",                                                     // ErrorOutOfMemory
        L"设备丢失",                                                     // ErrorDeviceLost
        L"自检通过",                                                     // SelfTestPass
        L"自检失败",                                                     // SelfTestFail
        L"硬件探测正常",                                                 // StatusProbeOk

        // Stage 2 SSOT additions: WebUI & shared parameter labels
        L"WebUI · JSON 状态驱动 · 与后端彻底解耦",                       // WuiSubtitle
        L"转码参数",                                                     // WuiParamsTitle
        L"目标帧率 (FRUC 补帧)",                                         // WuiParamFps
        L"RIFE TensorRT 引擎路径",                                       // WuiParamRifeEngine
        L"HDR10+ 动态元数据 (ST 2094-40)",                               // WuiParamHdr10plus
        L"自动补全输出文件名",                                           // WuiAutoOutput
        L"3D LUT (.cube, pack 内核内应用)",                              // WuiParamLut
        L"GPU 设备序号",                                                 // WuiParamGpu
        L"帧数上限 (0 = 全流)",                                          // WuiParamFrames
        L"后端引擎",                                                     // WuiEngine
        L"硬件探针 (--probe --json)",                                    // WuiProbeTitle
        L"运行探针",                                                     // WuiProbeRun
        L"任务报告",                                                     // WuiReportTitle
        L"引擎日志",                                                     // WuiLogTitle
        L"显示",                                                         // WuiShowLog
        L"跳转",                                                         // WuiFsGo
        L"上级",                                                         // WuiFsUp
        L"选择当前",                                                     // WuiFsSelect
        L"状态文档: state.json · 前后端仅以 JSON 通信 (HTTP + SSE)",     // WuiFoot
        L"探针不可用",                                                   // WuiProbeUnavailable
        L"请求失败",                                                     // WuiReqFailed

        // Stage 2 SSOT additions: unified dropdown option labels
        L"AV1 10-bit (默认推荐)",                                        // OptCodecAv1
        L"HEVC Main10 (H.265)",                                          // OptCodecHevc
        L"H.264 High (8-bit SDR · 已废弃)",                              // OptCodecH264
        L"P1 (最快)",                                                    // OptPresetP1
        L"P2 (更快)",                                                    // OptPresetP2
        L"P3 (快)",                                                      // OptPresetP3
        L"P4 (中等)",                                                    // OptPresetP4
        L"P5 (平衡默认)",                                                // OptPresetP5
        L"P6 (更高画质)",                                                // OptPresetP6
        L"P7 (最高画质)",                                                // OptPresetP7
        L"CBR (恒定码率)",                                               // OptRcCbr
        L"VBR (动态码率)",                                               // OptRcVbr
        L"Constant QP (固定量化)",                                       // OptRcQp
        L"4K UHD (3840x2160 / 2160x3840 自适应)",                        // OptRes4k
        L"2K QHD (2560x1440)",                                           // OptRes2k
        L"FHD (1920x1080)",                                              // OptResFhd
        L"保持原画分辨率 (Native)",                                      // OptResNative
        L"自动竖屏检测旋转 (9:16 / 16:9 智能适配)",                      // OptGeomAuto
        L"横屏填充黑边居中 (Pillarbox 3840x2160)",                       // OptGeomPillar
        L"Level 1 (Basic)",                                              // OptVsrQ1
        L"Level 2 (Standard)",                                           // OptVsrQ2
        L"Level 3 (High)",                                               // OptVsrQ3
        L"Level 4 (极致超分 - 默认)",                                    // OptVsrQ4
        L"保持原帧率 (不补帧)",                                          // OptFpsOriginal
        L"60 FPS (NVOFA 硬件光流 - 默认)",                               // OptFps60Nvofa
        L"120 FPS (NVOFA 硬件光流)",                                     // OptFps120Nvofa
        L"60 FPS (RIFE 深度学习 - TensorRT)",                            // OptFps60Rife
        L"120 FPS (RIFE 深度学习 - TensorRT)",                           // OptFps120Rife
    },
};

constexpr const char* kStrIdNames[kStrIdCount] = {
    "AppTitle",
    "AppSubtitle",
    "StatusHardware",
    "StatusNgxPlugins",
    "StatusFrucEngine",
    "StatusVramWatermark",
    "ParamInput",
    "ParamOutput",
    "ParamCodec",
    "ParamResolution",
    "ParamPreset",
    "ParamRateControl",
    "ParamBitrate",
    "ParamCq",
    "ParamVsr",
    "ParamVsrQuality",
    "ParamTrueHdr",
    "ParamPeakNits",
    "ParamAspect",
    "ParamAudioCopy",
    "BtnBrowse",
    "ButtonStart",
    "ButtonStop",
    "ButtonDropFirst",
    "TelemetryTitle",
    "TelemetrySubtitle",
    "ProgressLabel",
    "ProgressWorking",
    "ProgressIdle",
    "StatThroughput",
    "StatFrames",
    "StatElapsed",
    "StatZeroCopy",
    "ValueZeroCopyClean",
    "ValueHostCopies",
    "ValueRunning",
    "ValueDash",
    "StatusGpuProbing",
    "StatusGpuMissing",
    "StatusShimMissing",
    "StatusReady",
    "StatusWorking",
    "StatusStopping",
    "StatusCancelled",
    "StatusFailed",
    "StatusDonePrefix",
    "StatusSamePath",
    "StatusDropFileOnly",
    "StatusBusy",
    "StatusNoThread",
    "LangToggleHint",
    "ViewportTitle",
    "ViewportSplitHint",
    "AudioMixerTitle",
    "WatermarkLow",
    "WatermarkHigh",
    "WatermarkCritical",
    "ErrorInvalidArgument",
    "ErrorIo",
    "ErrorOutOfMemory",
    "ErrorDeviceLost",
    "SelfTestPass",
    "SelfTestFail",
    "StatusProbeOk",

    "WuiSubtitle",
    "WuiParamsTitle",
    "WuiParamFps",
    "WuiParamRifeEngine",
    "WuiParamHdr10plus",
    "WuiAutoOutput",
    "WuiParamLut",
    "WuiParamGpu",
    "WuiParamFrames",
    "WuiEngine",
    "WuiProbeTitle",
    "WuiProbeRun",
    "WuiReportTitle",
    "WuiLogTitle",
    "WuiShowLog",
    "WuiFsGo",
    "WuiFsUp",
    "WuiFsSelect",
    "WuiFoot",
    "WuiProbeUnavailable",
    "WuiReqFailed",

    "OptCodecAv1",
    "OptCodecHevc",
    "OptCodecH264",
    "OptPresetP1",
    "OptPresetP2",
    "OptPresetP3",
    "OptPresetP4",
    "OptPresetP5",
    "OptPresetP6",
    "OptPresetP7",
    "OptRcCbr",
    "OptRcVbr",
    "OptRcQp",
    "OptRes4k",
    "OptRes2k",
    "OptResFhd",
    "OptResNative",
    "OptGeomAuto",
    "OptGeomPillar",
    "OptVsrQ1",
    "OptVsrQ2",
    "OptVsrQ3",
    "OptVsrQ4",
    "OptFpsOriginal",
    "OptFps60Nvofa",
    "OptFps120Nvofa",
    "OptFps60Rife",
    "OptFps120Rife",
};

static_assert(sizeof(kTableW[0]) / sizeof(kTableW[0][0]) == kStrIdCount,
              "i18n English table size mismatch");
static_assert(sizeof(kTableW[1]) / sizeof(kTableW[1][0]) == kStrIdCount,
              "i18n Chinese table size mismatch");
static_assert(sizeof(kStrIdNames) / sizeof(kStrIdNames[0]) == kStrIdCount,
              "i18n identifier table size mismatch");

std::atomic<Language> g_language{Language::Zh};

void appendCodePoint(std::string& out, uint32_t code_point) {
    if (code_point <= 0x7F) {
        out.push_back(static_cast<char>(code_point));
    } else if (code_point <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (code_point >> 6)));
        out.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    } else if (code_point <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | (code_point >> 12)));
        out.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (code_point >> 18)));
        out.push_back(static_cast<char>(0x80 | ((code_point >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    }
}

std::string toUtf8(const wchar_t* text) {
    std::string out;
    if (text == nullptr) {
        return out;
    }
    out.reserve(64);
    for (size_t index = 0; text[index] != L'\0'; ++index) {
        uint32_t code_point = static_cast<uint32_t>(text[index]);
        if constexpr (sizeof(wchar_t) == 2) {
            if (code_point >= 0xD800 && code_point <= 0xDBFF) {
                const uint32_t low = static_cast<uint32_t>(text[index + 1]);
                if (low >= 0xDC00 && low <= 0xDFFF) {
                    code_point = 0x10000 + ((code_point - 0xD800) << 10) + (low - 0xDC00);
                    ++index;
                } else {
                    code_point = 0xFFFD;
                }
            }
        }
        appendCodePoint(out, code_point);
    }
    return out;
}

std::string escapeJsonUtf8(std::string_view input) {
    std::string out;
    out.reserve(input.size() + 8);
    for (const char raw : input) {
        const unsigned char c = static_cast<unsigned char>(raw);
        if (c == '"') {
            out += "\\\"";
        } else if (c == '\\') {
            out += "\\\\";
        } else if (c == '\n') {
            out += "\\n";
        } else if (c == '\r') {
            out += "\\r";
        } else if (c == '\t') {
            out += "\\t";
        } else if (c < 0x20) {
            char buf[8] = {};
            std::snprintf(buf, sizeof(buf), "\\u%04x", c);
            out += buf;
        } else {
            out.push_back(raw);
        }
    }
    return out;
}

}  // namespace

Language currentLanguage() noexcept {
    return g_language.load(std::memory_order_relaxed);
}

void setLanguage(Language language) noexcept {
    if (language == Language::En || language == Language::Zh) {
        g_language.store(language, std::memory_order_relaxed);
    }
}

Language toggleLanguage() noexcept {
    const Language next = currentLanguage() == Language::Zh ? Language::En : Language::Zh;
    setLanguage(next);
    return next;
}

const char* languageTag(Language language) noexcept {
    return language == Language::Zh ? "zh" : "en";
}

Result<Language> parseLanguage(std::string_view tag) {
    std::string lowered;
    lowered.reserve(tag.size());
    for (char c : tag) {
        lowered.push_back(static_cast<char>(c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c));
    }
    if (lowered.empty() || lowered == "en" || lowered == "en-us" || lowered == "english") {
        return Language::En;
    }
    if (lowered == "zh" || lowered == "zh-cn" || lowered == "cn" || lowered == "chinese" ||
        lowered == "chs") {
        return Language::Zh;
    }
    return err(ErrorCode::InvalidArgument, "unknown language tag",
               std::string(tag));
}

std::wstring_view trWView(StrId id) noexcept {
    return trW(id);
}

std::string_view strIdNameView(StrId id) noexcept {
    const size_t index = static_cast<size_t>(id);
    if (index >= kStrIdCount) {
        return "Unknown";
    }
    return kStrIdNames[index];
}

const wchar_t* trW(StrId id) noexcept {
    const size_t index = static_cast<size_t>(id);
    if (index >= kStrIdCount) {
        return L"";
    }
    const size_t language = static_cast<size_t>(currentLanguage());
    const wchar_t* text = kTableW[language][index];
    return text != nullptr ? text : L"";
}

std::string tr(StrId id) { return toUtf8(trW(id)); }

std::string trForLanguage(Language language, StrId id) {
    const size_t index = static_cast<size_t>(id);
    const size_t lang_idx = static_cast<size_t>(language);
    if (index >= kStrIdCount || lang_idx >= static_cast<size_t>(Language::Count)) {
        return {};
    }
    return toUtf8(kTableW[lang_idx][index]);
}

const char* strIdName(StrId id) noexcept {
    const size_t index = static_cast<size_t>(id);
    return index < kStrIdCount ? kStrIdNames[index] : "Unknown";
}

StrId strIdForError(ErrorCode code) noexcept {
    switch (code) {
        case ErrorCode::InvalidArgument:
        case ErrorCode::Unsupported:
        case ErrorCode::Config:
        case ErrorCode::State:
            return StrId::ErrorInvalidArgument;
        case ErrorCode::Io:
        case ErrorCode::NotFound:
            return StrId::ErrorIo;
        case ErrorCode::OutOfMemory:
            return StrId::ErrorOutOfMemory;
        case ErrorCode::DeviceLost:
        case ErrorCode::ArchUnsupported:
        case ErrorCode::DriverTooOld:
            return StrId::ErrorDeviceLost;
        default:
            return StrId::StatusFailed;
    }
}

std::string exportI18nJson() {
    std::string out = "{\n";
    const Language langs[2] = {Language::En, Language::Zh};
    for (size_t l = 0; l < 2; ++l) {
        out += "    \"";
        out += languageTag(langs[l]);
        out += "\": {\n";
        for (size_t i = 0; i < kStrIdCount; ++i) {
            const StrId id = static_cast<StrId>(i);
            out += "      \"";
            out += strIdName(id);
            out += "\": \"";
            out += escapeJsonUtf8(trForLanguage(langs[l], id));
            out += (i + 1 < kStrIdCount) ? "\",\n" : "\"\n";
        }
        out += (l == 0) ? "    },\n" : "    }\n";
    }
    out += "  }";
    return out;
}

}  // namespace rtx
