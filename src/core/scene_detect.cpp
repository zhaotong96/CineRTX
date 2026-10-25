// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors

#include "rtx/scene_detect.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace rtx {

SceneCutResult SceneCutDetector::analyze(const uint8_t* previous_nv12, const uint8_t* current_nv12,
                                         int width, int height, int y_pitch) const {
    ++evaluate_count_;
    SceneCutResult result;
    result.threshold = config_.threshold;
    if (previous_nv12 == nullptr || current_nv12 == nullptr || width <= 0 || height <= 0) {
        return result;
    }
    const int stride = std::max(1, config_.sample_stride);
    uint64_t sad = 0;
    uint64_t samples = 0;
    for (int y = 0; y < height; y += stride) {
        const uint8_t* previous_row = previous_nv12 + static_cast<size_t>(y) * y_pitch;
        const uint8_t* current_row = current_nv12 + static_cast<size_t>(y) * y_pitch;
        for (int x = 0; x < width; x += stride) {
            sad += static_cast<uint64_t>(
                std::abs(static_cast<int>(previous_row[x]) - static_cast<int>(current_row[x])));
            ++samples;
        }
    }
    result.sampled_pixels = samples;
    if (samples != 0) {
        result.normalized_sad = static_cast<double>(sad) / (static_cast<double>(samples) * 255.0);
        result.cut = result.normalized_sad >= config_.threshold;
        if (result.cut) {
            ++cut_count_;
        }
    }
    return result;
}

void LumaHistogram::reset() {
    std::fill(std::begin(bins_), std::end(bins_), 0);
    samples_ = 0;
}

void LumaHistogram::accumulateNv12(const uint8_t* nv12, int width, int height, int y_pitch,
                                   int stride) {
    if (nv12 == nullptr || width <= 0 || height <= 0) {
        return;
    }
    const int step = std::max(1, stride);
    for (int y = 0; y < height; y += step) {
        const uint8_t* row = nv12 + static_cast<size_t>(y) * y_pitch;
        for (int x = 0; x < width; x += step) {
            ++bins_[row[x]];
            ++samples_;
        }
    }
}

uint32_t LumaHistogram::percentile(double fraction) const {
    if (samples_ == 0) {
        return 0;
    }
    const uint64_t target = static_cast<uint64_t>(
        std::clamp(fraction, 0.0, 1.0) * static_cast<double>(samples_));
    uint64_t cumulative = 0;
    for (int i = 0; i < 256; ++i) {
        cumulative += bins_[i];
        if (cumulative >= target) {
            return static_cast<uint32_t>(i);
        }
    }
    return 255;
}

double LumaHistogram::mean() const {
    if (samples_ == 0) {
        return 0.0;
    }
    double sum = 0.0;
    for (int i = 0; i < 256; ++i) {
        sum += static_cast<double>(i) * static_cast<double>(bins_[i]);
    }
    return sum / static_cast<double>(samples_);
}

double LumaHistogram::fractionAbove(uint8_t threshold) const {
    if (samples_ == 0) {
        return 0.0;
    }
    uint64_t above = 0;
    for (int i = threshold; i < 256; ++i) {
        above += bins_[i];
    }
    return static_cast<double>(above) / static_cast<double>(samples_);
}

FrameLumaStats lumaStats(const LumaHistogram& histogram) {
    FrameLumaStats stats;
    stats.y_norm = histogram.mean() / 255.0;
    stats.p_high = histogram.fractionAbove(235);
    return stats;
}

TrueHdrAdapter::TrueHdrAdapter(double smoothing, TrueHdrAdaptiveConstants constants)
    : smoothing_(smoothing), constants_(constants) {}

TrueHdrAdaptiveParams TrueHdrAdapter::target(double y_norm, double p_high) const {
    const TrueHdrAdaptiveConstants c = constants_;
    const double y = std::clamp(y_norm, 0.0, 1.0);
    const double p = std::clamp(p_high, 0.0, 1.0);

    // Contrast follows the APL law; highlight-heavy frames are additionally
    // capped at 110 so the 200-1000 nit roll-off keeps headroom.
    double contrast = y >= c.dark_y ? static_cast<double>(c.contrast_base)
                                    : c.contrast_dark_min +
                                          (c.contrast_base - c.contrast_dark_min) * (y / c.dark_y);
    if (p > c.high_p) {
        contrast = std::min(contrast, static_cast<double>(c.contrast_highlight_cap));
    }

    // Middle gray: highlight protection takes precedence, then the dark-scene
    // lift, otherwise the golden baseline.
    double middle_gray = static_cast<double>(c.middle_gray_base);
    const double span = std::max(1e-6, c.high_p_span);
    if (p > c.high_p) {
        const double highlight_ratio = std::min(1.0, (p - c.high_p) / span);
        middle_gray = c.middle_gray_base -
                      (c.middle_gray_base - c.middle_gray_min) * highlight_ratio;
    } else if (y < c.dark_y) {
        middle_gray = c.middle_gray_base +
                      (c.middle_gray_dark_max - c.middle_gray_base) * (1.0 - y / c.dark_y);
    }

    // Saturation converges toward its floor on highlight-heavy frames.
    double saturation = static_cast<double>(c.saturation_base);
    if (p > c.high_p) {
        const double highlight_ratio = std::min(1.0, (p - c.high_p) / span);
        saturation = c.saturation_base - (c.saturation_base - c.saturation_min) * highlight_ratio;
    }

    TrueHdrAdaptiveParams params;
    params.contrast = static_cast<int>(std::lround(contrast));
    params.middle_gray = static_cast<int>(std::lround(middle_gray));
    params.saturation = static_cast<int>(std::lround(saturation));
    return params;
}

TrueHdrAdaptiveParams TrueHdrAdapter::update(double y_norm, double p_high, bool scene_cut) {
    const TrueHdrAdaptiveParams goal = target(y_norm, p_high);
    if (!initialized_ || scene_cut) {
        // First frame, or a cut: jump straight to the new shot's steady state.
        contrast_ = goal.contrast;
        middle_gray_ = goal.middle_gray;
        saturation_ = goal.saturation;
        initialized_ = true;
    } else {
        contrast_ = smoothing_ * contrast_ + (1.0 - smoothing_) * goal.contrast;
        middle_gray_ = smoothing_ * middle_gray_ + (1.0 - smoothing_) * goal.middle_gray;
        saturation_ = smoothing_ * saturation_ + (1.0 - smoothing_) * goal.saturation;
    }
    TrueHdrAdaptiveParams params;
    params.contrast = static_cast<int>(std::lround(contrast_));
    params.middle_gray = static_cast<int>(std::lround(middle_gray_));
    params.saturation = static_cast<int>(std::lround(saturation_));
    return params;
}

}  // namespace rtx
