// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors

#include "rtx/pipeline.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <iomanip>
#include <sstream>

#include "rtx/color.h"
#include "rtx/io.h"
#include "rtx/scene_detect.h"

namespace rtx {
namespace {

std::string escapeJson(const std::string& value) {
    std::string output;
    output.reserve(value.size() + 8);
    for (char c : value) {
        switch (c) {
            case '"':
                output += "\\\"";
                break;
            case '\\':
                output += "\\\\";
                break;
            case '\n':
                output += "\\n";
                break;
            case '\r':
                output += "\\r";
                break;
            case '\t':
                output += "\\t";
                break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    std::ostringstream hex;
                    hex << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                        << static_cast<int>(c);
                    output += hex.str();
                } else {
                    output += c;
                }
                break;
        }
    }
    return output;
}

void recordStage(PipelineMetrics& metrics, Stage stage, double milliseconds) {
    StageMetrics& stage_metrics = metrics.stages[static_cast<size_t>(stage)];
    ++stage_metrics.submissions;
    stage_metrics.ewma_ms = stage_metrics.ewma_ms == 0.0
                                ? milliseconds
                                : 0.2 * milliseconds + 0.8 * stage_metrics.ewma_ms;
    stage_metrics.max_ms = std::max(stage_metrics.max_ms, milliseconds);
}

double elapsedMs(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
        .count();
}

Result<PipelineReport> runPipeline(const PipelineRunConfig& config,
                                   const std::function<Result<Frame>()>& nextFrame,
                                   const PipelineLog& log) {
    if (config.width <= 0 || config.height <= 0 || config.target_width <= 0 ||
        config.target_height <= 0) {
        return Result<PipelineReport>::fail(ErrorCode::InvalidArgument,
                                            "pipeline geometry must be positive");
    }
    if (config.width % 2 != 0 || config.height % 2 != 0 || config.target_width % 2 != 0 ||
        config.target_height % 2 != 0) {
        return Result<PipelineReport>::fail(ErrorCode::InvalidArgument,
                                            "NV12/P010 geometry must be even");
    }
    if (config.target_width < config.width || config.target_height < config.height) {
        return Result<PipelineReport>::fail(ErrorCode::Unsupported,
                                            "the reference pipeline does not downscale");
    }
    if (!config.input_fps.valid() || !config.output_fps.valid()) {
        return Result<PipelineReport>::fail(ErrorCode::InvalidArgument,
                                            "invalid input or output frame rate");
    }

    BackendConfig backend_config;
    backend_config.width = config.width;
    backend_config.height = config.height;
    backend_config.target_width = config.target_width;
    backend_config.target_height = config.target_height;
    backend_config.vsr_quality = config.vsr_quality;
    backend_config.enable_truehdr = config.enable_hdr;
    backend_config.enable_nvofa = config.interpolate;
    backend_config.enable_rife = false;

    auto backend = makeCpuBackend();
    const Status initialized = backend->initialize(backend_config);
    if (!initialized.ok()) {
        return Result<PipelineReport>::fromStatus(initialized);
    }

    Hdr10Metadata hdr;
    if (config.enable_hdr) {
        TrueHdrParameters parameters;
        parameters.max_luminance = 1000;
        const Result<Hdr10Metadata> metadata = SeiSynthesizer::makeMetadata(parameters);
        if (!metadata.ok()) {
            return Result<PipelineReport>::fromStatus(metadata.status());
        }
        hdr = metadata.value();
    }

    RawP010Writer writer;
    if (!config.output_p010.empty()) {
        const Status opened = writer.open(config.output_p010, config.target_width,
                                          config.target_height);
        if (!opened.ok()) {
            return Result<PipelineReport>::fromStatus(opened);
        }
    }

    TraceRecorder trace;
    FrameTimestampMapper mapper(config.input_fps, config.output_fps, kTimeBase90k);
    SceneCutDetector scene_detector;
    Frame enhanced;
    Frame p010;
    Frame interpolated;

    const auto allocate_output = enhanced.allocateHost(config.target_width, config.target_height,
                                                        PixelFormat::RGBA16F);
    if (!allocate_output.ok()) {
        return Result<PipelineReport>::fromStatus(allocate_output);
    }
    const auto allocate_p010 =
        p010.allocateHost(config.target_width, config.target_height, PixelFormat::P010);
    if (!allocate_p010.ok()) {
        return Result<PipelineReport>::fromStatus(allocate_p010);
    }
    if (config.interpolate) {
        const auto allocate_interp =
            interpolated.allocateHost(config.width, config.height, PixelFormat::NV12);
        if (!allocate_interp.ok()) {
            return Result<PipelineReport>::fromStatus(allocate_interp);
        }
    }

    PipelineReport report;
    report.backend = backend->name();
    const Result<DeviceInfo> device = backend->deviceInfo();
    if (device.ok()) {
        report.device = device.value().toString();
    }
    report.input_width = config.width;
    report.input_height = config.height;
    report.output_width = config.target_width;
    report.output_height = config.target_height;
    report.hdr = hdr;

    const auto run_start = std::chrono::steady_clock::now();
    Result<Frame> current_result = nextFrame();
    if (!current_result.ok()) {
        if (current_result.code() == ErrorCode::NotFound) {
            if (!config.output_p010.empty()) {
                const Status finalized = writer.finalize();
                if (!finalized.ok()) {
                    return Result<PipelineReport>::fromStatus(finalized);
                }
                report.p010_bytes = writer.bytesWritten();
            }
            report.zero_copy = "not-applicable (no frames)";
            return Result<PipelineReport>::ok(std::move(report));
        }
        return Result<PipelineReport>::fromStatus(current_result.status());
    }

    int64_t input_index = 0;
    bool has_next = false;
    Packet packet;

    while (current_result.ok()) {
        Frame current = std::move(current_result.value());
        current_result = nextFrame();
        has_next = current_result.ok();
        if (!has_next && current_result.code() != ErrorCode::NotFound) {
            return Result<PipelineReport>::fromStatus(current_result.status());
        }

        int interval_frames = 1;
        bool cut = false;
        SceneCutResult cut_result;
        if (has_next && config.scene_detection) {
            const Frame& next = current_result.value();
            cut_result = scene_detector.analyze(current.planes[0], next.planes[0],
                                                config.width, config.height, current.pitches[0]);
            cut = cut_result.cut;
            if (cut) {
                ++report.scene_cuts;
                scene_detector.noteCut();
            }
        }
        if (config.interpolate && config.output_fps.toDouble() > config.input_fps.toDouble()) {
            interval_frames = mapper.outputFramesForInterval(input_index);
            if (interval_frames < 1) {
                interval_frames = 1;
            }
        }

        for (int k = 0; k < interval_frames; ++k) {
            const int64_t output_pts = mapper.outputPts(input_index, k);
            Frame* working = &current;
            if (k > 0) {
                if (cut || !has_next) {
                    ++report.duplicated_frames;
                } else {
                    const float timestep = static_cast<float>(k) /
                                           static_cast<float>(interval_frames);
                    const auto start = std::chrono::steady_clock::now();
                    trace.eventBegin("fruc.interpolate", "fruc");
                    const Result<void> blended = backend->interpolate(
                        InterpRequest{&current, &current_result.value(), timestep, &interpolated});
                    trace.eventEnd("fruc.interpolate", "fruc");
                    recordStage(report.metrics, Stage::Interpolate, elapsedMs(start));
                    if (!blended.ok()) {
                        return Result<PipelineReport>::fromStatus(blended.status());
                    }
                    ++report.interpolated_frames;
                    working = &interpolated;
                }
            }

            working->pts = output_pts;
            working->time_base = kTimeBase90k;
            working->duration = rescaleQ(1, config.output_fps.inverse(), kTimeBase90k);

            const auto enhance_start = std::chrono::steady_clock::now();
            trace.eventBegin("ngx.enhance", "enhance");
            const Result<void> enhanced_result =
                backend->enhance(EnhanceRequest{working, &enhanced, hdr});
            trace.eventEnd("ngx.enhance", "enhance");
            recordStage(report.metrics, Stage::Enhance, elapsedMs(enhance_start));
            if (!enhanced_result.ok()) {
                return Result<PipelineReport>::fromStatus(enhanced_result.status());
            }
            enhanced.pts = output_pts;
            enhanced.time_base = kTimeBase90k;

            const auto pack_start = std::chrono::steady_clock::now();
            trace.eventBegin("color.pack_p010", "color");
            const Result<void> packed = backend->packP010(enhanced, p010);
            trace.eventEnd("color.pack_p010", "color");
            recordStage(report.metrics, Stage::Pack, elapsedMs(pack_start));
            if (!packed.ok()) {
                return Result<PipelineReport>::fromStatus(packed.status());
            }

            const auto encode_start = std::chrono::steady_clock::now();
            trace.eventBegin("nvenc.encode", "encode");
            const Result<void> encoded = backend->encode(p010, packet);
            trace.eventEnd("nvenc.encode", "encode");
            recordStage(report.metrics, Stage::Encode, elapsedMs(encode_start));
            if (!encoded.ok()) {
                return Result<PipelineReport>::fromStatus(encoded.status());
            }
            report.packet_bytes += packet.data.size();
            ++report.output_frames;

            if (!config.output_p010.empty()) {
                const Status written = writer.write(p010);
                if (!written.ok()) {
                    return Result<PipelineReport>::fromStatus(written);
                }
            }
            trace.instant("frame.output", "pipeline");
        }

        if (log) {
            std::ostringstream message;
            message << "frame " << input_index << " -> " << interval_frames
                    << " output frame(s)";
            if (cut) {
                message << " [scene cut: bypass interpolator]";
            }
            log(message.str());
        }

        ++input_index;
        if (!has_next) {
            break;
        }
    }

    if (!config.output_p010.empty()) {
        const Status finalized = writer.finalize();
        if (!finalized.ok()) {
            return Result<PipelineReport>::fromStatus(finalized);
        }
        report.p010_bytes = writer.bytesWritten();
    }
    if (!config.trace_path.empty()) {
        const Status saved = trace.save(config.trace_path);
        if (!saved.ok()) {
            return Result<PipelineReport>::fromStatus(saved);
        }
    }
    if (!config.hdr_sidecar.empty() && hdr.enabled) {
        const Status sidecar = writeHdrSidecar(config.hdr_sidecar, hdr);
        if (!sidecar.ok()) {
            return Result<PipelineReport>::fromStatus(sidecar);
        }
    }

    report.input_frames = input_index;
    report.metrics.frames = report.output_frames;
    report.metrics.elapsed_ms = elapsedMs(run_start);
    report.metrics.flush_count = report.output_frames;
    if (report.metrics.elapsed_ms > 0.0) {
        report.metrics.input_fps = static_cast<double>(report.input_frames) * 1000.0 /
                                   report.metrics.elapsed_ms;
        report.metrics.output_fps = static_cast<double>(report.output_frames) * 1000.0 /
                                    report.metrics.elapsed_ms;
    }
    return Result<PipelineReport>::ok(std::move(report));
}

}  // namespace

std::string PipelineReport::toJson() const {
    std::ostringstream out;
    out << "{\n";
    out << "  \"backend\": \"" << escapeJson(backend) << "\",\n";
    out << "  \"device\": \"" << escapeJson(device) << "\",\n";
    out << "  \"input\": {\"width\": " << input_width << ", \"height\": " << input_height
        << ", \"frames\": " << input_frames << "},\n";
    out << "  \"output\": {\"width\": " << output_width << ", \"height\": " << output_height
        << ", \"frames\": " << output_frames << ", \"interpolated\": " << interpolated_frames
        << ", \"duplicated\": " << duplicated_frames << "},\n";
    out << "  \"scene_cuts\": " << scene_cuts << ",\n";
    out << "  \"packet_bytes\": " << packet_bytes << ",\n";
    out << "  \"p010_bytes\": " << p010_bytes << ",\n";
    out << "  \"zero_copy\": \"" << escapeJson(zero_copy) << "\",\n";
    out << "  \"hdr\": " << SeiSynthesizer::toJson(hdr) << ",\n";
    out << "  \"metrics\": " << metrics.toJson();
    out << "}\n";
    return out.str();
}

Result<PipelineReport> runSyntheticPipeline(const PipelineRunConfig& config,
                                            const PipelineLog& log) {
    if (config.input_frames <= 0) {
        return Result<PipelineReport>::fail(ErrorCode::InvalidArgument,
                                            "synthetic pipeline needs at least one frame");
    }
    SyntheticNv12Source::Config source_config;
    source_config.width = config.width;
    source_config.height = config.height;
    source_config.frame_count = config.input_frames;
    source_config.frame_rate = config.input_fps;
    source_config.scene_cut_frame = config.scene_cut_frame;
    source_config.bar_speed = config.bar_speed;
    auto source = std::make_shared<SyntheticNv12Source>(source_config);
    const auto next = [source]() { return source->next(); };
    return runPipeline(config, next, log);
}

Result<PipelineReport> runRawNv12Pipeline(const PipelineRunConfig& config,
                                          const std::string& input_path,
                                          const PipelineLog& log) {
    auto reader = std::make_shared<RawNv12Reader>();
    const Status opened = reader->open(input_path, config.width, config.height);
    if (!opened.ok()) {
        return Result<PipelineReport>::fromStatus(opened);
    }
    auto frame_index = std::make_shared<int64_t>(0);
    const int64_t limit = config.input_frames;
    const Rational time_base = config.input_fps.inverse();
    const auto next = [reader, frame_index, limit, time_base]() -> Result<Frame> {
        if (limit > 0 && *frame_index >= limit) {
            return Result<Frame>::fail(ErrorCode::NotFound, "frame limit reached");
        }
        Result<Frame> frame = reader->read(*frame_index, time_base);
        if (frame.ok()) {
            ++*frame_index;
        }
        return frame;
    };
    return runPipeline(config, next, log);
}

}  // namespace rtx
