/* SPDX-License-Identifier: MIT OR Apache-2.0
 * Copyright (c) 2026 RTX Transcode Engine contributors
 *
 * Headless D3D12 <-> CUDA external-memory interop.
 *
 * The bridge never creates a window, a swap chain, or an implicit adapter 0.
 * It matches the DXGI adapter to the CUDA device by the 8-byte LUID that
 * cuDeviceGetLuid reports, allocates committed shared textures, exports Win32
 * handles, and imports them into CUDA as dedicated external memory bound to a
 * CUsurfObject.  Surface objects make the driver resolve the D3D12 tiled layout,
 * so the CUDA side never sees a swizzled pointer.
 *
 * d3d12.dll and dxgi.dll are loaded with LoadLibraryExW: the bridge carries no
 * static Direct3D import either.
 */

#ifndef RTX_NGX_BRIDGE_D3D12_INTEROP_H
#define RTX_NGX_BRIDGE_D3D12_INTEROP_H

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "ngx_bridge_abi.h"

namespace rtx_ngx {

struct InteropTexture {
    void* resource = nullptr;        // ID3D12Resource*
    void* shared_handle = nullptr;   // Win32 HANDLE
    uint64_t surface = 0;            // CUsurfObject
    void* external_memory = nullptr; // CUexternalMemory
    void* mip_array = nullptr;       // CUmipmappedArray
    void* array = nullptr;           // CUarray
    uint32_t width = 0;
    uint32_t height = 0;
    int32_t format = 0;              // RtxPixelFormat
    uint64_t allocation_size = 0;
};

// Owns the headless D3D12 device, the direct command queue and the two shared
// ping-pong textures that NGX consumes and produces.
class D3D12Interop {
public:
    D3D12Interop() = default;
    ~D3D12Interop();

    D3D12Interop(const D3D12Interop&) = delete;
    D3D12Interop& operator=(const D3D12Interop&) = delete;

    bool create(const RtxNgxBridgeConfig& config, std::string* error);
    void destroy();

    bool valid() const noexcept { return device_ != nullptr; }
    InteropTexture& input() noexcept { return input_; }
    InteropTexture& output() noexcept { return output_; }
    // Internal 4K SDR intermediate texture (R8G8B8A8_UNORM) between VSR and
    // TrueHDR (expert directive B-2.1).  Pure D3D12: no shared handle, no CUDA
    // import.  Valid only when the configuration upscales.
    InteropTexture& mid() noexcept { return mid_4k_; }
    const std::string& diagnostics() const noexcept { return diagnostics_; }

    // Fills a texture from host memory through an upload buffer.  Used by the
    // interop self-test and by the CPU reference path.
    bool upload(InteropTexture& texture, const void* data, size_t size, std::string* error);
    // Copies a texture back into host memory through a readback buffer.
    // `queue_wait_value` optionally records a queue-side wait on the shared
    // fence before the copy, so the GPU (not the CPU) orders a preceding CUDA
    // signal against this readback.
    bool download(InteropTexture& texture, void* data, size_t size, std::string* error,
                  uint64_t queue_wait_value = 0);
    bool flush_queue(std::string* error);

    // Re-binds the CUDA context that owns the imported arrays.  D3D12 device
    // creation and the driver's module loader can leave another context
    // current, which makes cuMemcpy2D fail with CUDA_ERROR_INVALID_CONTEXT.
    bool bind_context(std::string* error);

    // ---------------------------------------------------------------------
    // GPU hardware fence interop (expert hardening item 2, RTX verdict 3.2)
    //
    // The shared 64-bit ID3D12Fence is exported as a Win32 handle and
    // imported into CUDA as an external semaphore, so the CUDA pre-process
    // stream, the D3D12 direct queue and the CUDA post-process pack stream
    // hand work over purely on the GPU: no CPU thread ever blocks on a
    // cross-engine boundary.  Returns false when the driver does not provide
    // the external-semaphore API; callers then fall back to a CPU wait.
    // ---------------------------------------------------------------------
    bool import_fence_semaphore(std::string* error);
    bool semaphore_ready() const noexcept { return external_semaphore_ != nullptr; }
    void* fence_shared_handle() const noexcept { return fence_shared_handle_; }

    // Enqueues a signal of `value` on the shared fence from a CUDA stream.
    bool fence_signal_cuda(void* cuda_stream, uint64_t value, std::string* error);
    // Enqueues a wait on `value` of the shared fence from a CUDA stream.
    bool fence_wait_cuda(void* cuda_stream, uint64_t value, std::string* error);
    // D3D12 queue side of the same timeline fence.
    bool queue_wait_fence(uint64_t value, std::string* error);
    bool queue_signal_fence(uint64_t value, std::string* error);
    // Last completed value of the shared fence (CPU-readable).  The W5/W6
    // queue-depth cap compares it against the submitted value.
    uint64_t queue_completed_value() const;
    // Number of command-list submissions whose completion the queue has not
    // observed yet.  Anti-TDR mechanism 2 caps this at two.
    size_t queue_depth() const;
    // Blocks until the queue has retired enough work to stay below
    // `max_depth`.  The wait is a Win32 event on the ID3D12Fence and never
    // touches pixel data, so the zero-copy bus audit is unaffected.
    bool wait_for_queue_depth(size_t max_depth, std::chrono::milliseconds timeout,
                              std::string* error);
    // Waits until the shared fence reaches `value`.  Used to guarantee that a
    // command allocator is safe to Reset before the next frame is recorded; a
    // value of 0 means no outstanding submission and returns immediately.
    bool wait_for_fence_value(uint64_t value, std::chrono::milliseconds timeout,
                              std::string* error);
    bool deviceLost() const noexcept { return device_lost_; }
    // Queries GetDeviceRemovedReason and latches the result.  Returns true
    // when the adapter has been removed/reset.
    bool pollDeviceLost(std::string* error);
    bool adapterRemovalDeclared() const noexcept { return adapter_removal_declared_; }

    // Anti-TDR mechanism 3: a CUDA stream at the driver's highest priority for
    // the P010 pack stage, so the tail of the pipeline drains ahead of any
    // other CUDA work.  Priority inversion would otherwise let a lower-priority
    // decode copy keep the pack kernel queued past the frame budget.
    bool create_pack_stream(std::string* error);
    void* pack_stream() const noexcept { return pack_stream_; }
    bool packStreamHighPriority() const noexcept { return pack_stream_high_priority_; }

    // ---------------------------------------------------------------------
    // Remaster support (P0-1.4): pitched device buffers for the bridge-owned
    // P010 output and CUDA events for per-stage GPU timing.  All of them bind
    // the retained primary context first, mirroring every other entry point.
    // ---------------------------------------------------------------------
    bool alloc_pitched(uint64_t* device_ptr, size_t* pitch, size_t width_bytes, size_t rows,
                       std::string* error);
    bool free_device(uint64_t device_ptr, std::string* error);
    bool event_create(void** event, std::string* error);
    void event_destroy(void* event) noexcept;
    bool event_record(void* event, void* stream, std::string* error);
    bool event_elapsed(double* milliseconds, void* start, void* end, std::string* error);
    bool stream_synchronize(void* stream, std::string* error);

    void* device() const noexcept { return device_; }
    void* queue() const noexcept { return queue_; }
    // Current value of the shared timeline fence (advanced by every queue
    // signal).  Callers pick the next CUDA-side signal value from it.
    uint64_t timeline_value() const noexcept { return fence_value_; }
    // Advances the shared timeline and returns the new value.  Both the queue
    // signals and the CUDA external-semaphore signals MUST draw their values
    // from here: a CUDA signal that collides with a later queue signal would
    // satisfy the queue's own CPU wait early and race the readback.
    uint64_t advance_timeline() noexcept { return ++fence_value_; }
    // Raises the timeline to a value chosen outside advance_timeline (the NGX
    // evaluator must signal exactly input_fence_value + 1 to the core, which
    // may be ahead of this object's own counter).  Values never move backwards.
    void adopt_timeline(uint64_t value) noexcept {
        if (value > fence_value_) {
            fence_value_ = value;
        }
    }

private:
    bool create_device(const uint8_t luid[8], std::string* error);
    bool create_texture(uint32_t width, uint32_t height, int32_t format,
                        InteropTexture* texture, std::string* error);
    bool create_mid_texture(uint32_t width, uint32_t height, std::string* error);
    bool import_into_cuda(InteropTexture* texture, std::string* error);

    void* cuda_context_ = nullptr;
    void* device_ = nullptr;
    void* queue_ = nullptr;
    void* fence_ = nullptr;
    // Win32 handle of the shared fence and its CUDA external-semaphore twin.
    void* fence_shared_handle_ = nullptr;
    void* external_semaphore_ = nullptr;
    uint64_t fence_value_ = 0;
    uint64_t queue_submitted_value_ = 0;
    // Signal values of queue submissions whose completion is not yet observed.
    // Pruned on every new submission, so the vector length stays at the live
    // queue depth.
    std::vector<uint64_t> queue_pending_values_;
    void* pack_stream_ = nullptr;
    int pack_stream_priority_ = 0;
    bool pack_stream_high_priority_ = false;
    bool device_lost_ = false;
    bool adapter_removal_declared_ = false;
    InteropTexture input_{};
    InteropTexture output_{};
    InteropTexture mid_4k_{};
    std::string diagnostics_;
};

// End-to-end check on the live GPU: D3D12 writes a known gradient into a shared
// RGBA16F texture, CUDA reads it through the imported surface, writes it back,
// and D3D12 reads the result.  Returns RTX_OK only when every sample matches.
RtxResult run_interop_selftest(uint32_t width, uint32_t height, char* detail,
                               size_t detail_capacity);

// ---------------------------------------------------------------------------
// Proprietary SDK plugin isolation (expert directive B-1, RTX verdict 5.1)
//
// The proprietary RTX Video SDK binaries are deployed either in the bridge's
// own directory (build/external/plugins/nvidia_rtx, <app_root>/plugins/nvidia_rtx)
// or under <app_root>/plugins/nvidia_rtx when the bridge sits at the app root;
// overridable with RTX_NGX_PLUGIN_DIR so the open-source package ships no
// proprietary bytes.  Plugin DLLs are loaded by full path with
// LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32 so their
// dependencies resolve only next to them and in System32, which blocks
// current-directory DLL planting.
// ---------------------------------------------------------------------------
std::wstring plugin_directory();
void* load_plugin_library(const wchar_t* name);

// Reports whether the interop prerequisites (d3d12.dll, dxgi.dll, nvcuda.dll)
// are loadable.  A capability query must not create a device, so this is the
// honest answer for rtx_ngx_bridge_query_caps: the interop layer (W4) is
// independent of the proprietary RTX Video SDK (W5).
bool interopPrerequisitesAvailable();

}  // namespace rtx_ngx

#endif /* RTX_NGX_BRIDGE_D3D12_INTEROP_H */
