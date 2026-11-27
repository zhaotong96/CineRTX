// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors

#include "test_framework.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <system_error>
#include <vector>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif defined(__linux__)
#include <unistd.h>
#endif

#include "rtx/flicks.h"
#include "rtx/io.h"
#include "rtx/ngx_remaster.h"
#include "rtx/nvidia_shim.h"
#include "rtx/playback_session.h"
#include "rtx/rational.h"
#include "rtx/telemetry_bus.h"
#include "rtx/time_model.h"

using namespace rtx;

namespace {

constexpr int64_t kSecond = static_cast<int64_t>(kFlicksPerSecond);

const char* fixtureName(VideoCodec codec) {
    switch (codec) {
        case VideoCodec::H264: return "h264_25fps_gop12.mkv";
        case VideoCodec::HEVC: return "hevc_25fps_gop12.mkv";
        case VideoCodec::AV1: return "av1_25fps_gop12.mkv";
        default: return nullptr;
    }
}
[[nodiscard]] std::filesystem::path currentExecutableDir() {
#if defined(_WIN32)
    std::wstring buf(512, L'\0');
    for (;;) {
        const DWORD len = ::GetModuleFileNameW(
            nullptr, buf.data(), static_cast<DWORD>(buf.size()));
        if (len == 0) return {};
        if (static_cast<size_t>(len) < buf.size()) {
            buf.resize(len);
            return std::filesystem::path(buf).parent_path();
        }
        if (buf.size() >= 32768) return {};
        buf.resize(buf.size() * 2);
    }
#elif defined(__linux__)
    std::string buf(4096, '\0');
    const ssize_t len = ::readlink("/proc/self/exe", buf.data(), buf.size() - 1);
    if (len <= 0) return {};
    buf.resize(static_cast<size_t>(len));
    return std::filesystem::path(buf).parent_path();
#else
    return {};
#endif
}

[[nodiscard]] std::optional<std::filesystem::path> resolveFixturePath(const char* name) {
    if (name == nullptr || *name == '\0') return std::nullopt;
    std::error_code ec;
    const std::filesystem::path rel_name(name);
    auto isRegular = [&ec](const std::filesystem::path& p) {
        return std::filesystem::exists(p, ec) && std::filesystem::is_regular_file(p, ec);
    };

    // 1. 首选：可执行文件同级 fixtures/ 目录（CMake POST_BUILD 标准部署位）
    const std::filesystem::path exe_dir = currentExecutableDir();
    if (!exe_dir.empty()) {
        const std::filesystem::path alongside = exe_dir / "fixtures" / rel_name;
        if (isRegular(alongside)) return alongside;
    }

    // 2. 次选：当前工作目录（兼容从仓库根目录或构建目录直接调用）
    const std::filesystem::path cwd = std::filesystem::current_path(ec);
    if (!cwd.empty()) {
        for (const std::filesystem::path& cand :
             {cwd / "fixtures" / rel_name, cwd / "tests" / "fixtures" / rel_name}) {
            if (isRegular(cand)) return cand;
        }
    }

    // 3. 回退：沿可执行目录向上逐层探测源码树 tests/fixtures/（零宿主路径硬编码）
    if (!exe_dir.empty()) {
        std::filesystem::path probe = exe_dir;
        for (int depth = 0; depth < 6; ++depth) {
            const std::filesystem::path cand = probe / "tests" / "fixtures" / rel_name;
            if (isRegular(cand)) return cand;
            const std::filesystem::path parent = probe.parent_path();
            if (parent.empty() || parent == probe) break;
            probe = parent;
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<std::vector<uint8_t>> readFixture(const char* name) {
    const auto resolved = resolveFixturePath(name);
    if (!resolved.has_value()) return std::nullopt;
    std::ifstream file(*resolved, std::ios::binary | std::ios::ate);
    if (!file) return std::nullopt;
    const std::streamoff raw_size = file.tellg();
    if (raw_size <= 0) return std::nullopt;
    const auto byte_count = static_cast<std::streamsize>(raw_size);
    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> buffer(static_cast<size_t>(raw_size));
    if (!file.read(reinterpret_cast<char*>(buffer.data()), byte_count) ||
        file.gcount() != byte_count) {
        return std::nullopt;
    }
    return buffer;
}
struct FakeAudioClock {
    int64_t value = 0;
    bool active = false;
    AudioMasterClockFn asClock() noexcept {
        return [this]() noexcept -> std::optional<int64_t> {
            return active ? std::optional<int64_t>(value) : std::nullopt;
        };
    }
};

struct FakeDemuxer {
    int64_t last_seek_pts = -1;
    int seek_calls = 0;
    bool fail_seek = false;
    std::function<Result<void>(int64_t)> asSeekFn() noexcept {
        return [this](int64_t video_stream_pts) -> Result<void> {
            ++seek_calls;
            if (fail_seek) return err(ErrorCode::Io, "fake demuxer seek failure");
            last_seek_pts = video_stream_pts;
            return {};
        };
    }
};

struct CountingSink {
    VideoFrameSink asSink() noexcept {
        return {
            [this](const RtxFrameDesc& frame, int64_t deadline_flicks, uint64_t retire_fence) -> Result<void> {
                std::lock_guard<std::mutex> lock(mutex);
                ++presented;
                last_deadline = deadline_flicks;
                last_pts = frame.pts;
                last_fence = retire_fence;
                deadlines.push_back(deadline_flicks);
                return {};
            },
            []() noexcept -> uint64_t { return UINT64_MAX; }
        };
    }
    std::mutex mutex;
    uint64_t presented = 0;
    int64_t last_deadline = 0;
    int64_t last_pts = 0;
    uint64_t last_fence = 0;
    std::vector<int64_t> deadlines;
};

struct GatedFenceSink {
    VideoFrameSink asSink() noexcept {
        return {
            [this](const RtxFrameDesc&, int64_t, uint64_t retire_fence) -> Result<void> {
                std::lock_guard<std::mutex> lock(mutex);
                issued = retire_fence;
                ++presented;
                return {};
            },
            [this]() noexcept -> uint64_t { return released.load(std::memory_order_acquire); }
        };
    }
    void release(uint64_t token) noexcept { released.store(token, std::memory_order_release); }

    std::mutex mutex;
    std::atomic<uint64_t> issued{0};
    uint64_t presented = 0;
    std::atomic<uint64_t> released{0};
};

bool waitFor(const std::function<bool()>& predicate, int timeout_ms) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return predicate();
}
}  // namespace

RTX_TEST(playback_clock_software_master) {
    PlaybackClockState clock{AudioMasterClockFn{}, std::nullopt, Flicks{0}, 1.0};
    RTX_CHECK_EQ(clockNow(clock).count(), 0LL);
    clockAnchor(clock, Flicks{5 * kSecond}, 1.0);
    RTX_CHECK(clockNow(clock).count() >= (5 * kSecond));

    {
        auto& [audio_clk, anchor_tp, base_flicks, clk_speed] = clock;
        (void)audio_clk;
        base_flicks = clockNow(clock);
        anchor_tp.reset();
        const int64_t frozen = clockNow(clock).count();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        RTX_CHECK_EQ(clockNow(clock).count(), frozen);
        clockAnchor(clock, base_flicks, clk_speed);
        RTX_CHECK(clockNow(clock).count() >= frozen);
    }

    RTX_CHECK_NEAR(clampPlaybackSpeed(4.0), 2.0, 1e-12);
    RTX_CHECK_NEAR(clampPlaybackSpeed(0.01), 0.25, 1e-12);
    RTX_CHECK_NEAR(clampPlaybackSpeed(-1.0), 0.25, 1e-12);
    RTX_CHECK_NEAR(clampPlaybackSpeed(1.0), 1.0, 1e-12);

    clockAnchor(clock, clockNow(clock), 2.0);
    const int64_t start = clockNow(clock).count();
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    RTX_CHECK(clockNow(clock).count() - start >= static_cast<int64_t>(kFlicksPerSecond) * 60 / 1000);
}
RTX_TEST(playback_clock_audio_master_override) {
    FakeAudioClock audio;
    PlaybackClockState clock{audio.asClock(), std::nullopt, Flicks{0}, 1.0};
    clockAnchor(clock, Flicks{0}, 1.0);

    audio.active = false;
    RTX_CHECK(waitFor([&] { return clockNow(clock).count() > 0; }, 1000));

    audio.active = true;
    audio.value = 42 * kSecond;
    RTX_CHECK_EQ(clockNow(clock).count(), 42 * kSecond);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    RTX_CHECK_EQ(clockNow(clock).count(), 42 * kSecond);

    auto& [audio_clk, anchor_tp, base_flicks, clk_speed] = clock;
    (void)clk_speed;
    anchor_tp.reset();
    audio.value = 43 * kSecond;
    RTX_CHECK_EQ(clockNow(clock).count(), 43 * kSecond);

    audio_clk = nullptr;
    audio.active = false;
    RTX_CHECK_EQ(clockNow(clock), base_flicks);
}
RTX_TEST(playback_frame_deadline_verdicts) {
    const int64_t interval = kSecond / 24;
    const int64_t tolerance = dropToleranceFlicks(interval);
    RTX_CHECK_EQ(tolerance, interval / 2);

    const int64_t deadline = 10 * interval;
    RTX_CHECK(classifyFrameDeadline(deadline, deadline - 1, tolerance) == FrameVerdict::Wait);
    RTX_CHECK(classifyFrameDeadline(deadline, deadline, tolerance) == FrameVerdict::Present);
    RTX_CHECK(classifyFrameDeadline(deadline, deadline + interval / 2, tolerance) == FrameVerdict::Present);
    RTX_CHECK(classifyFrameDeadline(deadline, deadline + interval / 2 + 1, tolerance) == FrameVerdict::Drop);
    RTX_CHECK_EQ(waitDurationFlicks(deadline, deadline - 500), 500LL);
    RTX_CHECK_EQ(waitDurationFlicks(deadline, deadline + 500), 0LL);

    RTX_CHECK_EQ(dropToleranceFlicks(kSecond / 120), kSecond / 240);
    RTX_CHECK_EQ(dropToleranceFlicks(1), 1LL);
    RTX_CHECK_EQ(dropToleranceFlicks(0), 1LL);
}
RTX_TEST(playback_decode_ahead_capacity_policy) {
    const int64_t interval = kSecond / 24;
    RTX_CHECK_EQ(decodeAheadCapacity(interval, interval / 10, 0, 0, 1.0), 4u);
    RTX_CHECK_EQ(decodeAheadCapacity(interval, interval, 0, 0, 1.0), 4u);
    RTX_CHECK_EQ(decodeAheadCapacity(interval, interval * 3, 0, 0, 1.0), 6u);
    RTX_CHECK_EQ(decodeAheadCapacity(interval, interval * 9, 0, 0, 1.0), 6u);
    RTX_CHECK_EQ(decodeAheadCapacity(interval, interval / 10, 0, 0, 2.0), 5u);
    RTX_CHECK_EQ(decodeAheadCapacity(interval, interval * 3, 0, 0, 2.0), 6u);
    RTX_CHECK_EQ(decodeAheadCapacity(interval, interval / 10, 0, 0, 0.25), 4u);

    const uint64_t budget = 8ULL * 1024 * 1024 * 1024;
    RTX_CHECK_EQ(decodeAheadCapacity(interval, interval, budget * 81 / 100, budget, 1.0), 4u);
    RTX_CHECK_EQ(decodeAheadCapacity(interval, interval, budget * 82 / 100, budget, 1.0), 4u);
    RTX_CHECK_EQ(decodeAheadCapacity(interval, interval, budget * 83 / 100, budget, 1.0), 2u);
    RTX_CHECK_EQ(decodeAheadCapacity(interval, interval, budget, budget, 2.0), 2u);
    RTX_CHECK_EQ(decodeAheadCapacity(interval, interval, budget, 0, 1.0), 4u);
}
RTX_TEST(playback_decode_ahead_queue_bounded_deque) {
    std::deque<DecodeAheadSlot> queue;
    const uint32_t capacity = clampDecodeAheadCapacity(4, kDecodeAheadMaxSlots);
    RTX_CHECK_EQ(capacity, 4u);
    RtxFrameDesc frame{};
    for (uint32_t i = 0; i < 4; ++i) {
        frame.pts = static_cast<int32_t>(i * 3750);
        frame.slot_index = i;
        RTX_CHECK(pushDecodeSlot(queue, capacity, frame, static_cast<int64_t>(i) * (kSecond / 24)));
    }
    RTX_CHECK_EQ(queue.size(), capacity);
    frame.pts = 99999;
    RTX_CHECK(!pushDecodeSlot(queue, capacity, frame, 0));  // 有界：满队列拒绝而不是扩容
    RTX_CHECK_EQ(queue.size(), 4u);

    for (uint32_t i = 0; i < 4; ++i) {
        RTX_REQUIRE(!queue.empty());
        auto& [q_desc, q_pts, q_gen] = queue.front();
        RTX_CHECK_EQ(q_desc.pts, static_cast<int64_t>(i) * 3750);
        RTX_CHECK_EQ(q_desc.slot_index, static_cast<uint64_t>(i));
        RTX_CHECK_EQ(q_pts, static_cast<int64_t>(i) * (kSecond / 24));
        RTX_CHECK_EQ(q_gen, 0ULL);
        q_gen = 100 + i;
        queue.pop_front();
    }
    RTX_CHECK(queue.empty());

    const uint32_t wide = clampDecodeAheadCapacity(6, kDecodeAheadMaxSlots);
    RTX_CHECK_EQ(wide, 6u);
    for (int round = 0; round < 5; ++round) {
        for (uint32_t i = 0; i < wide; ++i) {
            frame.pts = static_cast<int32_t>(round * 100 + i);
            RTX_CHECK(pushDecodeSlot(queue, wide, frame, frame.pts));
        }
        RTX_CHECK_EQ(queue.size(), wide);
        for (uint32_t i = 0; i < wide; ++i) {
            { const auto& [q_desc, q_pts, q_gen] = queue.front(); (void)q_pts;(void)q_gen; RTX_CHECK_EQ(q_desc.pts, static_cast<int64_t>(round) * 100 + i); }
            queue.pop_front();
        }
    }
    RTX_CHECK_EQ(clampDecodeAheadCapacity(0, 6), 1u);
    RTX_CHECK_EQ(clampDecodeAheadCapacity(99, 6), 6u);
    RTX_CHECK_EQ(clampDecodeAheadCapacity(0, 0), 1u);

    RTX_CHECK(pushDecodeSlot(queue, wide, frame, 1));
    RTX_CHECK(pushDecodeSlot(queue, wide, frame, 2));
    queue.clear();
    RTX_CHECK(queue.empty());
    RTX_CHECK(pushDecodeSlot(queue, wide, frame, 3));
}
RTX_TEST(playback_gop_seek_preroll) {
    FakeDemuxer demuxer;
    GopSeekScheduler scheduler = makeGopSeekScheduler(demuxer.asSeekFn(), Rational{1, 90000});
    RTX_CHECK(!gopSeekActive(scheduler));
    RTX_CHECK(gopSeekAccepting(scheduler, 0));
    RTX_CHECK(gopSeekAccepting(scheduler, -kSecond));

    const int64_t target = kSecond + kSecond / 2;
    RTX_REQUIRE(gopSeekTo(scheduler, target).has_value());
    RTX_CHECK(gopSeekActive(scheduler));
    RTX_CHECK_EQ(gopSeekTarget(scheduler), target);
    RTX_CHECK_EQ(demuxer.seek_calls, 1);
    RTX_CHECK_EQ(demuxer.last_seek_pts, 135000LL);

    FakeDemuxer ms_demuxer;
    GopSeekScheduler ms_scheduler = makeGopSeekScheduler(ms_demuxer.asSeekFn(), Rational{1, 1000});
    RTX_REQUIRE(gopSeekTo(ms_scheduler, target).has_value());
    RTX_CHECK_EQ(ms_demuxer.last_seek_pts, 1500LL);

    FakeDemuxer odd_demuxer;
    GopSeekScheduler odd_scheduler = makeGopSeekScheduler(odd_demuxer.asSeekFn(), Rational{1, 15360});
    RTX_REQUIRE(gopSeekTo(odd_scheduler, static_cast<int64_t>((kFlicksPerSecond + 15359ULL) / 15360ULL)).has_value());
    RTX_CHECK_EQ(odd_demuxer.last_seek_pts, 1LL);

    RTX_CHECK(!gopSeekAccepting(scheduler, 0));
    RTX_CHECK(!gopSeekAccepting(scheduler, kSecond / 2));
    RTX_CHECK(!gopSeekAccepting(scheduler, target - 1));
    RTX_CHECK_EQ(gopSeekPrerollSkipped(scheduler), 3ULL);
    RTX_CHECK(gopSeekAccepting(scheduler, target));
    RTX_CHECK(!gopSeekActive(scheduler));
    RTX_CHECK(gopSeekAccepting(scheduler, target + kSecond));
    RTX_CHECK_EQ(gopSeekPrerollSkipped(scheduler), 3ULL);

    RTX_REQUIRE(gopSeekTo(scheduler, 2 * kSecond).has_value());
    RTX_CHECK(!gopSeekAccepting(scheduler, 0));
    gopSeekCancel(scheduler);
    RTX_CHECK(!gopSeekActive(scheduler));
    RTX_CHECK_EQ(gopSeekPrerollSkipped(scheduler), 0ULL);
    RTX_CHECK(gopSeekAccepting(scheduler, 0));

    demuxer.fail_seek = true;
    const Result<void> failed = gopSeekTo(scheduler, 3 * kSecond);
    RTX_CHECK(!failed.has_value());
    RTX_CHECK_EQ(failed.error().code, ErrorCode::Io);
    RTX_CHECK(!gopSeekActive(scheduler));

    GopSeekScheduler broken = makeGopSeekScheduler(demuxer.asSeekFn(), Rational{1, 0});
    RTX_CHECK(!gopSeekTo(broken, kSecond).has_value());
}
RTX_TEST(playback_telemetry_seqlock) {
    AtomicTelemetryBus bus{};
    TelemetrySnapshot zero = telemetryRead(bus);
    RTX_CHECK_EQ(telemetryU64(zero, TelemetrySlot::FramesDecoded), 0ULL);
    RTX_CHECK_EQ(telemetryI64(zero, TelemetrySlot::PlayheadFlicks), 0LL);
    RTX_CHECK_EQ(telemetryU32(zero, TelemetrySlot::CurrentFpsX100), 0u);

    TelemetrySnapshot value{};
    telemetrySetU64(value, TelemetrySlot::FramesDecoded, 1234);
    telemetrySetU64(value, TelemetrySlot::FramesRendered, 1200);
    telemetrySetU64(value, TelemetrySlot::FramesDropped, 34);
    telemetrySetU32(value, TelemetrySlot::CurrentFpsX100, 5994);
    telemetrySetI64(value, TelemetrySlot::AvDriftUs, -12345);
    telemetrySetU64(value, TelemetrySlot::VramUsedBytes, 6ULL * 1024 * 1024 * 1024);
    telemetrySetU64(value, TelemetrySlot::VramBudgetBytes, 8ULL * 1024 * 1024 * 1024);
    telemetrySetI64(value, TelemetrySlot::PlayheadFlicks, 42 * kSecond);
    telemetrySetU64(value, TelemetrySlot::SequenceEpoch, 7);
    telemetryPublish(bus, value);
    const TelemetrySnapshot back = telemetryRead(bus);
    RTX_CHECK_EQ(telemetryU64(back, TelemetrySlot::FramesDecoded), 1234ULL);
    RTX_CHECK_EQ(telemetryU64(back, TelemetrySlot::FramesRendered), 1200ULL);
    RTX_CHECK_EQ(telemetryU64(back, TelemetrySlot::FramesDropped), 34ULL);
    RTX_CHECK_EQ(telemetryU32(back, TelemetrySlot::CurrentFpsX100), 5994u);
    RTX_CHECK_EQ(telemetryI64(back, TelemetrySlot::AvDriftUs), -12345LL);
    RTX_CHECK_EQ(telemetryU64(back, TelemetrySlot::VramUsedBytes), 6ULL * 1024 * 1024 * 1024);
    RTX_CHECK_EQ(telemetryU64(back, TelemetrySlot::VramBudgetBytes), 8ULL * 1024 * 1024 * 1024);
    RTX_CHECK_EQ(telemetryI64(back, TelemetrySlot::PlayheadFlicks), 42 * kSecond);
    RTX_CHECK_EQ(telemetryU64(back, TelemetrySlot::SequenceEpoch), 7ULL);

    TelemetrySnapshot consistent{};
    telemetryPublish(bus, consistent);
    std::atomic<bool> stop{false};
    std::atomic<uint64_t> reads{0};
    std::atomic<uint64_t> torn{0};
    std::thread writer([&] {
        for (uint64_t i = 1; !stop.load(std::memory_order_relaxed); ++i) {
            TelemetrySnapshot snapshot{};
            telemetrySetU64(snapshot, TelemetrySlot::FramesDecoded, i);
            telemetrySetU64(snapshot, TelemetrySlot::FramesRendered, i);
            telemetrySetU64(snapshot, TelemetrySlot::FramesDropped, 0);
            telemetrySetI64(snapshot, TelemetrySlot::PlayheadFlicks, static_cast<int64_t>(i) * (kSecond / 24));
            telemetrySetU64(snapshot, TelemetrySlot::SequenceEpoch, i);
            telemetrySetU32(snapshot, TelemetrySlot::CurrentFpsX100, 2400);
            telemetryPublish(bus, snapshot);
        }
    });
    std::thread reader([&] {
        while (!stop.load(std::memory_order_relaxed)) {
            const TelemetrySnapshot snapshot = telemetryRead(bus);
            const uint64_t dec = telemetryU64(snapshot, TelemetrySlot::FramesDecoded);
            if (dec != telemetryU64(snapshot, TelemetrySlot::FramesRendered) ||
                dec != telemetryU64(snapshot, TelemetrySlot::SequenceEpoch) ||
                telemetryI64(snapshot, TelemetrySlot::PlayheadFlicks) !=
                    static_cast<int64_t>(dec) * (kSecond / 24)) {
                torn.fetch_add(1, std::memory_order_relaxed);
            }
            reads.fetch_add(1, std::memory_order_relaxed);
        }
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    stop.store(true, std::memory_order_relaxed);
    writer.join();
    reader.join();
    RTX_CHECK(reads.load() > 100);
    RTX_CHECK_EQ(torn.load(), 0ULL);
    RTX_CHECK(telemetryU64(telemetryRead(bus), TelemetrySlot::FramesDecoded) > 0ULL);
}
RTX_TEST(playback_wasapi_shared_mode_master_clock) {
#if defined(_WIN32)
    Result<std::unique_ptr<WasapiAudioOutput>> created = WasapiAudioOutput::create();
    if (!created.has_value()) {
        std::printf("[SKIP-AUDIO] playback_wasapi_shared_mode_master_clock: no render endpoint (%s)\n",
                    created.error().message.c_str());
        return;
    }
    std::unique_ptr<WasapiAudioOutput> audio = std::move(created.value());
    const uint32_t rate = audio->sampleRate();
    const uint32_t channels = audio->channels();
    RTX_CHECK(rate >= 8000 && rate <= 384000);
    RTX_CHECK(channels >= 1 && channels <= 8);
    RTX_CHECK_EQ(audio->reconnects(), 0ULL);
    RTX_CHECK_EQ(audio->writtenFrames(), 0ULL);

    audio->anchor(3 * kSecond);
    RTX_CHECK_EQ(audio->masterFlicks(), 3 * kSecond);
    RTX_CHECK(!audio->running());

    const size_t frames = static_cast<size_t>(rate) / 10;
    // Phase 11.3-R: 写入 0.0f 全静音缓冲（-inf dB），完全消除单测执行时的 440 Hz 扬声器嘟嘟声
    std::vector<float> interleaved(frames * channels, 0.0f);
    audio->resume();
    RTX_CHECK(audio->running());
    uint32_t consumed = 0;
    RTX_REQUIRE(waitFor([&] {
        Result<uint32_t> written = audio->write(interleaved);
        if (!written.has_value()) {
            return false;
        }
        consumed += written.value();
        return consumed >= frames;
    }, 3000));
    RTX_CHECK(waitFor([&] {
        return audio->writtenFrames() >= static_cast<uint64_t>(consumed);
    }, 3000));

    RTX_CHECK(waitFor([&] { return audio->masterFlicks() > 3 * kSecond; }, 3000));
    const auto window_start = std::chrono::steady_clock::now();
    const int64_t first = audio->masterFlicks();
    while (std::chrono::steady_clock::now() - window_start < std::chrono::milliseconds(30)) {
        Result<uint32_t> more = audio->write(interleaved);
        if (more.has_value()) {
            consumed += more.value();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    const int64_t second = audio->masterFlicks();
    RTX_CHECK(second > first);
    const int64_t delta = second - first;
    RTX_CHECK(delta > kSecond * 15 / 1000);
    RTX_CHECK(delta < kSecond * 80 / 1000);

    audio->pause();
    RTX_CHECK(!audio->running());
    const int64_t held = audio->masterFlicks();
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
    RTX_CHECK_EQ(audio->masterFlicks(), held);
    audio->resume();
    RTX_CHECK(waitFor([&] {
        Result<uint32_t> more = audio->write(interleaved);
        if (more.has_value()) {
            consumed += more.value();
        }
        return audio->masterFlicks() > held;
    }, 3000));

    audio->flush();
    RTX_CHECK_EQ(audio->reconnects(), 0ULL);

    if (channels > 1) {
        std::vector<float> ragged(channels + 1, 0.0f);
        RTX_CHECK(!audio->write(ragged).has_value());
    }
#else
    std::printf("[SKIP-AUDIO] playback_wasapi_shared_mode_master_clock: WASAPI is Windows-only\n");
#endif
}
RTX_TEST(playback_session_native_decode_h264_hevc_av1) {
    const VideoCodec codecs[] = {VideoCodec::H264, VideoCodec::HEVC, VideoCodec::AV1};
    const char* names[] = {"h264", "hevc", "av1"};
    bool any_ran = false;

    for (size_t index = 0; index < 3; ++index) {
        auto bytes = readFixture(fixtureName(codecs[index]));
        RTX_REQUIRE(bytes.has_value());
        std::unique_ptr<FfmpegDemuxer> demuxer = makeMemoryDemuxer(std::move(*bytes));
        RTX_REQUIRE(demuxer != nullptr);
        Result<MediaInfo> opened = demuxer->open("fixture.mkv");
        RTX_REQUIRE(opened.has_value());
        RTX_CHECK(opened.value().codec == codecs[index]);
        RTX_CHECK_EQ(opened.value().width, 320);
        RTX_CHECK_EQ(opened.value().height, 240);
        RTX_CHECK_EQ(opened.value().frame_rate.num, 25);
        RTX_CHECK(opened.value().primary_audio_index >= 0);

        CountingSink sink;
        PlaybackOptions options{Rational{25, 1}, 1.0, 5u, 0ull, 0, true, /*null_sink=*/true, /*audio_gain=*/0.0f};
        Result<std::unique_ptr<MediaPlaybackSession>> created =
            MediaPlaybackSession::create(std::move(demuxer), opened.value(), sink.asSink(), options);
        if (!created.has_value()) {
            std::printf("[SKIP-GPU] %s: NVDEC session unavailable (%s)\n", names[index],
                        created.error().message.c_str());
            continue;
        }
        any_ran = true;
        std::unique_ptr<MediaPlaybackSession> session = std::move(created.value());
        RTX_CHECK(session->state() == PlaybackState::Idle);
        RTX_CHECK_EQ(session->playheadFlicks(), 0LL);

        RTX_REQUIRE(session->play().has_value());
        RTX_CHECK(waitFor([&] { return session->state() == PlaybackState::Ended; }, 15000));
        const TelemetrySnapshot done = session->telemetry();
        const uint64_t dec = telemetryU64(done, TelemetrySlot::FramesDecoded);
        const uint64_t ren = telemetryU64(done, TelemetrySlot::FramesRendered);
        const uint64_t drp = telemetryU64(done, TelemetrySlot::FramesDropped);
        const int64_t ph = telemetryI64(done, TelemetrySlot::PlayheadFlicks);
        std::printf("[PLAYBACK] %s decoded=%llu rendered=%llu dropped=%llu fps_x100=%u "
                    "playhead_flicks=%lld audio=%d\n",
                    names[index], static_cast<unsigned long long>(dec),
                    static_cast<unsigned long long>(ren),
                    static_cast<unsigned long long>(drp), telemetryU32(done, TelemetrySlot::CurrentFpsX100),
                    static_cast<long long>(ph),
                    session->audioActive() ? 1 : 0);
        RTX_CHECK(dec >= 25);
        RTX_CHECK(ren > 0);
        RTX_CHECK(ren + drp <= dec);
        RTX_CHECK(ph > 0);
        {
            std::lock_guard<std::mutex> lock(sink.mutex);
            RTX_CHECK(sink.presented == ren);
            RTX_CHECK(sink.last_fence > 0);
            RTX_CHECK(std::is_sorted(sink.deadlines.begin(), sink.deadlines.end()));
        }
        const uint64_t epoch_before_replay = telemetryU64(session->telemetry(), TelemetrySlot::SequenceEpoch);
        RTX_REQUIRE(session->play().has_value());
        RTX_CHECK(waitFor([&] {
            return telemetryU64(session->telemetry(), TelemetrySlot::SequenceEpoch) > epoch_before_replay &&
                   session->playheadFlicks() > 0;
        }, 5000));
        session->pause();
        RTX_CHECK(session->state() == PlaybackState::Paused);
        int64_t settled = session->playheadFlicks();
        RTX_CHECK(waitFor([&] {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            const int64_t current = session->playheadFlicks();
            const bool stable = current == settled;
            settled = current;
            return stable;
        }, 2000));
        const int64_t held = settled;
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
        RTX_CHECK_EQ(session->playheadFlicks(), held);

        const int64_t target = kSecond * 3 / 5;
        const uint64_t decoded_before_seek = telemetryU64(session->telemetry(), TelemetrySlot::FramesDecoded);
        RTX_REQUIRE(session->seek(target).has_value());
        RTX_CHECK(session->playheadFlicks() >= target);
        RTX_CHECK(session->playheadFlicks() < target + kSecond / 25 + 1);
        RTX_CHECK(telemetryU64(session->telemetry(), TelemetrySlot::FramesDecoded) >= decoded_before_seek + 4);
        RTX_CHECK_EQ(session->playheadFlicks(), target);

        const int64_t frame_interval = kSecond / 25;
        const int64_t before_step = session->playheadFlicks();
        RTX_REQUIRE(session->stepFrame(1).has_value());
        RTX_CHECK_EQ(session->playheadFlicks(), before_step + frame_interval);
        RTX_REQUIRE(session->stepFrame(-1).has_value());
        RTX_CHECK_EQ(session->playheadFlicks(), before_step);

        const uint64_t epoch_before = telemetryU64(session->telemetry(), TelemetrySlot::SequenceEpoch);
        session->setSpeed(2.0);
        RTX_REQUIRE(session->play().has_value());
        RTX_CHECK(waitFor([&] { return session->playheadFlicks() > before_step; }, 5000));
        session->setSpeed(0.25);
        RTX_REQUIRE(session->seek(kSecond / 2).has_value());
        RTX_CHECK(telemetryU64(session->telemetry(), TelemetrySlot::SequenceEpoch) > epoch_before);
        session->pause();

        RTX_REQUIRE(session->seek(-5 * kSecond).has_value());
        RTX_CHECK(session->playheadFlicks() >= 0);
        RTX_REQUIRE(session->seek(100 * kSecond).has_value());
    }
    auto corrupted = readFixture(fixtureName(VideoCodec::H264));
    RTX_REQUIRE(corrupted.has_value());
    const size_t mid = corrupted->size() / 2;
    (*corrupted)[mid] = static_cast<uint8_t>((*corrupted)[mid] ^ 0xFF);
    (*corrupted)[mid + 7] = static_cast<uint8_t>((*corrupted)[mid + 7] ^ 0x5A);
    std::unique_ptr<FfmpegDemuxer> corrupt_demuxer = makeMemoryDemuxer(std::move(*corrupted));
    RTX_REQUIRE(corrupt_demuxer != nullptr);
    Result<MediaInfo> corrupt_opened = corrupt_demuxer->open("corrupt.mkv");
    if (!corrupt_opened.has_value()) {
        std::printf("[PLAYBACK] corrupt-h264 rejected by demuxer: %s\n",
                    corrupt_opened.error().toString().c_str());
        return;
    }
    PlaybackOptions corrupt_options{Rational{24, 1}, 1.0, 5u, 0ull, 0, false, /*null_sink=*/true, /*audio_gain=*/0.0f};
    CountingSink corrupt_sink;
    Result<std::unique_ptr<MediaPlaybackSession>> corrupt_session = MediaPlaybackSession::create(
        std::move(corrupt_demuxer), corrupt_opened.value(), corrupt_sink.asSink(), corrupt_options);
    if (!corrupt_session.has_value()) {
        std::printf("[SKIP-GPU] corruption injection: NVDEC session unavailable (%s)\n",
                    corrupt_session.error().message.c_str());
        return;
    }
    RTX_REQUIRE(corrupt_session.value()->play().has_value());
    RTX_CHECK(waitFor([&] { return corrupt_session.value()->state() == PlaybackState::Ended; }, 15000));
    std::printf("[PLAYBACK] corrupt-h264 decoded=%llu rendered=%llu ended=%d\n",
                static_cast<unsigned long long>(telemetryU64(corrupt_session.value()->telemetry(), TelemetrySlot::FramesDecoded)),
                static_cast<unsigned long long>(telemetryU64(corrupt_session.value()->telemetry(), TelemetrySlot::FramesRendered)),
                corrupt_session.value()->state() == PlaybackState::Ended ? 1 : 0);

    if (!any_ran) {
        std::printf("[SKIP-GPU] playback_session_native_decode_h264_hevc_av1: no NVDEC host\n");
    }
}
RTX_TEST(playback_session_fence_gated_slot_retirement) {
    auto bytes = readFixture(fixtureName(VideoCodec::H264));
    RTX_REQUIRE(bytes.has_value());
    std::unique_ptr<FfmpegDemuxer> demuxer = makeMemoryDemuxer(std::move(*bytes));
    RTX_REQUIRE(demuxer != nullptr);
    Result<MediaInfo> opened = demuxer->open("fixture.mkv");
    RTX_REQUIRE(opened.has_value());

    GatedFenceSink sink;
    PlaybackOptions options{Rational{24, 1}, 1.0, 4u, 0ull, 0, false, /*null_sink=*/true, /*audio_gain=*/0.0f};
    Result<std::unique_ptr<MediaPlaybackSession>> created =
        MediaPlaybackSession::create(std::move(demuxer), opened.value(), sink.asSink(), options);
    if (!created.has_value()) {
        std::printf("[SKIP-GPU] fence retirement: NVDEC session unavailable (%s)\n",
                    created.error().message.c_str());
        return;
    }
    std::unique_ptr<MediaPlaybackSession> session = std::move(created.value());
    RTX_CHECK(!session->audioActive());
    RTX_REQUIRE(session->play().has_value());

    RTX_CHECK(waitFor([&] {
        std::lock_guard<std::mutex> lock(sink.mutex);
        return sink.presented >= 1;
    }, 10000));
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    {
        std::lock_guard<std::mutex> lock(sink.mutex);
        RTX_CHECK_EQ(sink.presented, 1ULL);
        RTX_CHECK_EQ(sink.issued, 1ULL);
    }
    RTX_CHECK_EQ(telemetryU64(session->telemetry(), TelemetrySlot::FramesRendered), 1ULL);

    RTX_CHECK(waitFor([&] {
        sink.release(sink.issued);
        std::lock_guard<std::mutex> lock(sink.mutex);
        return sink.presented >= 4;
    }, 10000));
    {
        std::lock_guard<std::mutex> lock(sink.mutex);
        RTX_CHECK(sink.issued >= 4);
    }
    RTX_CHECK(waitFor([&] {
        sink.release(sink.issued);
        return session->state() == PlaybackState::Ended;
    }, 20000));
    RTX_CHECK(telemetryU64(session->telemetry(), TelemetrySlot::FramesRendered) >= 4);
}

RTX_TEST(playback_session_2d_preview_ring_integrity) {
    NvidiaShim shim;
    std::string shim_err;
    RtxDiagnostics diag{};
    if (!shim.open(&shim_err) || shim.probe(&diag) != RTX_OK) {
        std::printf("[SKIP-GPU] 2d_preview_ring_integrity: rtx_shim unavailable\n");
        return;
    }
    NgxRemasterBridge loader;
    std::string bridge_err;
    if (!loader.open(&bridge_err) || !loader.hasPlaybackPackApi() || !loader.hasBorrowPreviewApi()) {
        std::printf("[SKIP-GPU] 2d_preview_ring_integrity: rtx_ngx_bridge unavailable\n");
        return;
    }
    // Start with small 64x64 ring to verify Doc 47 Invariant 1 dynamic reallocation on 1920x1080 pack.
    RtxNgxBridgeConfig cfg{};
    std::memcpy(cfg.luid, diag.device.luid, sizeof(cfg.luid));
    cfg.luid_node_mask = diag.device.luid_node_mask;
    cfg.input_width = 64;
    cfg.input_height = 64;
    cfg.output_width = 64;
    cfg.output_height = 64;
    cfg.reserved[0] = 1u;
    RtxNgxBridge* bridge = nullptr;
    if (loader.create(&cfg, &bridge) != RTX_OK || bridge == nullptr) {
        std::printf("[SKIP-GPU] 2d_preview_ring_integrity: headless bridge create unavailable\n");
        return;
    }
    constexpr uint32_t kW = 1920u;
    constexpr uint32_t kH = 1080u;
    std::vector<uint8_t> y_host(static_cast<size_t>(kW) * kH, 180u);
    std::vector<uint8_t> uv_host(static_cast<size_t>(kW) * (kH / 2u), 128u);
    uint64_t y_dev = 0, uv_dev = 0;
    const RtxResult up = loader.uploadFrame(bridge, y_host.data(), uv_host.data(), kW, kW,
                                            static_cast<int32_t>(kW), static_cast<int32_t>(kH),
                                            &y_dev, &uv_dev);
    if (up != RTX_OK) {
        loader.releaseBridge(bridge);
        std::printf("[SKIP-GPU] 2d_preview_ring_integrity: uploadFrame unavailable\n");
        return;
    }
    const int32_t packed = loader.playbackPack(bridge, y_dev, uv_dev, kW, kW, kW, kH,
                                               RTX_PIXEL_NV12, 0, 0, 0u);
    RTX_CHECK_EQ(packed, static_cast<int32_t>(RTX_OK));
    void* d3d_dev = nullptr;
    void* d3d_res = nullptr;
    uint32_t out_pitch = 0u;
    RTX_CHECK_EQ(loader.borrowPreview(bridge, 0u, &d3d_dev, &d3d_res, &out_pitch),
                 static_cast<int32_t>(RTX_OK));
    RTX_CHECK(d3d_dev != nullptr && d3d_res != nullptr);
    RTX_CHECK(out_pitch >= kW * 4u);
    RTX_CHECK_EQ(out_pitch % 256u, 0u);
    std::vector<uint8_t> readback(static_cast<size_t>(out_pitch) * kH, 0u);
    RTX_CHECK_EQ(loader.downloadPreview(bridge, readback.data(), readback.size()), RTX_OK);
    const auto pixelAt = [&](uint32_t x, uint32_t y) -> uint32_t {
        uint32_t word = 0u;
        std::memcpy(&word, readback.data() + static_cast<size_t>(y) * out_pitch + static_cast<size_t>(x) * 4u, 4u);
        return word;
    };
    RTX_CHECK(pixelAt(0u, 0u) != 0u);
    RTX_CHECK(pixelAt(kW / 2u, kH / 2u) != 0u);
    RTX_CHECK(pixelAt(kW - 1u, kH - 1u) != 0u);
    loader.releaseBridge(bridge);
}