// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <minidumpapiset.h>
#include <shellapi.h>
#endif

#include <algorithm>
#include <clocale>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <iomanip>
#include <iostream>
#include <locale>
#include <memory>
#include <string>
#include <vector>

#include "rtx/hw_transcode.h"
#include "rtx/i18n.h"
#include "rtx/runtime_env.h"
#include "rtx/ngx_remaster.h"
#include "rtx/status.h"
#include "rtx/types.h"
#include "rtx/vram_pool.h"
#include "shim_probe.h"
#include "viewport_throttle.h"

#if defined(_WIN32)
extern "C" {
// Wake NVIDIA discrete GPU high performance profile on Optimus laptops
__declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;
}
#endif

#ifndef RTX_VERSION_STRING
#define RTX_VERSION_STRING "0.2.0"
#endif

namespace {

using namespace rtx;

enum class Mode {
    None,
    Probe,
    Container,
    ViewportSelfTest,
    Help,
    Version,
};

struct Options {
    Mode mode = Mode::None;
    int frames = 0;
    int width = 1920;
    int height = 1080;
    int target_width = 0;
    int target_height = 0;
    Rational input_fps{24, 1};
    int vsr_quality = 2;
    bool json = false;
    bool quiet = false;
    bool verbose = false;
    bool frames_set = false;
    bool fps_set = false;
    bool four_k_profile = false;
    bool no_cuda_graph = false;
    bool truehdr = false;
    bool hdr_set = false;
    std::filesystem::path input_path;
    std::filesystem::path output_path;
    std::filesystem::path onnx;
    std::filesystem::path engine;
    std::string language;
    VideoCodec codec = VideoCodec::AV1;
    int bitrate_kbps = 0;
    int preset = 5;
    int rate_control = 0;
    int qp = 0;
};

void printHelp() {
    std::cout << "CineRTX " << RTX_VERSION_STRING << "\n"
              << tr(StrId::AppTitle) << "\n"
              << "Offline SDR -> 4K HDR10 transcode engine\n\n"
              << "Usage:\n"
              << "  CineRTX --probe [--json]\n"
              << "  CineRTX --input in.mp4 --output out.mp4 [options]\n"
              << "  CineRTX --viewport-self-test\n\n"
              << "Options:\n"
              << "  --input PATH                 Container input (MP4/MKV)\n"
              << "  --output PATH                Container output (MP4/MKV)\n"
              << "  --codec av1|hevc|h264        Output video codec (default: av1)\n"
              << "  --bitrate KBPS               Encoder target bitrate (0 = auto)\n"
              << "  --preset 1..7                NVENC P1..P7 preset (default 5)\n"
              << "  --rate-control cbr|vbr|qp    NVENC rate control (default cbr)\n"
              << "  --qp N                       Constant QP when --rate-control qp\n"
              << "  --width, --height            Input geometry (default 1920x1080)\n"
              << "  --target-width/height        Remaster output geometry (default 3840x2160)\n"
              << "  --fps N[/D]                  Input frame rate (default 24)\n"
              << "  --frames N                   Frame limit (0 = whole stream)\n"
              << "  --vsr-quality 1..4           NGX VSR quality (default 2)\n"
              << "  --json                       Machine-readable output\n"
              << "  --quiet, --verbose           Logging verbosity\n"
              << "  --truehdr                    Enable TrueHDR inverse tone mapping (SDR -> HDR10)\n"
              << "  --sdr                        Force SDR native color space (default for SDR sources)\n"
              << "  --lang en|zh                 Output language (default: zh)\n"
              << "  --version, --help            Show version/help\n\n"
              << "The product transcode: every container input is decoded by NVDEC, remastered by\n"
              << "RTX VSR in device memory, and encoded as AV1 10-bit / HEVC Main10 / H.264.\n";
}

bool parseInteger(const std::string& text, int& value) {
    try {
        size_t consumed = 0;
        const int parsed = std::stoi(text, &consumed);
        if (consumed != text.size()) {
            return false;
        }
        value = parsed;
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool requireValue(int argc, char** argv, int& index, std::string& value) {
    if (index + 1 >= argc) {
        std::cerr << "error: " << argv[index] << " requires a value\n";
        return false;
    }
    value = argv[++index];
    return true;
}

bool requirePath(int argc, char** argv, int& index, std::filesystem::path& path) {
    if (index + 1 >= argc) {
        std::cerr << "error: " << argv[index] << " requires a value\n";
        return false;
    }
    path = std::filesystem::path(argv[++index]);
    return true;
}

bool parseOptions(int argc, char** argv, Options& options, std::string& error) {
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        std::string value;
        if (argument == "--help" || argument == "-h") {
            options.mode = Mode::Help;
        } else if (argument == "--version") {
            options.mode = Mode::Version;
        } else if (argument == "--probe") {
            options.mode = Mode::Probe;
        } else if (argument == "--viewport-self-test") {
            options.mode = Mode::ViewportSelfTest;
        } else if (argument == "--lang") {
            if (!requireValue(argc, argv, i, options.language)) {
                return false;
            }
            const Result<Language> parsed = parseLanguage(options.language);
            if (!parsed.has_value()) {
                error = "unknown language: " + options.language;
                return false;
            }
            setLanguage(parsed.value());
        } else if (argument == "--input") {
            if (!requirePath(argc, argv, i, options.input_path)) {
                return false;
            }
            options.mode = Mode::Container;
        } else if (argument == "--output") {
            if (!requirePath(argc, argv, i, options.output_path)) {
                return false;
            }
        } else if (argument == "--codec") {
            if (!requireValue(argc, argv, i, value)) {
                return false;
            }
            std::transform(value.begin(), value.end(), value.begin(),
                           [](unsigned char c) { return static_cast<char>(c | 0x20); });
            if (value == "av1" || value == "0") {
                options.codec = VideoCodec::AV1;
            } else if (value == "hevc" || value == "h265" || value == "1") {
                options.codec = VideoCodec::HEVC;
            } else if (value == "h264" || value == "avc" || value == "2") {
                options.codec = VideoCodec::H264;
            } else {
                error = "--codec must be av1, hevc or h264";
                return false;
            }
        } else if (argument == "--rate-control") {
            if (!requireValue(argc, argv, i, value)) {
                return false;
            }
            std::transform(value.begin(), value.end(), value.begin(),
                           [](unsigned char c) { return static_cast<char>(c | 0x20); });
            if (value == "cbr" || value == "0") {
                options.rate_control = 0;
            } else if (value == "vbr" || value == "1") {
                options.rate_control = 1;
            } else if (value == "qp" || value == "constqp" || value == "2") {
                options.rate_control = 2;
            } else {
                error = "--rate-control must be cbr, vbr or qp";
                return false;
            }
        } else if (argument == "--width" || argument == "--height" ||
                   argument == "--target-width" || argument == "--target-height" ||
                   argument == "--frames" || argument == "--vsr-quality" ||
                   argument == "--bitrate" || argument == "--preset" || argument == "--qp") {
            if (!requireValue(argc, argv, i, value)) {
                return false;
            }
            int parsed = 0;
            if (!parseInteger(value, parsed)) {
                error = "invalid integer for " + argument + ": " + value;
                return false;
            }
            if (argument == "--width") {
                options.width = parsed;
            } else if (argument == "--height") {
                options.height = parsed;
            } else if (argument == "--target-width") {
                options.target_width = parsed;
            } else if (argument == "--target-height") {
                options.target_height = parsed;
            } else if (argument == "--frames") {
                options.frames = parsed;
                options.frames_set = true;
            } else if (argument == "--bitrate") {
                options.bitrate_kbps = parsed;
            } else if (argument == "--preset") {
                options.preset = parsed;
            } else if (argument == "--qp") {
                options.qp = parsed;
            } else {
                options.vsr_quality = parsed;
            }
        } else if (argument == "--fps") {
            if (!requireValue(argc, argv, i, value)) {
                return false;
            }
            const Result<Rational> parsed = Rational::parse(value);
            if (!parsed.has_value()) {
                error = "invalid frame rate for " + argument + ": " + value;
                return false;
            }
            options.input_fps = parsed.value();
            options.fps_set = true;
        } else if (argument == "--onnx") {
            if (!requirePath(argc, argv, i, options.onnx)) {
                return false;
            }
        } else if (argument == "--engine") {
            if (!requirePath(argc, argv, i, options.engine)) {
                return false;
            }
        } else if (argument == "--json") {
            options.json = true;
        } else if (argument == "--quiet") {
            options.quiet = true;
        } else if (argument == "--verbose") {
            options.verbose = true;
        } else if (argument == "--4k-profile") {
            options.four_k_profile = true;
        } else if (argument == "--truehdr") {
            options.truehdr = true;
            options.hdr_set = true;
        } else if (argument == "--sdr") {
            options.truehdr = false;
            options.hdr_set = true;
        } else if (argument == "--no-cuda-graph") {
            options.no_cuda_graph = true;
        } else {
            error = "unknown argument: " + argument;
            return false;
        }
    }
    if (options.mode == Mode::None) {
        error = "no command specified (try --help)";
        return false;
    }
    if (options.width <= 0 || options.height <= 0 || options.target_width < 0 ||
        options.target_height < 0) {
        error = "geometry must be positive (target may be 0 for auto 4K)";
        return false;
    }
    if (options.vsr_quality < 1 || options.vsr_quality > 4) {
        error = "--vsr-quality must be in 1..4";
        return false;
    }
    if (options.preset < 1 || options.preset > 7) {
        error = "--preset must be in 1..7";
        return false;
    }
    if (options.bitrate_kbps < 0 || options.qp < 0 || options.qp > 51) {
        error = "--bitrate/--qp are out of range";
        return false;
    }
    if (options.mode == Mode::Container) {
        if (options.input_path.empty()) {
            error = "--input needs a container path";
            return false;
        }
        if (options.output_path.empty()) {
            error = "container transcode requires --output";
            return false;
        }
    }
    return true;
}

void printNgxBridgeProbe() {
    rtx::NgxRemasterBridge bridge;
    std::string error;
    if (!bridge.open(&error)) {
        std::cout << "\nNGX bridge probe\n  status: unavailable\n  detail: " << error << "\n";
        return;
    }
    RtxNgxBridgeCaps caps{};
    const RtxResult result = bridge.queryCaps(&caps);
    std::cout << "\nNGX bridge probe\n  bridge: " << bridge.path() << "\n";
    std::cout << "  vsr: " << (caps.vsr_available != 0 ? "available" : "missing")
              << "; truehdr: " << (caps.truehdr_available != 0 ? "available" : "missing")
              << "; d3d12: " << (caps.d3d12_device_created != 0 ? "created" : "not created")
              << "; cuda_interop: "
              << (caps.cuda_interop_available != 0 ? "available" : "unavailable") << "\n";
    std::cout << "  detail: " << caps.diagnostics << "\n";
    if (result != RTX_OK) {
        std::cout << "  query_caps: error " << static_cast<int>(result) << "\n";
    }
}

std::string formatCombinedProbeJson(const cli::ShimProbeResult& probe) {
    std::string base = cli::formatProbeJson(probe);
    rtx::NgxRemasterBridge bridge;
    std::string error;
    const bool opened = bridge.open(&error);
    RtxNgxBridgeCaps caps{};
    const RtxResult result = opened ? bridge.queryCaps(&caps) : RTX_ERR_DLL_MISSING;

    const std::string needle = "}\n";
    const auto pos = base.rfind(needle);
    if (pos != std::string::npos) {
        std::ostringstream ngx;
        ngx << ",\n  \"ngx_bridge\": {\n"
            << "    \"available\": " << (opened ? "true" : "false") << ",\n"
            << "    \"bridge\": \"" << cli::escapeJson(opened ? bridge.path() : "") << "\",\n"
            << "    \"vsr\": " << (caps.vsr_available != 0 ? "true" : "false") << ",\n"
            << "    \"truehdr\": " << (caps.truehdr_available != 0 ? "true" : "false") << ",\n"
            << "    \"d3d12\": " << (caps.d3d12_device_created != 0 ? "true" : "false") << ",\n"
            << "    \"cuda_interop\": " << (caps.cuda_interop_available != 0 ? "true" : "false") << ",\n"
            << "    \"result\": " << static_cast<int>(result) << "\n"
            << "  }\n"
            << "}\n";
        base.replace(pos, needle.length(), ngx.str());
    }
    return base;
}

int runProbe(const Options& options) {
    const cli::ShimProbeResult probe = cli::probeShim();
    if (options.json) {
        std::cout << formatCombinedProbeJson(probe);
    } else {
        std::cout << cli::formatProbeHuman(probe);
        printNgxBridgeProbe();
    }
    if (!probe.loaded) {
        return 4;
    }
    if (probe.result != RTX_OK) {
        return 5;
    }
    return 0;
}

HwTranscodeOptions makeHwOptions(const Options& options) {
    HwTranscodeOptions hardware;
    hardware.input_path = options.input_path;
    hardware.output_path = options.output_path;
    hardware.width = options.width;
    hardware.height = options.height;
    hardware.frame_limit = options.frames_set ? options.frames : 0;
    hardware.codec = options.codec;
    hardware.bit_depth = 10;
    hardware.bitrate_kbps = options.bitrate_kbps;
    hardware.preset = options.preset;
    hardware.rate_control = options.rate_control;
    hardware.qp = options.qp;
    hardware.fps = options.fps_set ? options.input_fps : Rational{0, 1};
    hardware.target_width = options.target_width;
    hardware.target_height = options.target_height;
    hardware.vsr_quality = options.vsr_quality;
    hardware.verbose = options.verbose;
    hardware.hdr = options.hdr_set ? options.truehdr : false;
    return hardware;
}

void printHwReport(const HwTranscodeReport& report, const Options& options) {
    if (options.json) {
        std::cout << report.toJson();
        return;
    }
    std::cout << "backend: " << report.backend << " (" << report.device << ")\n";
    std::cout << "path: " << report.path << "\n";
    std::cout << "input: " << report.input_codec << " " << report.input_width << "x"
              << report.input_height << " (" << report.input_container << ")\n";
    std::cout << "output: " << report.output_codec << " " << report.output_width << "x"
              << report.output_height << " (" << report.output_bit_depth << "-bit, "
              << report.output_container << ")\n";
    std::cout << "frames: " << report.frames_decoded << " decoded, " << report.frames_encoded
              << " encoded; packets: " << report.packets_read << " read, "
              << report.packets_written << " written\n";
    std::cout << "bitstream bytes: " << report.bitstream_bytes << "\n";
    std::cout << "zero-copy main loop: " << (report.zero_copy_main_loop ? "yes" : "no") << "\n";
    std::cout << "host pixel copies: decoder=" << report.decoder_host_pixel_copies
              << ", encoder=" << report.encoder_host_pixel_copies
              << "; D2H=" << report.decoder_device_to_host_bytes
              << " bytes, H2D=" << report.encoder_host_to_device_bytes << " bytes\n";
    std::cout << "device-to-device bytes: " << report.decoder_device_to_device_bytes << "\n";
    if (report.remaster_pack_launches > 0) {
        std::cout << "remaster: vsr_quality " << report.vsr_quality << ", "
                  << report.remaster_pack_launches << " pack launches; avg preprocess "
                  << std::setprecision(3) << report.remaster_preprocess_ms << " ms, NGX "
                  << report.remaster_ngx_ms << " ms, pack " << report.remaster_pack_kernel_ms
                  << " ms (max " << report.remaster_pack_kernel_max_ms << " ms), frame "
                  << report.remaster_frame_ms << " ms\n";
        std::cout << std::fixed << std::setprecision(1);
    }
    if (report.main_loop_ms > 0.0 && report.frames_encoded > 0) {
        std::cout << "main loop: " << std::fixed << std::setprecision(1) << report.main_loop_ms
                  << " ms, steady " << std::setprecision(2)
                  << (static_cast<double>(report.frames_encoded) * 1000.0 / report.main_loop_ms)
                  << " FPS\n";
    }
    std::cout << "elapsed: " << std::fixed << std::setprecision(1) << report.elapsed_ms
              << " ms\n";
}

int runHardwareMode(const Options& options) {
    HwTranscodeOptions hardware = makeHwOptions(options);
    if (!options.quiet && !options.json) {
        hardware.progress = [](const HwTranscodeProgress& p) {
            if (p.total_frames > 0) {
                const double pct = static_cast<double>(p.frames_encoded) * 100.0 / p.total_frames;
                const double fps = p.elapsed_ms > 0 ? (p.frames_encoded * 1000.0 / p.elapsed_ms) : 0.0;
                std::cout << std::format("\r[Transcode] {:5.1f}% ({}/{} frames, {:5.1f} FPS)",
                                         pct, p.frames_encoded, p.total_frames, fps) << std::flush;
            } else {
                const double fps = p.elapsed_ms > 0 ? (p.frames_encoded * 1000.0 / p.elapsed_ms) : 0.0;
                std::cout << std::format("\r[Transcode] {} frames encoded ({:5.1f} FPS)",
                                         p.frames_encoded, fps) << std::flush;
            }
        };
    }
    Result<HwTranscodeReport> result = runNvidiaRemasterTranscode(hardware);
    if (!options.quiet && !options.json) {
        std::cout << "\n";
    }
    if (!result.has_value()) {
        std::cerr << "error: " << result.error().toString() << "\n";
        return 1;
    }
    printHwReport(result.value(), options);
    return 0;
}

// Headless verification of the DirectComposition viewport contract (Gate 6).
int runViewportSelfTest() {
    gui::ViewportThrottle throttle;
    constexpr int kTicks = 1000;
    constexpr double kTickSeconds = 0.001;
    int presents = 0;
    for (int tick = 0; tick < kTicks; ++tick) {
        if (throttle.shouldPresent(static_cast<double>(tick) * kTickSeconds, false, false)) {
            ++presents;
        }
    }
    const bool rate_ok = presents >= 59 && presents <= 61;

    gui::ViewportThrottle occluded;
    int occluded_presents = 0;
    for (int tick = 0; tick < 120; ++tick) {
        if (occluded.shouldPresent(static_cast<double>(tick) * 0.016, true, false)) {
            ++occluded_presents;
        }
    }
    const bool occlusion_ok = occluded_presents == 0 && occluded.occlusionBypasses() == 120;

    gui::ViewportThrottle minimised;
    int minimised_presents = 0;
    for (int tick = 0; tick < 60; ++tick) {
        if (minimised.shouldPresent(static_cast<double>(tick) * 0.016, false, true)) {
            ++minimised_presents;
        }
    }
    const bool minimised_ok = minimised_presents == 0;

    const bool split_ok = throttle.splitForCursor(-40, 800) == 0.0 &&
                          throttle.splitForCursor(400, 800) == 0.5 &&
                          throttle.splitForCursor(9999, 800) == 1.0 &&
                          throttle.splitForCursor(10, 0) == 0.5;

    const bool pass = rate_ok && occlusion_ok && minimised_ok && split_ok;
    std::cout << "viewport self-test " << (pass ? "PASS" : "FAIL") << ": " << presents
              << " presents/s, " << occluded_presents << " presents while occluded, "
              << minimised_presents << " while minimised, split clamp "
              << (split_ok ? "OK" : "FAILED") << "\n";
    return pass ? 0 : 1;
}

}  // namespace

#if defined(_WIN32)
namespace {

std::vector<std::string> utf8CommandLine(int* argc) {
    std::vector<std::string> arguments;
    int wide_count = 0;
    LPWSTR* wide = CommandLineToArgvW(GetCommandLineW(), &wide_count);
    if (wide == nullptr || wide_count <= 0) {
        *argc = 0;
        return arguments;
    }
    arguments.reserve(static_cast<size_t>(wide_count));
    for (int i = 0; i < wide_count; ++i) {
        const int size =
            WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, nullptr, 0, nullptr, nullptr);
        if (size <= 1) {
            arguments.emplace_back();
            continue;
        }
        std::vector<char> buffer(static_cast<size_t>(size));
        WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, buffer.data(), size, nullptr, nullptr);
        arguments.emplace_back(buffer.data());
    }
    LocalFree(wide);
    *argc = wide_count;
    return arguments;
}

}  // namespace
#endif

int rtxCliMain(int argc, char** argv) {
    rtx::initRuntimeEnvironment();
#if defined(_WIN32)
    int utf8_argc = 0;
    std::vector<std::string> utf8_arguments = utf8CommandLine(&utf8_argc);
    std::vector<char*> utf8_argv;
    utf8_argv.reserve(utf8_arguments.size());
    for (std::string& argument : utf8_arguments) {
        utf8_argv.push_back(argument.data());
    }
    if (utf8_argc > 0) {
        argc = utf8_argc;
        argv = utf8_argv.data();
    }
#endif
    Options options;
    std::string error;
    if (!parseOptions(argc, argv, options, error)) {
        std::cerr << "error: " << error << "\n";
        printHelp();
        return 2;
    }
    switch (options.mode) {
        case Mode::Help:
            printHelp();
            return 0;
        case Mode::Version:
            std::cout << "CineRTX " << RTX_VERSION_STRING << "\n";
            return 0;
        case Mode::Probe:
            return runProbe(options);
        case Mode::ViewportSelfTest:
            return runViewportSelfTest();
        case Mode::Container:
            return runHardwareMode(options);
        case Mode::None:
            break;
    }
    printHelp();
    return 2;
}
