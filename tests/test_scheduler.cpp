// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors

#include "test_framework.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

#include "rtx/scheduler.h"
#include "test_util.h"

using namespace rtx;

RTX_TEST(scheduler_stage_names) {
    RTX_CHECK_EQ(std::string(stageName(Stage::Demux)), std::string("Demux"));
    RTX_CHECK_EQ(std::string(stageName(Stage::Decode)), std::string("Decode"));
    RTX_CHECK_EQ(std::string(stageName(Stage::Interpolate)), std::string("Interpolate"));
    RTX_CHECK_EQ(std::string(stageName(Stage::Preprocess)), std::string("Preprocess"));
    RTX_CHECK_EQ(std::string(stageName(Stage::Enhance)), std::string("Enhance"));
    RTX_CHECK_EQ(std::string(stageName(Stage::Pack)), std::string("Pack"));
    RTX_CHECK_EQ(std::string(stageName(Stage::Encode)), std::string("Encode"));
    RTX_CHECK_EQ(std::string(stageName(Stage::Mux)), std::string("Mux"));
    RTX_CHECK_EQ(std::string(stageName(Stage::Count)), std::string("Unknown"));
    RTX_CHECK_EQ(std::string(stageName(static_cast<Stage>(99))), std::string("Unknown"));
}

RTX_TEST(scheduler_runs_stages_in_order) {
    std::vector<int> trace;
    int flushes = 0;
    std::vector<StageBinding> stages;
    for (int i = 0; i < 3; ++i) {
        stages.push_back(StageBinding{
            static_cast<Stage>(i),
            [&trace, i](FrameContext& context) {
                trace.push_back(i * 100 + static_cast<int>(context.index));
                context.stage_ms[i] = 0.5;
                return Result<void>{};
            }});
    }
    PipelineConfig config;
    config.max_in_flight = 4;
    PipelineHooks hooks;
    hooks.flush = [&flushes]() { ++flushes; };
    PipelineScheduler scheduler(config, hooks);
    TraceRecorder trace_recorder("scheduler-test");
    const Result<void> result = scheduler.run(stages, 5, &trace_recorder);
    RTX_REQUIRE(result.ok());
    RTX_REQUIRE(trace.size() == static_cast<size_t>(15));
    for (int frame = 0; frame < 5; ++frame) {
        RTX_CHECK_EQ(trace[static_cast<size_t>(frame) * 3 + 0], frame);
        RTX_CHECK_EQ(trace[static_cast<size_t>(frame) * 3 + 1], 100 + frame);
        RTX_CHECK_EQ(trace[static_cast<size_t>(frame) * 3 + 2], 200 + frame);
    }
    RTX_CHECK_EQ(flushes, 5);
    const PipelineMetrics& metrics = scheduler.metrics();
    RTX_CHECK_EQ(metrics.frames, static_cast<uint64_t>(5));
    RTX_CHECK_EQ(metrics.flush_count, static_cast<uint64_t>(5));
    RTX_CHECK_EQ(metrics.dropped, static_cast<uint64_t>(0));
    RTX_CHECK_EQ(metrics.stages[0].submissions, static_cast<uint64_t>(5));
    RTX_CHECK_EQ(metrics.stages[2].submissions, static_cast<uint64_t>(5));
    // Each stage reports its own duration, so the recorded metrics are exact
    // instead of depending on wall-clock granularity.
    RTX_CHECK_NEAR(metrics.stages[0].ewma_ms, 0.5, 1e-9);
    RTX_CHECK_NEAR(metrics.stages[0].max_ms, 0.5, 1e-9);
    RTX_CHECK(metrics.elapsed_ms > 0.0);
    RTX_CHECK(metrics.output_fps > 0.0);
    RTX_CHECK(trace_recorder.size() > 0);

    scheduler.resetMetrics();
    RTX_CHECK_EQ(scheduler.metrics().frames, static_cast<uint64_t>(0));
}

RTX_TEST(scheduler_stage_reported_duration_wins) {
    // A stage that reports a duration overrides the scheduler's wall clock, so a
    // GPU timestamp delta is not replaced by the launch overhead.
    const std::vector<StageBinding> stages{StageBinding{Stage::Demux, [](FrameContext& context) {
        context.stage_ms[0] = 250.0;
        return Result<void>{};
    }}};
    PipelineScheduler scheduler(PipelineConfig{}, PipelineHooks{});
    RTX_REQUIRE(scheduler.run(stages, 1).ok());
    RTX_CHECK_NEAR(scheduler.metrics().stages[0].ewma_ms, 250.0, 1e-9);
    RTX_CHECK_NEAR(scheduler.metrics().stages[0].max_ms, 250.0, 1e-9);
    RTX_CHECK(scheduler.metrics().elapsed_ms < 250.0);
}

RTX_TEST(scheduler_measures_stages_without_reported_duration) {
    // Without a reported duration the scheduler measures the call.  The value
    // must survive as a floating point duration, never truncated to 0 ms.
    const std::vector<StageBinding> stages{StageBinding{Stage::Demux, [](FrameContext&) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        return Result<void>{};
    }}};
    PipelineScheduler scheduler(PipelineConfig{}, PipelineHooks{});
    RTX_REQUIRE(scheduler.run(stages, 1).ok());
    const StageMetrics& metrics = scheduler.metrics().stages[0];
    RTX_CHECK_EQ(metrics.submissions, static_cast<uint64_t>(1));
    RTX_CHECK(metrics.ewma_ms >= 1.0);
    RTX_CHECK(metrics.max_ms >= metrics.ewma_ms);
}

RTX_TEST(scheduler_stage_failure_stops_pipeline) {
    int executed = 0;
    int dropped_index = -1;
    std::string logged;
    std::vector<StageBinding> stages;
    stages.push_back(StageBinding{Stage::Demux, [&](FrameContext&) {
                                      ++executed;
                                      return Result<void>{};
                                  }});
    stages.push_back(StageBinding{Stage::Decode, [&](FrameContext&) {
                                      return Result<void>::fail(ErrorCode::Io, "decode failed",
                                                                "test detail");
                                  }});
    stages.push_back(StageBinding{Stage::Encode, [&](FrameContext&) {
                                      ++executed;
                                      return Result<void>{};
                                  }});
    PipelineHooks hooks;
    hooks.on_drop = [&](int64_t index) { dropped_index = static_cast<int>(index); };
    hooks.log = [&](const char* message) { logged = message != nullptr ? message : ""; };
    PipelineScheduler scheduler(PipelineConfig{}, hooks);
    const Result<void> result = scheduler.run(stages, 3);
    RTX_CHECK(!result.ok());
    RTX_CHECK_EQ(result.code(), ErrorCode::Io);
    RTX_CHECK(result.error().message.find("Decode") != std::string::npos);
    RTX_CHECK_EQ(executed, 1);
    RTX_CHECK_EQ(dropped_index, 0);
    RTX_CHECK(logged.find("decode failed") != std::string::npos);
    RTX_CHECK_EQ(scheduler.metrics().frames, static_cast<uint64_t>(0));
    RTX_CHECK_EQ(scheduler.metrics().dropped, static_cast<uint64_t>(1));
    RTX_CHECK_EQ(scheduler.metrics().stages[1].failures, static_cast<uint64_t>(1));
}

RTX_TEST(scheduler_invalid_configuration) {
    PipelineScheduler scheduler(PipelineConfig{}, PipelineHooks{});
    RTX_CHECK_EQ(scheduler.run({}, 1).code(), ErrorCode::InvalidArgument);
    RTX_CHECK_EQ(scheduler.run({}, -1).code(), ErrorCode::InvalidArgument);
    const Result<void> empty = scheduler.run({}, 0);
    RTX_CHECK(empty.ok());
    RTX_CHECK_EQ(scheduler.metrics().frames, static_cast<uint64_t>(0));

    std::vector<StageBinding> stages;
    stages.push_back(StageBinding{Stage::Demux, nullptr});
    const Result<void> missing = scheduler.run(stages, 1);
    RTX_CHECK(!missing.ok());
    RTX_CHECK_EQ(missing.code(), ErrorCode::InvalidArgument);
    stages[0].stage = Stage::Count;
    RTX_CHECK_EQ(scheduler.run(stages, 1).code(), ErrorCode::InvalidArgument);
}

RTX_TEST(scheduler_backpressure_drain) {
    std::atomic<bool> paused{true};
    int drained = 0;
    PipelineHooks hooks;
    hooks.pause_decode = [&paused]() { return paused.load(); };
    hooks.drain_one = [&]() {
        ++drained;
        if (drained >= 4) {
            paused.store(false);
        }
    };
    PipelineConfig config;
    config.drain_batch = 4;
    PipelineScheduler scheduler(config, hooks);
    const std::vector<StageBinding> stages{
        StageBinding{Stage::Demux, [](FrameContext&) { return Result<void>{}; }}};
    TraceRecorder trace;
    RTX_REQUIRE(scheduler.run(stages, 1, &trace).ok());
    RTX_CHECK_EQ(drained, 4);
    RTX_CHECK_EQ(scheduler.metrics().backpressure_events, static_cast<uint64_t>(1));
    RTX_CHECK(trace.toJson().find("backpressure.pause") != std::string::npos);
    RTX_CHECK(trace.toJson().find("backpressure.resume") != std::string::npos);
}

RTX_TEST(scheduler_backpressure_errors) {
    PipelineHooks no_drain;
    no_drain.pause_decode = []() { return true; };
    PipelineScheduler scheduler(PipelineConfig{}, no_drain);
    const std::vector<StageBinding> stages{
        StageBinding{Stage::Demux, [](FrameContext&) { return Result<void>{}; }}};
    const Result<void> missing = scheduler.run(stages, 1);
    RTX_CHECK(!missing.ok());
    RTX_CHECK_EQ(missing.code(), ErrorCode::State);

    PipelineHooks stuck;
    stuck.pause_decode = []() { return true; };
    stuck.drain_one = []() {};
    PipelineConfig config;
    config.drain_timeout = std::chrono::milliseconds(1);
    PipelineScheduler timeout_scheduler(config, stuck);
    const Result<void> timeout = timeout_scheduler.run(stages, 1);
    RTX_CHECK(!timeout.ok());
    RTX_CHECK_EQ(timeout.code(), ErrorCode::Timeout);
}

RTX_TEST(scheduler_in_flight_limit) {
    int in_flight = 2;
    int drained = 0;
    PipelineHooks hooks;
    hooks.in_flight = [&in_flight]() { return static_cast<size_t>(in_flight); };
    hooks.drain_one = [&]() {
        ++drained;
        if (in_flight > 0) {
            --in_flight;
        }
    };
    int stage_calls = 0;
    const std::vector<StageBinding> stages{
        StageBinding{Stage::Encode, [&](FrameContext&) {
                         ++stage_calls;
                         ++in_flight;
                         return Result<void>{};
                     }}};
    PipelineConfig config;
    config.max_in_flight = 2;
    PipelineScheduler scheduler(config, hooks);
    RTX_REQUIRE(scheduler.run(stages, 4).ok());
    RTX_CHECK_EQ(stage_calls, 4);
    RTX_CHECK(drained >= 4);
    RTX_CHECK(in_flight <= 2);
}

RTX_TEST(scheduler_queue_depth_gate) {
    // Anti-TDR mechanism 2: a full hardware queue must be drained before the
    // next frame is recorded, and the stall is visible in the metrics/trace.
    size_t depth = 2;
    int drained = 0;
    int flushes = 0;
    PipelineHooks hooks;
    hooks.queue_depth = [&depth]() { return depth; };
    hooks.drain_one = [&]() {
        ++drained;
        if (depth > 0) {
            --depth;
        }
    };
    hooks.flush = [&]() {
        ++flushes;
        ++depth;
    };
    PipelineConfig config;
    PipelineScheduler scheduler(config, hooks);
    const std::vector<StageBinding> stages{
        StageBinding{Stage::Demux, [](FrameContext&) { return Result<void>{}; }}};
    TraceRecorder trace;
    RTX_REQUIRE(scheduler.run(stages, 3, &trace).ok());
    RTX_CHECK_EQ(flushes, 3);
    RTX_CHECK_EQ(drained, 3);
    RTX_CHECK_EQ(scheduler.metrics().queue_stalls, static_cast<uint64_t>(3));
    RTX_CHECK(depth <= config.max_queue_depth);
    RTX_CHECK(trace.toJson().find("queue.stall") != std::string::npos);
}

RTX_TEST(scheduler_queue_depth_errors) {
    PipelineHooks no_drain;
    no_drain.queue_depth = []() { return static_cast<size_t>(2); };
    PipelineScheduler scheduler(PipelineConfig{}, no_drain);
    const std::vector<StageBinding> stages{
        StageBinding{Stage::Demux, [](FrameContext&) { return Result<void>{}; }}};
    const Result<void> missing = scheduler.run(stages, 1);
    RTX_CHECK(!missing.ok());
    RTX_CHECK_EQ(missing.code(), ErrorCode::State);

    PipelineHooks stuck;
    stuck.queue_depth = []() { return static_cast<size_t>(2); };
    stuck.drain_one = []() {};
    PipelineConfig config;
    config.drain_timeout = std::chrono::milliseconds(1);
    PipelineScheduler timeout_scheduler(config, stuck);
    const Result<void> timeout = timeout_scheduler.run(stages, 1);
    RTX_CHECK(!timeout.ok());
    RTX_CHECK_EQ(timeout.code(), ErrorCode::Timeout);
}

RTX_TEST(scheduler_single_frame_submission) {
    // Anti-TDR mechanism 1: one submission per frame even when per-frame
    // flushing is off; only an explicit opt-out disables the final flush.
    int flushes = 0;
    PipelineHooks hooks;
    hooks.flush = [&flushes]() { ++flushes; };
    PipelineConfig per_frame_disabled;
    per_frame_disabled.per_frame_flush = false;
    PipelineScheduler scheduler(per_frame_disabled, hooks);
    const std::vector<StageBinding> stages{
        StageBinding{Stage::Demux, [](FrameContext&) { return Result<void>{}; }}};
    RTX_REQUIRE(scheduler.run(stages, 4).ok());
    RTX_CHECK_EQ(flushes, 4);
    RTX_CHECK_EQ(scheduler.metrics().flush_count, static_cast<uint64_t>(4));

    int disabled_flushes = 0;
    PipelineHooks disabled_hooks;
    disabled_hooks.flush = [&disabled_flushes]() { ++disabled_flushes; };
    PipelineConfig fully_disabled;
    fully_disabled.per_frame_flush = false;
    fully_disabled.single_frame_submission = false;
    PipelineScheduler disabled_scheduler(fully_disabled, disabled_hooks);
    RTX_REQUIRE(disabled_scheduler.run(stages, 4).ok());
    RTX_CHECK_EQ(disabled_flushes, 0);
}

RTX_TEST(trace_recorder_events_and_save) {
    TraceRecorder trace("rtx-test");
    trace.eventBegin("alpha", "stage");
    trace.instant("marker", "pipeline");
    trace.eventEnd("alpha", "stage");
    trace.eventEnd("never-begun", "stage");  // becomes an instant
    trace.counter("vram", 123.5);
    RTX_CHECK(trace.size() >= 4);
    const std::string json = trace.toJson();
    RTX_CHECK(json.find("\"displayTimeUnit\": \"ms\"") != std::string::npos);
    RTX_CHECK(json.find("\"name\":\"alpha\"") != std::string::npos);
    RTX_CHECK(json.find("\"cat\":\"stage\"") != std::string::npos);
    RTX_CHECK(json.find("\"ph\":\"X\"") != std::string::npos);
    RTX_CHECK(json.find("\"ph\":\"i\"") != std::string::npos);
    RTX_CHECK(json.find("\"ph\":\"C\"") != std::string::npos);
    RTX_CHECK(json.find("\"value\":123.5") != std::string::npos);
    RTX_CHECK(json.find("\"pid\":1") != std::string::npos);
    RTX_CHECK(json.find("never-begun") != std::string::npos);
    RTX_CHECK(json.find("rtx-test") != std::string::npos);

    test::TempDir directory;
    const std::string path = directory.file("trace.json");
    RTX_CHECK(trace.save(path).ok());
    const std::vector<uint8_t> bytes = test::readAll(path);
    RTX_CHECK(!bytes.empty());
    RTX_CHECK(std::string(bytes.begin(), bytes.end()) == json);
    RTX_CHECK_EQ(trace.save(directory.file("missing/dir/trace.json")).code(), ErrorCode::Io);

    trace.clear();
    RTX_CHECK_EQ(trace.size(), static_cast<size_t>(0));
    RTX_CHECK(trace.toJson().find("traceEvents") != std::string::npos);
}

RTX_TEST(trace_json_escaping) {
    TraceRecorder trace("quote\"and\\slash\nnewline");
    trace.instant("a\"b\\c\nd", "cat\ttype");
    const std::string json = trace.toJson();
    RTX_CHECK(json.find("quote\\\"and\\\\slash\\nnewline") != std::string::npos);
    RTX_CHECK(json.find("a\\\"b\\\\c\\nd") != std::string::npos);
    RTX_CHECK(json.find("cat\\ttype") != std::string::npos);
}

RTX_TEST(scheduler_zero_copy_audit) {
    ZeroCopyAudit audit;
    RTX_CHECK(audit.passed());
    RTX_CHECK(audit.report().find("PASS") != std::string::npos);
    audit.onDeviceToDevice(1024);
    audit.onPacketBytes(2048);
    RTX_CHECK(audit.passed());
    RTX_CHECK_EQ(audit.deviceToDeviceBytes(), static_cast<uint64_t>(1024));
    RTX_CHECK_EQ(audit.packetBytes(), static_cast<uint64_t>(2048));
    audit.onHostToDevice(16);
    RTX_CHECK(!audit.passed());
    RTX_CHECK(audit.report().find("FAIL") != std::string::npos);
    RTX_CHECK(audit.report().find("H2D=16") != std::string::npos);
    RTX_CHECK_EQ(audit.hostToDeviceBytes(), static_cast<uint64_t>(16));
    audit.onDeviceToHost(32);
    RTX_CHECK_EQ(audit.deviceToHostBytes(), static_cast<uint64_t>(32));
    RTX_CHECK(!audit.passed());
}

RTX_TEST(pipeline_metrics_json) {
    PipelineMetrics metrics;
    metrics.frames = 12;
    metrics.dropped = 1;
    metrics.backpressure_events = 2;
    metrics.flush_count = 12;
    metrics.queue_stalls = 3;
    metrics.elapsed_ms = 3.5;
    metrics.input_fps = 24.0;
    metrics.output_fps = 60.0;
    metrics.stages[0].submissions = 12;
    metrics.stages[0].failures = 0;
    metrics.stages[0].ewma_ms = 0.25;
    const std::string json = metrics.toJson();
    RTX_CHECK(json.find("\"frames\": 12") != std::string::npos);
    RTX_CHECK(json.find("\"dropped\": 1") != std::string::npos);
    RTX_CHECK(json.find("\"queue_stalls\": 3") != std::string::npos);
    RTX_CHECK(json.find("\"name\": \"Demux\"") != std::string::npos);
    RTX_CHECK(json.find("\"elapsed_ms\": 3.500") != std::string::npos);
    RTX_CHECK(json.find("\"output_fps\": 60") != std::string::npos);
}

// ---------------------------------------------------------------------------
// Phase-3 seven-stage DAG and three-tier VRAM watermark (W8-3)
// ---------------------------------------------------------------------------

RTX_TEST(scheduler_identity_dag_fill) {
    // The canonical graph is exactly the seven phase-3 operators, in order.
    const std::vector<Stage> order = canonicalStageOrder();
    RTX_REQUIRE_EQ(order.size(), static_cast<size_t>(7));
    RTX_CHECK(order[0] == Stage::Demux);
    RTX_CHECK(order[1] == Stage::Decode);
    RTX_CHECK(order[2] == Stage::Interpolate);
    RTX_CHECK(order[3] == Stage::Enhance);
    RTX_CHECK(order[4] == Stage::Pack);
    RTX_CHECK(order[5] == Stage::Encode);
    RTX_CHECK(order[6] == Stage::Mux);

    // A bare transcode supplies only Demux/Decode/Encode/Mux; the other three
    // stages are bound to the identity operator so both modes share one DAG.
    std::vector<StageBinding> bare;
    for (Stage stage : {Stage::Demux, Stage::Decode, Stage::Encode, Stage::Mux}) {
        StageBinding binding;
        binding.stage = stage;
        binding.execute = [stage](FrameContext& context) -> Result<void> {
            context.stage_ms[static_cast<size_t>(stage)] = 1.0;
            return {};
        };
        bare.push_back(std::move(binding));
    }
    const std::vector<StageBinding> completed = completeStageDag(std::move(bare));
    RTX_REQUIRE_EQ(completed.size(), static_cast<size_t>(7));
    for (size_t index = 0; index < completed.size(); ++index) {
        RTX_CHECK(completed[index].stage == order[index]);
        RTX_REQUIRE(static_cast<bool>(completed[index].execute));
    }

    // Every slot runs, and the identity stages complete without touching the
    // frame context.
    FrameContext context;
    for (const StageBinding& binding : completed) {
        RTX_CHECK(binding.execute(context).ok());
    }
    RTX_CHECK_NEAR(context.stage_ms[static_cast<size_t>(Stage::Decode)], 1.0, 1e-12);
    RTX_CHECK_NEAR(context.stage_ms[static_cast<size_t>(Stage::Interpolate)], 0.0, 1e-12);

    // An empty input becomes a complete no-op DAG rather than an error.
    const std::vector<StageBinding> empty = completeStageDag({});
    RTX_CHECK_EQ(empty.size(), static_cast<size_t>(7));
    for (const StageBinding& binding : empty) {
        FrameContext noop_context;
        RTX_CHECK(binding.execute(noop_context).ok());
    }
}

RTX_TEST(scheduler_vram_watermark_tiers) {
    BackpressureController controller;
    uint64_t fallbacks = 0;
    uint64_t restores = 0;
    controller.setEmergencyFallback([&fallbacks]() { ++fallbacks; });
    controller.setRestorePreferred([&restores]() { ++restores; });

    constexpr uint64_t kTotal = 8ull * 1024 * 1024 * 1024;  // 8 GB
    // Round to the nearest byte: truncating 8 GiB * 0.88 leaves the sample one
    // byte below the 88 % threshold, which would test 87.99999...% instead.
    const auto bytes = [](double ratio) {
        return static_cast<uint64_t>(std::llround(static_cast<double>(kTotal) * ratio));
    };

    RTX_CHECK(controller.vramTier() == WatermarkTier::Low);
    controller.updateVramState(bytes(0.50), kTotal);
    RTX_CHECK(controller.vramTier() == WatermarkTier::Low);

    // Low -> High at 75 %.
    controller.updateVramState(bytes(0.74), kTotal);
    RTX_CHECK(controller.vramTier() == WatermarkTier::Low);
    controller.updateVramState(bytes(0.75), kTotal);
    RTX_CHECK(controller.vramTier() == WatermarkTier::High);
    RTX_CHECK_EQ(fallbacks, static_cast<uint64_t>(0));

    // High -> Critical at 88 %: the TensorRT RIFE engine is unloaded here.
    controller.updateVramState(bytes(0.88), kTotal);
    RTX_CHECK(controller.vramTier() == WatermarkTier::Critical);
    RTX_CHECK_EQ(fallbacks, static_cast<uint64_t>(1));

    // A single dip below 55 % is not enough to restore: 300 consecutive
    // samples (about 5 s at 60 Hz) are required.
    controller.updateVramState(bytes(0.50), kTotal);
    RTX_CHECK(controller.vramTier() == WatermarkTier::Critical);
    for (int sample = 0; sample < 298; ++sample) {
        controller.updateVramState(bytes(0.50), kTotal);
    }
    RTX_CHECK(controller.vramTier() == WatermarkTier::Critical);
    RTX_CHECK_EQ(restores, static_cast<uint64_t>(0));
    controller.updateVramState(bytes(0.50), kTotal);
    RTX_CHECK(controller.vramTier() == WatermarkTier::Low);
    RTX_CHECK_EQ(restores, static_cast<uint64_t>(1));

    // Any spike inside the stable window restarts the counter.
    controller.updateVramState(bytes(0.90), kTotal);
    RTX_CHECK(controller.vramTier() == WatermarkTier::High);
    controller.updateVramState(bytes(0.89), kTotal);
    RTX_CHECK(controller.vramTier() == WatermarkTier::Critical);
    for (int sample = 0; sample < 200; ++sample) {
        controller.updateVramState(bytes(0.40), kTotal);
    }
    controller.updateVramState(bytes(0.70), kTotal);  // > 55 %: reset
    for (int sample = 0; sample < 200; ++sample) {
        controller.updateVramState(bytes(0.40), kTotal);
    }
    RTX_CHECK(controller.vramTier() == WatermarkTier::Critical);
    RTX_CHECK_EQ(restores, static_cast<uint64_t>(1));

    // A zero total (a driver that reports nothing) must not divide by zero.
    controller.updateVramState(1024, 0);
    RTX_CHECK(controller.vramTier() == WatermarkTier::Critical);

    RTX_CHECK_EQ(std::string(watermarkTierName(WatermarkTier::Low)), std::string("Low"));
    RTX_CHECK_EQ(std::string(watermarkTierName(WatermarkTier::Critical)),
                 std::string("Critical"));
    controller.reset();
    RTX_CHECK(controller.vramTier() == WatermarkTier::Low);
}
