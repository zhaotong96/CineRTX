// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
//
// Scene-cut detection and resolution guards.  A hard cut must bypass optical
// flow and RIFE, otherwise the interpolator produces the classic "melted"
// cross-cut ghost frame.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "rtx/status.h"
#include "rtx/types.h"

namespace rtx {

struct SceneCutConfig {
    double threshold = 0.18;
    int sample_stride = 4;
    int min_interval_frames = 2;
};

struct SceneCutResult {
    bool cut = false;
    double normalized_sad = 0.0;
    double threshold = 0.18;
    uint64_t sampled_pixels = 0;
};

class SceneCutDetector {
public:
    explicit SceneCutDetector(SceneCutConfig config = {}) : config_(config) {}

    SceneCutResult analyze(const uint8_t* previous_nv12, const uint8_t* current_nv12, int width,
                           int height, int y_pitch) const;

    const SceneCutConfig& config() const noexcept { return config_; }
    uint64_t cutCount() const noexcept { return cut_count_; }
    uint64_t evaluateCount() const noexcept { return evaluate_count_; }
    void noteCut() noexcept { ++cut_count_; }

private:
    SceneCutConfig config_;
    mutable uint64_t cut_count_ = 0;
    mutable uint64_t evaluate_count_ = 0;
};

// Detects mid-stream resolution changes.  NGX contexts are size-specific, so a
// change must either reset the bridge or abort with a readable diagnostic.
class ResolutionGuard {
public:
    struct Config {
        bool allow_reset = true;
        int max_resets = 4;
    };

    ResolutionGuard() = default;
    explicit ResolutionGuard(Config config) : config_(config) {}

    // Returns true when a reset is required (and allowed).
    Result<bool> observe(int width, int height) {
        if (!initialized_) {
            initialized_ = true;
            width_ = width;
            height_ = height;
            return Result<bool>::ok(false);
        }
        if (width == width_ && height == height_) {
            return Result<bool>::ok(false);
        }
        if (!config_.allow_reset) {
            return Result<bool>::fail(
                ErrorCode::Unsupported,
                "input resolution changed mid-stream and dynamic reset is disabled",
                "initial=" + std::to_string(width_) + "x" + std::to_string(height_) +
                    " current=" + std::to_string(width) + "x" + std::to_string(height));
        }
        if (resets_ >= config_.max_resets) {
            return Result<bool>::fail(
                ErrorCode::State, "too many mid-stream resolution changes",
                "limit=" + std::to_string(config_.max_resets));
        }
        ++resets_;
        width_ = width;
        height_ = height;
        return Result<bool>::ok(true);
    }

    int resets() const noexcept { return resets_; }
    int width() const noexcept { return width_; }
    int height() const noexcept { return height_; }

private:
    Config config_;
    bool initialized_ = false;
    int width_ = 0;
    int height_ = 0;
    int resets_ = 0;
};

// Content-adaptive bitrate helper: computes the 99.9th percentile luma from a
// subsampled histogram.  Used by the encoder configuration and by the HDR
// MaxFALL estimator in the reference path.
class LumaHistogram {
public:
    void reset();
    void accumulateNv12(const uint8_t* nv12, int width, int height, int y_pitch, int stride = 4);
    uint32_t percentile(double fraction) const;
    double mean() const;
    // Fraction of accumulated samples with luma >= threshold (8-bit domain).
    double fractionAbove(uint8_t threshold) const;
    uint64_t samples() const noexcept { return samples_; }

private:
    uint64_t bins_[256] = {0};
    uint64_t samples_ = 0;
};

// Per-frame luma statistics feeding the TrueHDR adaptation law (expert
// directive B-2.2).  y_norm is the mean luma normalised to [0, 1]; p_high is
// the fraction of near-white samples (luma >= 235, the SDR 205-235 band that
// saturates first under an aggressive inverse tone map).
struct FrameLumaStats {
    double y_norm = 0.0;
    double p_high = 0.0;
};

FrameLumaStats lumaStats(const LumaHistogram& histogram);

// Smoothed TrueHDR parameter set on the 100-based scale used by
// RtxTrueHdrParameters and the NGX parameter dictionary.
struct TrueHdrAdaptiveParams {
    int contrast = 125;
    int middle_gray = 44;
    int saturation = 75;
};

// Tunables of the TrueHDR adaptation law (expert directive B-2.2).
struct TrueHdrAdaptiveConstants {
    double dark_y = 0.18;        // APL below this counts as a dark scene
    double high_p = 0.05;        // highlight ratio above this triggers protection
    double high_p_span = 0.15;   // normaliser for the highlight formulas
    int contrast_base = 125;
    int contrast_dark_min = 105;
    int contrast_highlight_cap = 110;
    int middle_gray_base = 44;
    int middle_gray_dark_max = 48;
    int middle_gray_min = 36;
    int saturation_base = 75;
    int saturation_min = 68;
};

// Content-adaptive TrueHDR parameters (expert directive B-2.2).  The golden
// 100-based baseline (contrast 125, saturation 75, middle gray 44) is kept
// for standard daylight content; dark scenes pull contrast back toward 105
// and lift middle gray so 8-bit quantisation steps are not amplified into
// visible noise, and highlight-heavy scenes drop middle gray toward 36 (and
// cap contrast at 110, saturation toward 68) so the 200-1000 nit roll-off
// keeps its headroom.  A temporal EMA (alpha 0.92) removes per-frame
// breathing; a scene cut jumps straight to the new shot's steady state.
class TrueHdrAdapter {
public:
    explicit TrueHdrAdapter(double smoothing = 0.92,
                            TrueHdrAdaptiveConstants constants = {});

    // Target parameters for the frame statistics, after temporal smoothing.
    // `scene_cut` resets the smoothed state to the target (no carry-over
    // breathing across a cut).
    TrueHdrAdaptiveParams update(double y_norm, double p_high, bool scene_cut);

    // Uns smoothed target for the statistics (the law itself, no temporal
    // filtering); exposed for tests and diagnostics.
    TrueHdrAdaptiveParams target(double y_norm, double p_high) const;

    void reset() noexcept { initialized_ = false; }
    bool initialized() const noexcept { return initialized_; }

private:
    double smoothing_;
    TrueHdrAdaptiveConstants constants_;
    bool initialized_ = false;
    double contrast_ = 125.0;
    double middle_gray_ = 44.0;
    double saturation_ = 75.0;
};

}  // namespace rtx
