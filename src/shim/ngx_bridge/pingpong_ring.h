/* SPDX-License-Identifier: MIT OR Apache-2.0
 * Copyright (c) 2026 RTX Transcode Engine contributors
 *
 * Two-slot even/odd ping-pong scheduler (W8-2).
 *
 * The ring owns the parity contract, the fence bookkeeping and the event
 * configuration; the D3D12 resources, CUDA arrays and CUevent handles are
 * filled in by D3D12Interop.  Keeping the scheduling decision in a dependency-
 * free object is what makes "the CPU never blocks on a cross-engine boundary"
 * a testable property rather than a claim in a comment.
 *
 * Contract:
 *   * frame N uses slot N & 1;
 *   * before slot S is reused, the GPU (not the CPU) waits on the completion
 *     event of the previous use of the same parity, i.e. frame N-2;
 *   * every CUevent is created with CU_EVENT_DISABLE_TIMING unless the caller
 *     explicitly enabled tracing, which caps the per-event synchronisation cost
 *     at < 0.5 us instead of the phase-2 2.424 ms spike;
 *   * hostWaits() must stay 0 for the whole run (Gate 3.3).
 */

#ifndef RTX_NGX_BRIDGE_PINGPONG_RING_H
#define RTX_NGX_BRIDGE_PINGPONG_RING_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace rtx_ngx {

// Mirrors the slot layout the phase-3 spec prescribes.  Every pointer is opaque
// here: the ring never dereferences a D3D12 or CUDA object.
struct PingPongSlot {
    void* cmd_allocator = nullptr;      // ID3D12CommandAllocator*
    void* shared_input = nullptr;       // ID3D12Resource*, 1080p R10G10B10A2
    void* shared_output = nullptr;      // ID3D12Resource*, 4K RGBA16F
    void* cuda_in_array = nullptr;      // CUmipmappedArray
    uint64_t cuda_in_surf = 0;          // CUsurfObject
    void* cuda_out_array = nullptr;     // CUmipmappedArray
    uint64_t cuda_out_surf = 0;         // CUsurfObject
    uint64_t p010_devptr = 0;           // CUdeviceptr of the packed output
    uint64_t d3d12_fence_val = 0;       // last submitted fence value
    void* encode_done_ev = nullptr;     // CUevent
    void* pack_done_ev = nullptr;       // CUevent
    uint32_t frames_served = 0;
};

// CU_EVENT_DISABLE_TIMING is the default; Default is only used with --trace.
enum class PingPongEventMode : uint8_t { DisableTiming = 0, Default = 1 };

class PingPongRing {
public:
    PingPongRing() = default;

    // Prepares both slots.  `mode` selects the CUevent creation flags.
    bool initialize(PingPongEventMode mode = PingPongEventMode::DisableTiming,
                    std::string* error = nullptr);
    void shutdown();
    bool ready() const noexcept { return ready_; }

    // Parity contract: slot index is frame_index & 1.
    static size_t slotIndexFor(int64_t frame_index) noexcept { return static_cast<size_t>(frame_index & 1); }

    PingPongSlot& slotFor(int64_t frame_index) noexcept { return slots_[slotIndexFor(frame_index)]; }
    const PingPongSlot& slotFor(int64_t frame_index) const noexcept {
        return slots_[slotIndexFor(frame_index)];
    }

    // Returns the fence value the GPU must wait on before slot N is rewritten
    // (the completion value of frame N-2), or 0 when the slot is still free.
    // Never blocks: the wait is enqueued on the stream, not performed here.
    uint64_t prepareSlot(int64_t frame_index);

    // Records that the GPU work for `frame_index` was submitted with
    // `fence_value`.  The next reuse of the same parity waits for it.
    void completeSlot(int64_t frame_index, uint64_t fence_value);

    // Scene-cut / static-frame duplicate bypass on host-visible buffers.  The
    // GPU path uses cuMemcpyDtoDAsync instead; both bump duplicateCount().
    bool duplicateFrame(const void* previous, void* current, size_t bytes);

    bool eventTimingDisabled() const noexcept {
        return mode_ == PingPongEventMode::DisableTiming;
    }
    PingPongEventMode eventMode() const noexcept { return mode_; }

    // Number of times the CPU blocked on a cross-engine boundary.  Gate 3.3
    // requires this to stay 0.
    uint64_t hostWaits() const noexcept { return host_waits_; }
    uint64_t submittedFrames() const noexcept { return submitted_frames_; }
    uint64_t duplicateCount() const noexcept { return duplicate_count_; }
    uint64_t sceneCutCount() const noexcept { return scene_cut_count_; }

    // Called when a frame is flagged as a scene cut; records the bypass and
    // returns the mode the interpolator must use for it.
    static constexpr uint32_t kFrucModeDuplicate = 3;

private:
    std::array<PingPongSlot, 2> slots_{};
    PingPongEventMode mode_ = PingPongEventMode::DisableTiming;
    uint64_t host_waits_ = 0;
    uint64_t submitted_frames_ = 0;
    uint64_t duplicate_count_ = 0;
    uint64_t scene_cut_count_ = 0;
    bool ready_ = false;
};

}  // namespace rtx_ngx

#endif /* RTX_NGX_BRIDGE_PINGPONG_RING_H */
