// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors

#include <algorithm>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#endif

#include "rtx/hw_transcode.h"
#include "rtx/pipeline.h"
#include "rtx/rife.h"
#include "rtx/status.h"
#include "shim_probe.h"

#ifndef RTX_VERSION_STRING
#define RTX_VERSION_STRING "0.1.0"
#endif

namespace {

using namespace rtx;

enum class Mode {
    None,
    Probe,
    Simulate,
    Raw,
    Container,
    BuildRife,
    SelfTest,
    Help,
    Version,
};

struct Options {
    Mode mode = Mode::None;
    int frames = 12;
    int width = 1920;
    int height = 1080;
    int target_width = 3840;
    int target_height = 2160;
    int scene_cut = -1;
    int bar_speed = 7;
    Rational input_fps{24, 1};
    Rational output_fps{60, 1};
    int vsr_quality = 2;
    bool interpolate = true;
    bool hdr = true;
    bool scene_detection = true;
    bool json = false;
    bool quiet = false;
    bool verbose = false;
    bool frames_set = false;
    bool fps_set = false;
    bool remaster = false;
    bool four_k_profile = false;
    bool no_cuda_graph = false;
    std::string backend = "auto";
    std::string input_raw;
    std::string output_raw;
    std::string input_path;
    std::string output_path;
    std::string codec = "hevc";
    std::string hdr_sidecar;
    std::string trace;
    std::string onnx;
    std::string engine;
    int bitrate_kbps = 0;
    int preset = 5;
    int rate_control = 0;
    int qp = 0;
    int bit_depth = 0;
};

void printHelp() {
    std::cout
        << "rtx-transcode " << RTX_VERSION_STRING << "\n"
        << "Offline SDR -> 4K HDR10 transcode engine (reference CLI)\n\n"
        << "Usage:\n"
        << "  rtx-transcode --probe [--json]\n"
        << "  rtx-transcode --simulate N [options]\n"
        << "  rtx-transcode --input in.mp4 --output out.mp4 --backend nvidia [options]\n"
        << "  rtx-transcode --input-raw in.nv12 --width W --height H --output-raw out.p010 "
           "--backend nvidia\n"
        << "  rtx-transcode --self-test\n"
        << "  rtx-transcode --build-rife --onnx model.onnx --engine model.engine\n\n"
        << "Options:\n"
        << "  --input PATH                 Container input (MP4/MKV) for hardware transcode\n"
        << "  --output PATH                Container output (MP4/MKV)\n"
        << "  --codec hevc|h264            NVENC output codec (default hevc)\n"
        << "  --bitrate KBPS               Encoder target bitrate (0 = auto)\n"
        << "  --preset 1..7                NVENC P1..P7 preset (default 5)\n"
        << "  --rate-control cbr|vbr|qp    NVENC rate control (default cbr)\n"
        << "  --qp N                       Constant QP when --rate-control qp\n"
        << "  --bit-depth 8|10             NVENC input depth (default: preserve source)\n"
        << "  --width, --height            Raw input geometry (default 1920x1080)\n"
        << "  --target-width/height        Output geometry (default 3840x2160)\n"
        << "  --fps N[/D]                  Input frame rate (default 24)\n"
        << "  --output-fps N[/D]           Output frame rate (default 60)\n"
        << "  --frames N                   Frame limit (raw input / synthetic count /\n"
        << "                               container and remaster runs)\n"
        << "  --scene-cut N                Synthetic hard cut at frame N\n"
        << "  --bar-speed N                Synthetic moving-edge speed\n"
        << "  --output-raw PATH            Write tight 10-bit P010 output\n"
        << "  --hdr-sidecar PATH           Write HDR10 JSON sidecar\n"
        << "  --trace PATH                 Write Chrome tracing JSON\n"
        << "  --vsr-quality 1..4           Reference upscaler quality hint\n"
        << "  --remaster                   GPU-only VSR + TrueHDR remaster: 1080p SDR in,\n"
        << "                               4K HDR10 HEVC Main10 out (requires --backend nvidia)\n"
        << "  --no-interpolate             Disable 24->60 FRUC synthesis\n"
        << "  --no-hdr                     Disable HDR10 metadata synthesis\n"
        << "  --no-scene-detect            Disable cut bypass\n"
        << "  --backend cpu|auto|nvidia    Select backend (nvidia runs NVDEC/NVENC)\n"
        << "  --json                       Machine-readable output\n"
        << "  --quiet, --verbose           Logging verbosity\n"
        << "  --4k-profile                 Allow a 4K RIFE workspace\n"
        << "  --no-cuda-graph              Omit --useCudaGraph from trtexec\n"
        << "  --version, --help            Show version/help\n\n"
        << "The default backend is the deterministic CPU reference implementation.\n"
        << "The NVIDIA backend is loaded at runtime from rtx_shim.dll/librtx_shim.so.\n"
        << "Container mode keeps decoded frames in device memory: NVDEC writes a\n"
        << "pre-allocated VRAM slot and NVENC registers the CUdeviceptr directly, so\n"
        << "the main loop performs no host pixel copies.  The raw NV12 path is a\n"
        << "verification artefact that reports its host staging traffic separately.\n";
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
        } else if (argument == "--self-test") {
            options.mode = Mode::SelfTest;
        } else if (argument == "--build-rife") {
            options.mode = Mode::BuildRife;
        } else if (argument == "--simulate") {
            if (!requireValue(argc, argv, i, value) || !parseInteger(value, options.frames) ||
                options.frames <= 0) {
                error = "--simulate needs a positive frame count";
                return false;
            }
            options.mode = Mode::Simulate;
        } else if (argument == "--input-raw") {
            if (!requireValue(argc, argv, i, options.input_raw)) {
                return false;
            }
            options.mode = Mode::Raw;
        } else if (argument == "--input") {
            if (!requireValue(argc, argv, i, options.input_path)) {
                return false;
            }
            if (options.mode != Mode::Raw) {
                options.mode = Mode::Container;
            }
        } else if (argument == "--output") {
            if (!requireValue(argc, argv, i, options.output_path)) {
                return false;
            }
        } else if (argument == "--codec") {
            if (!requireValue(argc, argv, i, options.codec)) {
                return false;
            }
            std::transform(options.codec.begin(), options.codec.end(), options.codec.begin(),
                           [](unsigned char c) { return static_cast<char>(c | 0x20); });
            if (options.codec != "hevc" && options.codec != "h264") {
                error = "--codec must be hevc or h264";
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
        } else if (argument == "--output-raw") {
            if (!requireValue(argc, argv, i, options.output_raw)) {
                return false;
            }
        } else if (argument == "--hdr-sidecar") {
            if (!requireValue(argc, argv, i, options.hdr_sidecar)) {
                return false;
            }
        } else if (argument == "--trace") {
            if (!requireValue(argc, argv, i, options.trace)) {
                return false;
            }
        } else if (argument == "--width" || argument == "--height" ||
                   argument == "--target-width" || argument == "--target-height" ||
                   argument == "--frames" || argument == "--scene-cut" ||
                   argument == "--bar-speed" || argument == "--vsr-quality" ||
                   argument == "--bitrate" || argument == "--preset" || argument == "--qp" ||
                   argument == "--bit-depth") {
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
            } else if (argument == "--scene-cut") {
                options.scene_cut = parsed;
            } else if (argument == "--bar-speed") {
                options.bar_speed = parsed;
            } else if (argument == "--bitrate") {
                options.bitrate_kbps = parsed;
            } else if (argument == "--preset") {
                options.preset = parsed;
            } else if (argument == "--qp") {
                options.qp = parsed;
            } else if (argument == "--bit-depth") {
                options.bit_depth = parsed;
            } else {
                options.vsr_quality = parsed;
            }
        } else if (argument == "--fps" || argument == "--output-fps") {
            if (!requireValue(argc, argv, i, value)) {
                return false;
            }
            const Result<Rational> parsed = Rational::parse(value);
            if (!parsed.ok()) {
                error = "invalid frame rate for " + argument + ": " + value;
                return false;
            }
            if (argument == "--fps") {
                options.input_fps = parsed.value();
                options.fps_set = true;
            } else {
                options.output_fps = parsed.value();
            }
        } else if (argument == "--backend") {
            if (!requireValue(argc, argv, i, options.backend)) {
                return false;
            }
        } else if (argument == "--onnx") {
            if (!requireValue(argc, argv, i, options.onnx)) {
                return false;
            }
        } else if (argument == "--engine") {
            if (!requireValue(argc, argv, i, options.engine)) {
                return false;
            }
        } else if (argument == "--json") {
            options.json = true;
        } else if (argument == "--remaster") {
            options.remaster = true;
        } else if (argument == "--quiet") {
            options.quiet = true;
        } else if (argument == "--verbose") {
            options.verbose = true;
        } else if (argument == "--no-interpolate") {
            options.interpolate = false;
        } else if (argument == "--no-hdr") {
            options.hdr = false;
        } else if (argument == "--no-scene-detect") {
            options.scene_detection = false;
        } else if (argument == "--4k-profile") {
            options.four_k_profile = true;
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
    if (options.width <= 0 || options.height <= 0 || options.target_width <= 0 ||
        options.target_height <= 0) {
        error = "geometry must be positive";
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
    if (options.bit_depth != 0 && options.bit_depth != 8 && options.bit_depth != 10) {
        error = "--bit-depth must be 8 or 10";
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
    if (options.mode == Mode::Raw) {
        if (options.input_raw.empty()) {
            error = "--input-raw needs a path";
            return false;
        }
        if (options.output_raw.empty() && options.output_path.empty()) {
            error = "raw transcode requires --output-raw";
            return false;
        }
    }
    if (options.mode == Mode::Raw && !options.frames_set) {
        options.frames = 0;  // consume the whole raw stream by default
    }
    return true;
}

int runProbe(const Options& options) {
    const cli::ShimProbeResult probe = cli::probeShim();
    if (options.json) {
        std::cout << cli::formatProbeJson(probe);
    } else {
        std::cout << cli::formatProbeHuman(probe);
    }
    if (!probe.loaded) {
        return 4;
    }
    if (probe.result != RTX_OK) {
        return 5;
    }
    return 0;
}

int selectNvidia(const Options& options) {
    const cli::ShimProbeResult probe = cli::probeShim();
    if (!probe.loaded) {
        std::cerr << "error: " << probe.error << "\n";
        return 3;
    }
    const RtxDeviceInfo& device = probe.diagnostics.device;
    if (device.name[0] == '\0') {
        std::cerr << "error: no CUDA device is visible to the NVIDIA shim\n";
        std::cerr << probe.diagnostics.summary << "\n";
        return 3;
    }
    std::cerr << "error: --simulate exercises the CPU reference pipeline; use --input or "
                 "--input-raw with --backend nvidia for the hardware path\n";
    std::cerr << "detail: " << probe.diagnostics.summary << "\n";
    (void)options;
    return 3;
}

HwTranscodeOptions makeHwOptions(const Options& options) {
    HwTranscodeOptions hardware;
    hardware.input_path =
        options.mode == Mode::Container ? options.input_path : options.input_raw;
    hardware.output_path =
        options.mode == Mode::Container
            ? options.output_path
            : (options.output_raw.empty() ? options.output_path : options.output_raw);
    hardware.width = options.width;
    hardware.height = options.height;
    hardware.frame_limit = options.mode == Mode::Raw || options.remaster ? options.frames : 0;
    hardware.codec = videoCodecFromName(options.codec);
    hardware.bit_depth = options.bit_depth;
    hardware.bitrate_kbps = options.bitrate_kbps;
    hardware.preset = options.preset;
    hardware.rate_control = options.rate_control;
    hardware.qp = options.qp;
    hardware.fps = options.fps_set ? options.input_fps : Rational{0, 1};
    hardware.hdr = options.hdr;
    hardware.remaster = options.remaster;
    hardware.target_width = options.target_width;
    hardware.target_height = options.target_height;
    hardware.vsr_quality = options.vsr_quality;
    hardware.verbose = options.verbose;
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
    if (options.backend != "nvidia") {
        std::cerr << "error: container hardware transcode requires --backend nvidia\n";
        return 2;
    }
    const HwTranscodeOptions hardware = makeHwOptions(options);
    Result<HwTranscodeReport> result =
        options.mode != Mode::Container
            ? runNvidiaRawTranscode(hardware)
            : (options.remaster ? runNvidiaRemasterTranscode(hardware)
                                : runNvidiaContainerTranscode(hardware));
    if (!result.ok()) {
        std::cerr << "error: " << result.error().toString() << "\n";
        return 1;
    }
    printHwReport(result.value(), options);
    return 0;
}

PipelineRunConfig makePipelineConfig(const Options& options) {
    PipelineRunConfig config;
    config.width = options.width;
    config.height = options.height;
    config.target_width = options.target_width;
    config.target_height = options.target_height;
    config.input_frames = options.frames;
    config.scene_cut_frame = options.scene_cut;
    config.bar_speed = options.bar_speed;
    config.input_fps = options.input_fps;
    config.output_fps = options.output_fps;
    config.interpolate = options.interpolate;
    config.enable_hdr = options.hdr;
    config.scene_detection = options.scene_detection;
    config.vsr_quality = options.vsr_quality;
    config.output_p010 = options.output_raw;
    config.hdr_sidecar = options.hdr_sidecar;
    config.trace_path = options.trace;
    return config;
}

void printReport(const PipelineReport& report, const Options& options) {
    if (options.json) {
        std::cout << report.toJson();
        return;
    }
    std::cout << "backend: " << report.backend << " (" << report.device << ")\n";
    std::cout << "input: " << report.input_width << "x" << report.input_height << ", "
              << report.input_frames << " frames\n";
    std::cout << "output: " << report.output_width << "x" << report.output_height << ", "
              << report.output_frames << " frames (" << report.interpolated_frames
              << " interpolated, " << report.duplicated_frames << " duplicated)\n";
    std::cout << "scene cuts: " << report.scene_cuts << "\n";
    std::cout << "P010 bytes: " << report.p010_bytes << ", reference packet bytes: "
              << report.packet_bytes << "\n";
    std::cout << "elapsed: " << std::fixed << std::setprecision(1) << report.metrics.elapsed_ms
              << " ms, output fps: " << std::setprecision(2) << report.metrics.output_fps << "\n";
    std::cout << "zero-copy: " << report.zero_copy << "\n";
    std::cout << "HDR10: " << SeiSynthesizer::toJson(report.hdr);
}

int runPipelineMode(const Options& options) {
    // The container path and the raw NV12 -> P010 artefact path both require the
    // NVIDIA hardware codec ABI.  Everything else runs on the CPU reference
    // pipeline (or the synthetic simulator).
    if (options.mode == Mode::Container) {
        return runHardwareMode(options);
    }
    if (options.backend == "nvidia" && options.mode == Mode::Raw) {
        return runHardwareMode(options);
    }
    if (options.backend == "nvidia") {
        return selectNvidia(options);
    }
    if (options.backend != "cpu" && options.backend != "auto") {
        std::cerr << "error: unknown backend '" << options.backend << "'\n";
        return 2;
    }
    const PipelineRunConfig config = makePipelineConfig(options);
    const PipelineLog log = [&options](const std::string& message) {
        if (!options.quiet && options.verbose) {
            std::cerr << message << "\n";
        }
    };
    Result<PipelineReport> result = options.mode == Mode::Raw
                                        ? runRawNv12Pipeline(config, options.input_raw, log)
                                        : runSyntheticPipeline(config, log);
    if (!result.ok()) {
        std::cerr << "error: " << result.error().toString() << "\n";
        return 1;
    }
    printReport(result.value(), options);
    return 0;
}

int runSelfTest() {
    PipelineRunConfig config;
    config.width = 64;
    config.height = 64;
    config.target_width = 128;
    config.target_height = 128;
    config.input_frames = 4;
    config.scene_cut_frame = 2;
    config.input_fps = kFps24;
    config.output_fps = kFps48;
    config.interpolate = true;
    config.enable_hdr = true;
    config.scene_detection = true;

    const Result<PipelineReport> report = runSyntheticPipeline(config);
    if (!report.ok()) {
        std::cerr << "self-test FAILED: " << report.error().toString() << "\n";
        return 1;
    }
    const PipelineReport& value = report.value();
    const bool pass = value.input_frames == 4 && value.output_frames >= 4 &&
                      value.scene_cuts >= 1 && value.packet_bytes > 0;
    std::cout << "self-test " << (pass ? "PASS" : "FAIL") << ": " << value.input_frames
              << " input -> " << value.output_frames << " output frames, " << value.scene_cuts
              << " scene cut(s), " << value.packet_bytes << " packet bytes\n";
    return pass ? 0 : 1;
}

int buildRife(const Options& options) {
    if (options.onnx.empty() || options.engine.empty()) {
        std::cerr << "error: --build-rife requires --onnx and --engine\n";
        return 2;
    }
    RifeConfig config;
    config.source_width = options.width;
    config.source_height = options.height;
    const RifeTensorShape shape = rifeAlignedShape(options.width, options.height);
    config.tensor_width = shape.w;
    config.tensor_height = shape.h;
    config.enable_4k_profile = options.four_k_profile;
    config.use_cuda_graph = !options.no_cuda_graph;
    const Result<void> valid = validateRifeConfig(config);
    if (!valid.ok()) {
        std::cerr << "error: " << valid.error().toString() << "\n";
        return 1;
    }
    std::cout << buildTrtexecCommand(config, options.onnx, options.engine) << "\n";
    std::cout << "# TensorRT baseline: 10.13.3.9 (10.14+ regresses RIFE v4.25 activation "
                 "memory)\n";
    return 0;
}

}  // namespace

#if defined(_WIN32)
// The C runtime hands main() an ANSI command line, which corrupts non-ASCII
// paths on a Chinese (or any non-UTF-8) code page.  Re-read the UTF-16 command
// line and hand the rest of the program UTF-8 instead.
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
#if defined(_WIN32)
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
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
#ifdef RTX_BASE_EDITION
    options.backend = "nvidia";
#endif
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
            std::cout << "rtx-transcode " << RTX_VERSION_STRING << "\n";
            return 0;
        case Mode::Probe:
            return runProbe(options);
        case Mode::SelfTest:
            return runSelfTest();
        case Mode::BuildRife:
            return buildRife(options);
        case Mode::Simulate:
        case Mode::Raw:
        case Mode::Container:
            return runPipelineMode(options);
        case Mode::None:
            break;
    }
    printHelp();
    return 2;
}
