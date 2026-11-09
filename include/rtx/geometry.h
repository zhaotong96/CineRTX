// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors

#pragma once

#include <cstdint>

namespace rtx {

enum class GeometryTargetMode : int32_t {
    AutoDirect = 0,  // Mode A: Portrait 4K (2160x3840) if src W < H, else Landscape 4K (3840x2160)
    Pillarbox = 1,   // Mode B: 3840x2160 canvas, center effective video with studio black padding
};

struct GeometryResult {
    int canvas_width = 3840;
    int canvas_height = 2160;
    int scaled_width = 3840;
    int scaled_height = 2160;
    int offset_x = 0;
    int offset_y = 0;
    int pillarbox_left = 0;
    int pillarbox_right = 0;
    bool is_portrait = false;
};

class SmartGeometryCalculator {
public:
    static GeometryResult calculate(int src_w, int src_h,
                                    GeometryTargetMode mode = GeometryTargetMode::AutoDirect,
                                    int user_target_w = 0, int user_target_h = 0);
};

}  // namespace rtx
