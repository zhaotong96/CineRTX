// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
//
// Track 11C-3：事件驱动 WASAPI 共享模式音频主时钟。
//
// 实时回调线程 4 条绝对红线：零文件 I/O、零动态堆分配、零长锁等待、零同步 GPU
// Fence Wait。回调只做「预分配环形缓冲 -> 设备缓冲」的 memcpy 与一次
// IAudioClock::GetPosition 发布；control_ 互斥量仅由馈送/控制线程持有，回调
// 线程永不触碰它。
//
// AUDCLNT_E_DEVICE_INVALIDATED 由回调置原子标志后立即返回，重连状态机在馈送
// 线程的 write() 中执行：join 回调线程 -> 重新枚举默认端点 -> 重建客户端 ->
// 重启回调线程，锚点承接失效瞬间的时间线位置以平滑续播。
//
// Phase 11.2-R（Std-First）：COM 所有权统一 rtx::ComPtr；CoUninitialize 与
// CoTaskMemFree 走 FnDeleter 哨兵；回调线程改用 std::jthread + stop_callback，
// 停止信号经既有事件唤醒等待，析构自动 request_stop + join。

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <audioclient.h>
#include <mmdeviceapi.h>
// initguid 必须晚于 API 头、早于 ksmedia：只让 KSDATAFORMAT_SUBTYPE_* 在本翻译
// 单元实例化，避免与 uuid 库里的 IID 定义冲突。
#include <initguid.h>
#include <ksmedia.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <format>
#include <memory>
#include <mutex>
#include <span>
#include <stop_token>
#include <thread>
#include <utility>
#include <vector>

#include "rtx/flicks.h"
#include "rtx/playback_session.h"
#include "rtx/raii_owners.h"
#include "rtx/sys_dynload.h"

namespace rtx {
namespace {

// CoUninitialize 作用域哨兵：SUCCEEDED(CoInitializeEx(...)) 后 reset() 一次，
// 作用域结束自动配平。FnDeleter 要求单参函数，此处以自由函数适配。
using ComApartment = std::unique_ptr<void, FnDeleter<&rtx::sys::coUninitializeGuard>>;
// CoUninitialize 作用域哨兵别名（非拥有指针恒为非空哨兵值）。

using MixFormatOwner = std::unique_ptr<WAVEFORMATEX, FnDeleter<&rtx::sys::coTaskMemFreeVoid>>;
// GetMixFormat 结果所有权：CoTaskMemFree 收口。

[[nodiscard]] std::string hresultText(HRESULT hr) {
    return std::format("0x{:08X}", static_cast<uint32_t>(hr));
}
[[nodiscard]] int64_t qpcNow() noexcept {
    LARGE_INTEGER counter{};
    QueryPerformanceCounter(&counter);
    return counter.QuadPart;
}
}  // namespace

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4324)
#endif
// @rtx-type-justification
// Category: NonTrivial-StateMachine
// Why-Std-Fails: 管理 WASAPI 异步生命周期的复杂状态机：封装多 COM 接口、事件句柄、缓存行对齐无锁环形游标与专用渲染工作线程，std::tuple 无法表达拓扑依赖与析构逆序。
// Invariant: 成员声明序即析构逆序（worker_ 居末先 join）；热原子游标居首并显式 64 字节补齐消除伪共享；无锁实时音频线程与控制线程互斥安全。
struct WasapiAudioOutput::Impl {
    alignas(64) std::atomic<uint64_t> write_cursor_{0};
    uint8_t pad_write_cursor_[64 - sizeof(std::atomic<uint64_t>)]{};
    alignas(64) std::atomic<uint64_t> read_cursor_{0};
    uint8_t pad_read_cursor_[64 - sizeof(std::atomic<uint64_t>)]{};
    alignas(64) std::atomic<int64_t> anchor_{0};
    uint8_t pad_anchor_[64 - sizeof(std::atomic<int64_t>)]{};
    alignas(64) std::atomic<int64_t> published_flicks_{0};
    uint8_t pad_published_flicks_[64 - sizeof(std::atomic<int64_t>)]{};
    alignas(64) std::atomic<int64_t> published_qpc_{0};
    uint8_t pad_published_qpc_[64 - sizeof(std::atomic<int64_t>)]{};
    alignas(64) std::atomic<uint64_t> written_frames_{0};
    uint8_t pad_written_frames_[64 - sizeof(std::atomic<uint64_t>)]{};
    alignas(64) std::atomic<uint64_t> reconnects_{0};
    uint8_t pad_reconnects_[64 - sizeof(std::atomic<uint64_t>)]{};

    uint32_t sample_rate_ = 0;
    uint32_t channels_ = 0;
    uint32_t buffer_frames_ = 0;
    uint32_t ring_frames_ = 0;
    uint64_t clock_freq_ = 0;
    int64_t qpc_freq_ = 1;

    std::vector<float> ring_;
    std::atomic<bool> started_{false};
    std::atomic<bool> invalidated_{false};

    ComPtr<IMMDeviceEnumerator> enumerator_;
    ComPtr<IMMDevice> device_;
    ComPtr<IAudioClient> client_;
    ComPtr<IAudioRenderClient> render_;
    ComPtr<IAudioClock> clock_;
    UniqueHandle buffer_event_;
    UniqueHandle stop_event_;
    std::mutex control_;
// 声明顺序即析构逆序：worker_ 最后声明，因此最先 join，保证回调线程在任何
// COM 接口、事件句柄与环形缓冲析构之前彻底退出。
    std::jthread worker_;

    Result<void> openDevice() {
        ComApartment com;
        if (SUCCEEDED(rtx::sys::coInitializeEx(nullptr, COINIT_MULTITHREADED))) {
            com.reset(reinterpret_cast<void*>(1));
        }
        IMMDeviceEnumerator* raw_enumerator = nullptr;
        HRESULT hr = rtx::sys::coCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                      __uuidof(IMMDeviceEnumerator),
                                      reinterpret_cast<void**>(&raw_enumerator));
        if (FAILED(hr)) {
            return err(ErrorCode::Io, "wasapi: MMDeviceEnumerator unavailable", hresultText(hr));
        }
        enumerator_.reset(raw_enumerator);
        IMMDevice* raw_device = nullptr;
        hr = enumerator_->GetDefaultAudioEndpoint(eRender, eConsole, &raw_device);
        if (FAILED(hr)) {
            return err(ErrorCode::Io, "wasapi: no default render endpoint", hresultText(hr));
        }
        device_.reset(raw_device);
        IAudioClient* raw_client = nullptr;
        hr = device_->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                               reinterpret_cast<void**>(&raw_client));
        if (FAILED(hr)) {
            return err(ErrorCode::Io, "wasapi: IAudioClient activation failed", hresultText(hr));
        }
        client_.reset(raw_client);

        WAVEFORMATEX* raw = nullptr;
        hr = client_->GetMixFormat(&raw);
        if (FAILED(hr)) {
            return err(ErrorCode::Io, "wasapi: GetMixFormat failed", hresultText(hr));
        }
        MixFormatOwner mix{raw};
        const auto* ext = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(mix.get());
        if (mix->wFormatTag != WAVE_FORMAT_EXTENSIBLE || mix->wBitsPerSample != 32 ||
            !IsEqualGUID(ext->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)) {
            return err(ErrorCode::Unsupported, "wasapi: mix format is not 32-bit float");
        }
        sample_rate_ = mix->nSamplesPerSec;
        channels_ = mix->nChannels;
        if (sample_rate_ == 0 || channels_ == 0 || channels_ > 8) {
            return err(ErrorCode::Unsupported, "wasapi: unsupported mix format geometry");
        }
        REFERENCE_TIME default_period = 0;
        REFERENCE_TIME minimum_period = 0;
        hr = client_->GetDevicePeriod(&default_period, &minimum_period);
        if (FAILED(hr)) {
            return err(ErrorCode::Io, "wasapi: GetDevicePeriod failed", hresultText(hr));
        }
        buffer_event_.reset(normalizeWin32Handle(CreateEventW(nullptr, FALSE, FALSE, nullptr)));
        if (!buffer_event_) {
            return err(ErrorCode::Io, "wasapi: buffer event creation failed");
        }
        hr = client_->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                                 default_period * 2, 0, mix.get(), nullptr);
        if (FAILED(hr)) {
            return err(ErrorCode::Io, "wasapi: IAudioClient::Initialize failed", hresultText(hr));
        }
        hr = client_->GetBufferSize(&buffer_frames_);
        if (FAILED(hr)) {
            return err(ErrorCode::Io, "wasapi: GetBufferSize failed", hresultText(hr));
        }
        IAudioRenderClient* raw_render = nullptr;
        hr = client_->GetService(__uuidof(IAudioRenderClient),
                                 reinterpret_cast<void**>(&raw_render));
        if (FAILED(hr)) {
            return err(ErrorCode::Io, "wasapi: IAudioRenderClient unavailable", hresultText(hr));
        }
        render_.reset(raw_render);
        IAudioClock* raw_clock = nullptr;
        hr = client_->GetService(__uuidof(IAudioClock), reinterpret_cast<void**>(&raw_clock));
        if (FAILED(hr)) {
            return err(ErrorCode::Io, "wasapi: IAudioClock unavailable", hresultText(hr));
        }
        clock_.reset(raw_clock);
        UINT64 frequency = 0;
        hr = clock_->GetFrequency(&frequency);
        if (FAILED(hr) || frequency == 0) {
            return err(ErrorCode::Io, "wasapi: IAudioClock::GetFrequency failed", hresultText(hr));
        }
        clock_freq_ = frequency;
        hr = client_->SetEventHandle(buffer_event_.get());
        if (FAILED(hr)) {
            return err(ErrorCode::Io, "wasapi: SetEventHandle failed", hresultText(hr));
        }
        LARGE_INTEGER qpf{};
        QueryPerformanceFrequency(&qpf);
        qpc_freq_ = qpf.QuadPart;

        ring_frames_ = buffer_frames_ * 8;
        ring_.assign(static_cast<size_t>(ring_frames_) * channels_, 0.0f);
        write_cursor_.store(0, std::memory_order_relaxed);
        read_cursor_.store(0, std::memory_order_relaxed);
        invalidated_.store(false, std::memory_order_relaxed);
        return {};
    }
    void launch() {
        ResetEvent(stop_event_.get());
        worker_ = std::jthread([this](std::stop_token st) { callbackLoop(std::move(st)); });
    }
    void callbackLoop(std::stop_token st) noexcept {
        ComApartment com;
        if (SUCCEEDED(rtx::sys::coInitializeEx(nullptr, COINIT_MULTITHREADED))) {
            com.reset(reinterpret_cast<void*>(1));
        }
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
        // 停止请求经既有事件唤醒 INFINITE 等待：jthread 析构 request_stop ->
        // stop_callback SetEvent -> WaitForMultipleObjects 返回 -> 线程退出。
        std::stop_callback stop_set{st, [this] { SetEvent(stop_event_.get()); }};
        HANDLE waits[2] = {buffer_event_.get(), stop_event_.get()};
        for (;;) {
            const DWORD wait = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
            if (wait != WAIT_OBJECT_0) {
                return;
            }
            pumpDevice();
        }
    }
    void pumpDevice() noexcept {
        UINT32 padding = 0;
        HRESULT hr = client_->GetCurrentPadding(&padding);
        if (hr == AUDCLNT_E_DEVICE_INVALIDATED) {
            invalidated_.store(true, std::memory_order_release);
            return;
        }
        if (FAILED(hr) || padding >= buffer_frames_) {
            return;
        }
        const UINT32 space = buffer_frames_ - padding;
        BYTE* data = nullptr;
        hr = render_->GetBuffer(space, &data);
        if (hr == AUDCLNT_E_DEVICE_INVALIDATED) {
            invalidated_.store(true, std::memory_order_release);
            return;
        }
        if (FAILED(hr)) {
            return;
        }
        // 红线区：只有 memcpy、原子游标与一次 GetPosition。无锁、无分配、无 I/O。
        const uint64_t read = read_cursor_.load(std::memory_order_relaxed);
        const uint64_t written = write_cursor_.load(std::memory_order_acquire);
        const uint32_t available =
            static_cast<uint32_t>(std::min<uint64_t>(written - read, space));
        auto* dst = reinterpret_cast<float*>(data);
        copyFromRing(dst, read, available);
        if (available < space) {
            std::memset(dst + static_cast<size_t>(available) * channels_, 0,
                        static_cast<size_t>(space - available) * channels_ * sizeof(float));
        }
        render_->ReleaseBuffer(space, 0);
        read_cursor_.store(read + available, std::memory_order_release);
        written_frames_.fetch_add(available, std::memory_order_relaxed);
        publishClock();
    }
    void publishClock() noexcept {
        UINT64 position = 0;
        UINT64 qpc = 0;
        if (FAILED(clock_->GetPosition(&position, &qpc))) {
            return;
        }
        const double seconds = static_cast<double>(position) / static_cast<double>(clock_freq_);
        published_flicks_.store(
            anchor_.load(std::memory_order_relaxed) +
                static_cast<int64_t>(seconds * static_cast<double>(kFlicksPerSecond)),
            std::memory_order_release);
        published_qpc_.store(static_cast<int64_t>(qpc), std::memory_order_release);
    }
    void copyFromRing(float* dst, uint64_t read, uint32_t frames) const noexcept {
        const size_t offset = static_cast<size_t>(read % ring_frames_) * channels_;
        const size_t count = static_cast<size_t>(frames) * channels_;
        const size_t tail = std::min(count, ring_.size() - offset);
        std::memcpy(dst, ring_.data() + offset, tail * sizeof(float));
        if (tail < count) {
            std::memcpy(dst + tail, ring_.data(), (count - tail) * sizeof(float));
        }
    }
    void copyToRing(const float* src, uint64_t write, uint32_t frames) noexcept {
        const size_t offset = static_cast<size_t>(write % ring_frames_) * channels_;
        const size_t count = static_cast<size_t>(frames) * channels_;
        const size_t tail = std::min(count, ring_.size() - offset);
        std::memcpy(ring_.data() + offset, src, tail * sizeof(float));
        if (tail < count) {
            std::memcpy(ring_.data(), src + tail, (count - tail) * sizeof(float));
        }
    }
    Result<void> reopen() {
        anchor_.store(published_flicks_.load(std::memory_order_acquire),
                      std::memory_order_release);
        published_qpc_.store(0, std::memory_order_release);
        const bool was_started = started_.load(std::memory_order_relaxed);

        worker_.request_stop();
        if (worker_.joinable()) {
            worker_.join();
        }
        clock_.reset();
        render_.reset();
        client_.reset();
        device_.reset();
        buffer_event_.reset();
// 锚点承接失效瞬间的时间线位置，新设备从 0 计数即平滑续播。
        started_.store(false, std::memory_order_relaxed);

        Result<void> opened = openDevice();
        if (!opened) {
            return opened;
        }
        reconnects_.fetch_add(1, std::memory_order_relaxed);
        launch();
        if (was_started) {
            const HRESULT hr = client_->Start();
            if (FAILED(hr)) {
                return err(ErrorCode::Io, "wasapi: reconnect start failed", hresultText(hr));
            }
            started_.store(true, std::memory_order_relaxed);
        }
        return {};
    }
};
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

Result<std::unique_ptr<WasapiAudioOutput>> WasapiAudioOutput::create() {
    std::unique_ptr<WasapiAudioOutput> out{new WasapiAudioOutput()};
    out->impl_ = std::make_unique<Impl>();
    LARGE_INTEGER qpf{};
    QueryPerformanceFrequency(&qpf);
    out->impl_->qpc_freq_ = qpf.QuadPart;
    out->impl_->stop_event_.reset(normalizeWin32Handle(CreateEventW(nullptr, TRUE, FALSE, nullptr)));
    if (!out->impl_->stop_event_) {
        return err(ErrorCode::Io, "wasapi: stop event creation failed");
    }
    Result<void> opened = out->impl_->openDevice();
    if (!opened) {
        return std::unexpected(opened.error());
    }
    out->impl_->launch();
    return out;
}
WasapiAudioOutput::~WasapiAudioOutput() = default;

Result<uint32_t> WasapiAudioOutput::write(std::span<const float> interleaved) {
    Impl& impl = *impl_;
    std::lock_guard<std::mutex> lock(impl.control_);
    if (impl.invalidated_.load(std::memory_order_acquire)) {
        Result<void> reopened = impl.reopen();
        if (!reopened) {
            return std::unexpected(reopened.error());
        }
    }
    if (interleaved.size() % impl.channels_ != 0) {
        return err(ErrorCode::InvalidArgument, "wasapi: interleaved frame count is not integral");
    }
    const uint32_t frames = static_cast<uint32_t>(interleaved.size() / impl.channels_);
    const uint64_t write = impl.write_cursor_.load(std::memory_order_relaxed);
    const uint64_t read = impl.read_cursor_.load(std::memory_order_acquire);
    const uint32_t space = static_cast<uint32_t>(impl.ring_frames_ - (write - read));
    const uint32_t consumed = std::min(frames, space);
    if (consumed > 0) {
        impl.copyToRing(interleaved.data(), write, consumed);
        impl.write_cursor_.store(write + consumed, std::memory_order_release);
    }
    return consumed;
}
void WasapiAudioOutput::anchor(int64_t base_flicks) noexcept {
    Impl& impl = *impl_;
    std::lock_guard<std::mutex> lock(impl.control_);
    impl.anchor_.store(base_flicks, std::memory_order_release);
    impl.published_flicks_.store(base_flicks, std::memory_order_release);
    impl.published_qpc_.store(0, std::memory_order_release);
}
void WasapiAudioOutput::pause() noexcept {
    Impl& impl = *impl_;
    std::lock_guard<std::mutex> lock(impl.control_);
    if (impl.started_.load(std::memory_order_relaxed) && impl.client_) {
        // 暂停即冻结：先把外推位置固化成锚点，否则 resume 后会把整段暂停时长
        // 当作已播放时间补进主时钟，A/V 同步随之跳变。
        impl.published_flicks_.store(masterFlicks(), std::memory_order_release);
        impl.published_qpc_.store(0, std::memory_order_release);
        impl.client_->Stop();
        impl.started_.store(false, std::memory_order_relaxed);
    }
}
void WasapiAudioOutput::resume() noexcept {
    Impl& impl = *impl_;
    std::lock_guard<std::mutex> lock(impl.control_);
    if (impl.invalidated_.load(std::memory_order_acquire)) {
        if (!impl.reopen()) {
            return;
        }
    }
    if (!impl.started_.load(std::memory_order_relaxed) && impl.client_) {
        if (SUCCEEDED(impl.client_->Start())) {
            impl.started_.store(true, std::memory_order_relaxed);
        }
    }
}
void WasapiAudioOutput::flush() noexcept {
    Impl& impl = *impl_;
    std::lock_guard<std::mutex> lock(impl.control_);
    const uint64_t write = impl.write_cursor_.load(std::memory_order_relaxed);
    impl.read_cursor_.store(write, std::memory_order_release);
    if (impl.client_) {
        const bool was_started = impl.started_.load(std::memory_order_relaxed);
        if (was_started) {
            impl.client_->Stop();
            impl.started_.store(false, std::memory_order_relaxed);
        }
        impl.client_->Reset();
        if (was_started && SUCCEEDED(impl.client_->Start())) {
            impl.started_.store(true, std::memory_order_relaxed);
        }
    }
    impl.published_qpc_.store(0, std::memory_order_release);
}
int64_t WasapiAudioOutput::masterFlicks() const noexcept {
    const Impl& impl = *impl_;
    const int64_t base = impl.published_flicks_.load(std::memory_order_acquire);
    if (!impl.started_.load(std::memory_order_acquire)) {
        return base;
    }
    const int64_t then = impl.published_qpc_.load(std::memory_order_acquire);
    if (then == 0) {
        return base;
    }
    const double elapsed = static_cast<double>(qpcNow() - then) / static_cast<double>(impl.qpc_freq_);
    return base + static_cast<int64_t>(elapsed * static_cast<double>(kFlicksPerSecond));
}
bool WasapiAudioOutput::running() const noexcept {
    return impl_->started_.load(std::memory_order_acquire) &&
           !impl_->invalidated_.load(std::memory_order_acquire);
}
uint32_t WasapiAudioOutput::sampleRate() const noexcept { return impl_->sample_rate_; }
uint32_t WasapiAudioOutput::channels() const noexcept { return impl_->channels_; }
uint64_t WasapiAudioOutput::writtenFrames() const noexcept {
    return impl_->written_frames_.load(std::memory_order_relaxed);
}
uint64_t WasapiAudioOutput::reconnects() const noexcept {
    return impl_->reconnects_.load(std::memory_order_relaxed);
}
}  // namespace rtx
