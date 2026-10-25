// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
//
// Micro-kernel DAG scheduler.  The scheduler owns ordering, backpressure,
// per-frame command-list flushing and observability; the stages own the actual
// device work.  This separation is what makes the same ordering testable on a
// CPU-only machine.

#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "rtx/status.h"
#include "rtx/vram_pool.h"

namespace rtx {

enum class Stage : int {
    Demux = 0,
    Decode = 1,
    Interpolate = 2,
    Preprocess = 3,
    Enhance = 4,
    Pack = 5,
    Encode = 6,
    Mux = 7,
    Count = 8,
};

const char* stageName(Stage stage) noexcept;

struct StageMetrics {
    uint64_t submissions = 0;
    uint64_t failures = 0;
    double ewma_ms = 0.0;
    double max_ms = 0.0;
};

struct PipelineMetrics {
    StageMetrics stages[static_cast<size_t>(Stage::Count)];
    uint64_t frames = 0;
    uint64_t dropped = 0;
    uint64_t backpressure_events = 0;
    uint64_t flush_count = 0;
    // Submissions held until the hardware queue retired below the depth cap
    // (anti-TDR mechanism 2).
    uint64_t queue_stalls = 0;
    double elapsed_ms = 0.0;
    double input_fps = 0.0;
    double output_fps = 0.0;

    std::string toJson() const;
};

struct FrameContext {
    int64_t index = 0;
    int64_t pts = 0;
    bool scene_cut = false;
    bool bypass_interpolation = false;
    bool duplicate_previous = false;
    // Per-stage duration reported by the stage itself, in milliseconds.  A stage
    // fills its own slot when it knows a more accurate duration than the
    // scheduler's wall clock (a GPU timestamp delta, for example).  Zero means
    // "not reported" and makes the scheduler measure the call instead.
    double stage_ms[static_cast<size_t>(Stage::Count)] = {};
};

struct StageBinding {
    Stage stage = Stage::Demux;
    std::function<Result<void>(FrameContext&)> execute;
};

struct PipelineHooks {
    std::function<bool()> pause_decode;
    std::function<void()> drain_one;
    std::function<size_t()> in_flight;
    // Number of command lists submitted but not yet retired on the D3D12
    // queue.  When it reaches max_queue_depth the scheduler drains before it
    // records the next frame, keeping queue latency bounded.
    std::function<size_t()> queue_depth;
    std::function<void()> flush;
    std::function<void(int64_t)> on_drop;
    std::function<void(const char*)> log;
};

struct PipelineConfig {
    // Two frames is the hard WDDM queue ceiling on the Blackwell mobile path;
    // one frame is the preferred pre-emption granularity.
    size_t max_in_flight = 2;
    size_t max_queue_depth = 2;
    bool per_frame_flush = true;
    // Every frame gets its own command list, giving the GPU a pre-emption
    // point at roughly 10 ms instead of a multi-frame batch.
    bool single_frame_submission = true;
    bool enable_backpressure = true;
    std::chrono::milliseconds drain_timeout{10000};
    size_t drain_batch = 4;
};

class TraceRecorder;

class PipelineScheduler {
public:
    PipelineScheduler(PipelineConfig config, PipelineHooks hooks);

    Result<void> run(const std::vector<StageBinding>& stages, int64_t frame_count,
                     TraceRecorder* trace = nullptr);

    const PipelineMetrics& metrics() const noexcept { return metrics_; }
    void resetMetrics() { metrics_ = {}; }

private:
    Result<void> runStage(const StageBinding& binding, FrameContext& context,
                          TraceRecorder* trace);
    Result<void> applyBackpressure(TraceRecorder* trace);

    PipelineConfig config_;
    PipelineHooks hooks_;
    PipelineMetrics metrics_;
};

// Chrome tracing format ("cat":"RTX", "ph":"X").  Consumed by chrome://tracing
// and by the zero-copy audit script in tools/.
class TraceRecorder {
public:
    explicit TraceRecorder(std::string process_name = "rtx-transcoder");

    void eventBegin(const char* name, const char* category);
    void eventEnd(const char* name, const char* category);
    void instant(const char* name, const char* category);
    void counter(const char* name, double value);

    Status save(const std::string& path) const;
    std::string toJson() const;
    void clear() { events_.clear(); }
    size_t size() const noexcept { return events_.size(); }

private:
    struct Event {
        std::string name;
        std::string category;
        char phase = 'X';
        uint64_t timestamp_us = 0;
        uint64_t duration_us = 0;
        int thread_id = 0;
        double counter_value = 0.0;
    };

    static uint64_t nowMicroseconds();
    static int currentThreadId();
    Event* findOpen(const char* name, const char* category);

    std::string process_name_;
    std::vector<Event> events_;
};

// Zero-copy audit.  Pixel traffic must never cross PCIe; only compressed
// packet bytes are allowed to touch host memory.
class ZeroCopyAudit {
public:
    void onHostToDevice(size_t bytes) { h2d_bytes_.fetch_add(bytes, std::memory_order_relaxed); }
    void onDeviceToHost(size_t bytes) { d2h_bytes_.fetch_add(bytes, std::memory_order_relaxed); }
    void onDeviceToDevice(size_t bytes) { d2d_bytes_.fetch_add(bytes, std::memory_order_relaxed); }
    void onPacketBytes(size_t bytes) { packet_bytes_.fetch_add(bytes, std::memory_order_relaxed); }

    uint64_t hostToDeviceBytes() const { return h2d_bytes_.load(); }
    uint64_t deviceToHostBytes() const { return d2h_bytes_.load(); }
    uint64_t deviceToDeviceBytes() const { return d2d_bytes_.load(); }
    uint64_t packetBytes() const { return packet_bytes_.load(); }
    bool passed() const { return hostToDeviceBytes() == 0 && deviceToHostBytes() == 0; }

    std::string report() const;

private:
    std::atomic<uint64_t> h2d_bytes_{0};
    std::atomic<uint64_t> d2h_bytes_{0};
    std::atomic<uint64_t> d2d_bytes_{0};
    std::atomic<uint64_t> packet_bytes_{0};
};

}  // namespace rtx
