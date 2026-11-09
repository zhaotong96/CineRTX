// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors

#include "rtx/geometry.h"

#include <algorithm>
#include <cstdint>

namespace rtx {

GeometryResult SmartGeometryCalculator::calculate(int src_w, int src_h,
                                                 GeometryTargetMode mode,
                                                 int user_target_w, int user_target_h) {
    GeometryResult res;
    if (src_w <= 0 || src_h <= 0) {
        return res;
    }

    res.is_portrait = (src_w < src_h);

    const bool is_default_landscape_4k = (user_target_w == 3840 && user_target_h == 2160);
    if (user_target_w > 0 && user_target_h > 0 && !(res.is_portrait && is_default_landscape_4k)) {
        res.canvas_width = user_target_w;
        res.canvas_height = user_target_h;
        res.scaled_width = user_target_w;
        res.scaled_height = user_target_h;
        res.offset_x = 0;
        res.offset_y = 0;
        res.pillarbox_left = 0;
        res.pillarbox_right = 0;
        return res;
    }

    if (res.is_portrait) {
        if (mode == GeometryTargetMode::AutoDirect) {
            // Mode A: Portrait 4K (2160x3840) direct out
            res.canvas_width = 2160;
            res.canvas_height = 3840;
            res.scaled_width = 2160;
            res.scaled_height = 3840;
            res.offset_x = 0;
            res.offset_y = 0;
            res.pillarbox_left = 0;
            res.pillarbox_right = 0;
        } else {
            // Mode B: 3840x2160 landscape canvas with center video & pillarbox padding
            res.canvas_width = 3840;
            res.canvas_height = 2160;
            res.scaled_height = 2160;
            int scaled_w = static_cast<int>(static_cast<int64_t>(2160) * src_w / src_h);
            scaled_w &= ~1;  // Align to 2 for YUV 4:2:0 subsampling
            res.scaled_width = scaled_w;
            res.offset_x = ((3840 - scaled_w) / 2) & ~1;
            res.pillarbox_left = res.offset_x;
            res.pillarbox_right = 3840 - res.scaled_width - res.pillarbox_left;
        }
    } else {
        // Standard landscape 4K (3840x2160)
        res.canvas_width = 3840;
        res.canvas_height = 2160;
        res.scaled_width = 3840;
        res.scaled_height = 2160;
        res.offset_x = 0;
        res.offset_y = 0;
        res.pillarbox_left = 0;
        res.pillarbox_right = 0;
    }

    return res;
}

}  // namespace rtx
