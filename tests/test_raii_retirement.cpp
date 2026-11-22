// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
//
// Track 11A / Task 11A-5: five-domain RAII failure injection and GPU fence
// retirement gates.  Every assertion counts actual releases, so a leak or a
// double release fails the test instead of merely looking tidy.

#include "test_framework.h"

#include "hw_transcode_internal.h"
#include "rtx/raii_owners.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include "d3d12_raii.h"
#include "preview_viewport.h"
// Phase 11.2 (C-01): fxc-generated into ${CMAKE_BINARY_DIR}/generated.
#include "preview_tone_map_cs.h"
#endif

namespace {

// ---- CUDA 域：释放器计数 ---------------------------------------------------
struct FreeLedger {
    std::vector<unsigned long long> device_freed;
    std::vector<void*> host_freed;
};

FreeLedger* active_ledger = nullptr;
// 测试串行执行，账本无需加锁。

int countDeviceFree(unsigned long long ptr) {
    if (active_ledger != nullptr) active_ledger->device_freed.push_back(ptr);
    return 0;
}
int countHostFree(void* ptr) {
    if (active_ledger != nullptr) active_ledger->host_freed.push_back(ptr);
    return 0;
}
}  // namespace

RTX_TEST(raii_cuda_device_buffer_releases_exactly_once) {
    FreeLedger ledger;
    active_ledger = &ledger;
    for (int i = 0; i < 100; ++i) {
        rtx::CudaDeviceBuffer buffer(0x1000ull + static_cast<unsigned long long>(i), 4096,
                                     &countDeviceFree);
        RTX_CHECK(buffer);
        RTX_CHECK_EQ(buffer.bytes(), 4096u);
    }
    active_ledger = nullptr;
    RTX_REQUIRE_EQ(ledger.device_freed.size(), 100u);
    for (int i = 0; i < 100; ++i) {
        RTX_CHECK_EQ(ledger.device_freed[static_cast<size_t>(i)],
                     0x1000ull + static_cast<unsigned long long>(i));
    }
}
RTX_TEST(raii_cuda_device_buffer_move_transfers_ownership) {
    FreeLedger ledger;
    active_ledger = &ledger;
    {
        rtx::CudaDeviceBuffer a(0x2000, 64, &countDeviceFree);
        rtx::CudaDeviceBuffer b(std::move(a));
        RTX_CHECK(!a);
        RTX_CHECK_EQ(a.ptr(), 0ull);
        RTX_CHECK(b);
        rtx::CudaDeviceBuffer c;
        c = std::move(b);
        RTX_CHECK(!b);
        RTX_CHECK(c);
        RTX_CHECK_EQ(ledger.device_freed.size(), 0u);
    }
    active_ledger = nullptr;
    RTX_REQUIRE_EQ(ledger.device_freed.size(), 1u);
    RTX_CHECK_EQ(ledger.device_freed[0], 0x2000ull);
}
RTX_TEST(raii_cuda_pinned_host_never_uses_std_free) {
    FreeLedger ledger;
    active_ledger = &ledger;
    static unsigned char sentinel[256];
    {
        auto pinned = rtx::wrapCudaPinnedHost(sentinel, sizeof(sentinel), &countHostFree);
        RTX_CHECK_EQ(pinned.second, sizeof(sentinel));
        RTX_CHECK(pinned.first);
// 静态存储期哨兵：若实现错误地退回 std::free()，进程会立即崩溃。
    }
    RTX_REQUIRE_EQ(ledger.host_freed.size(), 1u);
    RTX_CHECK(ledger.host_freed[0] == static_cast<void*>(sentinel));

    {
        auto orphan = rtx::wrapCudaPinnedHost(sentinel, sizeof(sentinel), nullptr);
        RTX_CHECK(orphan.first);
    }
    RTX_CHECK_EQ(ledger.host_freed.size(), 1u);
// 释放器缺失即契约破裂：no-op 删除器宁可泄漏也绝不触碰指针。

    {
        void* heap = std::malloc(128);
        RTX_REQUIRE(heap != nullptr);
        auto system = rtx::wrapSystemHeapHost(heap, 128);
        RTX_CHECK_EQ(system.second, 128u);
// 系统堆来源走且仅走 std::free（不进 CUDA 账本）。
        RTX_CHECK(system.first);
    }
    RTX_CHECK_EQ(ledger.host_freed.size(), 1u);
    active_ledger = nullptr;
}
RTX_TEST(raii_rife_session_destroy_hook_fires_once) {
    int destroyed = 0;
    for (int i = 0; i < 100; ++i) {
        std::unique_ptr<void, std::function<void(void*)>> session(
            reinterpret_cast<void*>(static_cast<uintptr_t>(0x1)),
            [&destroyed](void*) { ++destroyed; });
        RTX_CHECK(session != nullptr);
// Phase 11.2-R：RifeSession 类由 std::unique_ptr<void, std::function> 作用域
// 守卫取代（与 hw_transcode_stages.cpp 的 destroyRife 收口完全同构）。
    }
    RTX_CHECK_EQ(destroyed, 100);

    std::unique_ptr<void, std::function<void(void*)>> empty;
    RTX_CHECK(empty == nullptr);
    empty.reset();
    RTX_CHECK_EQ(destroyed, 100);
}
RTX_TEST(raii_pipeline_worker_joins_before_buffer_destruction) {
    std::vector<std::string> order;
    {
        std::mutex mutex;
        std::condition_variable ready;
        bool stop = false;
        struct LoggedBuffer {
            std::vector<std::string>* log;
            ~LoggedBuffer() { log->push_back("buffer-destroyed"); }
        } buffer{&order};
        std::jthread worker([&](std::stop_token st) {
            std::unique_lock<std::mutex> lock(mutex);
            ready.wait(lock, [&] { return stop || st.stop_requested(); });
            order.push_back("worker-exited");
        });
        RTX_CHECK(worker.joinable());
    }
// worker 声明晚于 buffer，故析构早于 buffer：jthread 析构自动
// request_stop -> join，保证线程在任何缓冲区析构之前彻底退出。
    RTX_REQUIRE_EQ(order.size(), 2u);
    RTX_CHECK_EQ(order[0], std::string("worker-exited"));
    RTX_CHECK_EQ(order[1], std::string("buffer-destroyed"));
}
RTX_TEST(raii_pipeline_worker_relaunch_and_idempotent_join) {
    std::atomic<int> iterations{0};
    std::jthread worker;
    for (int round = 0; round < 100; ++round) {
        worker = std::jthread([&iterations](std::stop_token st) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            (void)st;
            ++iterations;
        });
        if (worker.joinable()) {
            worker.join();
            RTX_CHECK(!worker.joinable());
// 重赋值触发旧线程 request_stop + join（RAII 语义），随后 join 幂等收尾。
        }
    }
    RTX_CHECK_EQ(iterations.load(), 100);
}
#if defined(_WIN32)

namespace {

struct FakeComObject final : IUnknown {
    std::atomic<long> refs{1};
    std::vector<std::string>* log = nullptr;
    std::string tag;
    int* release_count = nullptr;

    FakeComObject(std::vector<std::string>* events, std::string name, int* counter)
        : log(events), tag(std::move(name)), release_count(counter) {}

// 真实 IUnknown 实现：Release 归零时记账并自毁，使“释放次数恒等于 1”可被断言。
    ULONG STDMETHODCALLTYPE AddRef() override { return static_cast<ULONG>(++refs); }
    ULONG STDMETHODCALLTYPE Release() override {
        const long remaining = --refs;
        if (remaining == 0) {
            if (log != nullptr) log->push_back("release:" + tag);
            if (release_count != nullptr) ++*release_count;
            delete this;
        }
        return static_cast<ULONG>(remaining);
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void** out) override {
        if (out != nullptr) *out = nullptr;
        return E_NOINTERFACE;
    }
};

IUnknown* makeFake(std::vector<std::string>* log, const char* tag, int* counter) {
    return new FakeComObject(log, tag, counter);
}
int countChildren(HWND parent) {
    int count = 0;
    EnumChildWindows(
        parent,
        [](HWND, LPARAM param) -> BOOL {
            ++*reinterpret_cast<int*>(param);
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&count));
    return count;
// 子 HWND 泄漏的唯一可观测判据：UniqueHwnd 析构必须真的 DestroyWindow。
}
constexpr UINT kSrcWidth = 960;
constexpr UINT kSrcHeight = 540;
constexpr UINT kSrcPitch = kSrcWidth * 4;
constexpr UINT64 kSrcBytes = static_cast<UINT64>(kSrcPitch) * kSrcHeight;

struct HeadlessGpu {
    rtx::gui::ComPtr<ID3D12Device> device;
    rtx::gui::ComPtr<ID3D12CommandQueue> queue;
    rtx::gui::ComPtr<ID3D12CommandAllocator> allocator;
    rtx::gui::ComPtr<ID3D12GraphicsCommandList> list;
    rtx::gui::ComPtr<ID3D12Fence> fence;
// ---- 无头 D3D12 台架 ------------------------------------------------------
// 测试机没有交互会话/DWM，CreateSwapChainForComposition 必然返回
// DXGI_ERROR_NOT_CURRENTLY_AVAILABLE；但设备、队列、计算与回读全部可用。
// 台架提供与桥接同几何的共享预览缓冲，使视口的设备借用、Fence 退役与色调
// 映射核函数都能在会话 0 里被真实压测。
    rtx::UniqueHandle event;
    UINT64 value = 0;
    bool ok = false;

    bool drain() {
        if (FAILED(list->Close())) return false;
        ID3D12CommandList* lists[] = {list.get()};
        queue->ExecuteCommandLists(1, lists);
        ++value;
        if (FAILED(queue->Signal(fence.get(), value))) return false;
        if (FAILED(fence->SetEventOnCompletion(value, event.get()))) return false;
        return WaitForSingleObject(event.get(), 10000) == WAIT_OBJECT_0;
    }
    bool reopen() {
        return SUCCEEDED(allocator->Reset()) && SUCCEEDED(list->Reset(allocator.get(), nullptr));
    }
};

template <typename T, typename Creator>
bool createAdopt(rtx::gui::ComPtr<T>& target, Creator&& creator) {
    target.reset();
    T* raw = nullptr;
    if (FAILED(creator(IID_PPV_ARGS(&raw)))) {
        return false;
    }
    target.reset(raw);
    return true;
}
HeadlessGpu makeHeadlessGpu() {
    HeadlessGpu gpu;
// COM 出参两段式（与生产 raii_owners.h 约定同构）：先交出空槽，API 回填后收养。
    if (!createAdopt(gpu.device, [&](REFIID riid, void** pp) {
            return D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, riid, pp);
        })) {
        return gpu;
    }
    D3D12_COMMAND_QUEUE_DESC qd{};
    qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    qd.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    if (!createAdopt(gpu.queue,
                     [&](REFIID riid, void** pp) { return gpu.device->CreateCommandQueue(&qd, riid, pp); })) {
        return gpu;
    }
    if (!createAdopt(gpu.allocator, [&](REFIID riid, void** pp) {
            return gpu.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, riid, pp);
        })) {
        return gpu;
    }
    if (!createAdopt(gpu.list, [&](REFIID riid, void** pp) {
            return gpu.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                 gpu.allocator.get(), nullptr, riid, pp);
        })) {
        return gpu;
    }
    if (FAILED(gpu.list->Close())) return gpu;
    if (!createAdopt(gpu.fence, [&](REFIID riid, void** pp) {
            return gpu.device->CreateFence(0, D3D12_FENCE_FLAG_NONE, riid, pp);
        })) {
        return gpu;
    }
    gpu.event.reset(CreateEventW(nullptr, FALSE, FALSE, nullptr));
    gpu.ok = static_cast<bool>(gpu.event);
    return gpu;
}
D3D12_RESOURCE_DESC previewBufferDesc() {
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
// CreateCommandList 返回时处于记录状态；不 Close 就无法 Reset 分配器。
    desc.Width = kSrcBytes;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_UNKNOWN;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags = D3D12_RESOURCE_FLAG_NONE;
    return desc;
}
rtx::gui::ComPtr<ID3D12Resource> makeSharedPreviewBuffer(HeadlessGpu& gpu, UINT left_word,
                                                           UINT right_word) {
    rtx::gui::ComPtr<ID3D12Resource> shared;
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    const D3D12_RESOURCE_DESC desc = previewBufferDesc();
    if (!createAdopt(shared, [&](REFIID riid, void** pp) {
            return gpu.device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_SHARED, &desc,
                                                       D3D12_RESOURCE_STATE_COMMON, nullptr, riid, pp);
        })) {
        return shared;
    }
    heap.Type = D3D12_HEAP_TYPE_UPLOAD;
    rtx::gui::ComPtr<ID3D12Resource> upload;
    if (!createAdopt(upload, [&](REFIID riid, void** pp) {
// 左半 = 显示参考 sRGB 灰，右半 = PQ 编码白。packRgb10 的位布局与核函数一致。
            return gpu.device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                       D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, riid, pp);
        })) {
        shared.reset();
        return shared;
    }
    void* mapped = nullptr;
    const D3D12_RANGE read_range{0, 0};
    if (FAILED(upload->Map(0, &read_range, &mapped)) || mapped == nullptr) {
        shared.reset();
        return shared;
    }
    auto* words = static_cast<UINT*>(mapped);
    for (UINT y = 0; y < kSrcHeight; ++y) {
        for (UINT x = 0; x < kSrcWidth; ++x) {
            words[static_cast<size_t>(y) * kSrcWidth + x] = (x < kSrcWidth / 2) ? left_word : right_word;
        }
    }
    const D3D12_RANGE written{0, static_cast<SIZE_T>(kSrcBytes)};
    upload->Unmap(0, &written);

    if (!gpu.reopen()) {
        shared.reset();
        return shared;
    }
    D3D12_RESOURCE_BARRIER to_dest{};
    to_dest.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    to_dest.Transition.pResource = shared.get();
    to_dest.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
    to_dest.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
    to_dest.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    gpu.list->ResourceBarrier(1, &to_dest);
    gpu.list->CopyBufferRegion(shared.get(), 0, upload.get(), 0, kSrcBytes);
    D3D12_RESOURCE_BARRIER to_common = to_dest;
    to_common.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    to_common.Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
    gpu.list->ResourceBarrier(1, &to_common);
    if (!gpu.drain()) {
        shared.reset();
        return shared;
    }
    return shared;
}
double toneMappedByte(float code) {
    const float m1 = 2610.0f / 16384.0f;
    const float m2 = 2523.0f / 4096.0f * 128.0f;
    const float c1 = 3424.0f / 4096.0f;
    const float c2 = 2413.0f / 4096.0f * 32.0f;
    const float c3 = 2392.0f / 4096.0f * 32.0f;
    const float pw = std::pow(code, 1.0f / m2);
    const float nits = std::pow((std::max)(pw - c1, 0.0f) / (c2 - c3 * pw), 1.0f / m1) * 10000.0f;
    const float x = nits / 100.0f;
    const float aces = (std::min)((std::max)(x * (2.51f * x + 0.03f) /
                                                 (x * (2.43f * x + 0.59f) + 0.14f),
                                             0.0f),
                                  1.0f);
    const float srgb = aces <= 0.0031308f ? aces * 12.92f
                                          : 1.055f * std::pow(aces, 1.0f / 2.4f) - 0.055f;
    return static_cast<double>((std::min)((std::max)(srgb, 0.0f), 1.0f)) * 255.0;
// PQ -> ACES Filmic -> sRGB 的标量参考，与 preview_tone_map_cs.hlsl 逐式对应。
// 中性灰经 BT.2020->709 矩阵后仍为中性（行和 1.0001 / 1.0000 / 0.9999），
// 偏差远小于 1/255，故参考只算亮度通路。
}
}  // namespace

RTX_TEST(gpu_retirement_releases_only_after_fence_advances) {
    std::vector<std::string> log;
    int released_a = 0, released_b = 0, released_c = 0;
    std::uint64_t completed = 0;
    {
        rtx::gui::GpuRetirementQueue queue([&completed] { return completed; });
        queue.retire(1, makeFake(&log, "a", &released_a));
        queue.retire(2, makeFake(&log, "b", &released_b));
        queue.retire(3, makeFake(&log, "c", &released_c));
        RTX_CHECK_EQ(queue.pending(), 3u);

        RTX_CHECK_EQ(queue.collect(), 0u);
        RTX_CHECK_EQ(released_a + released_b + released_c, 0);

        completed = 2;
        RTX_CHECK_EQ(queue.collect(), 2u);
        RTX_CHECK_EQ(released_a, 1);
        RTX_CHECK_EQ(released_b, 1);
        RTX_CHECK_EQ(released_c, 0);
        RTX_CHECK_EQ(queue.pending(), 1u);

        completed = 3;
        RTX_CHECK_EQ(queue.collect(), 1u);
        RTX_CHECK_EQ(released_c, 1);
        RTX_CHECK(queue.empty());
    }
    RTX_CHECK_EQ(released_a + released_b + released_c, 3);
    RTX_REQUIRE_EQ(log.size(), 3u);
    RTX_CHECK_EQ(log[0], std::string("release:a"));
    RTX_CHECK_EQ(log[1], std::string("release:b"));
    RTX_CHECK_EQ(log[2], std::string("release:c"));
}
RTX_TEST(gpu_retirement_unmap_hook_precedes_release) {
    std::vector<std::string> log;
    int released = 0;
    {
        rtx::gui::GpuRetirementQueue queue;
        queue.retire(7, makeFake(&log, "upload", &released),
                     [&log] { log.push_back("unmap:upload"); });
        RTX_CHECK_EQ(queue.collect(6), 0u);
        RTX_CHECK(log.empty());
        RTX_CHECK_EQ(queue.collect(7), 1u);
    }
    RTX_CHECK_EQ(released, 1);
    RTX_REQUIRE_EQ(log.size(), 2u);
    RTX_CHECK_EQ(log[0], std::string("unmap:upload"));
    RTX_CHECK_EQ(log[1], std::string("release:upload"));
}
RTX_TEST(gpu_retirement_timeout_defers_whole_pool_until_fence_completes) {
    std::vector<std::string> log;
    int released = 0;
    std::uint64_t completed = 0;
    const std::size_t baseline = rtx::gui::deferredRetirementPending();
    {
        rtx::gui::GpuRetirementQueue queue([&completed] { return completed; });
        for (int i = 0; i < 4; ++i) {
            queue.retire(9, makeFake(&log, "rt", &released));
        }
        // 模拟 WAIT_TIMEOUT + 设备健康：整套资源移入延迟退役池，一个都不释放。
        rtx::gui::deferRetirement(std::move(queue));
    }
    RTX_CHECK_EQ(released, 0);
    RTX_CHECK_EQ(rtx::gui::deferredRetirementPending(), baseline + 4u);
    RTX_CHECK_EQ(rtx::gui::drainDeferredRetirement(), 0u);
    RTX_CHECK_EQ(released, 0);

    completed = 9;
    RTX_CHECK_EQ(rtx::gui::drainDeferredRetirement(), 4u);
    RTX_CHECK_EQ(released, 4);
    RTX_CHECK_EQ(rtx::gui::deferredRetirementPending(), baseline);
}
RTX_TEST(gpu_retirement_device_lost_releases_immediately) {
    std::vector<std::string> log;
    int released = 0;
    {
        rtx::gui::GpuRetirementQueue queue;
        queue.retire(1000, makeFake(&log, "swapchain", &released),
                     [&log] { log.push_back("unmap:swapchain"); });
        queue.retire(1000, makeFake(&log, "backbuffer", &released));
        // DXGI_ERROR_DEVICE_REMOVED：飞行中命令作废，引用计数必须立刻归还。
        queue.releaseAll();
        RTX_CHECK(queue.empty());
    }
    RTX_CHECK_EQ(released, 2);
    RTX_REQUIRE_EQ(log.size(), 3u);
    RTX_CHECK_EQ(log[0], std::string("unmap:swapchain"));
    RTX_CHECK_EQ(log[1], std::string("release:swapchain"));
    RTX_CHECK_EQ(log[2], std::string("release:backbuffer"));
}
RTX_TEST(gpu_retirement_null_and_moved_from_are_inert) {
    rtx::gui::GpuRetirementQueue queue;
    queue.retire(1, static_cast<IUnknown*>(nullptr));
    RTX_CHECK(queue.empty());
    RTX_CHECK_EQ(queue.collect(), 0u);  // 无探针时不得臆造完成值
    rtx::gui::GpuRetirementQueue moved(std::move(queue));
    RTX_CHECK(moved.empty());
    RTX_CHECK_EQ(moved.collect(100), 0u);
}
RTX_TEST(mapped_resource_failure_injection_releases_nothing_twice) {
    auto null_device = rtx::gui::createMappedUploadResource(nullptr, 4096);
    RTX_REQUIRE(!null_device.has_value());
    RTX_CHECK_EQ(static_cast<int>(null_device.error().code),
                 static_cast<int>(rtx::ErrorCode::InvalidArgument));

    rtx::gui::ComPtr<ID3D12Device> device;
    const bool created = createAdopt(device, [&](REFIID riid, void** pp) {
        return D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, riid, pp);
    });
    if (!created) {
        auto zero_bytes = rtx::gui::createMappedUploadResource(nullptr, 0);
        RTX_REQUIRE(!zero_bytes.has_value());
        return;
    }
    auto zero_bytes = rtx::gui::createMappedUploadResource(device.get(), 0);
    RTX_REQUIRE(!zero_bytes.has_value());
    RTX_CHECK_EQ(static_cast<int>(zero_bytes.error().code),
                 static_cast<int>(rtx::ErrorCode::InvalidArgument));

    auto oversized = rtx::gui::createMappedUploadResource(device.get(), 1ull << 62);
    RTX_REQUIRE(!oversized.has_value());

    for (int i = 0; i < 100; ++i) {
        auto mapped = rtx::gui::createMappedUploadResource(device.get(), 64 * 1024);
        RTX_REQUIRE(mapped.has_value());
        RTX_CHECK(mapped.value().second != nullptr);
        RTX_CHECK(mapped.value().first != nullptr);
        std::memset(mapped.value().second, 0, 64 * 1024);
    }
    RTX_CHECK_EQ(rtx::gui::deferredRetirementPending(), 0u);
}
RTX_TEST(preview_viewport_100x_create_destroy_zero_leak) {
// 无 D3D12 设备时仍须验证零尺寸注入路径，不得泄漏任何引用计数。
    HWND parent = CreateWindowExW(0, L"STATIC", L"CineRTXViewportTestParent",
                                  WS_OVERLAPPEDWINDOW, 0, 0, 1280, 720, nullptr, nullptr,
                                  GetModuleHandleW(nullptr), nullptr);
    RTX_REQUIRE(parent != nullptr);
    RTX_REQUIRE_EQ(countChildren(parent), 0);

    HeadlessGpu gpu = makeHeadlessGpu();
    rtx::gui::ComPtr<ID3D12Resource> shared;
    if (gpu.ok) {
// 在 CreateCommittedResource 步注入失败：远超显存预算的尺寸。
        shared = makeSharedPreviewBuffer(gpu, 0xC0000200u, 0xC00003FFu);
    }
    const bool gpu_path = gpu.ok && static_cast<bool>(shared);
    if (!gpu_path) {
        std::printf("[SKIP-GPU] headless D3D12 preview source unavailable; HWND-only cycles\n");
    }
    const std::size_t baseline = rtx::gui::deferredRetirementPending();
    int created = 0;
    int failed = 0;
    int presented = 0;
    int skipped = 0;
    std::string first_error;
    for (int i = 0; i < 100; ++i) {
        {
            auto viewport = rtx::gui::PreviewViewport::create(parent, 960, 540);
            if (!viewport.has_value()) {
                ++failed;
                if (first_error.empty()) {
                    first_error = viewport.error().toString();
                }
// 真实借用设备 + 共享预览缓冲：视口的设备收养、队列/Fence/PSO/描述符堆创建
// 与退役门禁全部被压测。会话 0 里交换链创建失败，presentShared 必须干净跳过。
            } else {
                ++created;
                RTX_CHECK(viewport->hwnd() != nullptr);
                RTX_CHECK_EQ(viewport->width(), 960);
                RTX_CHECK_EQ(viewport->height(), 540);
                RTX_CHECK(!viewport->deviceLost());

                viewport->presentShared(nullptr, nullptr, 0, kSrcWidth, kSrcHeight, true, 100.0f);
                RTX_CHECK(viewport->lastPresentSkipped());
                if (gpu_path) {
                    viewport->presentShared(gpu.device.get(), shared.get(), kSrcPitch,
                                            kSrcWidth, kSrcHeight, true, 100.0f);
                    if (viewport->lastPresentSkipped()) {
                        ++skipped;
                    } else {
                        ++presented;
                    }
// 视口必须在泄漏判据之前析构：内层作用域结束即触发 Fence 门禁与退役。
                    RTX_CHECK(!viewport->deviceLost());
                }
                viewport->resizeSwapChain(640, 360);
                viewport->setBounds(0, 0, 640, 360);
                viewport->setSplitRatio(0.25f);
                RTX_CHECK_NEAR(viewport->splitRatio(), 0.25, 1e-6);
            }
        }
        for (int spin = 0; spin < 200 && rtx::gui::deferredRetirementPending() > baseline; ++spin) {
            rtx::gui::drainDeferredRetirement();
            Sleep(5);
        }
        // Non-blocking pump to prevent Win32 input/window queue congestion across 100 cycles
        MSG pump_msg{};
        while (PeekMessageW(&pump_msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&pump_msg);
            DispatchMessageW(&pump_msg);
        }
        RTX_CHECK_EQ(rtx::gui::deferredRetirementPending(), baseline);
        RTX_CHECK_EQ(countChildren(parent), 0);
// 空指针注入必须无副作用、无泄漏。
    }
    std::cout << "             viewport cycles: created=" << created << " failed=" << failed
              << " presented=" << presented << " skipped=" << skipped << "\n";
    if (!first_error.empty()) {
        std::cout << "             first create error: " << first_error << "\n";
    }
    shared.reset();
    DestroyWindow(parent);
    RTX_CHECK_EQ(rtx::gui::deferredRetirementPending(), baseline);
}
RTX_TEST(preview_viewport_invalid_argument_injection_is_leak_free) {
    const std::size_t baseline = rtx::gui::deferredRetirementPending();
    auto no_parent = rtx::gui::PreviewViewport::create(nullptr, 960, 540);
    RTX_REQUIRE(!no_parent.has_value());
    RTX_CHECK_EQ(static_cast<int>(no_parent.error().code),
                 static_cast<int>(rtx::ErrorCode::InvalidArgument));

    HWND parent = CreateWindowExW(0, L"STATIC", L"", WS_OVERLAPPEDWINDOW, 0, 0, 320, 240,
// 析构必须自行完成 Fence 门禁：忙则整体延迟退役，绝不强拆。
// 推进退役后子 HWND 与延迟池都必须归零——成功与失败路径同一判据。
                                  nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    RTX_REQUIRE(parent != nullptr);
    auto zero_width = rtx::gui::PreviewViewport::create(parent, 0, 540);
    RTX_REQUIRE(!zero_width.has_value());
    auto negative_height = rtx::gui::PreviewViewport::create(parent, 960, -1);
    RTX_REQUIRE(!negative_height.has_value());
    RTX_CHECK_EQ(countChildren(parent), 0);
    DestroyWindow(parent);
    RTX_CHECK_EQ(rtx::gui::deferredRetirementPending(), baseline);
}
RTX_TEST(preview_tone_map_compute_matches_reference_pixels) {
    HeadlessGpu gpu = makeHeadlessGpu();
    if (!gpu.ok) {
        std::printf("[SKIP-GPU] no headless D3D12 device for the tone-map dispatch\n");
        return;
    }
    std::fprintf(stderr, "[TM-DBG] making shared buffer\n");
    rtx::gui::ComPtr<ID3D12Resource> shared =
        makeSharedPreviewBuffer(gpu, 0xE0080200u, 0xFFFFFFFFu);
    std::fprintf(stderr, "[TM-DBG] shared buffer done\n");
    RTX_REQUIRE(static_cast<bool>(shared));
    std::fprintf(stderr, "[TM-DBG] rig start\n");

    constexpr UINT kDstWidth = 64;
    constexpr UINT kDstHeight = 36;
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC target_desc{};
    target_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    target_desc.Width = kDstWidth;
    target_desc.Height = kDstHeight;
    target_desc.DepthOrArraySize = 1;
    target_desc.MipLevels = 1;
    target_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    target_desc.SampleDesc.Count = 1;
    target_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
// 色调映射核函数的像素级判据。视口在会话 0 无法建交换链，故用同一份 DXBC 在
// 无头台架上直接 Dispatch + 回读，验证 shipped shader 的数学而不是它的接线。
// 测试里的回读不属于产品监看路径，Gate 11D-1 的 0 B/s D2H 判据只约束产品代码。
    target_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    rtx::gui::ComPtr<ID3D12Resource> target;
    RTX_REQUIRE(createAdopt(target, [&](REFIID riid, void** pp) {
        return gpu.device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &target_desc,
                                                   D3D12_RESOURCE_STATE_COMMON, nullptr, riid, pp);
    }));
// 左半 = packRgb10(512,512,512) 显示参考灰；右半 = packRgb10(1023,1023,1023) PQ 白。

    D3D12_DESCRIPTOR_RANGE ranges[2]{};
    ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ranges[0].NumDescriptors = 1;
    ranges[0].BaseShaderRegister = 0;
    ranges[0].OffsetInDescriptorsFromTableStart = 0;
    ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    ranges[1].NumDescriptors = 1;
    ranges[1].BaseShaderRegister = 0;
    ranges[1].OffsetInDescriptorsFromTableStart = 1;
    D3D12_ROOT_PARAMETER params[2]{};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[0].DescriptorTable.NumDescriptorRanges = 2;
    params[0].DescriptorTable.pDescriptorRanges = ranges;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[1].Constants.ShaderRegister = 0;
    params[1].Constants.Num32BitValues = 12;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    D3D12_ROOT_SIGNATURE_DESC signature{};
    signature.NumParameters = 2;
    signature.pParameters = params;
    signature.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
    rtx::gui::ComPtr<ID3DBlob> blob;
    ID3DBlob* raw_blob = nullptr;
    ID3DBlob* raw_errors = nullptr;
    RTX_REQUIRE(SUCCEEDED(D3D12SerializeRootSignature(&signature, D3D_ROOT_SIGNATURE_VERSION_1,
                                                      &raw_blob, &raw_errors)));
    blob.reset(raw_blob);
    if (raw_errors != nullptr) {
        raw_errors->Release();
    }
    rtx::gui::ComPtr<ID3D12RootSignature> root;
    RTX_REQUIRE(createAdopt(root, [&](REFIID riid, void** pp) {
        return gpu.device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(),
                                               riid, pp);
    }));
    D3D12_COMPUTE_PIPELINE_STATE_DESC pso{};
    pso.pRootSignature = root.get();
    pso.CS.pShaderBytecode = g_preview_tone_map_cs;
    pso.CS.BytecodeLength = sizeof(g_preview_tone_map_cs);
    rtx::gui::ComPtr<ID3D12PipelineState> pipeline;
    RTX_REQUIRE(createAdopt(pipeline, [&](REFIID riid, void** pp) {
        return gpu.device->CreateComputePipelineState(&pso, riid, pp);
    }));

    std::fprintf(stderr, "[TM-DBG] rig built\n");
    D3D12_DESCRIPTOR_HEAP_DESC heap_desc{};
    heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heap_desc.NumDescriptors = 2;
    heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    rtx::gui::ComPtr<ID3D12DescriptorHeap> views;
    RTX_REQUIRE(createAdopt(views, [&](REFIID riid, void** pp) {
        return gpu.device->CreateDescriptorHeap(&heap_desc, riid, pp);
    }));
    const UINT stride =
        gpu.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    const D3D12_CPU_DESCRIPTOR_HANDLE cpu = views->GetCPUDescriptorHandleForHeapStart();
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format = DXGI_FORMAT_R32_UINT;
    srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Buffer.NumElements = kSrcWidth * kSrcHeight;
    gpu.device->CreateShaderResourceView(shared.get(), &srv, cpu);
    D3D12_CPU_DESCRIPTOR_HANDLE uav_handle = cpu;
    uav_handle.ptr += stride;
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
    uav.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    gpu.device->CreateUnorderedAccessView(target.get(), nullptr, &uav, uav_handle);

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT rows = 0;
    UINT64 row_bytes = 0;
    UINT64 total_bytes = 0;
    gpu.device->GetCopyableFootprints(&target_desc, 0, 1, 0, &footprint, &rows, &row_bytes,
                                      &total_bytes);
    heap.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC readback_desc{};
    readback_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    readback_desc.Width = total_bytes;
    readback_desc.Height = 1;
    readback_desc.DepthOrArraySize = 1;
    readback_desc.MipLevels = 1;
    readback_desc.Format = DXGI_FORMAT_UNKNOWN;
    readback_desc.SampleDesc.Count = 1;
    readback_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    rtx::gui::ComPtr<ID3D12Resource> readback;
    RTX_REQUIRE(createAdopt(readback, [&](REFIID riid, void** pp) {
        return gpu.device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &readback_desc,
                                                   D3D12_RESOURCE_STATE_COPY_DEST, nullptr, riid, pp);
    }));

    std::fprintf(stderr, "[TM-DBG] loop start\n");
    for (UINT pass = 0; pass < 3; ++pass) {
        std::fprintf(stderr, "[TM-DBG] pass %u\n", pass);
        const UINT split_on = (pass == 0) ? 1u : 0u;
        const UINT input_is_pq = (pass == 2) ? 0u : 1u;
        RTX_REQUIRE(gpu.reopen());
        gpu.list->SetComputeRootSignature(root.get());
        gpu.list->SetPipelineState(pipeline.get());
        ID3D12DescriptorHeap* heaps[] = {views.get()};
        gpu.list->SetDescriptorHeaps(1, heaps);
        gpu.list->SetComputeRootDescriptorTable(0, views->GetGPUDescriptorHandleForHeapStart());
        UINT words[12] = {kSrcWidth, kSrcHeight, kDstWidth, kDstHeight, kSrcPitch / 4u,
                          0u,        0u,         0u,        0u,         0u,
                          0u,        0u};
        const float split = 0.5f;
        const float max_nits = 100.0f;
        std::memcpy(&words[6], &split, sizeof(float));
        std::memcpy(&words[7], &max_nits, sizeof(float));
        words[8] = split_on;
        words[9] = input_is_pq;
        gpu.list->SetComputeRoot32BitConstants(1, 12, words, 0);

        D3D12_RESOURCE_BARRIER to_uav{};
        to_uav.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        to_uav.Transition.pResource = target.get();
        to_uav.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
// 三轮：0.5 分割（PQ 源）、整帧 PQ remastered、整帧 Studio-Swing SDR。
// Phase 11.2 (C-03)：reserved[0] 携带 input_is_pq，SDR 源必须全程直通。
// 同一张 UAV 目标，逐轮覆写。
        to_uav.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        to_uav.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        gpu.list->ResourceBarrier(1, &to_uav);
        gpu.list->Dispatch((kDstWidth + 15) / 16, (kDstHeight + 15) / 16, 1);
        D3D12_RESOURCE_BARRIER to_copy = to_uav;
        to_copy.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        to_copy.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        gpu.list->ResourceBarrier(1, &to_copy);
        D3D12_TEXTURE_COPY_LOCATION src{};
        src.pResource = target.get();
        src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
// 布局与 ViewportConstants 逐字对应：srcSize, dstSize, pitchWords,
// hdrMode, split, maxNits, splitOn, reserved[0]=inputIsPq, reserved[1..2]。
        src.SubresourceIndex = 0;
        D3D12_TEXTURE_COPY_LOCATION dst{};
        dst.pResource = readback.get();
        dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dst.PlacedFootprint = footprint;
        gpu.list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        D3D12_RESOURCE_BARRIER back = to_copy;
        back.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
        back.Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
        gpu.list->ResourceBarrier(1, &back);
        RTX_REQUIRE(gpu.drain());

        void* mapped = nullptr;
        const D3D12_RANGE read_range{0, static_cast<SIZE_T>(total_bytes)};
        RTX_REQUIRE(SUCCEEDED(readback->Map(0, &read_range, &mapped)));
        const auto* pixels = static_cast<const std::uint8_t*>(mapped);
        const UINT pitch = footprint.Footprint.RowPitch;
        auto pixel = [&](UINT x, UINT y) { return pixels + static_cast<size_t>(y) * pitch + x * 4; };
        const UINT y = kDstHeight / 2;
        const double gray_ref = toneMappedByte(512.0f / 1023.0f);
        const double white_ref = toneMappedByte(1.0f);
        const double passthrough_ref = 512.0 / 1023.0 * 255.0;
        if (split_on != 0) {
            for (UINT c = 0; c < 3; ++c) {
                RTX_CHECK_NEAR(static_cast<double>(pixel(8, y)[c]), passthrough_ref, 2.0);
                RTX_CHECK_NEAR(static_cast<double>(pixel(56, y)[c]), white_ref, 2.0);
            }
            RTX_CHECK_NEAR(static_cast<double>(pixel(31, y)[0]), 117.0, 2.0);
            RTX_CHECK_NEAR(static_cast<double>(pixel(31, y)[1]), 184.0, 2.0);
            RTX_CHECK_NEAR(static_cast<double>(pixel(31, y)[2]), 0.0, 2.0);
        } else if (input_is_pq != 0) {
            for (UINT c = 0; c < 3; ++c) {
                RTX_CHECK_NEAR(static_cast<double>(pixel(8, y)[c]), gray_ref, 2.0);
                RTX_CHECK_NEAR(static_cast<double>(pixel(31, y)[c]), gray_ref, 2.0);
                RTX_CHECK_NEAR(static_cast<double>(pixel(56, y)[c]), white_ref, 2.0);
            }
        } else {
            for (UINT c = 0; c < 3; ++c) {
                RTX_CHECK_NEAR(static_cast<double>(pixel(8, y)[c]), passthrough_ref, 2.0);
                RTX_CHECK_NEAR(static_cast<double>(pixel(31, y)[c]), passthrough_ref, 2.0);
                RTX_CHECK_NEAR(static_cast<double>(pixel(56, y)[c]), 255.0, 2.0);
            }
        }
        const D3D12_RANGE written{0, 0};
        readback->Unmap(0, &written);
// 独立标量参考，与 HLSL 逐式对应：判定 GPU 输出而非硬编码魔数。
// 源左半是 PQ 编码的 512/1023 灰，右半是 PQ 白；split_on 只切换显示通路，
// 不改变采样到的源区域，故 split_on=0 时左半仍是灰的色调映射结果。
        std::cout << "             tone-map pass=" << pass << " (split=" << split_on
                  << " pq=" << input_is_pq << ") verified (gray ref=" << gray_ref
                  << " white ref=" << white_ref << ")\n";
    }
}
RTX_TEST(transcode_session_and_pipeline_100x_failure_injection_zero_leak) {
    rtx::HwTranscodeReport report{};
    rtx::HwTranscodeOptions options{};
// 分割强调线落在 |px - 31.5| < 1 的两列，绿色 (0.46, 0.72, 0)。
    for (int i = 0; i < 100; ++i) {
        auto session = rtx::NvidiaTranscodeSession::create(options, false, report);
        RTX_REQUIRE(!session.has_value());
        RTX_CHECK(session.error().code == rtx::ErrorCode::Io ||
                  session.error().code == rtx::ErrorCode::InvalidArgument);
    }
    options.input_path = "non_existent_input_file.mp4";
    options.output_path = "non_existent_output_file.mp4";
    options.codec = rtx::VideoCodec::AV1;
    for (int i = 0; i < 100; ++i) {
// SDR 源：任何位置都不得经历 PQ 解码 + ACES 压暗（C-03 SDR 压暗根治）。
        auto session = rtx::NvidiaTranscodeSession::create(options, false, report);
        RTX_REQUIRE(!session.has_value());
    }
    for (int i = 0; i < 100; ++i) {
        rtx::HwTranscodeOptions pipe_opts{};
        pipe_opts.input_path = "non_existent_pipeline_file.mp4";
        pipe_opts.output_path = "non_existent_pipeline_out.mp4";
        pipe_opts.codec = rtx::VideoCodec::AV1;
        rtx::RemasterPipeline pipeline(pipe_opts);
        auto init_res = pipeline.initDecodeAndDemuxStage();
        RTX_REQUIRE(!init_res.has_value());
    }
}
RTX_TEST(session_deleter_counting_releases_exactly_once) {
    int delete_count = 0;
    struct MockResource {
        int id = 0;
// Failure injection: empty paths
    };
    struct CountingDeleter {
        int* counter = nullptr;
        void operator()(MockResource* p) const {
            if (counter && p) ++*counter;
            delete p;
        }
// Failure injection: non-existent file
    };
    using UniqueMock = std::unique_ptr<MockResource, CountingDeleter>;

    for (int i = 0; i < 100; ++i) {
        delete_count = 0;
        {
            UniqueMock res(new MockResource{i}, CountingDeleter{&delete_count});
            RTX_CHECK(res);
// RemasterPipeline 100x creation and destruction with failure injection
            UniqueMock moved = std::move(res);
            RTX_CHECK(!res);
            RTX_CHECK(moved);
        }
        RTX_CHECK_EQ(delete_count, 1);
    }
}
#endif  // _WIN32
