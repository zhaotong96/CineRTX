// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors

#include "test_framework.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
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
class FakeAudioClock final : public IAudioMasterClock {
public:
    int64_t value = 0;
    bool active = false;
    [[nodiscard]] int64_t masterFlicks() const noexcept override { return value; }
    [[nodiscard]] bool running() const noexcept override { return active; }
};

class FakeDemuxer final : public IDemuxer {
public:
    int64_t last_seek_pts = -1;
    int seek_calls = 0;
    bool fail_seek = false;

    Result<MediaInfo> open(const std::filesystem::path&) override { return MediaInfo{}; }
    Result<Packet> readPacket() override { return err(ErrorCode::NotFound, "eof"); }
    Result<void> seekToKeyframeBefore(int64_t video_stream_pts) override {
        ++seek_calls;
        if (fail_seek) {
            return err(ErrorCode::Io, "fake demuxer seek failure");
        }
        last_seek_pts = video_stream_pts;
        return {};
    }
};

class CountingSink final : public IVideoFrameSink {
public:
    Result<void> present(const RtxFrameDesc& frame, int64_t deadline_flicks,
                         uint64_t retire_fence) override {
        std::lock_guard<std::mutex> lock(mutex);
        ++presented;
        last_deadline = deadline_flicks;
        last_pts = frame.pts;
        last_fence = retire_fence;
        deadlines.push_back(deadline_flicks);
        return {};
    }
    std::mutex mutex;
    uint64_t presented = 0;
    int64_t last_deadline = 0;
    int64_t last_pts = 0;
    uint64_t last_fence = 0;
    std::vector<int64_t> deadlines;
};

class GatedFenceSink final : public IVideoFrameSink {
public:
    Result<void> present(const RtxFrameDesc&, int64_t, uint64_t retire_fence) override {
        std::lock_guard<std::mutex> lock(mutex);
        issued = retire_fence;
        ++presented;
        return {};
    }
    [[nodiscard]] uint64_t completedFence() const noexcept override {
        return released.load(std::memory_order_acquire);
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
    PlaybackClockState clock{nullptr, std::nullopt, Flicks{0}, 1.0};
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
    PlaybackClockState clock{&audio, std::nullopt, Flicks{0}, 1.0};
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
        RTX_CHECK_EQ(std::get<0>(queue.front()).pts, static_cast<int64_t>(i) * 3750);
        RTX_CHECK_EQ(std::get<0>(queue.front()).slot_index, static_cast<uint64_t>(i));
        RTX_CHECK_EQ(std::get<1>(queue.front()), static_cast<int64_t>(i) * (kSecond / 24));
        RTX_CHECK_EQ(std::get<2>(queue.front()), 0ULL);
        std::get<2>(queue.front()) = 100 + i;
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
            RTX_CHECK_EQ(std::get<0>(queue.front()).pts, static_cast<int64_t>(round) * 100 + i);
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
    GopSeekScheduler scheduler(demuxer, Rational{1, 90000});
    RTX_CHECK(!scheduler.active());
    RTX_CHECK(scheduler.accepting(0));
    RTX_CHECK(scheduler.accepting(-kSecond));

    const int64_t target = kSecond + kSecond / 2;
    RTX_REQUIRE(scheduler.seekTo(target).has_value());
    RTX_CHECK(scheduler.active());
    RTX_CHECK_EQ(scheduler.target(), target);
    RTX_CHECK_EQ(demuxer.seek_calls, 1);
    RTX_CHECK_EQ(demuxer.last_seek_pts, 135000LL);

    FakeDemuxer ms_demuxer;
    GopSeekScheduler ms_scheduler(ms_demuxer, Rational{1, 1000});
    RTX_REQUIRE(ms_scheduler.seekTo(target).has_value());
    RTX_CHECK_EQ(ms_demuxer.last_seek_pts, 1500LL);

    FakeDemuxer odd_demuxer;
    GopSeekScheduler odd_scheduler(odd_demuxer, Rational{1, 15360});
    RTX_REQUIRE(odd_scheduler.seekTo(45938).has_value());
    RTX_CHECK_EQ(odd_demuxer.last_seek_pts, 1LL);

    RTX_CHECK(!scheduler.accepting(0));
    RTX_CHECK(!scheduler.accepting(kSecond / 2));
    RTX_CHECK(!scheduler.accepting(target - 1));
    RTX_CHECK_EQ(scheduler.prerollSkipped(), 3ULL);
    RTX_CHECK(scheduler.accepting(target));
    RTX_CHECK(!scheduler.active());
    RTX_CHECK(scheduler.accepting(target + kSecond));
    RTX_CHECK_EQ(scheduler.prerollSkipped(), 3ULL);

    RTX_REQUIRE(scheduler.seekTo(2 * kSecond).has_value());
    RTX_CHECK(!scheduler.accepting(0));
    scheduler.cancel();
    RTX_CHECK(!scheduler.active());
    RTX_CHECK_EQ(scheduler.prerollSkipped(), 0ULL);
    RTX_CHECK(scheduler.accepting(0));

    demuxer.fail_seek = true;
    const Result<void> failed = scheduler.seekTo(3 * kSecond);
    RTX_CHECK(!failed.has_value());
    RTX_CHECK_EQ(failed.error().code, ErrorCode::Io);
    RTX_CHECK(!scheduler.active());

    GopSeekScheduler broken(demuxer, Rational{1, 0});
    RTX_CHECK(!broken.seekTo(kSecond).has_value());
}
RTX_TEST(playback_telemetry_seqlock) {
    AtomicTelemetryBus bus;
    TelemetrySnapshot zero = bus.read();
    RTX_CHECK_EQ(zero.frames_decoded, 0ULL);
    RTX_CHECK_EQ(zero.playhead_flicks, 0LL);
    RTX_CHECK_EQ(zero.current_fps_x100, 0u);

    TelemetrySnapshot value{};
    value.frames_decoded = 1234;
    value.frames_rendered = 1200;
    value.frames_dropped = 34;
    value.current_fps_x100 = 5994;
    value.av_drift_us = -12345;
    value.vram_used_bytes = 6ULL * 1024 * 1024 * 1024;
    value.vram_budget_bytes = 8ULL * 1024 * 1024 * 1024;
    value.playhead_flicks = 42 * kSecond;
    value.sequence_epoch = 7;
    bus.publish(value);
    const TelemetrySnapshot back = bus.read();
    RTX_CHECK_EQ(back.frames_decoded, 1234ULL);
    RTX_CHECK_EQ(back.frames_rendered, 1200ULL);
    RTX_CHECK_EQ(back.frames_dropped, 34ULL);
    RTX_CHECK_EQ(back.current_fps_x100, 5994u);
    RTX_CHECK_EQ(back.av_drift_us, -12345LL);
    RTX_CHECK_EQ(back.vram_used_bytes, 6ULL * 1024 * 1024 * 1024);
    RTX_CHECK_EQ(back.vram_budget_bytes, 8ULL * 1024 * 1024 * 1024);
    RTX_CHECK_EQ(back.playhead_flicks, 42 * kSecond);
    RTX_CHECK_EQ(back.sequence_epoch, 7ULL);

    TelemetrySnapshot consistent{};
    bus.publish(consistent);
    std::atomic<bool> stop{false};
    std::atomic<uint64_t> reads{0};
    std::atomic<uint64_t> torn{0};
    std::thread writer([&] {
        for (uint64_t i = 1; !stop.load(std::memory_order_relaxed); ++i) {
            TelemetrySnapshot snapshot{};
            snapshot.frames_decoded = i;
            snapshot.frames_rendered = i;
            snapshot.frames_dropped = 0;
            snapshot.playhead_flicks = static_cast<int64_t>(i) * (kSecond / 24);
            snapshot.sequence_epoch = i;
            snapshot.current_fps_x100 = 2400;
            bus.publish(snapshot);
        }
    });
    std::thread reader([&] {
        while (!stop.load(std::memory_order_relaxed)) {
            const TelemetrySnapshot snapshot = bus.read();
            if (snapshot.frames_decoded != snapshot.frames_rendered ||
                snapshot.frames_decoded != snapshot.sequence_epoch ||
                snapshot.playhead_flicks !=
                    static_cast<int64_t>(snapshot.frames_decoded) * (kSecond / 24)) {
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
    RTX_CHECK(bus.read().frames_decoded > 0ULL);
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
        std::unique_ptr<IDemuxer> demuxer = makeMemoryDemuxer(std::move(*bytes));
        RTX_REQUIRE(demuxer != nullptr);
        Result<MediaInfo> opened = demuxer->open("fixture.mkv");
        RTX_REQUIRE(opened.has_value());
        RTX_CHECK(opened.value().codec == codecs[index]);
        RTX_CHECK_EQ(opened.value().width, 320);
        RTX_CHECK_EQ(opened.value().height, 240);
        RTX_CHECK_EQ(opened.value().frame_rate.num, 25);
        RTX_CHECK(opened.value().primary_audio_index >= 0);

        CountingSink sink;
        PlaybackOptions options{};
        options.frame_rate = Rational{25, 1};
        options.decode_ahead = 5;
        options.enable_audio = true;
        Result<std::unique_ptr<MediaPlaybackSession>> created =
            MediaPlaybackSession::create(std::move(demuxer), opened.value(), &sink, options);
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
        std::printf("[PLAYBACK] %s decoded=%llu rendered=%llu dropped=%llu fps_x100=%u "
                    "playhead_flicks=%lld audio=%d\n",
                    names[index], static_cast<unsigned long long>(done.frames_decoded),
                    static_cast<unsigned long long>(done.frames_rendered),
                    static_cast<unsigned long long>(done.frames_dropped), done.current_fps_x100,
                    static_cast<long long>(done.playhead_flicks),
                    session->audioActive() ? 1 : 0);
        RTX_CHECK(done.frames_decoded >= 25);
        RTX_CHECK(done.frames_rendered > 0);
        RTX_CHECK(done.frames_rendered + done.frames_dropped <= done.frames_decoded);
        RTX_CHECK(done.playhead_flicks > 0);
        {
            std::lock_guard<std::mutex> lock(sink.mutex);
            RTX_CHECK(sink.presented == done.frames_rendered);
            RTX_CHECK(sink.last_fence > 0);
            RTX_CHECK(std::is_sorted(sink.deadlines.begin(), sink.deadlines.end()));
        }
        const uint64_t epoch_before_replay = session->telemetry().sequence_epoch;
        RTX_REQUIRE(session->play().has_value());
        RTX_CHECK(waitFor([&] {
            return session->telemetry().sequence_epoch > epoch_before_replay &&
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
        const uint64_t decoded_before_seek = session->telemetry().frames_decoded;
        RTX_REQUIRE(session->seek(target).has_value());
        RTX_CHECK(session->playheadFlicks() >= target);
        RTX_CHECK(session->playheadFlicks() < target + kSecond / 25 + 1);
        RTX_CHECK(session->telemetry().frames_decoded >= decoded_before_seek + 4);
        RTX_CHECK_EQ(session->playheadFlicks(), target);

        const int64_t frame_interval = kSecond / 25;
        const int64_t before_step = session->playheadFlicks();
        RTX_REQUIRE(session->stepFrame(1).has_value());
        RTX_CHECK_EQ(session->playheadFlicks(), before_step + frame_interval);
        RTX_REQUIRE(session->stepFrame(-1).has_value());
        RTX_CHECK_EQ(session->playheadFlicks(), before_step);

        const uint64_t epoch_before = session->telemetry().sequence_epoch;
        session->setSpeed(2.0);
        RTX_REQUIRE(session->play().has_value());
        RTX_CHECK(waitFor([&] { return session->playheadFlicks() > before_step; }, 5000));
        session->setSpeed(0.25);
        RTX_REQUIRE(session->seek(kSecond / 2).has_value());
        RTX_CHECK(session->telemetry().sequence_epoch > epoch_before);
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
    std::unique_ptr<IDemuxer> corrupt_demuxer = makeMemoryDemuxer(std::move(*corrupted));
    RTX_REQUIRE(corrupt_demuxer != nullptr);
    Result<MediaInfo> corrupt_opened = corrupt_demuxer->open("corrupt.mkv");
    if (!corrupt_opened.has_value()) {
        std::printf("[PLAYBACK] corrupt-h264 rejected by demuxer: %s\n",
                    corrupt_opened.error().toString().c_str());
        return;
    }
    PlaybackOptions corrupt_options{};
    corrupt_options.enable_audio = false;
    CountingSink corrupt_sink;
    Result<std::unique_ptr<MediaPlaybackSession>> corrupt_session = MediaPlaybackSession::create(
        std::move(corrupt_demuxer), corrupt_opened.value(), &corrupt_sink, corrupt_options);
    if (!corrupt_session.has_value()) {
        std::printf("[SKIP-GPU] corruption injection: NVDEC session unavailable (%s)\n",
                    corrupt_session.error().message.c_str());
        return;
    }
    RTX_REQUIRE(corrupt_session.value()->play().has_value());
    RTX_CHECK(waitFor([&] { return corrupt_session.value()->state() == PlaybackState::Ended; }, 15000));
    std::printf("[PLAYBACK] corrupt-h264 decoded=%llu rendered=%llu ended=%d\n",
                static_cast<unsigned long long>(corrupt_session.value()->telemetry().frames_decoded),
                static_cast<unsigned long long>(corrupt_session.value()->telemetry().frames_rendered),
                corrupt_session.value()->state() == PlaybackState::Ended ? 1 : 0);

    if (!any_ran) {
        std::printf("[SKIP-GPU] playback_session_native_decode_h264_hevc_av1: no NVDEC host\n");
    }
}
RTX_TEST(playback_session_fence_gated_slot_retirement) {
    auto bytes = readFixture(fixtureName(VideoCodec::H264));
    RTX_REQUIRE(bytes.has_value());
    std::unique_ptr<IDemuxer> demuxer = makeMemoryDemuxer(std::move(*bytes));
    RTX_REQUIRE(demuxer != nullptr);
    Result<MediaInfo> opened = demuxer->open("fixture.mkv");
    RTX_REQUIRE(opened.has_value());

    GatedFenceSink sink;
    PlaybackOptions options{};
    options.enable_audio = false;  // 软件主时钟，隔离 GPU 退役变量
    options.decode_ahead = 4;
    Result<std::unique_ptr<MediaPlaybackSession>> created =
        MediaPlaybackSession::create(std::move(demuxer), opened.value(), &sink, options);
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
    RTX_CHECK_EQ(session->telemetry().frames_rendered, 1ULL);

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
    RTX_CHECK(session->telemetry().frames_rendered >= 4);
}