// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
//
// Static localisation tables.  Everything here is constexpr-initialised and
// lands in .rdata: no static constructor runs, no heap is touched, and the
// accessor is a single indexed load.

#include "rtx/i18n.h"

#include <algorithm>
#include <atomic>
#include <cstddef>

namespace rtx {
namespace {

// ---------------------------------------------------------------------------
// .rdata tables: [language][StrId]
//
// The two static_asserts below are the whole safety net.  If someone adds an
// enumerator and forgets a translation, the build stops here instead of the GUI
// rendering an empty label.
// ---------------------------------------------------------------------------
constexpr const wchar_t* kTableW[static_cast<size_t>(Language::Count)][kStrIdCount] = {
    // English
    {
        L"CineRTX Professional GPU Remaster & NLE Engine",              // AppTitle
        L"Win32 - single EXE - no managed runtime",                     // AppSubtitle
        L"Hardware & Driver",                                           // StatusHardware
        L"NGX Plugins",                                                 // StatusNgxPlugins
        L"Frame Interpolation",                                         // StatusFrucEngine
        L"VRAM Pool Level",                                             // StatusVramWatermark
        L"Fast transcode",                                              // ModeTranscodeFast
        L"Master remaster",                                             // ModeRemasterMaster
        L"OFA (hardware optical flow)",                                 // ModeInterpOffRuc
        L"RIFE (TensorRT FP16)",                                        // ModeInterpRife
        L"NGX plugins are missing",                                     // DialogMissingPluginsTitle
        L"The RTX Video SDK feature DLLs were not found. Place them in "
        L"plugins\\nvidia_rtx or install the SDK, then retry.",          // DialogMissingPluginsBody
        L"Open plugin folder",                                          // BtnExplorePlugins
        L"Build RIFE engine",                                           // BtnBuildRife
        L"Input video",                                                 // DropTitle
        L"Drop an MP4 / MKV file into the window",                      // DropHint
        L"NVDEC decode - NVENC HEVC / H.264 encode - keeps the source "
        L"resolution and bit depth",                                    // DropSupported
        L"Output",                                                      // DropOutputPrefix
        L"Codec  HEVC",                                                 // CodecLabelHevc
        L"Codec  H.264",                                                // CodecLabelH264
        L"Preset",                                                      // PresetLabel
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
    },
    // Simplified Chinese
    {
        L"CineRTX 纯 GPU 专业转码与非编引擎",                            // AppTitle
        L"Win32 · 单 exe · 无托管运行时",                                // AppSubtitle
        L"硬件架构与驱动",                                               // StatusHardware
        L"NGX 增强插件",                                                 // StatusNgxPlugins
        L"AI 智能插帧引擎",                                              // StatusFrucEngine
        L"显存池安全水位",                                               // StatusVramWatermark
        L"快速转码",                                                     // ModeTranscodeFast
        L"母版级重制",                                                   // ModeRemasterMaster
        L"OFA 硬件光流",                                                 // ModeInterpOffRuc
        L"RIFE（TensorRT FP16）",                                        // ModeInterpRife
        L"缺少 NGX 插件",                                                // DialogMissingPluginsTitle
        L"未找到 RTX Video SDK 功能 DLL。请将其放入 plugins\\nvidia_rtx "
        L"或安装 SDK 后重试。",                                          // DialogMissingPluginsBody
        L"打开插件目录",                                                 // BtnExplorePlugins
        L"构建 RIFE 引擎",                                               // BtnBuildRife
        L"输入视频",                                                     // DropTitle
        L"将 MP4 / MKV 文件拖入窗口",                                    // DropHint
        L"NVDEC 解码 · NVENC HEVC / H.264 编码 · 保持源分辨率与位深",     // DropSupported
        L"输出",                                                         // DropOutputPrefix
        L"编码  HEVC",                                                   // CodecLabelHevc
        L"编码  H.264",                                                  // CodecLabelH264
        L"预设",                                                         // PresetLabel
        L"开始硬件转码",                                                 // ButtonStart
        L"停止转码",                                                     // ButtonStop
        L"拖入文件后开始",                                               // ButtonDropFirst
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
    },
};

constexpr const char* kStrIdNames[kStrIdCount] = {
    "AppTitle",
    "AppSubtitle",
    "StatusHardware",
    "StatusNgxPlugins",
    "StatusFrucEngine",
    "StatusVramWatermark",
    "ModeTranscodeFast",
    "ModeRemasterMaster",
    "ModeInterpOffRuc",
    "ModeInterpRife",
    "DialogMissingPluginsTitle",
    "DialogMissingPluginsBody",
    "BtnExplorePlugins",
    "BtnBuildRife",
    "DropTitle",
    "DropHint",
    "DropSupported",
    "DropOutputPrefix",
    "CodecLabelHevc",
    "CodecLabelH264",
    "PresetLabel",
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
};

// 1:1 alignment between the enumerators and both language tables.
static_assert(sizeof(kTableW[0]) / sizeof(kTableW[0][0]) == kStrIdCount,
              "i18n English table size mismatch");
static_assert(sizeof(kTableW[1]) / sizeof(kTableW[1][0]) == kStrIdCount,
              "i18n Chinese table size mismatch");
static_assert(sizeof(kStrIdNames) / sizeof(kStrIdNames[0]) == kStrIdCount,
              "i18n identifier table size mismatch");

std::atomic<Language> g_language{Language::Zh};

// Minimal UTF-16 (Windows) / UTF-32 (POSIX) to UTF-8 encoder.  A local copy is
// preferable to a dependency: the CLI must not link ICU and the GUI already
// owns its own conversion helpers.
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
        return Result<Language>::ok(Language::En);
    }
    if (lowered == "zh" || lowered == "zh-cn" || lowered == "cn" || lowered == "chinese" ||
        lowered == "chs") {
        return Result<Language>::ok(Language::Zh);
    }
    return Result<Language>::fail(ErrorCode::InvalidArgument, "unknown language tag",
                                  std::string(tag));
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

}  // namespace rtx
