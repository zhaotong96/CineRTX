// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "rtx/hw_transcode.h"
#include "rtx/i18n.h"
#include "rtx/logger.h"
#include "rtx/ngx_remaster.h"
#include "rtx/param_schema.h"
#include "rtx/rational.h"
#include "rtx/runtime_env.h"
#include "rtx/status.h"
#include "rtx/string_utils.h"
#include "rtx/types.h"
#include "shim/ngx_bridge_abi.h"
#include "shim/rtx_shim_abi.h"
#include "shim_probe.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>

extern "C" {
// Wake NVIDIA discrete GPU high performance profile on Optimus laptops
__declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;
}
#endif

namespace {

using namespace rtx;

enum class Mode {
    None,
    Probe,
    SchemaJson,
    SessionStdio,
    Container,
    Help,
    Version,
    BuildRifeEngine,
};

// @rtx-type-justification
// Category: Domain-Model
// Why-Std-Fails: 多字段领域模型：具名字段承载序列化/校验/反射契约，压进 std::tuple 会丢失全部字段语义与默认值契约。
// Invariant: 字段集合与默认值即对外契约（schema/测试逐字段锚定）；只增不删。
struct Options {
    Mode mode = Mode::None;
    TranscodeProfile profile{};
    bool explicit_target_dims = false;
    bool json = false;
    bool quiet = false;
    bool verbose = false;
    std::string language;
    std::filesystem::path onnx_path;
    std::filesystem::path engine_path;
    int opt_w = 1920;
    int opt_h = 1080;
};

void printHelp() {
    std::cout << "CineRTX " << RTX_VERSION_STRING << "\n"
              << tr(StrId::AppTitle) << "\n"
              << "Offline SDR -> 4K HDR10 60fps transcode engine\n\n"
              << "Usage:\n"
              << "  CineRTX --probe [--json]\n"
              << "  CineRTX --schema-json\n"
              << "  CineRTX --session-stdio\n"
              << "  CineRTX --input in.mp4 [--output out.mp4] [options]\n"
              << "  CineRTX --build-rife-engine --onnx model.onnx --engine model.engine [--opt-w W --opt-h H]\n\n"
              << "Options:\n"
              << "  --input PATH                 Container input (MP4/MKV)\n"
              << "  --output PATH                Container output (default: <stem>_remaster_<codec>.mp4)\n"
              << "  --codec av1|hevc             10-bit HDR10 output video codec (default: av1)\n"
              << "  --bitrate KBPS               Encoder target bitrate (0 = auto)\n"
              << "  --preset 1..7                NVENC P1..P7 preset (default 5)\n"
              << "  --rate-control cq|vbr|cbr|qp Rate control mode (default cq)\n"
              << "  --qp N                       Constant QP/CQ target (default 22, 0..51)\n"
              << "  --resolution 4k|2k|fhd|native Output resolution preset (default: 4k)\n"
              << "  --target-width PX            Explicit output width override (default 3840)\n"
              << "  --target-height PX           Explicit output height override (default 2160)\n"
              << "  --target-fps N[/D]           Target frame rate (default 60, 0 = keep input)\n"
              << "  --no-vsr                     Disable RTX VSR upscaling\n"
              << "  --vsr-quality 1..4           NGX VSR quality (default 4 Ultra)\n"
              << "  --peak-nits 400..4000        TrueHDR peak luminance in cd/m^2 (default 1000)\n"
              << "  --frames N                   Frame limit (0 = whole stream)\n"
              << "  --pillarbox                  Pad portrait content into 16:9 canvas instead of auto-rotate\n"
              << "  --no-audio                   Drop audio streams from output\n"
              << "  --lut PATH                   Apply .cube 3D LUT inside pack kernel\n"
              << "  --hdr10plus                  Emit SMPTE ST 2094-40 (HDR10+) dynamic metadata\n"
              << "  --gpu N                      Bind to GPU device N (default 0)\n"
              << "  --rife-engine PATH           TensorRT RIFE engine path for AI interpolation\n"
              << "  --build-rife-engine          Build TensorRT engine from RIFE ONNX\n"
              << "  --onnx PATH                  Input ONNX path for --build-rife-engine\n"
              << "  --engine PATH                Output engine path for --build-rife-engine\n"
              << "  --opt-w W, --opt-h H         Optimal profile resolution for --build-rife-engine (default: 1920x1080)\n"
              << "  --schema-json                Export SSOT parameter schema & bilingual i18n dictionary as JSON\n"
              << "  --session-stdio              Run JSON-over-stdio interactive transcode session (WebUI bridge)\n"
              << "  --json                       Machine-readable JSON output\n"
              << "  --quiet, --verbose           Logging verbosity (all logs also stream to DebugView)\n"
              << "  --lang en|zh                 Output language (default: zh)\n"
              << "  --version, --help            Show version/help\n";
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
    const std::string_view s(argv[++index]);
    path = std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(s.data()), s.size()));
    return true;
}
bool parseOptions(int argc, char** argv, Options& options, std::string& error) {
    Rational target_fps{60, 1};
    bool target_fps_set = false;

    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        std::string value;
        if (argument == "--help" || argument == "-h") {
            options.mode = Mode::Help;
        } else if (argument == "--version") {
            options.mode = Mode::Version;
        } else if (argument == "--probe") {
            options.mode = Mode::Probe;
        } else if (argument == "--schema-json" || argument == "--dump-schema-json") {
            options.mode = Mode::SchemaJson;
        } else if (argument == "--session-stdio") {
            options.mode = Mode::SessionStdio;
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
        } else if (argument == "--target-width" || argument == "--target-height" ||
                   argument == "--opt-w" || argument == "--opt-h") {
            if (!requireValue(argc, argv, i, value)) {
                return false;
            }
            int parsed = 0;
            if (!parseInteger(value, parsed)) {
                error = "invalid integer for " + argument + ": " + value;
                return false;
            }
            if (argument == "--target-width") {
                options.profile.target_width = parsed;
                options.explicit_target_dims = true;
            } else if (argument == "--target-height") {
                options.profile.target_height = parsed;
                options.explicit_target_dims = true;
            } else if (argument == "--opt-w") {
                options.opt_w = parsed;
            } else {
                options.opt_h = parsed;
            }
        } else if (const FieldBinding<TranscodeProfile>* binding =
                       findCliFieldBinding<TranscodeProfile>(argument)) {
            if (!requireValue(argc, argv, i, value)) {
                return false;
            }
            bool assigned = true;
            std::visit([&](auto member) {
                using T = std::decay_t<decltype(options.profile.*member)>;
                if constexpr (std::is_same_v<T, int>) {
                    int parsed = 0;
                    if (!parseInteger(value, parsed)) {
                        error = "invalid integer for " + argument + ": " + value;
                        assigned = false;
                        return;
                    }
                    options.profile.*member = parsed;
                } else if constexpr (std::is_same_v<T, bool>) {
                    options.profile.*member = (value == "true" || value == "1");
                } else {
                    options.profile.*member = value;
                }
            }, binding->member);
            if (!assigned) {
                return false;
            }
            if (argument == "--input") {
                options.mode = Mode::Container;
            } else if (argument == "--output") {
                options.profile.auto_output = false;
            }
        } else if (argument == "--target-fps") {
            if (!requireValue(argc, argv, i, value)) {
                return false;
            }
            const Result<Rational> parsed = Rational::parse(value);
            if (!parsed.has_value()) {
                error = "invalid frame rate for --target-fps: " + value;
                return false;
            }
            target_fps = parsed.value();
            target_fps_set = true;
        } else if (argument == "--no-vsr") {
            options.profile.vsr_enabled = false;
        } else if (argument == "--pillarbox") {
            options.profile.geometry = "pillarbox";
        } else if (argument == "--no-audio") {
            options.profile.copy_audio = false;
        } else if (argument == "--hdr10plus") {
            options.profile.hdr10plus = true;
        } else if (argument == "--build-rife-engine") {
            options.mode = Mode::BuildRifeEngine;
        } else if (argument == "--onnx") {
            if (!requirePath(argc, argv, i, options.onnx_path)) {
                return false;
            }
        } else if (argument == "--engine") {
            if (!requirePath(argc, argv, i, options.engine_path)) {
                return false;
            }
        } else if (argument == "--json") {
            options.json = true;
        } else if (argument == "--quiet") {
            options.quiet = true;
        } else if (argument == "--verbose") {
            options.verbose = true;
        } else {
            error = "unknown argument: " + argument;
            return false;
        }
    }
    if (options.mode == Mode::None) {
        error = "no command specified (try --help)";
        return false;
    }
    if (options.mode == Mode::Container) {
        const bool want_rife = !options.profile.rife_engine.empty();
        if (target_fps_set && target_fps.num <= 0) {
            options.profile.fps_mode = "original";
        } else if (target_fps_set && target_fps.toDouble() >= 100.0) {
            options.profile.fps_mode = want_rife ? "120-rife" : "120-nvofa";
        } else {
            options.profile.fps_mode = want_rife ? "60-rife" : "60-nvofa";
        }
        const int saved_w = options.profile.target_width;
        const int saved_h = options.profile.target_height;
        auto valid = validateAndNormalizeProfile(options.profile, /*require_io_paths=*/true);
        if (!valid.has_value()) {
            error = valid.error().message;
            return false;
        }
        if (options.explicit_target_dims) {
            options.profile.target_width = saved_w;
            options.profile.target_height = saved_h;
        }
        if (target_fps_set && target_fps.num > 0) {
        }
    } else if (options.mode == Mode::BuildRifeEngine) {
        if (options.onnx_path.empty()) {
            error = "--build-rife-engine requires --onnx <path>";
            return false;
        }
        if (options.engine_path.empty()) {
// Preserve custom fractional rates such as 60000/1001 when specified on CLI.
            error = "--build-rife-engine requires --engine <path>";
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
              << "\n";
    if (report.fruc_backend != "off") {
        std::cout << "fruc: backend=" << report.fruc_backend
                  << ", interpolated=" << report.frames_interpolated
                  << ", scene cuts=" << report.fruc_scene_cuts
                  << ", failures=" << report.fruc_failures << "\n";
    }
    std::cout << "elapsed: " << std::fixed << std::setprecision(1) << report.elapsed_ms
              << " ms\n";
}
int runHardwareMode(const Options& options) {
    HwTranscodeOptions hardware = profileToHwOptions(options.profile);
// Phase 9 (RC-2): the fruc line must be visible whenever the FRUC backend
// is active, even with zero interpolated frames, and must carry the
// interpolation failure count so silent degradation cannot hide.
    hardware.verbose = options.verbose;
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
        logError(result.error().toString());
        if (! isConsoleLogEnabled()) {
            std::cerr << "error: " << result.error().toString() << "\n";
        }
        return 1;
    }
    printHwReport(result.value(), options);
    return 0;
}
int runSessionStdio() {
    setConsoleLogEnabled(false);

    std::mutex out_mutex;
    setLogCallback([&out_mutex](LogLevel level, std::string_view msg) {
        std::lock_guard<std::mutex> lock(out_mutex);
        std::cout << std::format(
            "{{\"type\":\"log\",\"level\":\"{}\",\"text\":\"{}\"}}\n",
            logLevelName(level), cli::escapeJson(std::string(msg))) << std::flush;
    });

    std::mutex in_mutex;
    std::condition_variable in_cv;
    std::vector<std::string> job_lines;
    std::atomic<bool> cancel_flag{false};
    std::atomic<bool> stdin_eof{false};

    std::thread stdin_reader([&]() {
        std::string line;
        while (std::getline(std::cin, line)) {
            std::string trimmed = std::string(rtx::str::trimAscii(line));
            if (trimmed.empty()) continue;
            if (trimmed == "cancel" || trimmed == "stop" ||
                (trimmed.find("\"action\"") != std::string::npos && trimmed.find("cancel") != std::string::npos)) {
                cancel_flag.store(true, std::memory_order_release);
                logInfo("Received cooperative cancel signal over session-stdio");
                continue;
            }
            {
                std::lock_guard<std::mutex> lk(in_mutex);
                job_lines.push_back(std::move(trimmed));
            }
            in_cv.notify_all();
        }
        stdin_eof.store(true, std::memory_order_release);
        in_cv.notify_all();
    });
    stdin_reader.detach();

    int last_exit_code = 0;
    for (;;) {
        std::string payload_line;
        {
            std::unique_lock<std::mutex> lk(in_mutex);
            in_cv.wait(lk, [&] { return !job_lines.empty() || stdin_eof.load(std::memory_order_acquire); });
            if (job_lines.empty()) break;
            payload_line = std::move(job_lines.front());
            job_lines.erase(job_lines.begin());
        }
        if (payload_line == "quit" || payload_line == "exit") break;

        Result<TranscodeProfile> parsed = parseProfileFromJson(payload_line);
        if (!parsed.has_value()) {
            std::lock_guard<std::mutex> lock(out_mutex);
            std::cout << std::format(
                "{{\"type\":\"report\",\"status\":\"failed\",\"exitCode\":2,\"error\":\"{}\"}}\n",
                cli::escapeJson(parsed.error().toString())) << std::flush;
            last_exit_code = 2;
            continue;
        }
        TranscodeProfile profile = std::move(parsed.value());
        Result<void> valid = validateAndNormalizeProfile(profile, /*require_io_paths=*/true);
        if (!valid.has_value()) {
            std::lock_guard<std::mutex> lock(out_mutex);
            std::cout << std::format(
                "{{\"type\":\"report\",\"status\":\"failed\",\"exitCode\":2,\"error\":\"{}\"}}\n",
                cli::escapeJson(valid.error().toString())) << std::flush;
            last_exit_code = 2;
            continue;
        }
        cancel_flag.store(false, std::memory_order_release);
        HwTranscodeOptions hw = profileToHwOptions(profile);
        hw.cancel = &cancel_flag;

        auto last_emit = std::chrono::steady_clock::now() - std::chrono::seconds(1);
        hw.progress = [&](const HwTranscodeProgress& p) {
            const auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration<double>(now - last_emit).count() < 0.20 &&
                (p.total_frames == 0 || p.frames_encoded < p.total_frames)) {
                return;
            }
            last_emit = now;
            const double pct = p.total_frames > 0
                                   ? std::min(100.0, static_cast<double>(p.frames_encoded) * 100.0 / p.total_frames)
                                   : 0.0;
            const double fps = p.elapsed_ms > 0 ? (p.frames_encoded * 1000.0 / p.elapsed_ms) : 0.0;
            std::lock_guard<std::mutex> lock(out_mutex);
            std::cout << std::format(
                "{{\"type\":\"progress\",\"pct\":{:.1f},\"frames\":{},\"total\":{},\"fps\":{:.1f}}}\n",
                pct, p.frames_encoded, p.total_frames, fps) << std::flush;
        };

        Result<HwTranscodeReport> result = runNvidiaRemasterTranscode(hw);

        std::lock_guard<std::mutex> lock(out_mutex);
        if (result.has_value()) {
            std::string report_json = result.value().toJson();
            std::replace(report_json.begin(), report_json.end(), '\n', ' ');
            std::replace(report_json.begin(), report_json.end(), '\r', ' ');
            std::cout << std::format(
                "{{\"type\":\"report\",\"status\":\"done\",\"exitCode\":0,\"report\":{}}}\n",
                report_json) << std::flush;
            last_exit_code = 0;
        } else {
            const bool was_cancelled = cancel_flag.load(std::memory_order_acquire);
            const char* status_str = was_cancelled ? "cancelled" : "failed";
            const int exit_code = was_cancelled ? 130 : 1;
            std::cout << std::format(
                "{{\"type\":\"report\",\"status\":\"{}\",\"exitCode\":{},\"error\":\"{}\"}}\n",
                status_str, exit_code, cli::escapeJson(result.error().toString())) << std::flush;
            last_exit_code = exit_code;
        }
    }
    setLogCallback(nullptr);
    return last_exit_code;
}
int runBuildRifeEngine(const Options& options) {
    rtx::NgxRemasterBridge bridge;
    std::string error;
    if (!bridge.open(&error)) {
        logError("cannot load NGX bridge DLL: " + error);
        return 1;
    }
    if (!bridge.hasRifeApi()) {
        logError("loaded NGX bridge DLL does not support RIFE API");
        return 1;
    }
    const std::string onnx_str = options.onnx_path.string();
    const std::string engine_str = options.engine_path.string();
    const uint32_t opt_w = options.opt_w > 0 ? static_cast<uint32_t>(options.opt_w) : 1920u;
    const uint32_t opt_h = options.opt_h > 0 ? static_cast<uint32_t>(options.opt_h) : 1080u;

    std::cout << "Building RIFE TensorRT engine...\n"
              << "  ONNX: " << onnx_str << "\n"
              << "  Engine: " << engine_str << "\n"
              << "  Profile (Opt): " << opt_w << "x" << opt_h << "\n";

    char log_buf[2048] = {};
    const int32_t res = bridge.buildRifeEngine(onnx_str.c_str(), engine_str.c_str(), opt_w, opt_h, log_buf, sizeof(log_buf));
    if (res != RTX_OK) {
        logError(std::format("RIFE engine build failed (code {}): {}", res, log_buf));
        return 1;
    }
    std::cout << "Build succeeded.\n" << log_buf << "\n";
    return 0;
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
    if (options.quiet) {
        setLogLevel(LogLevel::Error);
    } else if (options.verbose) {
        setLogLevel(LogLevel::Debug);
    } else {
        setLogLevel(LogLevel::Info);
    }
    if (options.json) {
        setConsoleLogEnabled(false);
    }
    switch (options.mode) {
        case Mode::Help:
            printHelp();
            return 0;
        case Mode::Version:
            std::cout << "CineRTX " << RTX_VERSION_STRING << "\n";
            return 0;
        case Mode::SchemaJson:
            std::cout << exportSchemaJson();
            return 0;
        case Mode::SessionStdio:
            return runSessionStdio();
        case Mode::Probe:
            return runProbe(options);
        case Mode::Container:
            return runHardwareMode(options);
        case Mode::BuildRifeEngine:
            return runBuildRifeEngine(options);
        case Mode::None:
            break;
    }
    printHelp();
    return 2;
}
