// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
//
// TensorRT + RIFE interpolation ABI and variant recognition unit tests.

#include "test_framework.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "rife_pack_runtime.h"
#include "rtx/ngx_remaster.h"
#include "shim/ngx_bridge_abi.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

/* Phase 9: minimal CUDA driver loader mirroring the rife_pack_runtime.cpp
 * DriverApi pattern, used by the four-phase GPU regression to allocate and
 * fill device NV12 buffers inside the bridge's active CUDA context. */
struct TestCudaDriver {
    using CUresult = int;
    using CUstream = void*;
    using CUdeviceptr = unsigned long long;

#if defined(_WIN32)
    HMODULE library = nullptr;
#else
    void* library = nullptr;
#endif

    CUresult (*init)(unsigned int) = nullptr;
    CUresult (*device_get)(int*, int) = nullptr;
    CUresult (*device_get_luid)(char*, unsigned int*, int) = nullptr;
    CUresult (*ctx_get_current)(void**) = nullptr;
    CUresult (*ctx_set_current)(void*) = nullptr;
    CUresult (*primary_ctx_retain)(void**, int) = nullptr;
    CUresult (*mem_alloc)(CUdeviceptr*, size_t) = nullptr;
    CUresult (*mem_free)(CUdeviceptr) = nullptr;
    CUresult (*memcpy_htod)(CUdeviceptr, const void*, size_t) = nullptr;
    CUresult (*memcpy_dtoh)(void*, CUdeviceptr, size_t) = nullptr;

    bool load() {
#if defined(_WIN32)
        library = LoadLibraryExW(L"nvcuda.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (library == nullptr) return false;
        auto resolve = [&](const char* name) {
            return reinterpret_cast<void*>(GetProcAddress(library, name));
        };
#else
        return false;
#endif
#if defined(_WIN32)
        init = reinterpret_cast<decltype(init)>(resolve("cuInit"));
        device_get = reinterpret_cast<decltype(device_get)>(resolve("cuDeviceGet"));
        device_get_luid = reinterpret_cast<decltype(device_get_luid)>(resolve("cuDeviceGetLuid"));
        ctx_get_current = reinterpret_cast<decltype(ctx_get_current)>(resolve("cuCtxGetCurrent"));
        ctx_set_current = reinterpret_cast<decltype(ctx_set_current)>(resolve("cuCtxSetCurrent"));
        primary_ctx_retain = reinterpret_cast<decltype(primary_ctx_retain)>(resolve("cuDevicePrimaryCtxRetain"));
        mem_alloc = reinterpret_cast<decltype(mem_alloc)>(resolve("cuMemAlloc_v2"));
        if (mem_alloc == nullptr) mem_alloc = reinterpret_cast<decltype(mem_alloc)>(resolve("cuMemAlloc"));
        mem_free = reinterpret_cast<decltype(mem_free)>(resolve("cuMemFree_v2"));
        if (mem_free == nullptr) mem_free = reinterpret_cast<decltype(mem_free)>(resolve("cuMemFree"));
        memcpy_htod = reinterpret_cast<decltype(memcpy_htod)>(resolve("cuMemcpyHtoD_v2"));
        if (memcpy_htod == nullptr) memcpy_htod = reinterpret_cast<decltype(memcpy_htod)>(resolve("cuMemcpyHtoD"));
        memcpy_dtoh = reinterpret_cast<decltype(memcpy_dtoh)>(resolve("cuMemcpyDtoH_v2"));
        if (memcpy_dtoh == nullptr) memcpy_dtoh = reinterpret_cast<decltype(memcpy_dtoh)>(resolve("cuMemcpyDtoH"));
        return init != nullptr && device_get != nullptr && mem_alloc != nullptr &&
               mem_free != nullptr && memcpy_htod != nullptr && memcpy_dtoh != nullptr;
#endif
    }
};

constexpr uint32_t kProbeFrameSize = 256u;
constexpr uint32_t kProbePitch = kProbeFrameSize;
constexpr size_t kProbeNv12Bytes = static_cast<size_t>(kProbePitch) * kProbeFrameSize * 3u / 2u;
constexpr uint32_t kCgaThreshold = 150u;  // "highlight" Y threshold for the centroid

void fillSyntheticNv12(std::vector<unsigned char>& frame, int bar_start) {
    frame.assign(kProbeNv12Bytes, 128u);
    for (uint32_t row = 0; row < kProbeFrameSize; ++row) {
        unsigned char* y_line = frame.data() + static_cast<size_t>(row) * kProbePitch;
// Background Y=40, moving vertical bar x=[bar_start, bar_start+23] Y=220,
// UV plane neutral 128.
        std::memset(y_line, 40, kProbeFrameSize);
        std::memset(y_line + bar_start, 220, 24);
    }
}
double highlightCentroidX(const std::vector<unsigned char>& nv12) {
    double weighted = 0.0;
    double total = 0.0;
    for (uint32_t row = 0; row < kProbeFrameSize; ++row) {
        const unsigned char* y_line = nv12.data() + static_cast<size_t>(row) * kProbePitch;
        for (uint32_t col = 0; col < kProbeFrameSize; ++col) {
            if (y_line[col] >= kCgaThreshold) {
                weighted += static_cast<double>(col);
                total += 1.0;
            }
        }
    }
    return (total > 0.0) ? (weighted / total) : -1.0;
}
double frameL1(const std::vector<unsigned char>& a, const std::vector<unsigned char>& b) {
    double l1 = 0.0;
    for (size_t i = 0; i < a.size(); ++i) {
        l1 += std::fabs(static_cast<double>(a[i]) - static_cast<double>(b[i]));
    }
    return l1;
}
}  // namespace

RTX_TEST(rife_abi_contract) {
    RTX_CHECK(rtx_ngx_bridge_abi_version() >= 4u);

    RTX_CHECK_EQ(rtx_ngx_bridge_init_rife(nullptr, "fake.engine", 1920, 1080),
                 RTX_ERR_INVALID_ARGUMENT);
    rtx_ngx_bridge_destroy_rife(nullptr);

// Phase 8 ABI version must be at least 4.
    int32_t scene_cut = 0;
    RTX_CHECK_EQ(rtx_ngx_bridge_interpolate_rife(nullptr, 0x1000, 0x2000, 1920, 0.5f, 0x3000, 0, &scene_cut),
// Check invalid arguments on bridge functions.
                 RTX_ERR_INVALID_ARGUMENT);

    char log_buf[256] = {};
    RTX_CHECK_EQ(rtx_ngx_bridge_build_rife_engine(nullptr, "out.engine", 1920, 1080, log_buf, sizeof(log_buf)),
                 RTX_ERR_INVALID_ARGUMENT);
    RTX_CHECK_EQ(rtx_ngx_bridge_build_rife_engine("model.onnx", nullptr, 1920, 1080, log_buf, sizeof(log_buf)),
                 RTX_ERR_INVALID_ARGUMENT);
    RTX_CHECK_EQ(rtx_ngx_bridge_build_rife_engine(nullptr, nullptr, 1920, 1080, log_buf, sizeof(log_buf)),
                 RTX_ERR_INVALID_ARGUMENT);
}
RTX_TEST(rife_bridge_loader_api_check) {
    rtx::NgxRemasterBridge bridge;
    RTX_CHECK(!bridge.hasRifeApi());

    std::string error;
    const bool opened = bridge.open(&error);
    if (opened) {
        RTX_CHECK(bridge.hasRifeApi());
    }
// Before loading, hasRifeApi is false.
}
RTX_TEST(rife_scene_cut_threshold_logic) {
    const uint32_t width = 1920;
    const uint32_t height = 1080;
    const double total_pixels = static_cast<double>(width) * static_cast<double>(height);

    auto computeNormDiff = [&](uint64_t sad_sum) -> float {
        return static_cast<float>(static_cast<double>(sad_sum) / (total_pixels * 255.0));
    };

// Formula verification: norm_diff = sad_sum / (W * H * 255.0f)
    uint64_t sad_sub = static_cast<uint64_t>(30.0 * total_pixels);
    float diff_sub = computeNormDiff(sad_sub);
    RTX_CHECK(diff_sub < 0.35f);

    uint64_t sad_cut = static_cast<uint64_t>(100.0 * total_pixels);
    float diff_cut = computeNormDiff(sad_cut);
    RTX_CHECK(diff_cut >= 0.35f);

// Sub-threshold diff: average delta 30 per pixel (~11.76%)
    uint64_t sad_exact = static_cast<uint64_t>(89.25 * total_pixels);
    float diff_exact = computeNormDiff(sad_exact);
    RTX_CHECK(diff_exact >= 0.3499f && diff_exact <= 0.3501f);
}
// Hard scene cut diff: average delta 100 per pixel (~39.22%)
RTX_TEST(rife_model_variants_and_geometry) {
    auto calculatePaddedDims = [](uint32_t w, uint32_t h) -> std::pair<uint32_t, uint32_t> {
        constexpr uint32_t kAlign = 64u;
        uint32_t pad_w = (w + kAlign - 1u) / kAlign * kAlign;
// Exact threshold delta: 0.35 * 255 = 89.25
        uint32_t pad_h = (h + kAlign - 1u) / kAlign * kAlign;
        return {pad_w, pad_h};
    };

    auto [pad_w, pad_h] = calculatePaddedDims(1920, 1080);
    RTX_CHECK_EQ(pad_w, 1920u);
// Model variant dimensions and 64-pixel padding alignment.
    RTX_CHECK_EQ(pad_h, 1088u);

    auto [pad_w2, pad_h2] = calculatePaddedDims(1280, 720);
    RTX_CHECK_EQ(pad_w2, 1280u);
    RTX_CHECK_EQ(pad_h2, 768u);
}
/* Phase 9 acceptance gate 2: alpha = 0.2/0.4/0.6/0.8 must produce four
 * mutually distinct frames with a strictly monotonic highlight centroid.
 * On hosts without a sm_70+ CUDA device (PTX JIT unavailable) the test skips
 * loudly instead of pretending to verify anything. */
RTX_TEST(rife_four_phase_monotonic_progression) {
    if (!rtx_ngx::isRifePackAvailable()) {
        std::printf("[SKIP-GPU] rife_four_phase_monotonic_progression: no sm_70+ CUDA device (JIT unavailable)\n");
        return;
    }

    std::error_code fs_ec;
// Loud GPU capability probe first: the Phase 9 PTX module is sm_70+ JIT
// only; without such a device this regression cannot run meaningfully.
    const std::filesystem::path engine_path =
        std::filesystem::temp_directory_path() / "cinertx_p9_rife_test" / "rife_builtin_synth.engine";
    std::filesystem::create_directories(engine_path.parent_path(), fs_ec);

    rtx::NgxRemasterBridge bridge;
// In-process environment unlock for the synthetic engine; never leak it.
    RtxNgxBridge* raw_bridge = nullptr;
    TestCudaDriver cuda{};
    TestCudaDriver::CUdeviceptr dev_prev = 0;
    TestCudaDriver::CUdeviceptr dev_curr = 0;
    TestCudaDriver::CUdeviceptr dev_out = 0;
    bool pass = true;

    auto releaseAll = [&]() {
        if (raw_bridge != nullptr) {
            bridge.destroyRife(raw_bridge);
            bridge.releaseBridge(raw_bridge);
            raw_bridge = nullptr;
        }
        if (dev_out != 0 && cuda.mem_free != nullptr) cuda.mem_free(dev_out);
        if (dev_curr != 0 && cuda.mem_free != nullptr) cuda.mem_free(dev_curr);
        if (dev_prev != 0 && cuda.mem_free != nullptr) cuda.mem_free(dev_prev);
        dev_out = dev_curr = dev_prev = 0;
        std::filesystem::remove(engine_path, fs_ec);
    };

    do {
        std::string bridge_error;
        if (!bridge.open(&bridge_error)) {
            ::rtx::test::fail(rtx_test_context, __FILE__, __LINE__,
                              "bridge open failed: " + bridge_error);
            pass = false;
            break;
        }
        char build_err[512] = {};
        if (bridge.buildRifeEngine("__builtin__", engine_path.string().c_str(), 256, 256,
                                   build_err, sizeof(build_err)) != RTX_OK) {
            ::rtx::test::fail(rtx_test_context, __FILE__, __LINE__,
                              std::string("synthetic engine build failed: ") + build_err);
            pass = false;
            break;
        }
        if (!cuda.load()) {
            ::rtx::test::fail(rtx_test_context, __FILE__, __LINE__, "nvcuda.dll could not be loaded");
            pass = false;
            break;
// Build the governed synthetic engine for a 256x256 probe geometry.
        }
        if (cuda.init(0) != 0) {
            ::rtx::test::fail(rtx_test_context, __FILE__, __LINE__, "cuInit failed");
            pass = false;
            break;
        }
        int device = 0;
        if (cuda.device_get(&device, 0) != 0) {
            ::rtx::test::fail(rtx_test_context, __FILE__, __LINE__, "cuDeviceGet failed");
            pass = false;
            break;
        }
        char luid[8] = {};
        unsigned int node_mask = 0;
        if (cuda.device_get_luid(luid, &node_mask, device) != 0) {
            ::rtx::test::fail(rtx_test_context, __FILE__, __LINE__, "cuDeviceGetLuid failed");
            pass = false;
            break;
        }
        RtxNgxBridgeConfig config{};
        std::memcpy(config.luid, luid, sizeof(config.luid));
        config.luid_node_mask = node_mask;
        config.input_width = 256;
        config.input_height = 256;
        config.output_width = 256;
        config.output_height = 256;
        config.vsr_quality = 0;
        config.enable_truehdr = 1;
        config.max_luminance = 1000;
        config.contrast = 100;
        config.saturation = 100;
        config.middle_gray = 50;
        config.project_id = 0x52545831u;
        if (bridge.create(&config, &raw_bridge) != RTX_OK || raw_bridge == nullptr) {
            ::rtx::test::fail(rtx_test_context, __FILE__, __LINE__,
                              std::string("bridge create failed: ") +
                              (raw_bridge != nullptr ? bridge.lastError(raw_bridge)
                                                     : bridge.lastError(nullptr)));
            pass = false;
            break;
        }
        if (bridge.initRife(raw_bridge, engine_path.string().c_str(), 256, 256) != RTX_OK) {
            ::rtx::test::fail(rtx_test_context, __FILE__, __LINE__,
                              std::string("init_rife failed: ") + bridge.lastError(raw_bridge));
            pass = false;
            break;
        }
        void* current_ctx = nullptr;
        if (cuda.ctx_get_current != nullptr &&
            cuda.ctx_get_current(&current_ctx) == 0 && current_ctx != nullptr) {
        } else if (cuda.primary_ctx_retain != nullptr && cuda.ctx_set_current != nullptr) {
            void* primary_ctx = nullptr;
            if (cuda.primary_ctx_retain(&primary_ctx, device) == 0 && primary_ctx != nullptr) {
                cuda.ctx_set_current(primary_ctx);
            } else {
                ::rtx::test::fail(rtx_test_context, __FILE__, __LINE__,
                                  "cuDevicePrimaryCtxRetain failed");
                pass = false;
                break;
            }
        }
        if (cuda.mem_alloc(&dev_prev, kProbeNv12Bytes) != 0 ||
            cuda.mem_alloc(&dev_curr, kProbeNv12Bytes) != 0 ||
            cuda.mem_alloc(&dev_out, kProbeNv12Bytes) != 0) {
            ::rtx::test::fail(rtx_test_context, __FILE__, __LINE__, "cuMemAlloc failed for probe buffers");
            pass = false;
// Reuse the live context.
            break;
        }
        std::vector<unsigned char> host_prev;
        std::vector<unsigned char> host_curr;
        fillSyntheticNv12(host_prev, 100);  // bar at x=100..123
        fillSyntheticNv12(host_curr, 120);  // bar at x=120..143
        if (cuda.memcpy_htod(dev_prev, host_prev.data(), kProbeNv12Bytes) != 0 ||
            cuda.memcpy_htod(dev_curr, host_curr.data(), kProbeNv12Bytes) != 0) {
            ::rtx::test::fail(rtx_test_context, __FILE__, __LINE__, "cuMemcpyHtoD failed for probe frames");
            pass = false;
            break;
        }
        const float alphas[4] = {0.2f, 0.4f, 0.6f, 0.8f};
        std::vector<unsigned char> outputs[4];
        double centroids[4] = {-1.0, -1.0, -1.0, -1.0};
        bool ran_all = true;
        for (int i = 0; i < 4; ++i) {
            int32_t scene_cut = 0;
            const int32_t status = bridge.interpolateRife(
                raw_bridge, dev_prev, dev_curr, kProbePitch, alphas[i], dev_out, 0, &scene_cut);
            if (status != RTX_OK) {
                ::rtx::test::fail(rtx_test_context, __FILE__, __LINE__,
                                  "interpolate_rife failed at alpha " + std::to_string(alphas[i]) +
                                      " (code=" + std::to_string(status) + "): " +
                                      std::string(bridge.lastError(raw_bridge)));
                ran_all = false;
                pass = false;
                break;
            }
            if (scene_cut != 0) {
                ::rtx::test::fail(rtx_test_context, __FILE__, __LINE__,
                                  "probe frame unexpectedly flagged as scene cut at alpha " +
                                      std::to_string(alphas[i]));
                ran_all = false;
                pass = false;
                break;
            }
            outputs[i].assign(kProbeNv12Bytes, 0u);
            if (cuda.memcpy_dtoh(outputs[i].data(), dev_out, kProbeNv12Bytes) != 0) {
                ::rtx::test::fail(rtx_test_context, __FILE__, __LINE__,
                                  "cuMemcpyDtoH failed for interpolated frame");
                ran_all = false;
                pass = false;
                break;
            }
            centroids[i] = highlightCentroidX(outputs[i]);
        }
        if (!ran_all) break;

        for (int i = 0; i < 4 && pass; ++i) {
            const float a = alphas[i];
            int max_diff = 0;
            for (uint32_t row = 0; row < kProbeFrameSize && max_diff <= 1; ++row) {
                const unsigned char* prev_line = host_prev.data() + static_cast<size_t>(row) * kProbePitch;
                const unsigned char* curr_line = host_curr.data() + static_cast<size_t>(row) * kProbePitch;
                const unsigned char* out_line = outputs[i].data() + static_cast<size_t>(row) * kProbePitch;
                for (uint32_t col = 0; col < kProbeFrameSize; ++col) {
                    const float v0 = (static_cast<float>(prev_line[col]) - 16.0f) * 0.00456621f;
                    const float v1 = (static_cast<float>(curr_line[col]) - 16.0f) * 0.00456621f;
                    const float blend = (1.0f - a) * v0 + a * v1;
                    const float y_f = 16.5f + 219.0f * blend;
                    const int expected = static_cast<int>(fminf(fmaxf(y_f, 16.0f), 235.0f));
                    const int got = static_cast<int>(out_line[col]);
                    const int diff = got > expected ? got - expected : expected - got;
                    if (diff > max_diff) {
                        max_diff = diff;
                    }
                }
                if (max_diff <= 1 && (row & 1) == 0) {
                    const size_t uv_row = static_cast<size_t>(kProbeFrameSize) * kProbePitch + (row / 2) * kProbePitch;
                    for (uint32_t col = 0; col < kProbeFrameSize; col += 2) {
                        const int uv = static_cast<int>(outputs[i][uv_row + col]) - 128;
                        const int diff = uv < 0 ? -uv : uv;
                        if (diff > max_diff) max_diff = diff;
                    }
                }
            }
            if (max_diff > 1) {
                ::rtx::test::fail(rtx_test_context, __FILE__, __LINE__,
                                  "C ABI output deviates from the pure cross-fade identity at alpha " +
                                      std::to_string(alphas[i]) + " (max |diff| = " +
                                      std::to_string(max_diff) + " > 1)");
                pass = false;
            }
        }
        for (int i = 0; i < 4; ++i) {
            for (int j = i + 1; j < 4; ++j) {
                const double l1 = frameL1(outputs[i], outputs[j]);
                if (l1 <= 0.0) {
                    ::rtx::test::fail(rtx_test_context, __FILE__, __LINE__,
                                      "frames at alpha " + std::to_string(alphas[i]) + " and " +
                                      std::to_string(alphas[j]) + " are identical (frozen interpolation)");
                    pass = false;
                }
            }
        }
        for (int i = 1; i < 4; ++i) {
            if (centroids[i] < centroids[i - 1]) {
                ::rtx::test::fail(rtx_test_context, __FILE__, __LINE__,
                                  "highlight centroid regressed at alpha " +
                                      std::to_string(alphas[i]) + ": " +
                                      std::to_string(centroids[i - 1]) + " -> " +
                                      std::to_string(centroids[i]));
                pass = false;
            }
        }
        if (!(centroids[3] > centroids[0])) {
            ::rtx::test::fail(rtx_test_context, __FILE__, __LINE__,
                              "highlight centroid frozen across the four phases: " +
                                  std::to_string(centroids[0]) + " -> " +
                                  std::to_string(centroids[3]));
            pass = false;
        }
    } while (false);

    releaseAll();
    RTX_CHECK(pass);
}
/* Phase 9.1 (RW-P9-02, Gate 9.5): kHALF engine regression.
 *
 * Part 1 proves the rife_fp32_to_fp16 / rife_fp16_to_fp32 conversion kernels
 * round-trip within half precision (<= 1e-3 for values < 2.0).
 * Part 2 builds a minimal 7-channel linear-blend network with kHALF
 * input/output through bridge.buildRifeEngine("__builtin_khalf__", ...),
 * loads it through the production C ABI, and drives alpha = 0.2/0.4/0.6/0.8.
 */
RTX_TEST(rife_fp16_roundtrip_and_cache_progression) {
    if (!rtx_ngx::isRifePackAvailable()) {
        std::printf("[SKIP-GPU] rife_fp16_roundtrip_and_cache_progression: no sm_70+ CUDA device (JIT unavailable)\n");
        return;
    }

    rtx::NgxRemasterBridge bridge;
    RtxNgxBridge* raw_bridge = nullptr;
    TestCudaDriver cuda{};
    TestCudaDriver::CUdeviceptr dev_prev = 0;
    TestCudaDriver::CUdeviceptr dev_curr = 0;
    TestCudaDriver::CUdeviceptr dev_out = 0;
    TestCudaDriver::CUdeviceptr dev_f32 = 0;
    TestCudaDriver::CUdeviceptr dev_f16 = 0;
    TestCudaDriver::CUdeviceptr dev_f32_back = 0;
    bool pass = true;

    std::error_code fs_ec;
    const std::filesystem::path engine_path =
        std::filesystem::temp_directory_path() / "cinertx_p9_rife_test" / "rife_builtin_synth.engine";
    std::filesystem::create_directories(engine_path.parent_path(), fs_ec);

    auto releaseAll = [&]() {
        if (raw_bridge != nullptr) {
            bridge.destroyRife(raw_bridge);
            bridge.releaseBridge(raw_bridge);
            raw_bridge = nullptr;
        }
        if (dev_out != 0 && cuda.mem_free != nullptr) cuda.mem_free(dev_out);
        if (dev_curr != 0 && cuda.mem_free != nullptr) cuda.mem_free(dev_curr);
        if (dev_prev != 0 && cuda.mem_free != nullptr) cuda.mem_free(dev_prev);
        if (dev_f32_back != 0 && cuda.mem_free != nullptr) cuda.mem_free(dev_f32_back);
        if (dev_f16 != 0 && cuda.mem_free != nullptr) cuda.mem_free(dev_f16);
        if (dev_f32 != 0 && cuda.mem_free != nullptr) cuda.mem_free(dev_f32);
        dev_out = dev_curr = dev_prev = dev_f32_back = dev_f16 = dev_f32 = 0;
        std::filesystem::remove(engine_path, fs_ec);
    };

    do {
        if (!cuda.load()) {
            ::rtx::test::fail(rtx_test_context, __FILE__, __LINE__, "nvcuda.dll could not be loaded");
            pass = false;
            break;
        }
        if (cuda.init(0) != 0) {
            ::rtx::test::fail(rtx_test_context, __FILE__, __LINE__, "cuInit failed");
            pass = false;
            break;
        }
        int device = 0;
        if (cuda.device_get(&device, 0) != 0) {
            ::rtx::test::fail(rtx_test_context, __FILE__, __LINE__, "cuDeviceGet failed");
            pass = false;
            break;
        }
        void* current_ctx = nullptr;
        if (cuda.ctx_get_current != nullptr &&
            cuda.ctx_get_current(&current_ctx) == 0 && current_ctx != nullptr) {
        } else if (cuda.primary_ctx_retain != nullptr && cuda.ctx_set_current != nullptr) {
            void* primary_ctx = nullptr;
            if (cuda.primary_ctx_retain(&primary_ctx, device) == 0 && primary_ctx != nullptr) {
                cuda.ctx_set_current(primary_ctx);
// ---------- Part 1: fp32 -> fp16 -> fp32 roundtrip ----------
            } else {
                ::rtx::test::fail(rtx_test_context, __FILE__, __LINE__, "cuDevicePrimaryCtxRetain failed");
                pass = false;
                break;
            }
        }
        constexpr uint32_t kCvtCount = 4096;
        std::vector<float> host_f32(kCvtCount);
        for (uint32_t i = 0; i < kCvtCount; ++i) {
            host_f32[i] = static_cast<float>(i % 2048) / 1024.0f;
        }
        if (cuda.mem_alloc(&dev_f32, kCvtCount * 4u) != 0 ||
            cuda.mem_alloc(&dev_f16, kCvtCount * 2u) != 0 ||
            cuda.mem_alloc(&dev_f32_back, kCvtCount * 4u) != 0) {
            ::rtx::test::fail(rtx_test_context, __FILE__, __LINE__, "cuMemAlloc failed for roundtrip buffers");
            pass = false;
            break;
        }
        if (cuda.memcpy_htod(dev_f32, host_f32.data(), kCvtCount * 4u) != 0) {
// Reuse the live context.
            ::rtx::test::fail(rtx_test_context, __FILE__, __LINE__, "cuMemcpyHtoD failed for roundtrip");
            pass = false;
            break;
        }
        if (rtx_ngx::launchFp32ToFp16(dev_f32, dev_f16, kCvtCount, nullptr) != RTX_OK ||
            rtx_ngx::launchFp16ToFp32(dev_f16, dev_f32_back, kCvtCount, nullptr) != RTX_OK) {
            ::rtx::test::fail(rtx_test_context, __FILE__, __LINE__,
                              std::string("fp16 conversion kernel launch failed: ") + rtx_ngx::rifePackLastError());
            pass = false;
            break;
        }
        std::vector<float> host_back(kCvtCount, 0.0f);
        if (cuda.memcpy_dtoh(host_back.data(), dev_f32_back, kCvtCount * 4u) != 0) {
            ::rtx::test::fail(rtx_test_context, __FILE__, __LINE__, "cuMemcpyDtoH failed for roundtrip");
            pass = false;
            break;
        }
        for (uint32_t i = 0; i < kCvtCount; ++i) {
            const double err = std::fabs(static_cast<double>(host_back[i]) - static_cast<double>(host_f32[i]));
            if (err > 1e-3) {
                ::rtx::test::fail(rtx_test_context, __FILE__, __LINE__,
                                  "fp16 roundtrip error " + std::to_string(err) +
                                      " exceeds 1e-3 at index " + std::to_string(i));
                pass = false;
                break;
            }
        }
        if (!pass) break;

        std::string bridge_error;
        if (!bridge.open(&bridge_error)) {
            ::rtx::test::fail(rtx_test_context, __FILE__, __LINE__, "bridge open failed: " + bridge_error);
            pass = false;
            break;
        }
        char build_err[512] = {};
        if (bridge.buildRifeEngine("__builtin_khalf__", engine_path.string().c_str(), 256, 256,
                                   build_err, sizeof(build_err)) != RTX_OK) {
            ::rtx::test::fail(rtx_test_context, __FILE__, __LINE__,
                              std::string("kHALF synthetic engine build failed: ") + build_err);
            pass = false;
            break;
        }
        char luid[8] = {};
        unsigned int node_mask = 0;
        if (cuda.device_get_luid(luid, &node_mask, device) != 0) {
            ::rtx::test::fail(rtx_test_context, __FILE__, __LINE__, "cuDeviceGetLuid failed");
            pass = false;
            break;
        }
        RtxNgxBridgeConfig config_struct{};
        std::memcpy(config_struct.luid, luid, sizeof(config_struct.luid));
        config_struct.luid_node_mask = node_mask;
// ---------- Part 2: kHALF engine cache progression via C ABI ----------
        config_struct.input_width = 256;
        config_struct.input_height = 256;
        config_struct.output_width = 256;
        config_struct.output_height = 256;
        config_struct.vsr_quality = 0;
        config_struct.enable_truehdr = 1;
        config_struct.max_luminance = 1000;
        config_struct.contrast = 100;
        config_struct.saturation = 100;
        config_struct.middle_gray = 50;
        config_struct.project_id = 0x52545831u;
        if (bridge.create(&config_struct, &raw_bridge) != RTX_OK || raw_bridge == nullptr) {
            ::rtx::test::fail(rtx_test_context, __FILE__, __LINE__,
                              std::string("bridge create failed: ") + bridge.lastError(nullptr));
            pass = false;
            break;
        }
        if (bridge.initRife(raw_bridge, engine_path.string().c_str(), 256, 256) != RTX_OK) {
            ::rtx::test::fail(rtx_test_context, __FILE__, __LINE__,
                              std::string("init_rife failed for kHALF engine: ") + bridge.lastError(raw_bridge));
            pass = false;
            break;
        }
        if (cuda.mem_alloc(&dev_prev, kProbeNv12Bytes) != 0 ||
            cuda.mem_alloc(&dev_curr, kProbeNv12Bytes) != 0 ||
            cuda.mem_alloc(&dev_out, kProbeNv12Bytes) != 0) {
            ::rtx::test::fail(rtx_test_context, __FILE__, __LINE__, "cuMemAlloc failed for kHALF probe buffers");
            pass = false;
            break;
        }
        std::vector<unsigned char> host_prev;
        std::vector<unsigned char> host_curr;
        fillSyntheticNv12(host_prev, 100);  // bar at x=100..123
        fillSyntheticNv12(host_curr, 120);  // bar at x=120..143
        if (cuda.memcpy_htod(dev_prev, host_prev.data(), kProbeNv12Bytes) != 0 ||
            cuda.memcpy_htod(dev_curr, host_curr.data(), kProbeNv12Bytes) != 0) {
            ::rtx::test::fail(rtx_test_context, __FILE__, __LINE__, "cuMemcpyHtoD failed for kHALF probe frames");
            pass = false;
            break;
        }
        const float alphas[4] = {0.2f, 0.4f, 0.6f, 0.8f};
        std::vector<unsigned char> outputs[4];
        double centroids[4] = {-1.0, -1.0, -1.0, -1.0};
        bool ran_all = true;
        for (int i = 0; i < 4; ++i) {
            int32_t scene_cut = 0;
            const int32_t status = bridge.interpolateRife(
                raw_bridge, dev_prev, dev_curr, kProbePitch, alphas[i], dev_out, 0, &scene_cut);
            if (status != RTX_OK) {
                ::rtx::test::fail(rtx_test_context, __FILE__, __LINE__,
                                  "kHALF interpolate_rife failed at alpha " + std::to_string(alphas[i]) +
                                      " (code=" + std::to_string(status) + "): " +
                                      std::string(bridge.lastError(raw_bridge)));
                ran_all = false;
                pass = false;
                break;
            }
            outputs[i].assign(kProbeNv12Bytes, 0u);
            if (cuda.memcpy_dtoh(outputs[i].data(), dev_out, kProbeNv12Bytes) != 0) {
                ::rtx::test::fail(rtx_test_context, __FILE__, __LINE__,
                                  "cuMemcpyDtoH failed for kHALF interpolated frame");
                ran_all = false;
                pass = false;
                break;
            }
            centroids[i] = highlightCentroidX(outputs[i]);
        }
        if (!ran_all) break;

        for (int i = 0; i < 4; ++i) {
            for (int j = i + 1; j < 4; ++j) {
                const double l1 = frameL1(outputs[i], outputs[j]);
                if (l1 <= 0.0) {
                    ::rtx::test::fail(rtx_test_context, __FILE__, __LINE__,
                                      "kHALF frames at alpha " + std::to_string(alphas[i]) + " and " +
                                          std::to_string(alphas[j]) + " are identical (frozen timestep cache)");
                    pass = false;
                }
            }
        }
        for (int i = 1; i < 4; ++i) {
            if (centroids[i] < centroids[i - 1]) {
                ::rtx::test::fail(rtx_test_context, __FILE__, __LINE__,
                                  "kHALF highlight centroid regressed at alpha " +
                                      std::to_string(alphas[i]) + ": " +
                                      std::to_string(centroids[i - 1]) + " -> " +
                                      std::to_string(centroids[i]));
                pass = false;
            }
        }
        if (!(centroids[3] > centroids[0])) {
            ::rtx::test::fail(rtx_test_context, __FILE__, __LINE__,
                              "kHALF highlight centroid frozen across the four phases: " +
                                  std::to_string(centroids[0]) + " -> " +
                                  std::to_string(centroids[3]));
            pass = false;
        }
    } while (false);

    releaseAll();
    RTX_CHECK(pass);
}
#include "rtx/shot_segmenter.h"

RTX_TEST(test_shot_group_segmenter_hard_and_subtle_cuts) {
    rtx::ShotGroupSegmenter seg = rtx::makeShotSegmenter();
    std::array<uint32_t, 64> calm{};
    calm.fill(10000);
    const auto [c0, r0, i0, g0, s0, o0] = rtx::evaluateShot(seg, calm, 1920, 1080, 255);
    (void)r0; (void)i0; (void)g0; (void)s0; (void)o0;
    RTX_CHECK(!c0);

    std::array<uint32_t, 64> subtle_cut{};
    for (int row = 0; row < 8; ++row) {
        for (int col = 0; col < 8; ++col) {
            subtle_cut[row * 8 + col] = (col < 4) ? 1600000u : 20000u;
        }
    }
    const auto [c1, r1, i1, g1, s1, o1] = rtx::evaluateShot(seg, subtle_cut, 1920, 1080, 255);
    (void)s1; (void)o1;
    RTX_CHECK(c1);
    RTX_CHECK(r1);
    RTX_CHECK(i1);
    RTX_CHECK_EQ(g1, static_cast<uint64_t>(1));
}
// Subtle cut: left half of screen changes sharply (global avg diff ~0.11)
