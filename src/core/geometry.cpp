// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors

#include "rtx/geometry.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace rtx {

namespace {

constexpr int alignEven(int value) noexcept {
    return (value > 0) ? ((value + 1) & ~1) : 0;
}

}  // namespace

GeometryResult calculateSmartGeometry(int src_w, int src_h,
                                      GeometryTargetMode mode,
                                      int user_target_w, int user_target_h) {
    GeometryResult res;
    if (src_w <= 0 || src_h <= 0) {
        return res;
    }
    res.is_portrait = (src_w < src_h);

    const bool has_user_target = (user_target_w > 0 && user_target_h > 0);
    const int base_w = has_user_target ? alignEven(user_target_w) : 3840;
    const int base_h = has_user_target ? alignEven(user_target_h) : 2160;

    if (res.is_portrait) {
        if (mode == GeometryTargetMode::AutoDirect) {
            int target_w = 0;
            int target_h = 0;
            if (has_user_target && user_target_w <= user_target_h) {
                target_w = alignEven(user_target_w);
                target_h = alignEven(user_target_h);
            } else {
                // AutoDirect: flip landscape preset (4K/2K/FHD) to portrait preserving source aspect ratio.
                target_h = (std::max)(base_w, base_h);
                target_w = alignEven(static_cast<int>(
                    std::llround(static_cast<double>(target_h) * static_cast<double>(src_w) /
                                 static_cast<double>(src_h))));
            }
            // Floor safeguard: never shrink below portrait source dimensions (prevents VSR downscale rejection).
            const int min_w = alignEven(src_w);
            const int min_h = alignEven(src_h);
            if (target_h < min_h) {
                target_h = min_h;
                target_w = alignEven(static_cast<int>(
                    std::llround(static_cast<double>(target_h) * static_cast<double>(src_w) /
                                 static_cast<double>(src_h))));
            }
            if (target_w < min_w) {
                target_w = min_w;
                target_h = alignEven(static_cast<int>(
                    std::llround(static_cast<double>(target_w) * static_cast<double>(src_h) /
                                 static_cast<double>(src_w))));
            }
            res.canvas_width = (std::max)(target_w, min_w);
            res.canvas_height = (std::max)(target_h, min_h);
            res.scaled_width = res.canvas_width;
            res.scaled_height = res.canvas_height;
            res.offset_x = 0;
            res.offset_y = 0;
            res.pillarbox_left = 0;
            res.pillarbox_right = 0;
        } else {
            // Mode B: Landscape canvas with centered portrait video & studio black pillarbox padding.
            res.canvas_width = base_w;
            res.canvas_height = base_h;
            res.scaled_height = res.canvas_height;
            int scaled_w = static_cast<int>(
                static_cast<int64_t>(res.scaled_height) * src_w / src_h);
            scaled_w = std::clamp(scaled_w & ~1, 2, res.canvas_width);
            res.scaled_width = scaled_w;
            res.offset_x = ((res.canvas_width - scaled_w) / 2) & ~1;
            res.offset_y = 0;
            res.pillarbox_left = res.offset_x;
            res.pillarbox_right = res.canvas_width - res.scaled_width - res.pillarbox_left;
        }
    } else {
        res.canvas_width = base_w;
        res.canvas_height = base_h;
        res.scaled_width = base_w;
        res.scaled_height = base_h;
        res.offset_x = 0;
        res.offset_y = 0;
        res.pillarbox_left = 0;
        res.pillarbox_right = 0;
    }
    return res;
}

}  // namespace rtx
