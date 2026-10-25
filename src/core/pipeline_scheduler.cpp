// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors

#include "rtx/scheduler.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <thread>

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

}  // namespace

const char* stageName(Stage stage) noexcept {
    switch (stage) {
        case Stage::Demux:
            return "Demux";
        case Stage::Decode:
            return "Decode";
        case Stage::Interpolate:
            return "Interpolate";
        case Stage::Preprocess:
            return "Preprocess";
        case Stage::Enhance:
            return "Enhance";
        case Stage::Pack:
            return "Pack";
        case Stage::Encode:
            return "Encode";
        case Stage::Mux:
            return "Mux";
        case Stage::Count:
            break;
    }
    return "Unknown";
}

std::string PipelineMetrics::toJson() const {
    std::ostringstream out;
    out << "{\n";
    out << "  \"frames\": " << frames << ",\n";
    out << "  \"dropped\": " << dropped << ",\n";
    out << "  \"backpressure_events\": " << backpressure_events << ",\n";
    out << "  \"flush_count\": " << flush_count << ",\n";
    out << "  \"queue_stalls\": " << queue_stalls << ",\n";
    out << "  \"elapsed_ms\": " << std::fixed << std::setprecision(3) << elapsed_ms << ",\n";
    out << "  \"input_fps\": " << input_fps << ",\n";
    out << "  \"output_fps\": " << output_fps << ",\n";
    out << "  \"stages\": [\n";
    for (size_t i = 0; i < static_cast<size_t>(Stage::Count); ++i) {
        out << "    {\"name\": \"" << stageName(static_cast<Stage>(i)) << "\", \"submissions\": "
            << stages[i].submissions << ", \"failures\": " << stages[i].failures
            << ", \"ewma_ms\": " << stages[i].ewma_ms << ", \"max_ms\": " << stages[i].max_ms
            << "}";
        if (i + 1 != static_cast<size_t>(Stage::Count)) {
            out << ",";
        }
        out << "\n";
    }
    out << "  ]\n";
    out << "}\n";
    return out.str();
}

PipelineScheduler::PipelineScheduler(PipelineConfig config, PipelineHooks hooks)
    : config_(config), hooks_(std::move(hooks)) {}

Result<void> PipelineScheduler::applyBackpressure(TraceRecorder* trace) {
    if (!config_.enable_backpressure || !hooks_.pause_decode || !hooks_.pause_decode()) {
        return {};
    }
    ++metrics_.backpressure_events;
    if (trace != nullptr) {
        trace->instant("backpressure.pause", "pipeline");
    }
    const auto deadline = std::chrono::steady_clock::now() + config_.drain_timeout;
    while (hooks_.pause_decode()) {
        if (!hooks_.drain_one) {
            return Result<void>::fail(ErrorCode::State,
                                      "decode is backpressured but no drain callback is installed");
        }
        for (size_t i = 0; i < std::max<size_t>(1, config_.drain_batch); ++i) {
            hooks_.drain_one();
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            return Result<void>::fail(ErrorCode::Timeout,
                                      "backpressure did not clear before the drain timeout");
        }
    }
    if (trace != nullptr) {
        trace->instant("backpressure.resume", "pipeline");
    }
    return {};
}

Result<void> PipelineScheduler::runStage(const StageBinding& binding, FrameContext& context,
                                         TraceRecorder* trace) {
    const size_t index = static_cast<size_t>(binding.stage);
    if (index >= static_cast<size_t>(Stage::Count)) {
        return Result<void>::fail(ErrorCode::InvalidArgument, "invalid stage index");
    }
    const auto start = std::chrono::steady_clock::now();
    const char* name = stageName(binding.stage);
    if (trace != nullptr) {
        trace->eventBegin(name, "pipeline");
    }
    Result<void> result;
    if (binding.execute) {
        result = binding.execute(context);
    } else {
        result = Result<void>::fail(ErrorCode::InvalidArgument, "stage has no implementation",
                                    name);
    }
    const auto elapsed = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start);
    if (trace != nullptr) {
        trace->eventEnd(name, "pipeline");
    }
    StageMetrics& metrics = metrics_.stages[index];
    ++metrics.submissions;
    // A stage that knows its own duration (GPU timestamp queries on the NVIDIA
    // path, or a reference stage that reports its work) wins over the wall-clock
    // fallback.  The fallback must not be truncated to whole microseconds: a
    // sub-microsecond stage would then be recorded as 0 ms, which makes the
    // reported ewma/max depend on timer granularity rather than on the work.
    const double reported = context.stage_ms[index];
    const double milliseconds = reported > 0.0 ? reported : elapsed.count();
    metrics.ewma_ms = metrics.ewma_ms == 0.0 ? milliseconds : 0.2 * milliseconds + 0.8 * metrics.ewma_ms;
    metrics.max_ms = std::max(metrics.max_ms, milliseconds);
    if (!result.ok()) {
        ++metrics.failures;
        return result;
    }
    return {};
}

Result<void> PipelineScheduler::run(const std::vector<StageBinding>& stages, int64_t frame_count,
                                    TraceRecorder* trace) {
    if (frame_count < 0) {
        return Result<void>::fail(ErrorCode::InvalidArgument, "negative frame count");
    }
    if (stages.empty() && frame_count > 0) {
        return Result<void>::fail(ErrorCode::InvalidArgument, "pipeline has no stages");
    }

    const auto start = std::chrono::steady_clock::now();
    for (int64_t frame = 0; frame < frame_count; ++frame) {
        if (trace != nullptr) {
            trace->instant("frame.begin", "pipeline");
        }

        const Result<void> backpressure = applyBackpressure(trace);
        if (!backpressure.ok()) {
            return backpressure;
        }

        // Anti-TDR mechanism 2: never let more than max_queue_depth command
        // lists accumulate on the D3D12 queue.  Retirement is observed through
        // the shared fence, not through a CPU submit counter.
        if (hooks_.queue_depth) {
            if (hooks_.queue_depth() >= config_.max_queue_depth) {
                ++metrics_.queue_stalls;
                if (trace != nullptr) {
                    trace->instant("queue.stall", "pipeline");
                }
                const auto deadline =
                    std::chrono::steady_clock::now() + config_.drain_timeout;
                while (hooks_.queue_depth() >= config_.max_queue_depth) {
                    if (!hooks_.drain_one) {
                        return Result<void>::fail(
                            ErrorCode::State,
                            "the hardware queue is full but no drain callback is installed");
                    }
                    hooks_.drain_one();
                    if (std::chrono::steady_clock::now() >= deadline) {
                        return Result<void>::fail(
                            ErrorCode::Timeout,
                            "D3D12 queue did not retire a frame before the drain timeout");
                    }
                }
            }
        }

        if (hooks_.in_flight) {
            const auto deadline = std::chrono::steady_clock::now() + config_.drain_timeout;
            while (hooks_.in_flight() >= config_.max_in_flight) {
                if (!hooks_.drain_one) {
                    return Result<void>::fail(
                        ErrorCode::State,
                        "in-flight frame limit reached but no drain callback is installed");
                }
                hooks_.drain_one();
                if (std::chrono::steady_clock::now() >= deadline) {
                    return Result<void>::fail(ErrorCode::Timeout,
                                              "in-flight frame limit could not be satisfied");
                }
            }
        }

        FrameContext context;
        context.index = frame;
        for (const auto& stage : stages) {
            const Result<void> result = runStage(stage, context, trace);
            if (!result.ok()) {
                ++metrics_.dropped;
                if (hooks_.on_drop) {
                    hooks_.on_drop(frame);
                }
                if (hooks_.log) {
                    hooks_.log(result.error().toString().c_str());
                }
                return Result<void>::fail(
                    result.error().code,
                    std::string("stage ") + stageName(stage.stage) + " failed: " +
                        result.error().message,
                    result.error().detail);
            }
        }

        // Anti-TDR mechanism 1: one submission per frame.  A single-frame
        // command list hands the GPU scheduler a pre-emption point every
        // frame even when per-frame flushing was explicitly disabled.
        if ((config_.single_frame_submission || config_.per_frame_flush) && hooks_.flush) {
            hooks_.flush();
            ++metrics_.flush_count;
        }
        ++metrics_.frames;
        if (trace != nullptr) {
            trace->instant("frame.end", "pipeline");
        }
    }

    metrics_.elapsed_ms = std::chrono::duration<double, std::milli>(
                              std::chrono::steady_clock::now() - start)
                              .count();
    if (metrics_.elapsed_ms > 0.0) {
        metrics_.output_fps = static_cast<double>(metrics_.frames) * 1000.0 / metrics_.elapsed_ms;
    }
    return {};
}

uint64_t TraceRecorder::nowMicroseconds() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

int TraceRecorder::currentThreadId() {
    static thread_local int id = [] {
        static std::atomic<int> next{1};
        return next.fetch_add(1);
    }();
    return id;
}

TraceRecorder::TraceRecorder(std::string process_name) : process_name_(std::move(process_name)) {}

void TraceRecorder::eventBegin(const char* name, const char* category) {
    Event event;
    event.name = name != nullptr ? name : "";
    event.category = category != nullptr ? category : "";
    event.phase = 'X';
    event.timestamp_us = nowMicroseconds();
    event.thread_id = currentThreadId();
    events_.push_back(std::move(event));
}

TraceRecorder::Event* TraceRecorder::findOpen(const char* name, const char* category) {
    for (auto it = events_.rbegin(); it != events_.rend(); ++it) {
        if (it->phase == 'X' && it->duration_us == 0 && it->name == name &&
            it->category == category) {
            return &*it;
        }
    }
    return nullptr;
}

void TraceRecorder::eventEnd(const char* name, const char* category) {
    Event* event = findOpen(name, category);
    if (event == nullptr) {
        instant(name, category);
        return;
    }
    event->duration_us = nowMicroseconds() - event->timestamp_us;
}

void TraceRecorder::instant(const char* name, const char* category) {
    Event event;
    event.name = name != nullptr ? name : "";
    event.category = category != nullptr ? category : "";
    event.phase = 'i';
    event.timestamp_us = nowMicroseconds();
    event.thread_id = currentThreadId();
    events_.push_back(std::move(event));
}

void TraceRecorder::counter(const char* name, double value) {
    Event event;
    event.name = name != nullptr ? name : "";
    event.category = "counter";
    event.phase = 'C';
    event.timestamp_us = nowMicroseconds();
    event.thread_id = currentThreadId();
    event.counter_value = value;
    events_.push_back(std::move(event));
}

std::string TraceRecorder::toJson() const {
    std::ostringstream out;
    out << "{\n  \"displayTimeUnit\": \"ms\",\n  \"traceEvents\": [\n";
    for (size_t i = 0; i < events_.size(); ++i) {
        const Event& event = events_[i];
        out << "    {\"name\":\"" << escapeJson(event.name) << "\",\"cat\":\""
            << escapeJson(event.category) << "\",\"ph\":\"" << event.phase << "\",\"ts\":"
            << event.timestamp_us;
        if (event.phase == 'X') {
            out << ",\"dur\":" << event.duration_us;
        }
        if (event.phase == 'C') {
            out << ",\"args\":{\"value\":" << event.counter_value << "}";
        }
        out << ",\"pid\":1,\"tid\":" << event.thread_id << "}";
        if (i + 1 != events_.size()) {
            out << ",";
        }
        out << "\n";
    }
    out << "  ],\n  \"metadata\": {\"process_name\": \"" << escapeJson(process_name_)
        << "\"}\n}\n";
    return out.str();
}

Status TraceRecorder::save(const std::string& path) const {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) {
        return Status::fail(ErrorCode::Io, "failed to open trace file", path);
    }
    const std::string json = toJson();
    stream.write(json.data(), static_cast<std::streamsize>(json.size()));
    if (!stream) {
        return Status::fail(ErrorCode::Io, "failed to write trace file", path);
    }
    return Status::success();
}

std::string ZeroCopyAudit::report() const {
    std::ostringstream out;
    out << "Zero-copy audit: H2D=" << hostToDeviceBytes() << " B, D2H=" << deviceToHostBytes()
        << " B, D2D=" << deviceToDeviceBytes() << " B, compressed packets=" << packetBytes()
        << " B, status=" << (passed() ? "PASS" : "FAIL");
    return out.str();
}

}  // namespace rtx
