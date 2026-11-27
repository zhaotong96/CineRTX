// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
//
// Track 11C-3 & Phase 13 Wave 0-B (78_ Task 3):
// Event-driven WASAPI shared-mode audio master clock with redundant Pimpl
// struct WasapiAudioOutput::Impl physically retired (AuditTypes 34 -> 33).

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <audioclient.h>
#include <mmdeviceapi.h>
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

using ComApartment = std::unique_ptr<void, FnDeleter<&rtx::sys::coUninitializeGuard>>;
using MixFormatOwner = std::unique_ptr<WAVEFORMATEX, FnDeleter<&rtx::sys::coTaskMemFreeVoid>>;

[[nodiscard]] std::string hresultText(HRESULT hr) {
    return std::format("0x{:08X}", static_cast<uint32_t>(hr));
}
[[nodiscard]] int64_t qpcNow() noexcept {
    LARGE_INTEGER counter{};
    QueryPerformanceCounter(&counter);
    return counter.QuadPart;
}
template <typename T>
void resetCom(void*& slot, T* next = nullptr) noexcept {
    if (slot != nullptr) {
        static_cast<IUnknown*>(slot)->Release();
    }
    slot = next;
}
void resetEventHandle(void*& slot, HANDLE next = nullptr) noexcept {
    if (slot != nullptr) {
        CloseHandle(static_cast<HANDLE>(slot));
    }
    slot = normalizeWin32Handle(next);
}

}  // namespace

void WasapiAudioOutput::releaseComHandles() noexcept {
    resetCom<IAudioClock>(clock_, nullptr);
    resetCom<IAudioRenderClient>(render_, nullptr);
    resetCom<IAudioClient>(client_, nullptr);
    resetCom<IMMDevice>(device_, nullptr);
    resetCom<IMMDeviceEnumerator>(enumerator_, nullptr);
    resetEventHandle(buffer_event_, nullptr);
}

Result<void> WasapiAudioOutput::openDevice() {
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
    resetCom(enumerator_, raw_enumerator);
    IMMDevice* raw_device = nullptr;
    hr = static_cast<IMMDeviceEnumerator*>(enumerator_)->GetDefaultAudioEndpoint(
        eRender, eConsole, &raw_device);
    if (FAILED(hr)) {
        return err(ErrorCode::Io, "wasapi: no default render endpoint", hresultText(hr));
    }
    resetCom(device_, raw_device);
    IAudioClient* raw_client = nullptr;
    hr = static_cast<IMMDevice*>(device_)->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                                    reinterpret_cast<void**>(&raw_client));
    if (FAILED(hr)) {
        return err(ErrorCode::Io, "wasapi: IAudioClient activation failed", hresultText(hr));
    }
    resetCom(client_, raw_client);
    auto* client = static_cast<IAudioClient*>(client_);

    WAVEFORMATEX* raw = nullptr;
    hr = client->GetMixFormat(&raw);
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
    hr = client->GetDevicePeriod(&default_period, &minimum_period);
    if (FAILED(hr)) {
        return err(ErrorCode::Io, "wasapi: GetDevicePeriod failed", hresultText(hr));
    }
    resetEventHandle(buffer_event_, CreateEventW(nullptr, FALSE, FALSE, nullptr));
    if (buffer_event_ == nullptr) {
        return err(ErrorCode::Io, "wasapi: buffer event creation failed");
    }
    hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                            default_period * 2, 0, mix.get(), nullptr);
    if (FAILED(hr)) {
        return err(ErrorCode::Io, "wasapi: IAudioClient::Initialize failed", hresultText(hr));
    }
    hr = client->GetBufferSize(&buffer_frames_);
    if (FAILED(hr)) {
        return err(ErrorCode::Io, "wasapi: GetBufferSize failed", hresultText(hr));
    }
    IAudioRenderClient* raw_render = nullptr;
    hr = client->GetService(__uuidof(IAudioRenderClient), reinterpret_cast<void**>(&raw_render));
    if (FAILED(hr)) {
        return err(ErrorCode::Io, "wasapi: IAudioRenderClient unavailable", hresultText(hr));
    }
    resetCom(render_, raw_render);
    IAudioClock* raw_clock = nullptr;
    hr = client->GetService(__uuidof(IAudioClock), reinterpret_cast<void**>(&raw_clock));
    if (FAILED(hr)) {
        return err(ErrorCode::Io, "wasapi: IAudioClock unavailable", hresultText(hr));
    }
    resetCom(clock_, raw_clock);
    UINT64 frequency = 0;
    hr = static_cast<IAudioClock*>(clock_)->GetFrequency(&frequency);
    if (FAILED(hr) || frequency == 0) {
        return err(ErrorCode::Io, "wasapi: IAudioClock::GetFrequency failed", hresultText(hr));
    }
    clock_freq_ = frequency;
    hr = client->SetEventHandle(static_cast<HANDLE>(buffer_event_));
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

void WasapiAudioOutput::launch() {
    ResetEvent(static_cast<HANDLE>(stop_event_));
    worker_ = std::jthread([this](std::stop_token st) { callbackLoop(std::move(st)); });
}

void WasapiAudioOutput::callbackLoop(std::stop_token st) noexcept {
    ComApartment com;
    if (SUCCEEDED(rtx::sys::coInitializeEx(nullptr, COINIT_MULTITHREADED))) {
        com.reset(reinterpret_cast<void*>(1));
    }
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    std::stop_callback stop_set{st, [this] { SetEvent(static_cast<HANDLE>(stop_event_)); }};
    HANDLE waits[2] = {static_cast<HANDLE>(buffer_event_), static_cast<HANDLE>(stop_event_)};
    for (;;) {
        const DWORD wait = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
        if (wait != WAIT_OBJECT_0) {
            return;
        }
        pumpDevice();
    }
}

void WasapiAudioOutput::pumpDevice() noexcept {
    auto* client = static_cast<IAudioClient*>(client_);
    auto* render = static_cast<IAudioRenderClient*>(render_);
    UINT32 padding = 0;
    HRESULT hr = client->GetCurrentPadding(&padding);
    if (hr == AUDCLNT_E_DEVICE_INVALIDATED) {
        invalidated_.store(true, std::memory_order_release);
        return;
    }
    if (FAILED(hr) || padding >= buffer_frames_) {
        return;
    }
    const UINT32 space = buffer_frames_ - padding;
    BYTE* data = nullptr;
    hr = render->GetBuffer(space, &data);
    if (hr == AUDCLNT_E_DEVICE_INVALIDATED) {
        invalidated_.store(true, std::memory_order_release);
        return;
    }
    if (FAILED(hr)) {
        return;
    }
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
    render->ReleaseBuffer(space, 0);
    read_cursor_.store(read + available, std::memory_order_release);
    written_frames_.fetch_add(available, std::memory_order_relaxed);
    publishClock();
}

void WasapiAudioOutput::publishClock() noexcept {
    UINT64 position = 0;
    UINT64 qpc = 0;
    if (FAILED(static_cast<IAudioClock*>(clock_)->GetPosition(&position, &qpc))) {
        return;
    }
    const double seconds = static_cast<double>(position) / static_cast<double>(clock_freq_);
    published_flicks_.store(
        anchor_.load(std::memory_order_relaxed) +
            static_cast<int64_t>(seconds * static_cast<double>(kFlicksPerSecond)),
        std::memory_order_release);
    published_qpc_.store(static_cast<int64_t>(qpc), std::memory_order_release);
}

void WasapiAudioOutput::copyFromRing(float* dst, uint64_t read, uint32_t frames) const noexcept {
    const size_t offset = static_cast<size_t>(read % ring_frames_) * channels_;
    const size_t count = static_cast<size_t>(frames) * channels_;
    const size_t tail = std::min(count, ring_.size() - offset);
    std::memcpy(dst, ring_.data() + offset, tail * sizeof(float));
    if (tail < count) {
        std::memcpy(dst + tail, ring_.data(), (count - tail) * sizeof(float));
    }
}

void WasapiAudioOutput::copyToRing(const float* src, uint64_t write, uint32_t frames) noexcept {
    const size_t offset = static_cast<size_t>(write % ring_frames_) * channels_;
    const size_t count = static_cast<size_t>(frames) * channels_;
    const size_t tail = std::min(count, ring_.size() - offset);
    std::memcpy(ring_.data() + offset, src, tail * sizeof(float));
    if (tail < count) {
        std::memcpy(ring_.data(), src + tail, (count - tail) * sizeof(float));
    }
}

Result<void> WasapiAudioOutput::reopen() {
    anchor_.store(published_flicks_.load(std::memory_order_acquire), std::memory_order_release);
    published_qpc_.store(0, std::memory_order_release);
    const bool was_started = started_.load(std::memory_order_relaxed);

    worker_.request_stop();
    if (worker_.joinable()) {
        worker_.join();
    }
    releaseComHandles();
    started_.store(false, std::memory_order_relaxed);

    Result<void> opened = openDevice();
    if (!opened) {
        return opened;
    }
    reconnects_.fetch_add(1, std::memory_order_relaxed);
    launch();
    if (was_started) {
        const HRESULT hr = static_cast<IAudioClient*>(client_)->Start();
        if (FAILED(hr)) {
            return err(ErrorCode::Io, "wasapi: reconnect start failed", hresultText(hr));
        }
        started_.store(true, std::memory_order_relaxed);
    }
    return {};
}

Result<std::unique_ptr<WasapiAudioOutput>> WasapiAudioOutput::create() {
    std::unique_ptr<WasapiAudioOutput> out{new WasapiAudioOutput()};
    LARGE_INTEGER qpf{};
    QueryPerformanceFrequency(&qpf);
    out->qpc_freq_ = qpf.QuadPart;
    resetEventHandle(out->stop_event_, CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (out->stop_event_ == nullptr) {
        return err(ErrorCode::Io, "wasapi: stop event creation failed");
    }
    Result<void> opened = out->openDevice();
    if (!opened) {
        return std::unexpected(opened.error());
    }
    out->launch();
    return out;
}

WasapiAudioOutput::~WasapiAudioOutput() {
    worker_.request_stop();
    if (worker_.joinable()) {
        worker_.join();
    }
    releaseComHandles();
    resetEventHandle(stop_event_, nullptr);
}

Result<uint32_t> WasapiAudioOutput::write(std::span<const float> interleaved) {
    std::lock_guard<std::mutex> lock(control_);
    if (invalidated_.load(std::memory_order_acquire)) {
        Result<void> reopened = reopen();
        if (!reopened) {
            return std::unexpected(reopened.error());
        }
    }
    if (interleaved.size() % channels_ != 0) {
        return err(ErrorCode::InvalidArgument, "wasapi: interleaved frame count is not integral");
    }
    const uint32_t frames = static_cast<uint32_t>(interleaved.size() / channels_);
    const uint64_t write = write_cursor_.load(std::memory_order_relaxed);
    const uint64_t read = read_cursor_.load(std::memory_order_acquire);
    const uint32_t space = static_cast<uint32_t>(ring_frames_ - (write - read));
    const uint32_t consumed = std::min(frames, space);
    if (consumed > 0) {
        copyToRing(interleaved.data(), write, consumed);
        write_cursor_.store(write + consumed, std::memory_order_release);
    }
    return consumed;
}

void WasapiAudioOutput::anchor(int64_t base_flicks) noexcept {
    std::lock_guard<std::mutex> lock(control_);
    anchor_.store(base_flicks, std::memory_order_release);
    published_flicks_.store(base_flicks, std::memory_order_release);
    published_qpc_.store(0, std::memory_order_release);
}

void WasapiAudioOutput::pause() noexcept {
    std::lock_guard<std::mutex> lock(control_);
    if (started_.load(std::memory_order_relaxed) && client_ != nullptr) {
        published_flicks_.store(masterFlicks(), std::memory_order_release);
        published_qpc_.store(0, std::memory_order_release);
        static_cast<IAudioClient*>(client_)->Stop();
        started_.store(false, std::memory_order_relaxed);
    }
}

void WasapiAudioOutput::resume() noexcept {
    std::lock_guard<std::mutex> lock(control_);
    if (invalidated_.load(std::memory_order_acquire)) {
        if (!reopen()) {
            return;
        }
    }
    if (!started_.load(std::memory_order_relaxed) && client_ != nullptr) {
        if (SUCCEEDED(static_cast<IAudioClient*>(client_)->Start())) {
            started_.store(true, std::memory_order_relaxed);
        }
    }
}

void WasapiAudioOutput::flush() noexcept {
    std::lock_guard<std::mutex> lock(control_);
    const uint64_t write = write_cursor_.load(std::memory_order_relaxed);
    read_cursor_.store(write, std::memory_order_release);
    if (client_ != nullptr) {
        auto* client = static_cast<IAudioClient*>(client_);
        const bool was_started = started_.load(std::memory_order_relaxed);
        if (was_started) {
            client->Stop();
            started_.store(false, std::memory_order_relaxed);
        }
        client->Reset();
        if (was_started && SUCCEEDED(client->Start())) {
            started_.store(true, std::memory_order_relaxed);
        }
    }
    published_qpc_.store(0, std::memory_order_release);
}

int64_t WasapiAudioOutput::masterFlicks() const noexcept {
    const int64_t base = published_flicks_.load(std::memory_order_acquire);
    if (!started_.load(std::memory_order_acquire)) {
        return base;
    }
    const int64_t then = published_qpc_.load(std::memory_order_acquire);
    if (then == 0) {
        return base;
    }
    const double elapsed = static_cast<double>(qpcNow() - then) / static_cast<double>(qpc_freq_);
    return base + static_cast<int64_t>(elapsed * static_cast<double>(kFlicksPerSecond));
}

bool WasapiAudioOutput::running() const noexcept {
    return started_.load(std::memory_order_acquire) &&
           !invalidated_.load(std::memory_order_acquire);
}

uint32_t WasapiAudioOutput::sampleRate() const noexcept { return sample_rate_; }
uint32_t WasapiAudioOutput::channels() const noexcept { return channels_; }
uint64_t WasapiAudioOutput::writtenFrames() const noexcept {
    return written_frames_.load(std::memory_order_relaxed);
}
uint64_t WasapiAudioOutput::reconnects() const noexcept {
    return reconnects_.load(std::memory_order_relaxed);
}

}  // namespace rtx
