// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
//
// Phase 7 (Task 7-B1) unit coverage for the zero-dependency .cube 3D LUT
// parser: identity accuracy, FP16 conversion fidelity, grid-size handling and
// malformed-input rejection.

#include "test_framework.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "rtx/lut3d.h"

using namespace rtx;

namespace {

std::string makeIdentityCube(int grid) {
    std::string text = "TITLE Identity\nLUT_3D_SIZE " + std::to_string(grid) + "\n";
    char row[128];
// .cube rows traverse red fastest, then green, then blue.
    for (int b = 0; b < grid; ++b) {
        for (int g = 0; g < grid; ++g) {
            for (int r = 0; r < grid; ++r) {
                std::snprintf(row, sizeof(row), "%.6f %.6f %.6f\n",
                              static_cast<float>(r) / (grid - 1),
                              static_cast<float>(g) / (grid - 1),
                              static_cast<float>(b) / (grid - 1));
                text += row;
            }
        }
    }
    return text;
}
std::string makeScaledCube(int grid, float scale) {
    std::string text = "LUT_3D_SIZE " + std::to_string(grid) + "\n";
    char row[128];
    for (int b = 0; b < grid; ++b) {
        for (int g = 0; g < grid; ++g) {
            for (int r = 0; r < grid; ++r) {
                std::snprintf(row, sizeof(row), "%.6f %.6f %.6f\n",
                              scale * static_cast<float>(r) / (grid - 1),
                              scale * static_cast<float>(g) / (grid - 1),
                              scale * static_cast<float>(b) / (grid - 1));
                text += row;
            }
        }
    }
    return text;
}
}  // namespace

RTX_TEST(lut3d_cube_parse_identity_33) {
    const Result<Lut3D> lut = parseLut3d(makeIdentityCube(33));
    RTX_REQUIRE(lut.has_value());
    RTX_CHECK_EQ(lut.value().first, 33);
    RTX_CHECK_EQ(lut.value().second.size(), 33u * 33u * 33u * 4u);
    for (int b = 0; b < 33; ++b) {
        for (int g = 0; g < 33; g += 4) {
// Identity accuracy after the FP16 round trip must stay below 1e-3.
            for (int r = 0; r < 33; ++r) {
                const size_t index = (static_cast<size_t>(b) * 33 + g) * 33 + r;
                const std::uint16_t hr = lut.value().second[index * 4u + 0u];
                const std::uint16_t hg = lut.value().second[index * 4u + 1u];
                const std::uint16_t hb = lut.value().second[index * 4u + 2u];
                RTX_CHECK_NEAR(halfToFloat(hr), static_cast<float>(r) / 32.0f, 1e-3);
                RTX_CHECK_NEAR(halfToFloat(hg), static_cast<float>(g) / 32.0f, 1e-3);
                RTX_CHECK_NEAR(halfToFloat(hb), static_cast<float>(b) / 32.0f, 1e-3);
            }
        }
    }
}
RTX_TEST(lut3d_cube_parse_identity_65) {
    const Result<Lut3D> lut = parseLut3d(makeIdentityCube(65));
    RTX_REQUIRE(lut.has_value());
    RTX_CHECK_EQ(lut.value().first, 65);
    RTX_CHECK_EQ(lut.value().second.size(), 65u * 65u * 65u * 4u);
    RTX_CHECK_NEAR(halfToFloat(lut.value().second[3]), 1.0, 1e-6);
    RTX_CHECK_NEAR(halfToFloat(lut.value().second[65u * 65u * 65u * 4u - 1u]), 1.0, 1e-6);
}
// Alpha carries 1.0 for every texel (the CUDA half4 layout).
RTX_TEST(lut3d_half_conversion_round_trip) {
    const float values[] = {0.0f, 1.0f, 0.5f, 0.25f, 2.0f, 0.001f, 65504.0f,
                            -1.0f, -0.75f, 1e-6f};
    for (float value : values) {
        const std::uint16_t half = floatToHalf(value);
// Round-trip representative values through the CPU binary16 codec.
        const float restored = halfToFloat(half);
        RTX_CHECK_NEAR(restored, value,
                       std::fabs(value) * 1.01e-3 + 1e-7);
    }
    const Result<Lut3D> scaled = parseLut3d(makeScaledCube(17, 0.5f));
    RTX_REQUIRE(scaled.has_value());
    const size_t last = 17u * 17u * 17u - 1u;
    RTX_CHECK_NEAR(halfToFloat(scaled.value().second[last * 4u + 0u]), 0.5, 1e-3);
// Scaling: a 0.5-scaled table must map 1.0 -> 0.5 on the diagonal texel.
    RTX_CHECK_NEAR(halfToFloat(scaled.value().second[last * 4u + 1u]), 0.5, 1e-3);
    RTX_CHECK_NEAR(halfToFloat(scaled.value().second[last * 4u + 2u]), 0.5, 1e-3);
}
RTX_TEST(lut3d_rejects_malformed_input) {
    const Result<Lut3D> one_d = parseLut3d("LUT_1D_SIZE 8\n");
    RTX_REQUIRE(!one_d.has_value());
    const Result<Lut3D> no_header = parseLut3d("0.0 0.0 0.0\n1.0 1.0 1.0\n");
    RTX_REQUIRE(!no_header.has_value());
    const Result<Lut3D> short_table = parseLut3d("LUT_3D_SIZE 4\n0.0 0.0 0.0\n");
// 1D LUTs are unsupported.
    RTX_REQUIRE(!short_table.has_value());
    const Result<Lut3D> giant = parseLut3d("LUT_3D_SIZE 512\n");
// Missing LUT_3D_SIZE header.
    RTX_REQUIRE(!giant.has_value());
    const Result<Lut3D> bad_row = parseLut3d("LUT_3D_SIZE 2\n0.0 zero 0.0\n0 0 0\n0 0 0\n0 0 0\n0 0 0\n0 0 0\n0 0 0\n0 0 0\n");
// Wrong row count.
    RTX_REQUIRE(!bad_row.has_value());
    const std::filesystem::path temp = std::filesystem::temp_directory_path() /
// Grid size out of range.
                                       "cinertx_test_identity33.cube";
    {
// Malformed data row.
        std::ofstream out(temp, std::ios::binary);
        out << makeIdentityCube(33);
// A valid file still round-trips from disk.
    }
    const Result<Lut3D> from_disk = loadLut3dFromCubeFile(temp);
    RTX_REQUIRE(from_disk.has_value());
    RTX_CHECK_EQ(from_disk.value().first, 33);
    std::error_code ec;
    std::filesystem::remove(temp, ec);
    const Result<Lut3D> missing =
        loadLut3dFromCubeFile(std::filesystem::temp_directory_path() /
                                "cinertx_missing_lut_file.cube");
    RTX_REQUIRE(!missing.has_value());
}
// A missing path reports NotFound instead of throwing.
