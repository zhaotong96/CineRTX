/* SPDX-License-Identifier: MIT OR Apache-2.0
 * Copyright (c) 2026 RTX Transcode Engine contributors
 *
 * TensorRT 10.13.3.9 inference engine wrapper for Practical-RIFE v4.25.
 */

#ifndef RTX_NGX_BRIDGE_RIFE_ENGINE_H
#define RTX_NGX_BRIDGE_RIFE_ENGINE_H

#include <cstdint>
#include <memory>
#include <string>

namespace rtx_ngx {

// @rtx-type-justification
// Category: NonTrivial-StateMachine
// Why-Std-Fails: 协调多资源异步生命周期的宿主状态机：std::unique_ptr/std::jthread/std::tuple 无法表达其声明序即析构逆序、线程亲和与受控降级路径的交叉不变量。
// Invariant: 成员声明序即依赖序（先声明者后析构）；状态迁移仅在持锁线程或属主线程发生。
class RifeEngine {
public:
    enum class Variant {
        SingleTensor7Ch,   // Variant A: [1, 7, H_pad, W_pad]
        MultiTensor3Input  // Variant B: img0 [1, 3, H_pad, W_pad], img1, timestep
    };

    RifeEngine();
    ~RifeEngine();

    bool loadEngine(const char* engine_path, uint32_t width, uint32_t height, std::string* error = nullptr);
    bool loadEngineEx(const char* engine_path, uint32_t width, uint32_t height,
                      float scale, uint32_t align_mod, bool reflect_padding,
                      uint64_t* out_trt_peak_bytes = nullptr, std::string* error = nullptr);

    int32_t interpolate(
        uint64_t prev_nv12,
        uint64_t curr_nv12,
        uint32_t pitch,
        float alpha,
        uint64_t out_nv12,
        int32_t full_range,
        int32_t* scene_cut,
        void* stream
    );
    uint32_t width() const noexcept { return width_; }
    uint32_t height() const noexcept { return height_; }
    uint32_t paddedWidth() const noexcept { return padded_width_; }
    uint32_t paddedHeight() const noexcept { return padded_height_; }
    uint32_t inferWidth() const noexcept { return infer_width_; }
    uint32_t inferHeight() const noexcept { return infer_height_; }
    float scale() const noexcept { return scale_; }
    uint32_t alignMod() const noexcept { return align_mod_; }
    bool reflectPadding() const noexcept { return reflect_padding_; }
    uint64_t trtPeakAllocationBytes() const noexcept { return trt_peak_allocation_bytes_; }
    Variant variant() const noexcept { return variant_; }
    bool isLoaded() const noexcept;
    bool isBuiltinSynth() const noexcept { return builtin_synth_; }

    /* Phase 9: true when the load-time alpha-sensitivity probe proved the
     * engine output actually responds to the ch6 timestep channel. */
    bool hasValidAlphaResponse() const noexcept;

    /* Phase 9: drop the cached (prev, curr, pitch) frame pair so the next
     * interpolate() call performs a full preprocess. */
    void resetFramePairCache() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    uint32_t width_ = 0;
    uint32_t height_ = 0;
    uint32_t padded_width_ = 0;
    uint32_t padded_height_ = 0;
    uint32_t infer_width_ = 0;
    uint32_t infer_height_ = 0;
    Variant variant_ = Variant::SingleTensor7Ch;
    bool builtin_synth_ = false;
    float scale_ = 1.0f;
    uint32_t align_mod_ = 128u;
    bool reflect_padding_ = true;
    uint64_t trt_peak_allocation_bytes_ = 0;
};

}  // namespace rtx_ngx

#endif  // RTX_NGX_BRIDGE_RIFE_ENGINE_H
