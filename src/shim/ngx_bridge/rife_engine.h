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

class RifeEngine {
public:
    enum class Variant {
        SingleTensor7Ch,   // Variant A: [1, 7, H_pad, W_pad]
        MultiTensor3Input  // Variant B: img0 [1, 3, H_pad, W_pad], img1, timestep
    };

    RifeEngine();
    ~RifeEngine();

    bool loadEngine(const char* engine_path, uint32_t width, uint32_t height, std::string* error = nullptr);

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
    Variant variant_ = Variant::SingleTensor7Ch;
    bool builtin_synth_ = false;
};

}  // namespace rtx_ngx

#endif  // RTX_NGX_BRIDGE_RIFE_ENGINE_H
