// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors

#include "test_framework.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "rtx/color.h"
#include "rtx/rife.h"
#include "rtx/types.h"
#include "test_util.h"

using namespace rtx;

RTX_TEST(rife_aligned_shape) {
    const RifeTensorShape hd = rifeAlignedShape(1920, 1080);
    RTX_CHECK_EQ(hd.n, 1);
    RTX_CHECK_EQ(hd.c, 7);
    RTX_CHECK_EQ(hd.w, 1920);
    RTX_CHECK_EQ(hd.h, 1088);
    const RifeTensorShape uhd = rifeAlignedShape(3840, 2160);
    RTX_CHECK_EQ(uhd.w, 3840);
    RTX_CHECK_EQ(uhd.h, 2176);
    const RifeTensorShape tiny = rifeAlignedShape(1, 1);
    RTX_CHECK_EQ(tiny.w, 32);
    RTX_CHECK_EQ(tiny.h, 32);
    const RifeTensorShape odd = rifeAlignedShape(33, 33);
    RTX_CHECK_EQ(odd.w, 64);
    RTX_CHECK_EQ(odd.h, 64);
    RTX_CHECK_EQ(rifeSensitiveLayers().size(), static_cast<size_t>(16));
}

RTX_TEST(rife_tensorrt_version_gate) {
    RTX_CHECK(validateTensorRtVersion("10.13.3.9").ok());
    RTX_CHECK_EQ(validateTensorRtVersion("10.13.3.8").code(), ErrorCode::TensorRtVersion);
    RTX_CHECK_EQ(validateTensorRtVersion("10.13.2.9").code(), ErrorCode::TensorRtVersion);
    RTX_CHECK_EQ(validateTensorRtVersion("10.14.0.0").code(), ErrorCode::TensorRtVersion);
    RTX_CHECK_EQ(validateTensorRtVersion("10.15.1.29").code(), ErrorCode::TensorRtVersion);
    RTX_CHECK_EQ(validateTensorRtVersion("9.3.0.0").code(), ErrorCode::TensorRtVersion);
    RTX_CHECK_EQ(validateTensorRtVersion("10.13").code(), ErrorCode::TensorRtVersion);
    RTX_CHECK_EQ(validateTensorRtVersion("not-a-version").code(), ErrorCode::TensorRtVersion);
    const Result<void> regression = validateTensorRtVersion("10.14.0.0");
    RTX_REQUIRE(!regression.ok());
    RTX_CHECK(regression.error().message.find("10.14+") != std::string::npos);
}

RTX_TEST(rife_config_validation) {
    RifeConfig config;
    RTX_CHECK(validateRifeConfig(config).ok());

    config.source_width = 0;
    RTX_CHECK_EQ(validateRifeConfig(config).code(), ErrorCode::InvalidArgument);
    config.source_width = 1920;
    config.source_height = 5000;
    RTX_CHECK_EQ(validateRifeConfig(config).code(), ErrorCode::Unsupported);
    config.source_height = 1080;

    config.max_batch = 2;
    RTX_CHECK_EQ(validateRifeConfig(config).code(), ErrorCode::Unsupported);
    config.max_batch = 1;

    config.tensor_height = 1000;
    RTX_CHECK_EQ(validateRifeConfig(config).code(), ErrorCode::InvalidArgument);
    config.tensor_height = 1088;
    config.tensor_width = 1024;
    RTX_CHECK_EQ(validateRifeConfig(config).code(), ErrorCode::InvalidArgument);
    config.tensor_width = 1920;

    config.tensor_width = 3840;
    config.tensor_height = 2176;
    config.enable_4k_profile = false;
    RTX_CHECK_EQ(validateRifeConfig(config).code(), ErrorCode::OutOfMemory);
    config.enable_4k_profile = true;
    RTX_CHECK(validateRifeConfig(config).ok());

    RifeConfig small;
    small.source_width = 64;
    small.source_height = 64;
    small.tensor_width = 64;
    small.tensor_height = 64;
    RTX_CHECK(validateRifeConfig(small).ok());
}

RTX_TEST(rife_trtexec_command) {
    RifeConfig config;
    const std::string command = buildTrtexecCommand(config, "C:/models/rife.onnx", "C:/models/rife.engine");
    RTX_CHECK(command.find("trtexec") == 0);
    RTX_CHECK(command.find("--onnx=\"C:/models/rife.onnx\"") != std::string::npos);
    RTX_CHECK(command.find("--saveEngine=\"C:/models/rife.engine\"") != std::string::npos);
    RTX_CHECK(command.find("--fp16") != std::string::npos);
    RTX_CHECK(command.find("--noTF32") != std::string::npos);
    RTX_CHECK(command.find("--precisionConstraints=obey") != std::string::npos);
    RTX_CHECK(command.find("--minShapes=input:1x7x256x320") != std::string::npos);
    RTX_CHECK(command.find("--optShapes=input:1x7x1088x1920") != std::string::npos);
    RTX_CHECK(command.find("--maxShapes=input:1x7x1088x1920") != std::string::npos);
    RTX_CHECK(command.find("/Cast_2:fp32") != std::string::npos);
    RTX_CHECK(command.find("/Reciprocal_1:fp32") != std::string::npos);
    RTX_CHECK(command.find("ONNXTRT_Broadcast_275:fp32") != std::string::npos);
    RTX_CHECK(command.find("--builderOptimizationLevel=3") != std::string::npos);
    RTX_CHECK(command.find("--useCudaGraph") != std::string::npos);

    config.no_tf32 = false;
    config.obey_precision_constraints = false;
    config.use_cuda_graph = false;
    config.builder_optimization_level = 1;
    const std::string lean = buildTrtexecCommand(config, "a.onnx", "a.engine");
    RTX_CHECK(lean.find("--noTF32") == std::string::npos);
    RTX_CHECK(lean.find("--precisionConstraints") == std::string::npos);
    RTX_CHECK(lean.find("--useCudaGraph") == std::string::npos);
    RTX_CHECK(lean.find("--builderOptimizationLevel=1") != std::string::npos);
}

RTX_TEST(rife_pack_input) {
    Frame previous;
    Frame next;
    RTX_REQUIRE(previous.allocateHost(4, 4, PixelFormat::NV12).ok());
    RTX_REQUIRE(next.allocateHost(4, 4, PixelFormat::NV12).ok());
    test::fillNV12(previous, 16, 128, 128);  // black
    test::fillNV12(next, 235, 128, 128);     // white

    RifeTensorShape shape;
    shape.n = 1;
    shape.c = 7;
    shape.w = 8;
    shape.h = 8;
    const size_t plane = static_cast<size_t>(shape.w) * shape.h;
    std::vector<uint16_t> tensor(plane * static_cast<size_t>(shape.c) + 1, 0xDEAD);
    const RifeNv12Pair pair{previous.planes[0], next.planes[0], 4, 4, previous.pitches[0],
                            previous.pitches[1]};
    RTX_REQUIRE(packRifeInput(pair, 0.25f, tensor.data(), shape).ok());
    RTX_CHECK_EQ(halfToFloat(tensor[0]), 0.0f);
    RTX_CHECK_EQ(halfToFloat(tensor[1 * plane + 0]), 0.0f);
    RTX_CHECK_EQ(halfToFloat(tensor[2 * plane + 0]), 0.0f);
    RTX_CHECK_NEAR(halfToFloat(tensor[3 * plane + 0]), 1.0f, 1e-6);
    RTX_CHECK_NEAR(halfToFloat(tensor[4 * plane + 0]), 1.0f, 1e-6);
    RTX_CHECK_NEAR(halfToFloat(tensor[5 * plane + 0]), 1.0f, 1e-6);
    RTX_CHECK_NEAR(halfToFloat(tensor[6 * plane + 0]), 0.25f, 1e-6);
    RTX_CHECK_EQ(tensor[plane * 7], static_cast<uint16_t>(0xDEAD));

    // Padding mirrors the edge rather than reading out of bounds.
    const size_t last = plane - 1;
    RTX_CHECK_NEAR(halfToFloat(tensor[3 * plane + last]), 1.0f, 1e-6);
    RTX_CHECK_NEAR(halfToFloat(tensor[6 * plane + last]), 0.25f, 1e-6);

    RTX_CHECK_EQ(packRifeInput(RifeNv12Pair{}, 0.5f, tensor.data(), shape).code(),
                 ErrorCode::InvalidArgument);
    RifeNv12Pair oversized = pair;
    oversized.width = 64;
    RTX_CHECK_EQ(packRifeInput(oversized, 0.5f, tensor.data(), shape).code(),
                 ErrorCode::InvalidArgument);
    RTX_CHECK_EQ(packRifeInput(pair, 2.0f, nullptr, shape).code(), ErrorCode::InvalidArgument);
}

RTX_TEST(rife_unpack_output) {
    RifeTensorShape shape;
    shape.n = 1;
    shape.c = 7;
    shape.w = 4;
    shape.h = 4;
    const size_t plane = static_cast<size_t>(shape.w) * shape.h;
    std::vector<uint16_t> tensor(plane * 3, floatToHalf(0.0f));
    Frame nv12;
    RTX_REQUIRE(nv12.allocateHost(4, 4, PixelFormat::NV12).ok());
    test::fillNV12(nv12, 0, 0, 0);
    RTX_REQUIRE(unpackRifeOutput(tensor.data(), shape, 4, 4, nv12.planes[0], nv12.pitches[0],
                                 nv12.pitches[1])
                    .ok());
    RTX_CHECK_EQ(nv12.planes[0][0], static_cast<uint8_t>(16));
    RTX_CHECK_EQ(nv12.planes[1][0], static_cast<uint8_t>(128));
    RTX_CHECK_EQ(nv12.planes[1][1], static_cast<uint8_t>(128));

    // Saturated values must clamp; residual FP16 overflow may never reach P010.
    for (size_t i = 0; i < plane; ++i) {
        tensor[i] = floatToHalf(4.0f);
        tensor[plane + i] = floatToHalf(4.0f);
        tensor[2 * plane + i] = floatToHalf(4.0f);
    }
    RTX_REQUIRE(unpackRifeOutput(tensor.data(), shape, 4, 4, nv12.planes[0], nv12.pitches[0],
                                 nv12.pitches[1])
                    .ok());
    RTX_CHECK_EQ(nv12.planes[0][0], static_cast<uint8_t>(235));
    RTX_CHECK_EQ(nv12.planes[0][3 * nv12.pitches[0] + 3], static_cast<uint8_t>(235));
    RTX_CHECK_EQ(nv12.planes[1][0], static_cast<uint8_t>(128));
    RTX_CHECK_EQ(nv12.planes[1][1], static_cast<uint8_t>(128));

    // A 2x2 chroma box averages the four RGB samples of each cell.
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            const size_t index = static_cast<size_t>(y) * 4 + x;
            const bool left = x < 2;
            tensor[index] = floatToHalf(left ? 1.0f : 0.0f);
            tensor[plane + index] = floatToHalf(0.0f);
            tensor[2 * plane + index] = floatToHalf(left ? 0.0f : 1.0f);
        }
    }
    RTX_REQUIRE(unpackRifeOutput(tensor.data(), shape, 4, 4, nv12.planes[0], nv12.pitches[0],
                                 nv12.pitches[1])
                    .ok());
    // Red has a lower U and a higher V than blue.
    RTX_CHECK(nv12.planes[1][0] < nv12.planes[1][2]);
    RTX_CHECK(nv12.planes[1][1] > nv12.planes[1][3]);

    RTX_CHECK_EQ(unpackRifeOutput(nullptr, shape, 4, 4, nv12.planes[0], nv12.pitches[0],
                                  nv12.pitches[1])
                     .code(),
                 ErrorCode::InvalidArgument);
    RTX_CHECK_EQ(unpackRifeOutput(tensor.data(), shape, 8, 4, nv12.planes[0], nv12.pitches[0],
                                  nv12.pitches[1])
                     .code(),
                 ErrorCode::InvalidArgument);
    RTX_CHECK_EQ(unpackRifeOutput(tensor.data(), shape, 0, 4, nv12.planes[0], nv12.pitches[0],
                                  nv12.pitches[1])
                     .code(),
                 ErrorCode::InvalidArgument);
}

RTX_TEST(rife_pack_unpack_round_trip) {
    const int width = 8;
    const int height = 8;
    Frame source;
    RTX_REQUIRE(source.allocateHost(width, height, PixelFormat::NV12).ok());
    for (int y = 0; y < height; ++y) {
        std::memset(source.planes[0] + static_cast<size_t>(y) * source.pitches[0], 100, width);
    }
    for (int y = 0; y < height / 2; ++y) {
        for (int x = 0; x < width / 2; ++x) {
            source.planes[1][static_cast<size_t>(y) * source.pitches[1] + x * 2] = 110;
            source.planes[1][static_cast<size_t>(y) * source.pitches[1] + x * 2 + 1] = 140;
        }
    }
    RifeTensorShape shape;
    shape.w = width;
    shape.h = height;
    std::vector<uint16_t> tensor(static_cast<size_t>(7) * width * height, 0);
    const RifeNv12Pair pair{source.planes[0], source.planes[0], width, height, source.pitches[0],
                            source.pitches[1]};
    RTX_REQUIRE(packRifeInput(pair, 0.5f, tensor.data(), shape).ok());

    Frame output;
    RTX_REQUIRE(output.allocateHost(width, height, PixelFormat::NV12).ok());
    RTX_REQUIRE(unpackRifeOutput(tensor.data(), shape, width, height, output.planes[0],
                                 output.pitches[0], output.pitches[1])
                    .ok());
    RTX_CHECK(std::abs(static_cast<int>(output.planes[0][0]) - 100) <= 1);
    RTX_CHECK(std::abs(static_cast<int>(output.planes[1][0]) - 110) <= 2);
    RTX_CHECK(std::abs(static_cast<int>(output.planes[1][1]) - 140) <= 2);
}
