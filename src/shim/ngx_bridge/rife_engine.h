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

namespace rtx_ngx {

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
    int32_t last_scene_cut_ = 0;
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
