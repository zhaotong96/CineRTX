// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
//
// RIFE v4.25 + TensorRT 10.13 configuration.  The version guard is deliberate:
// TensorRT 10.14/10.15 regress the dynamic-shape RIFE graph from ~2.7 GB to
// ~10 GB of activation memory (NVIDIA/TensorRT issue #4721).

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "rtx/status.h"

namespace rtx {

inline constexpr const char* kRifeTensorRtBaseline = "10.13.3.9";
inline constexpr const char* kRifeTensorRtForbidden1 = "10.14";
inline constexpr const char* kRifeTensorRtForbidden2 = "10.15.1.29";

struct RifeConfig {
    int source_width = 1920;
    int source_height = 1080;
    int tensor_width = 1920;
    int tensor_height = 1088;  // padded to a multiple of 32
    bool fp16 = true;
    bool no_tf32 = true;
    bool obey_precision_constraints = true;
    int builder_optimization_level = 3;
    bool use_cuda_graph = true;
    bool enable_4k_profile = false;
    int max_batch = 1;
};

struct RifeTensorShape {
    int n = 1;
    int c = 7;
    int h = 1088;
    int w = 1920;
    bool operator==(const RifeTensorShape& other) const {
        return n == other.n && c == other.c && h == other.h && w == other.w;
    }
};

RifeTensorShape rifeAlignedShape(int width, int height);
std::vector<std::string> rifeSensitiveLayers();
Result<void> validateTensorRtVersion(const std::string& version);
Result<void> validateRifeConfig(const RifeConfig& config);

// Produces the exact trtexec invocation documented in the implementation plan.
std::string buildTrtexecCommand(const RifeConfig& config, const std::string& onnx_path,
                                const std::string& engine_path);

struct RifeNv12Pair {
    const uint8_t* previous = nullptr;
    const uint8_t* next = nullptr;
    int width = 0;
    int height = 0;
    int y_pitch = 0;
    int uv_pitch = 0;
};

// Converts two NV12 frames into a 1x7xHxW FP16 CHW tensor.  The first six
// channels carry the two RGB frames; the seventh is the timestep scalar.
Result<void> packRifeInput(const RifeNv12Pair& pair, float timestep, uint16_t* tensor,
                           const RifeTensorShape& shape);

// Clamps and crops the 1x3xHxW output back to NV12.  The clamp is mandatory:
// it prevents residual FP16 overflow from reaching TrueHDR.
Result<void> unpackRifeOutput(const uint16_t* tensor, const RifeTensorShape& shape, int width,
                              int height, uint8_t* nv12, int y_pitch, int uv_pitch);

}  // namespace rtx
