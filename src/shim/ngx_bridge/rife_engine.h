/* SPDX-License-Identifier: MIT OR Apache-2.0
 * Copyright (c) 2026 RTX Transcode Engine contributors
 *
 * TensorRT 10.13.3.9 inference engine wrapper for Practical-RIFE v4.25.
 * Phase 12 (Wave 12.2-B1): flattened RifeEngine::Impl directly into RifeEngine
 * using opaque void* TensorRT handles (no NvInfer.h leak into header).
 */

#ifndef RTX_NGX_BRIDGE_RIFE_ENGINE_H
#define RTX_NGX_BRIDGE_RIFE_ENGINE_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <tuple>

namespace rtx_ngx {

// Wave 2-A (DEC-011): 4 discrete single-profile engine buckets (strictly 1 OptimizationProfile per .engine).
enum class RifeAspectBucket : uint8_t {
    Ultra  = 0,  // R > 2.05  (21:9 cinema)
    Wide   = 1,  // 1.20 < R <= 2.05 (16:9 landscape)
    Square = 2,  // 0.80 <= R <= 1.20 (1:1 / 4:3)
    Tall   = 3,  // R < 0.80  (9:16 portrait)
};

[[nodiscard]] constexpr RifeAspectBucket classifyRifeBucket(uint32_t width, uint32_t height) noexcept {
    if (width == 0u || height == 0u) return RifeAspectBucket::Wide;
    const uint64_t ratio_x100 = (static_cast<uint64_t>(width) * 100ULL) / static_cast<uint64_t>(height);
    if (ratio_x100 > 205ULL) return RifeAspectBucket::Ultra;
    if (ratio_x100 > 120ULL) return RifeAspectBucket::Wide;
    if (ratio_x100 >= 80ULL) return RifeAspectBucket::Square;
    return RifeAspectBucket::Tall;
}

[[nodiscard]] constexpr const char* rifeBucketEngineFilename(RifeAspectBucket bucket) noexcept {
    switch (bucket) {
        case RifeAspectBucket::Ultra:  return "rife_v4.25_ultra.engine";
        case RifeAspectBucket::Wide:   return "rife_v4.25_wide.engine";
        case RifeAspectBucket::Square: return "rife_v4.25_square.engine";
        case RifeAspectBucket::Tall:   return "rife_v4.25_tall.engine";
    }
    return "rife_v4.25_wide.engine";
}

// Tuple: (opt_w, opt_h, max_w, max_h) — strictly 1 OptimizationProfile per bucket ([DEC-011])
using RifeBucketProfileDims = std::tuple<int, int, int, int>;
[[nodiscard]] constexpr RifeBucketProfileDims rifeBucketProfileDims(RifeAspectBucket bucket) noexcept {
    switch (bucket) {
        case RifeAspectBucket::Ultra:  return {2560, 1088, 5120, 1600};
        case RifeAspectBucket::Wide:   return {1920, 1088, 3840, 2176};
        case RifeAspectBucket::Square: return {1472, 1472, 2560, 2560};
        case RifeAspectBucket::Tall:   return {1088, 1920, 2176, 3840};
    }
    return {1920, 1088, 3840, 2176};
}

// Tuple: (src_w, src_h, pad_w, pad_h, pad_left, pad_top, bucket)
using RifeGeometryDesc = std::tuple<uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, RifeAspectBucket>;

[[nodiscard]] constexpr RifeGeometryDesc calculateRifeGeometry(uint32_t width, uint32_t height, uint32_t align = 128u) noexcept {
    const uint32_t a = (align >= 32u && (align & (align - 1u)) == 0u) ? align : 128u;
    const uint32_t pad_w = ((width + a - 1u) / a) * a;
    const uint32_t pad_h = ((height + a - 1u) / a) * a;
    const uint32_t pad_left = (pad_w - width) / 2u;
    const uint32_t pad_top  = (pad_h - height) / 2u;
    return std::make_tuple(width, height, pad_w, pad_h, pad_left, pad_top, classifyRifeBucket(width, height));
}

// @rtx-type-justification
// Category: NonTrivial-StateMachine
// Why-Std-Fails: 管理 TensorRT IRuntime/ICudaEngine/IExecutionContext、多块 CUDA 显存张量与帧对缓存的严格逆序析构状态机，析构须先释放设备缓冲再按 context->engine->runtime 逆序销毁。
// Invariant: 张量地址与执行上下文绑定保持一致；仅在持锁宿主线程调用 loadEngineEx/interpolate/析构。
class RifeEngine {
public:
    enum class Variant {
        SingleTensor7Ch,   // Variant A: [1, 7, H_pad, W_pad]
        MultiTensor3Input  // Variant B: img0 [1, 3, H_pad, W_pad], img1, timestep
    };

    RifeEngine() = default;
    ~RifeEngine();

    RifeEngine(const RifeEngine&) = delete;
    RifeEngine& operator=(const RifeEngine&) = delete;

    bool loadEngine(const char* engine_path, uint32_t width, uint32_t height, std::string* error = nullptr);
    bool loadEngineEx(const char* engine_path, uint32_t width, uint32_t height,
                      float scale, uint32_t align_mod, bool reflect_padding,
                      uint64_t* out_trt_peak_bytes = nullptr, std::string* error = nullptr);

    int32_t interpolate(uint64_t prev_nv12, uint64_t curr_nv12, uint32_t pitch,
                        float alpha, uint64_t out_nv12, int32_t full_range,
                        int32_t* scene_cut, void* stream);

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
    bool isLoaded() const noexcept { return context_ != nullptr; }
    bool isBuiltinSynth() const noexcept { return builtin_synth_; }
    bool hasValidAlphaResponse() const noexcept { return alpha_probe_passed_; }
    void resetFramePairCache() noexcept;

private:
    void releaseBuffers() noexcept;
    void releaseTrtObjects() noexcept;

    void* runtime_ = nullptr;   // nvinfer1::IRuntime*
    void* engine_ = nullptr;    // nvinfer1::ICudaEngine*
    void* context_ = nullptr;   // nvinfer1::IExecutionContext*

    std::string in_name_;
    std::string out_name_;
    std::string img0_name_;
    std::string img1_name_;
    std::string timestep_name_;

    int32_t in_dtype_ = 0;      // nvinfer1::DataType
    int32_t out_dtype_ = 0;     // nvinfer1::DataType

    uint64_t d_input_tensor_ = 0;
    uint64_t d_output_tensor_ = 0;
    uint64_t d_input_fp32_scratch_ = 0;
    uint64_t d_output_fp32_scratch_ = 0;
    uint64_t d_img0_tensor_ = 0;
    uint64_t d_img1_tensor_ = 0;
    uint64_t d_timestep_tensor_ = 0;
    uint64_t d_sad_sum_ = 0;
    uint64_t d_block_sad_ = 0;

    size_t input_bytes_ = 0;
    size_t output_bytes_ = 0;

    uint64_t last_prev_nv12_ = 0;
    uint64_t last_curr_nv12_ = 0;
    uint32_t last_pitch_ = 0;
    float last_alpha_ = -1.0f;
    int32_t last_scene_cut_ = 0;
    int32_t last_full_range_ = -1;
    uint32_t cache_generation_ = 0;
    uint32_t last_cache_generation_ = 0;
    void* rife_stream_ = nullptr;
    bool cache_valid_ = false;
    bool alpha_probe_passed_ = false;
    float ema_motion_ = 0.03f;

    uint32_t preprocess_launches_ = 0;
    uint32_t interpolate_calls_ = 0;
    uint32_t cache_reuse_ = 0;

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
