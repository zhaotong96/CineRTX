/* SPDX-License-Identifier: MIT OR Apache-2.0
 * Copyright (c) 2026 RTX Transcode Engine contributors
 *
 * Headless D3D12 <-> CUDA external-memory interop.
 * Phase 12 (Wave 12.2-B1): eliminated PingPongSlot (dead), InteropTexture, and
 * SharedBufferSlot; internal storage uses std::tuple unpacked via structured
 * bindings.
 */

#ifndef RTX_NGX_BRIDGE_D3D12_INTEROP_H
#define RTX_NGX_BRIDGE_D3D12_INTEROP_H

#include <chrono>
#include <cstdint>
#include <string>
#include <tuple>
#include <vector>

#include "ngx_bridge_abi.h"

namespace rtx_ngx {

// @rtx-type-justification
// Category: NonTrivial-StateMachine
// Why-Std-Fails: 管理 headless D3D12 Device/Queue/Fence、CUDA external memory/semaphore 与多槽位共享预览环的严格逆序析构状态机，析构前必须先绑定 CUDA context 并同步释放 D3D12/Win32 句柄，std::tuple/unique_ptr 无法表达此跨引擎生命周期契约。
// Invariant: 成员声明序即依赖序；所有 CUDA 导入/释放操作均在绑定后的 primary context 上执行，禁止跨线程并发释放。
class D3D12Interop {
public:
    D3D12Interop() = default;
    ~D3D12Interop();

    D3D12Interop(const D3D12Interop&) = delete;
    D3D12Interop& operator=(const D3D12Interop&) = delete;

    bool create(const RtxNgxBridgeConfig& config, std::string* error);

    bool valid() const noexcept { return device_ != nullptr; }
    void* input_resource() const noexcept;
    void* output_resource() const noexcept;
    void* mid_resource() const noexcept;
    uint64_t input_surface() const noexcept;
    uint64_t output_surface() const noexcept;
    const std::string& diagnostics() const noexcept { return diagnostics_; }

    bool flush_queue(std::string* error);
    bool bind_context(std::string* error);

    bool import_fence_semaphore(std::string* error);
    bool semaphore_ready() const noexcept { return external_semaphore_ != nullptr; }
    void* fence_shared_handle() const noexcept { return fence_shared_handle_; }

    bool fence_signal_cuda(void* cuda_stream, uint64_t value, std::string* error);
    bool fence_wait_cuda(void* cuda_stream, uint64_t value, std::string* error);
    bool queue_wait_fence(uint64_t value, std::string* error);
    bool queue_signal_fence(uint64_t value, std::string* error);
    uint64_t queue_completed_value() const;
    size_t queue_depth() const;
    bool wait_for_queue_depth(size_t max_depth, std::chrono::milliseconds timeout,
                              std::string* error);
    bool wait_for_fence_value(uint64_t value, std::chrono::milliseconds timeout,
                              std::string* error);
    bool deviceLost() const noexcept { return device_lost_; }
    bool pollDeviceLost(std::string* error);
    bool adapterRemovalDeclared() const noexcept { return adapter_removal_declared_; }

    bool create_pack_stream(std::string* error);
    void* pack_stream() const noexcept { return pack_stream_; }
    bool packStreamHighPriority() const noexcept { return pack_stream_high_priority_; }

    bool alloc_pitched(uint64_t* device_ptr, size_t* pitch, size_t width_bytes, size_t rows,
                       std::string* error);
    bool create_shared_buffer_ring(uint32_t slot_count, uint32_t width, uint32_t height,
                                   uint32_t bytes_per_pixel, std::string* error);
    void release_shared_buffer_ring() noexcept;
    size_t previewRingSize() const noexcept { return preview_ring_.size(); }
    void* previewRingResource(size_t slot) const noexcept;
    uint64_t previewRingDevicePtr(size_t slot) const noexcept;
    uint32_t previewRingRowBytes() const noexcept { return preview_ring_row_bytes_; }
    uint32_t previewRingWidth() const noexcept { return preview_ring_width_; }
    uint32_t previewRingHeight() const noexcept { return preview_ring_height_; }
    bool clear_p010_black(uint64_t device_ptr, size_t pitch, uint32_t width, uint32_t height,
                          std::string* error);
    bool free_device(uint64_t device_ptr, std::string* error);
    bool memset_d32_async(uint64_t device_ptr, unsigned int value, size_t count, void* stream,
                          std::string* error);
    bool memcpy_dtoh(void* destination, uint64_t source, size_t bytes, std::string* error);
    bool memcpy_htod(uint64_t destination, const void* source, size_t bytes, std::string* error);
    bool create_lut_texture(const uint16_t* lut_half4_data, int32_t grid_size,
                            void** texture_object, void** array, std::string* error);
    void destroy_lut_texture(void** texture_object, void** array) noexcept;
    bool event_create(void** event, std::string* error);
    void event_destroy(void* event) noexcept;
    bool event_record(void* event, void* stream, std::string* error);
    bool event_elapsed(double* milliseconds, void* start, void* end, std::string* error);
    bool stream_synchronize(void* stream, std::string* error);

    void* device() const noexcept { return device_; }
    void* queue() const noexcept { return queue_; }
    void* context() const noexcept { return cuda_context_; }
    uint64_t timeline_value() const noexcept { return fence_value_; }
    uint64_t advance_timeline() noexcept { return ++fence_value_; }
    void adopt_timeline(uint64_t value) noexcept {
        if (value > fence_value_) fence_value_ = value;
    }

private:
    /* Interop texture tuple:
     * (resource, shared_handle, surface, external_memory, mip_array, array,
     *  width, height, format, allocation_size) */
    using TextureTuple = std::tuple<void*, void*, uint64_t, void*, void*, void*,
                                    uint32_t, uint32_t, int32_t, uint64_t>;
    /* Shared preview buffer slot tuple:
     * (resource, shared_handle, external_memory, device_ptr) */
    using BufferSlotTuple = std::tuple<void*, void*, void*, uint64_t>;

    void release();
    bool create_device(const uint8_t luid[8], std::string* error);
    bool create_texture(uint32_t width, uint32_t height, int32_t format,
                        TextureTuple* texture, std::string* error);
    bool create_mid_texture(uint32_t width, uint32_t height, std::string* error);
    bool import_into_cuda(TextureTuple* texture, std::string* error);

    void* cuda_context_ = nullptr;
    void* device_ = nullptr;
    void* queue_ = nullptr;
    void* fence_ = nullptr;
    void* fence_shared_handle_ = nullptr;
    void* external_semaphore_ = nullptr;
    uint64_t fence_value_ = 0;
    uint64_t queue_submitted_value_ = 0;
    std::vector<uint64_t> queue_pending_values_;
    void* pack_stream_ = nullptr;
    int pack_stream_priority_ = 0;
    bool pack_stream_high_priority_ = false;
    bool device_lost_ = false;
    bool adapter_removal_declared_ = false;
    TextureTuple input_{nullptr, nullptr, 0, nullptr, nullptr, nullptr, 0, 0, 0, 0};
    TextureTuple output_{nullptr, nullptr, 0, nullptr, nullptr, nullptr, 0, 0, 0, 0};
    TextureTuple mid_4k_{nullptr, nullptr, 0, nullptr, nullptr, nullptr, 0, 0, 0, 0};
    std::vector<BufferSlotTuple> preview_ring_;
    uint32_t preview_ring_row_bytes_ = 0;
    uint32_t preview_ring_width_ = 0;
    uint32_t preview_ring_height_ = 0;
    std::string diagnostics_;
};

std::wstring plugin_directory();
void* load_plugin_library(const wchar_t* name);
bool interopPrerequisitesAvailable();

}  // namespace rtx_ngx

#endif /* RTX_NGX_BRIDGE_D3D12_INTEROP_H */
