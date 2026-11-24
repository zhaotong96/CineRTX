// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors

#pragma once

#include <cstdint>
#include <tuple>

namespace rtx {

enum class GeometryTargetMode : int32_t {
    AutoDirect = 0,  // Mode A: Portrait 4K (2160x3840) if src W < H, else Landscape 4K (3840x2160)
    Pillarbox = 1,   // Mode B: 3840x2160 canvas, center effective video with studio black padding
};

// Phase 12 (Wave 12.2-B2): tuple alias replaces passive GeometryResult bag.
// (canvas_width, canvas_height, scaled_width, scaled_height,
//  offset_x, offset_y, pillarbox_left, pillarbox_right, is_portrait)
using GeometryResult = std::tuple<int, int, int, int, int, int, int, int, bool>;

GeometryResult calculateSmartGeometry(int src_w, int src_h,
                                      GeometryTargetMode mode = GeometryTargetMode::AutoDirect,
                                      int user_target_w = 0, int user_target_h = 0);

}  // namespace rtx
