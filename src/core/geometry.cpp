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
    if (src_w <= 0 || src_h <= 0) {
        return {3840, 2160, 3840, 2160, 0, 0, 0, 0, false};
    }
    const bool is_portrait = (src_w < src_h);
    const bool has_user_target = (user_target_w > 0 && user_target_h > 0);
    const int base_w = has_user_target ? alignEven(user_target_w) : 3840;
    const int base_h = has_user_target ? alignEven(user_target_h) : 2160;

    if (!is_portrait) {
        return {base_w, base_h, base_w, base_h, 0, 0, 0, 0, false};
    }
    if (mode == GeometryTargetMode::AutoDirect) {
        int target_w = 0;
        int target_h = 0;
        if (has_user_target && user_target_w <= user_target_h) {
            target_w = alignEven(user_target_w);
            target_h = alignEven(user_target_h);
        } else {
            target_h = (std::max)(base_w, base_h);
            target_w = alignEven(static_cast<int>(
                std::llround(static_cast<double>(target_h) * static_cast<double>(src_w) /
                             static_cast<double>(src_h))));
        }
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
        const int cw = (std::max)(target_w, min_w);
        const int ch = (std::max)(target_h, min_h);
        return {cw, ch, cw, ch, 0, 0, 0, 0, true};
    }
    int scaled_w = static_cast<int>(static_cast<int64_t>(base_h) * src_w / src_h);
    scaled_w = std::clamp(scaled_w & ~1, 2, base_w);
    const int offset_x = ((base_w - scaled_w) / 2) & ~1;
    const int pillarbox_right = base_w - scaled_w - offset_x;
    return {base_w, base_h, scaled_w, base_h, offset_x, 0, offset_x, pillarbox_right, true};
}

}  // namespace rtx
