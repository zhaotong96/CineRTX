// SPDX-License-Identifier: MIT OR Apache-2.0
// Copyright (c) 2026 RTX Transcode Engine contributors
//
// TensorRT/RIFE configuration and tensor marshalling.  The version guard is a
// safety feature, not a suggestion: TensorRT 10.14+ regresses this graph.

#include "rtx/rife.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

#include "rtx/color.h"

namespace rtx {
namespace {

std::vector<int> parseVersion(const std::string& version) {
    std::vector<int> parts;
    std::stringstream stream(version);
    std::string token;
    while (std::getline(stream, token, '.')) {
        try {
            parts.push_back(std::stoi(token));
        } catch (const std::exception&) {
            return {};
        }
    }
    return parts;
}

int reflectIndex(int index, int size) {
    if (size <= 1) {
        return 0;
    }
    while (index < 0 || index >= size) {
        if (index < 0) {
            index = -index - 1;
        }
        if (index >= size) {
            index = 2 * size - index - 1;
        }
    }
    return index;
}

}  // namespace

RifeTensorShape rifeAlignedShape(int width, int height) {
    RifeTensorShape shape;
    shape.n = 1;
    shape.c = 7;
    shape.w = static_cast<int>((static_cast<size_t>(width) + 31) & ~size_t{31});
    shape.h = static_cast<int>((static_cast<size_t>(height) + 31) & ~size_t{31});
    return shape;
}

std::vector<std::string> rifeSensitiveLayers() {
    return {
        "/Cast_2",
        "/Cast_3",
        "/Cast_5",
        "/Cast_7",
        "/Reciprocal",
        "/Reciprocal_1",
        "/Mul",
        "/Mul_1",
        "/Mul_8",
        "/Mul_10",
        "/Sub_5",
        "/Sub_6",
        "ONNXTRT_Broadcast_236",
        "ONNXTRT_Broadcast_238",
        "ONNXTRT_Broadcast_273",
        "ONNXTRT_Broadcast_275",
    };
}

Result<void> validateTensorRtVersion(const std::string& version) {
    const std::vector<int> parts = parseVersion(version);
    if (parts.size() < 4) {
        return Result<void>::fail(ErrorCode::TensorRtVersion,
                                  "malformed TensorRT version string", version);
    }
    if (parts[0] != 10 || parts[1] != 13 || parts[2] != 3 || parts[3] != 9) {
        if (parts[0] == 10 && parts[1] >= 14) {
            return Result<void>::fail(
                ErrorCode::TensorRtVersion,
                "TensorRT 10.14+ has a known RIFE VRAM/latency regression; use 10.13.3.9",
                "found=" + version);
        }
        return Result<void>::fail(
            ErrorCode::TensorRtVersion,
            "TensorRT 10.13.3.9 is the only supported baseline for RIFE v4.25",
            "found=" + version + " required=10.13.3.9");
    }
    return {};
}

Result<void> validateRifeConfig(const RifeConfig& config) {
    if (config.source_width <= 0 || config.source_height <= 0) {
        return Result<void>::fail(ErrorCode::InvalidArgument, "invalid RIFE source geometry");
    }
    if (config.source_width > 4096 || config.source_height > 4096) {
        return Result<void>::fail(ErrorCode::Unsupported,
                                  "RIFE source exceeds the 4K profile limit");
    }
    if (config.max_batch != 1) {
        return Result<void>::fail(ErrorCode::Unsupported,
                                  "RIFE v4.25 is validated with batch size 1");
    }
    if (config.tensor_height % 32 != 0 || config.tensor_width % 32 != 0) {
        return Result<void>::fail(ErrorCode::InvalidArgument,
                                  "RIFE tensor dimensions must be multiples of 32");
    }
    if (config.tensor_width < config.source_width ||
        config.tensor_height < config.source_height) {
        return Result<void>::fail(ErrorCode::InvalidArgument,
                                  "RIFE tensor is smaller than the source frame");
    }
    if (!config.enable_4k_profile && (config.tensor_width > 1920 || config.tensor_height > 1088)) {
        return Result<void>::fail(
            ErrorCode::OutOfMemory,
            "8 GB profile forbids a 4K RIFE workspace; enable the workstation profile explicitly");
    }
    return {};
}

std::string buildTrtexecCommand(const RifeConfig& config, const std::string& onnx_path,
                                const std::string& engine_path) {
    std::ostringstream out;
    out << "trtexec --onnx=\"" << onnx_path << "\" --saveEngine=\"" << engine_path << "\""
        << " --fp16";
    if (config.no_tf32) {
        out << " --noTF32";
    }
    if (config.obey_precision_constraints) {
        out << " --precisionConstraints=obey";
    }
    out << " --inputIOFormats=fp16:chw --outputIOFormats=fp16:chw"
        << " --minShapes=input:1x7x256x320"
        << " --optShapes=input:1x7x" << config.tensor_height << "x" << config.tensor_width
        << " --maxShapes=input:1x7x" << config.tensor_height << "x" << config.tensor_width;

    out << " --layerPrecisions=";
    const auto layers = rifeSensitiveLayers();
    for (size_t i = 0; i < layers.size(); ++i) {
        if (i != 0) {
            out << ",";
        }
        out << layers[i] << ":fp32";
    }
    out << " --tacticSources=+CUBLAS,+CUBLAS_LT,+CUDNN,+EDGE_MASK_CONVOLUTIONS,+JIT_CONVOLUTIONS"
        << " --builderOptimizationLevel=" << config.builder_optimization_level;
    if (config.use_cuda_graph) {
        out << " --useCudaGraph";
    }
    return out.str();
}

Result<void> packRifeInput(const RifeNv12Pair& pair, float timestep, uint16_t* tensor,
                           const RifeTensorShape& shape) {
    if (pair.previous == nullptr || pair.next == nullptr || tensor == nullptr) {
        return Result<void>::fail(ErrorCode::InvalidArgument, "RIFE input pair is incomplete");
    }
    if (pair.width <= 0 || pair.height <= 0 || pair.width > shape.w || pair.height > shape.h) {
        return Result<void>::fail(ErrorCode::InvalidArgument,
                                  "RIFE input frame does not fit the tensor shape");
    }
    const size_t plane_size = static_cast<size_t>(shape.h) * shape.w;
    for (int y = 0; y < shape.h; ++y) {
        const int source_y = reflectIndex(y, pair.height);
        const uint8_t* previous_row =
            pair.previous + static_cast<size_t>(source_y) * pair.y_pitch;
        const uint8_t* next_row = pair.next + static_cast<size_t>(source_y) * pair.y_pitch;
        const uint8_t* previous_uv =
            pair.previous + static_cast<size_t>(pair.y_pitch) * pair.height +
            static_cast<size_t>(source_y / 2) * pair.uv_pitch;
        const uint8_t* next_uv = pair.next + static_cast<size_t>(pair.y_pitch) * pair.height +
                                 static_cast<size_t>(source_y / 2) * pair.uv_pitch;
        for (int x = 0; x < shape.w; ++x) {
            const int source_x = reflectIndex(x, pair.width);
            float previous_rgb[3];
            float next_rgb[3];
            bt709YuvToRgb(previous_row[source_x], previous_uv[(source_x / 2) * 2],
                          previous_uv[(source_x / 2) * 2 + 1], 8, previous_rgb[0],
                          previous_rgb[1], previous_rgb[2]);
            bt709YuvToRgb(next_row[source_x], next_uv[(source_x / 2) * 2],
                          next_uv[(source_x / 2) * 2 + 1], 8, next_rgb[0], next_rgb[1],
                          next_rgb[2]);
            const size_t index = static_cast<size_t>(y) * shape.w + x;
            tensor[0 * plane_size + index] = floatToHalf(previous_rgb[0]);
            tensor[1 * plane_size + index] = floatToHalf(previous_rgb[1]);
            tensor[2 * plane_size + index] = floatToHalf(previous_rgb[2]);
            tensor[3 * plane_size + index] = floatToHalf(next_rgb[0]);
            tensor[4 * plane_size + index] = floatToHalf(next_rgb[1]);
            tensor[5 * plane_size + index] = floatToHalf(next_rgb[2]);
            tensor[6 * plane_size + index] = floatToHalf(std::clamp(timestep, 0.0f, 1.0f));
        }
    }
    return {};
}

Result<void> unpackRifeOutput(const uint16_t* tensor, const RifeTensorShape& shape, int width,
                              int height, uint8_t* nv12, int y_pitch, int uv_pitch) {
    if (tensor == nullptr || nv12 == nullptr || width <= 0 || height <= 0) {
        return Result<void>::fail(ErrorCode::InvalidArgument, "RIFE output buffer is invalid");
    }
    if (width > shape.w || height > shape.h) {
        return Result<void>::fail(ErrorCode::InvalidArgument,
                                  "RIFE output crop exceeds the tensor shape");
    }
    const size_t plane_size = static_cast<size_t>(shape.h) * shape.w;
    uint8_t* y_plane = nv12;
    uint8_t* uv_plane = nv12 + static_cast<size_t>(y_pitch) * height;

    std::vector<uint32_t> u_sum(static_cast<size_t>((width + 1) / 2) * ((height + 1) / 2), 0);
    std::vector<uint32_t> v_sum(u_sum.size(), 0);
    std::vector<uint32_t> count(u_sum.size(), 0);

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const size_t index = static_cast<size_t>(y) * shape.w + x;
            const float r = std::clamp(halfToFloat(tensor[0 * plane_size + index]), 0.0f, 1.0f);
            const float g = std::clamp(halfToFloat(tensor[1 * plane_size + index]), 0.0f, 1.0f);
            const float b = std::clamp(halfToFloat(tensor[2 * plane_size + index]), 0.0f, 1.0f);
            uint16_t yy = 0, uu = 0, vv = 0;
            bt709RgbToYuv(r, g, b, yy, uu, vv, 8);
            y_plane[static_cast<size_t>(y) * y_pitch + x] = static_cast<uint8_t>(yy);
            const size_t ci = static_cast<size_t>(y / 2) * ((width + 1) / 2) + (x / 2);
            u_sum[ci] += uu;
            v_sum[ci] += vv;
            ++count[ci];
        }
    }
    const int chroma_width = (width + 1) / 2;
    for (int y = 0; y < (height + 1) / 2; ++y) {
        for (int x = 0; x < chroma_width; ++x) {
            const size_t ci = static_cast<size_t>(y) * chroma_width + x;
            const uint32_t n = std::max<uint32_t>(1, count[ci]);
            uint8_t* uv = uv_plane + static_cast<size_t>(y) * uv_pitch + static_cast<size_t>(x) * 2;
            uv[0] = static_cast<uint8_t>((u_sum[ci] + n / 2) / n);
            uv[1] = static_cast<uint8_t>((v_sum[ci] + n / 2) / n);
        }
    }
    return {};
}

}  // namespace rtx
