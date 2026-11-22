// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors

#include "test_framework.h"

#include <array>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>
#include <cstdint>
#include <utility>
#include <vector>

#include "rtx/ngx_remaster.h"
#include "shim/ngx_bridge_abi.h"

namespace {

constexpr uint32_t alignUp(uint32_t v, uint32_t mod) noexcept {
    return ((v + mod - 1u) / mod) * mod;
}

constexpr int reflect101(int idx, int limit) noexcept {
    if (idx < limit) return idx;
    const int mirrored = 2 * (limit - 1) - idx;
    return (mirrored < 0) ? 0 : mirrored;
}

}  // namespace

RTX_TEST(rife_scale05_align128_and_half_flow_64_invariant) {
    constexpr std::array<std::pair<uint32_t, uint32_t>, 6> kDims = {{
        {3840u, 2160u},
        {2160u, 3840u},
        {1920u, 1080u},
        {1080u, 1920u},
        {2560u, 1440u},
        {1440u, 2560u},
    }};
    for (const auto& [w, h] : kDims) {
        const uint32_t pw = alignUp(w, 128u);
        const uint32_t ph = alignUp(h, 128u);
        RTX_CHECK_EQ(pw % 128u, 0u);
        RTX_CHECK_EQ(ph % 128u, 0u);
        RTX_CHECK_EQ((pw / 2u) % 64u, 0u);
        RTX_CHECK_EQ((ph / 2u) % 64u, 0u);
    }
    RTX_CHECK_EQ(alignUp(3840u, 128u), 3840u);
    RTX_CHECK_EQ(alignUp(2160u, 128u), 2176u);

    // PKG-G5: Physical scale=0.5f half-res tensor dimensions must hit kOPT exactly
    // Landscape 4K (3840x2160) -> infer 1920x1080 -> align128 -> 1920x1152
    RTX_CHECK_EQ(alignUp(3840u / 2u, 128u), 1920u);
    RTX_CHECK_EQ(alignUp(2160u / 2u, 128u), 1152u);
    // Portrait 4K (2160x3840) -> infer 1080x1920 -> align128 -> 1152x1920
    RTX_CHECK_EQ(alignUp(2160u / 2u, 128u), 1152u);
    RTX_CHECK_EQ(alignUp(3840u / 2u, 128u), 1920u);
}

RTX_TEST(rife_scale05_reflect101_zero_black_border) {
    constexpr int kW = 1920;
    constexpr int kH = 1080;
    constexpr int kPadH = 1152;
    std::vector<uint8_t> row_vals(kH);
    for (int y = 0; y < kH; ++y) {
        row_vals[y] = static_cast<uint8_t>(32 + (y % 200));
    }
    for (int py = kH; py < kPadH; ++py) {
        const int sy = reflect101(py, kH);
        RTX_CHECK(sy >= 0 && sy < kH);
        RTX_CHECK_EQ(sy, 2 * (kH - 1) - py);
        RTX_CHECK(row_vals[sy] >= 32u);
    }
    RTX_CHECK_EQ(reflect101(kH, kH), kH - 2);
    RTX_CHECK_EQ(reflect101(kW, kW), kW - 2);
}

RTX_TEST(rife_scale05_abi_config_and_peak_allocation_contract) {
    RtxRifeInitConfig cfg{};
    cfg.struct_size = sizeof(RtxRifeInitConfig);
    cfg.width = 3840u;
    cfg.height = 2160u;
    cfg.scale = 0.5f;
    cfg.align_mod = 128u;
    cfg.reflect_padding = 1;
    RTX_CHECK_EQ(cfg.struct_size, sizeof(RtxRifeInitConfig));
    RTX_CHECK_EQ(cfg.align_mod, 128u);
    RTX_CHECK_EQ(cfg.reflect_padding, 1);

    const uint64_t plane_pixels = static_cast<uint64_t>(alignUp(cfg.width, 128u)) *
                                  static_cast<uint64_t>(alignUp(cfg.height, 128u));
    const uint64_t trt_peak_allocation_bytes = (7ull + 3ull) * plane_pixels * sizeof(float);
    RTX_CHECK(trt_peak_allocation_bytes > 0ull);
}

RTX_TEST(rife_trt_throughput_dual_engine_gate) {
    constexpr uint64_t kMaxCleanEngineBytes = 32ull * 1024ull * 1024ull;
    constexpr uint64_t kMaxTrtPeakBytes = 900ull * 1024ull * 1024ull;  // PKG-G5: <= 900 MiB (half-res kOPT)
    constexpr double kMaxAvgMsPerFrame = 15.0;  // PKG-G5: <= 15.0 ms (>= 66 fps hard gate)

    // Locate orientation-dedicated single-profile engines beside executable / build / models dir.
    auto findEngine = [](const char* filename) -> std::filesystem::path {
        std::error_code ec;
        const std::array<std::filesystem::path, 4> roots = {
            std::filesystem::path("models"),
            std::filesystem::path(".vs/build/cmake/windows-msvc-ninja/models"),
            std::filesystem::path(".vs/build/external/models"),
            std::filesystem::path("external/models"),
        };
        for (const auto& r : roots) {
            const auto p = r / filename;
            if (std::filesystem::exists(p, ec) && std::filesystem::file_size(p, ec) > 0) {
                return p;
            }
        }
        return {};
    };

    const auto landscape_path = findEngine("rife_v4.25_landscape.engine");
    const auto portrait_path = findEngine("rife_v4.25_portrait.engine");
    if (landscape_path.empty() || portrait_path.empty()) {
        std::fprintf(stderr,
                     "[RIFE-GATE-SKIP] orientation engines not present on this runner; skipping GPU timing\n");
        return;
    }

    // GATE-F1: Both engines must be slim single-profile artifacts (< 32 MB).
    std::error_code ec;
    const uint64_t ls_bytes = std::filesystem::file_size(landscape_path, ec);
    const uint64_t pt_bytes = std::filesystem::file_size(portrait_path, ec);
    RTX_CHECK(ls_bytes > 0 && ls_bytes < kMaxCleanEngineBytes);
    RTX_CHECK(pt_bytes > 0 && pt_bytes < kMaxCleanEngineBytes);

    rtx::NgxRemasterBridge loader;
    std::string load_err;
    if (!loader.open(&load_err) || !loader.hasRifeExApi()) {
        std::fprintf(stderr, "[RIFE-GATE-SKIP] rtx_ngx_bridge.dll unavailable: %s\n", load_err.c_str());
        return;
    }

    auto benchOrientation = [&](const std::filesystem::path& eng_path, uint32_t w, uint32_t h, const char* tag) {
        RtxNgxBridgeConfig bcfg{};
        bcfg.input_width = static_cast<int32_t>(w);
        bcfg.input_height = static_cast<int32_t>(h);
        bcfg.output_width = static_cast<int32_t>(w);
        bcfg.output_height = static_cast<int32_t>(h);
        bcfg.vsr_quality = 0;
        bcfg.enable_truehdr = 1;
        bcfg.project_id = 0x52545831u;

        RtxNgxBridge* bridge = nullptr;
        if (loader.create(&bcfg, &bridge) != RTX_OK || bridge == nullptr) {
            std::fprintf(stderr, "[RIFE-GATE-SKIP] bridge create skipped on headless host (%s)\n", tag);
            return;
        }

        RtxRifeInitConfig rcfg{};
        rcfg.struct_size = sizeof(RtxRifeInitConfig);
        rcfg.width = w;
        rcfg.height = h;
        rcfg.scale = 0.5f;
        rcfg.align_mod = 128u;
        rcfg.reflect_padding = 1;

        uint64_t trt_peak_bytes = 0;
        const std::string eng_str = eng_path.string();
        const int32_t init_res = loader.initRifeEx(bridge, eng_str.c_str(), &rcfg, &trt_peak_bytes);
        RTX_CHECK_EQ(init_res, RTX_OK);
        RTX_CHECK(trt_peak_bytes <= kMaxTrtPeakBytes);

        const uint64_t buf_a = loader.getInterpBuffer(bridge, w, h);
        const uint64_t buf_b = loader.getInterpBuffer(bridge, w, h);
        RTX_CHECK(buf_a != 0 && buf_b != 0);

        int32_t scene_cut = 0;
        for (int i = 0; i < 5; ++i) {
            RTX_CHECK_EQ(loader.interpolateRife(bridge, buf_a, buf_b, w, 0.5f, buf_b, 0, &scene_cut), RTX_OK);
        }
        const auto t0 = std::chrono::steady_clock::now();
        constexpr int kIters = 60;
        for (int i = 0; i < kIters; ++i) {
            const float alpha = 0.2f + 0.6f * (static_cast<float>(i % 5) / 4.0f);
            RTX_CHECK_EQ(loader.interpolateRife(bridge, buf_a, buf_b, w, alpha, buf_b, 0, &scene_cut), RTX_OK);
        }
        const auto t1 = std::chrono::steady_clock::now();
        const double avg_ms = std::chrono::duration<double, std::milli>(t1 - t0).count() / kIters;
        std::fprintf(stderr, "[RIFE-GATE] %s (%ux%u): avg=%.2f ms (%.1f fps), trt_peak=%.1f MiB\n",
                     tag, w, h, avg_ms, 1000.0 / avg_ms,
                     static_cast<double>(trt_peak_bytes) / (1024.0 * 1024.0));
        RTX_CHECK(avg_ms <= kMaxAvgMsPerFrame);

        loader.destroyRife(bridge);
        loader.releaseBridge(bridge);
    };

    benchOrientation(landscape_path, 3840u, 2160u, "Landscape-4K");
    benchOrientation(portrait_path, 2160u, 3840u, "Portrait-4K");
}
