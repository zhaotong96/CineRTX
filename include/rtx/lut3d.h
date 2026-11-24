// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
//
// Zero-dependency Adobe Iridas .cube 3D LUT parser with FP16 (half4) storage.

#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "rtx/status.h"

namespace rtx {

uint16_t floatToHalf(float value) noexcept;
float halfToFloat(uint16_t value) noexcept;

// Phase 12 (Wave 12.2-C): standard pair (grid_size, half4_table) replaces passive Lut3D wrapper.
using Lut3D = std::pair<int, std::vector<std::uint16_t>>;

[[nodiscard]] Result<Lut3D> loadLut3dFromCubeFile(const std::filesystem::path& path);
[[nodiscard]] Result<Lut3D> parseLut3d(const std::string& text);

}  // namespace rtx
