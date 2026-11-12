// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
//
// Zero-dependency Adobe Iridas .cube 3D LUT parser with FP16 (half4)
// storage.  The parsed table uploads verbatim into the bridge's CUDA 3D
// texture (cudaChannelFormatKindFloat / half4, hardware trilinear
// interpolation), so this type is deliberately a plain aggregate wrapper with
// 100% RAII semantics: no initialize/shutdown pair, value semantics only.

#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

#include "rtx/status.h"

namespace rtx {

// IEEE-754 binary16 (half-precision float) conversion utilities.
uint16_t floatToHalf(float value) noexcept;
float halfToFloat(uint16_t value) noexcept;

class Lut3D {
public:
    // Parses an Adobe Iridas .cube file.  Supports TITLE, LUT_3D_SIZE,
    // LUT_3D_INPUT_RANGE, DOMAIN_MIN / DOMAIN_MAX and DOMAIN_MIN comments;
    // 1D (LUT_1D_SIZE) files are rejected as unsupported.  Grid sizes 2..256
    // are accepted (17, 33, 64, 65 are the production targets).
    [[nodiscard]] static Result<Lut3D> loadFromCubeFile(const std::filesystem::path& path);

    // Parses from memory (hermetic tests).  Same contract as loadFromCubeFile.
    [[nodiscard]] static Result<Lut3D> parse(const std::string& text);

    [[nodiscard]] int gridSize() const noexcept { return grid_size_; }
    // grid_size^3 * 4 RGBA16F values (IEEE-754 binary16 in uint16 carriers).
    [[nodiscard]] std::span<const std::uint16_t> half4Data() const noexcept {
        return half4_table_;
    }

private:
    Lut3D() = default;
    int grid_size_ = 0;                      // Supports 17, 33, 64, 65, ...
    std::vector<std::uint16_t> half4_table_; // grid_size^3 * 4 (RGBA16F)
};

}  // namespace rtx
