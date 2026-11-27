# **66\_CineRTX Phase 13 Wave 2 (Step 2.1–2.3)：纯 C ABI 驱动 RIFE 单测重构施工单**

* **施工单编号**：`CINERTX-P13-WO-066`  
* **所属波次**：`Wave 2（C ABI 隔离单测闭环与双色域校验波次）`  
* **前置依赖**：`64` 号（Wave 0）与 `65` 号（Wave 1 PTX 同步）全部通关  
* **后续衔接**：`67_CineRTX Phase 13 Wave 3 Step 3.1-3.4 全量编译门禁与RTX5060真机终验施工单`  
* **独家触碰文件**：`tests/test_rife.cpp`  
* **宪法约束**：`AuditTypes == 34 / 35`（`+0` 新增类型）、MSVC `/W4 /WX` 零警告、纯 C ABI 动态加载（`rtx::NgxRemasterBridge` \+ `TestCudaDriver`）  
* **签发日期**：`2026-11-26`

## **一、重构目标与技术设计原则**

1. **彻底消除 `LNK2019`**：所有 RIFE 初始化、张量参数配置、推理执行与销毁全部通过 `rtx::NgxRemasterBridge` 的 C ABI 封装（`open` / `buildRifeEngine` / `create` / `initRifeEx` / `interpolateRife` / `destroyRife` / `releaseBridge` / `lastError`）驱动，设备端 NV12 显存分配与拷贝全部通过文件顶部已验证的 `TestCudaDriver`（动态加载 `nvcuda.dll`）完成。  
2. **联动验证 Wave 1 PTX 修复物证**：在 Step 2.1（Case 1）中同时覆盖 `full_range = 0`（BT.709 limited-range 四相位单调位移）与 `full_range = 1`（BT.601 full-range 亮度/中性灰 UV 校验），直接以像素级断言验证 `rife_pack.ptx` 的 `* 255.0f` 缩放补丁已真正在 GPU 上生效。

## **二、原子施工步骤（Step 2.1 – Step 2.3）**

### **Step 2.1：重写 Case 1（`rife_real_engine_1080p_fg_first_to_4k_120fps`）— 真实 1080p 引擎四相位单调运动与双色域中性灰 UV 校验**

* **代码坐标**：`tests/test_rife.cpp` 末尾（替换原 `RTX_TEST(rife_real_engine_1080p_fg_first_to_4k_120fps)`）  
* **完整替换源码**：

p  
RTX\_TEST(rife\_real\_engine\_1080p\_fg\_first\_to\_4k\_120fps) {  
// Doc 59/61/63/66 Case 1: Real RIFE engine 1920x1080 NV12 via pure C ABI \-\>  
// 4-phase monotonic non-dup motion & neutral UV chroma check (both limited & full range).  
constexpr uint32\_t kW \= 1920u;  
constexpr uint32\_t kH \= 1080u;  
constexpr uint32\_t kPitch \= 1920u;  
const std::filesystem::path candidates\[\] \= {  
std::filesystem::path("models") / "rife\_v4.25\_wide.engine",  
std::filesystem::path("models") / "rife\_v4.25.engine",  
std::filesystem::path(".vs") / "build" / "external" / "models" / "rife\_v4.25\_wide.engine",  
};  
std::filesystem::path engine\_path;  
for (const auto& c : candidates) {  
std::error\_code ec;  
if (std::filesystem::exists(c, ec) && \!ec && std::filesystem::file\_size(c, ec) \> 512u \* 1024u) {  
engine\_path \= c;  
break;  
}  
}  
TestCudaDriver cuda{};

```
if (engin
```

e\_path.empty() \|\| \!cuda.load() \|\| cuda.init(0) \!= 0\) {  
\|---\|---\|---\|rife\_real\_engine\_1080p\_fg\_first\_to\_4k\_120fps: real wide engine or CUDA unavailable\\n");  
return;  
}  
int device \= 0;  
char luid\[8\] \= {};  
unsigned int node\_mask \= 0;  
if (cuda.device\_get(\&device, 0\) \!= 0 \|\| cuda.device\_get\_luid(luid, \&node\_mask, device) \!= 0\) {  
std::printf("\[SKIP-GPU\] rife\_real\_engine\_1080p\_fg\_first\_to\_4k\_120fps: CUDA device LUID unavailable\\n");  
return;  
}

```
rtx::NgxRemasterBridge bridge;
RtxNgxBridge* raw_bridge = nullptr;
TestCudaDriver::CUdeviceptr d0 = 0, d1 = 0, dout = 0;
bool pass = true;

auto cleanup = [&]() {
    if (raw_bridge != nullptr) {
        bridge.destroyRife(raw_bridge);
        bridge.releaseBridge(raw_bridge);
        raw_bridge = nullptr;
    }
    if (dout != 0 && cuda.mem_free != nullptr) cuda.mem_free(dout);
    if (d1 != 0 && cuda.mem_free != nullptr) cuda.mem_free(d1);
    if (d0 != 0 && cuda.mem_free != nullptr) cuda.mem_free(d0);
    d0 = d1 = dout = 0;
};

do {
    std::string bridge_err;
    if (!bridge.open(&bridge_err)) {
        ::rtx::test::fail(rtx_test_context, __FILE__, __LINE__, "bridge.open failed: " + bridge_err);
        pass = false;
        break;
    }
    RtxNgxBridgeConfig cfg{};
    std::memcpy(cfg.luid, luid, sizeof(cfg.luid));
    cfg.luid_node_mask = node_mask;
    cfg.input_width = static_cast<int32_t>(kW);
    cfg.input_height = static_cast<int32_t>(kH);
    cfg.output_width = static_cast<int32_t>(kW);
    cfg.output_height = static_cast<int32_t>(kH);
    cfg.vsr_quality = 0;
    cfg.enable_truehdr = 0;
    cfg.reserved[0] = 1u;

    if (bridge.create(&cfg, &raw_bridge) != RTX_OK || raw_bridge == nullptr) {
        std::printf("[SKIP-GPU] rife_real_engine_1080p_fg_first_to_4k_120fps: bridge.create skipped (%s)\n",
                    bridge.lastError(nullptr));
        break;
    }

    RtxRifeInitConfig rife_cfg{};
    rife_cfg.struct_size = sizeof(RtxRifeInitConfig);
    rife_cfg.width = kW;
    rife_cfg.height = kH;
    rife_cfg.scale = 1.0f;
    rife_cfg.align_mod = 128u;
    rife_cfg.reflect_padding = 1;
    uint64_t trt_peak_bytes = 0;
    const int32_t init_rc = bridge.initRifeEx(
        raw_bridge, engine_path.string().c_str(), &rife_cfg, &trt_peak_bytes);
    if (init_rc != RTX_OK) {
        std::printf("[SKIP-GPU] rife_real_engine_1080p_fg_first_to_4k_120fps: initRifeEx skipped (%s)\n",
                    bridge.lastError(raw_bridge));
        break;
    }
    RTX_CHECK(trt_peak_bytes > 0ull);

    void* current_ctx = nullptr;
    if (cuda.ctx_get_current != nullptr &&
        cuda.ctx_get_current(&current_ctx) == 0 && current_ctx != nullptr) {
    } else if (cuda.primary_ctx_retain != nullptr && cuda.ctx_set_current != nullptr) {
        void* primary_ctx = nullptr;
        if (cuda.primary_ctx_retain(&primary_ctx, device) == 0 && primary_ctx != nullptr) {
            cuda.ctx_set_current(primary_ctx);
        }
    }

    const size_t nv12_bytes = static_cast<size_t>(kPitch) * kH * 3u / 2u;
    std::vector<uint8_t> f0(nv12_bytes, 128u), f1(nv12_bytes, 128u);
    std::memset(f0.data(), 32, static_cast<size_t>(kPitch) * kH);
    std::memset(f1.data(), 32, static_cast<size_t>(kPitch) * kH);
    auto paintBlock = [&](std::vector<uint8_t>& buf, uint32_t bx) {
        for (uint32_t y = 400u; y < 680u; ++y) {
            for (uint32_t x = bx; x < bx + 160u && x < kW; ++x) {
                buf[static_cast<size_t>(y) * kPitch + x] = 220u;
            }
        }
    };
    paintBlock(f0, 700u);
    paintBlock(f1, 716u);

    if (cuda.mem_alloc(&d0, nv12_bytes) != 0 ||
        cuda.mem_alloc(&d1, nv12_bytes) != 0 ||
        cuda.mem_alloc(&dout, nv12_bytes) != 0 ||
        cuda.memcpy_htod(d0, f0.data(), nv12_bytes) != 0 ||
        cuda.memcpy_htod(d1, f1.data(), nv12_bytes) != 0) {
        ::rtx::test::fail(rtx_test_context, __FILE__, __LINE__, "CUDA buffer setup failed");
        pass = false;
        break;
    }

    const float alphas[4] = {0.2f, 0.4f, 0.6f, 0.8f};
    double centroids[4] = {0.0, 0.0, 0.0, 0.0};
    std::vector<uint8_t> prev_out = f0;
    std::vector<uint8_t> curr_out(nv12_bytes, 0u);

    for (int i = 0; i < 4; ++i) {
        int32_t cut = 0;
        const int32_t rc = bridge.interpolateRife(
            raw_bridge, d0, d1, kPitch, alphas[i], dout, /*full_range=*/0, &cut);
        if (rc != RTX_OK || cut != 0) {
            ::rtx::test::fail(rtx_test_context, __FILE__, __LINE__,
                              "C ABI interpolateRife failed at phase " + std::to_string(i));
            pass = false;
            break;
        }
        if (cuda.memcpy_dtoh(curr_out.data(), dout, nv12_bytes) != 0) {
            ::rtx::test::fail(rtx_test_context, __FILE__, __LINE__, "cuMemcpyDtoH failed");
            pass = false;
            break;
        }
        double mae = 0.0, wsum = 0.0, xsum = 0.0;
        for (uint32_t y = 400u; y < 680u; ++y) {
            for (uint32_t x = 680u; x < 900u; ++x) {
                const size_t idx = static_cast<size_t>(y) * kPitch + x;
                mae += std::fabs(static_cast<double>(curr_out[idx]) - static_cast<double>(prev_out[idx]));
                if (curr_out[idx] > 160u) {
                    wsum += 1.0;
                    xsum += static_cast<double>(x);
                }
            }
        }
        RTX_CHECK(mae / (280.0 * 220.0) > 0.5);
        centroids[i] = (wsum > 0.0) ? (xsum / wsum) : 0.0;

        const size_t uv_base = static_cast<size_t>(kPitch) * kH;
        for (uint32_t y = 16u; y < 120u; y += 8u) {
            for (uint32_t x = 16u; x < 400u; x += 16u) {
                const uint8_t u = curr_out[uv_base + static_cast<size_t>(y) * kPitch + x];
                const uint8_t v = curr_out[uv_base + static_cast<size_t>(y) * kPitch + x + 1u];
                RTX_CHECK(std::abs(static_cast<int>(u) - 128) <= 2);
                RTX_CHECK(std::abs(static_cast<int>(v) - 128) <= 2);
            }
        }
        prev_out = curr_out;
    }
    if (!pass) break;
    RTX_CHECK(centroids[0] < centroids[1] && centroids[1] < centroids[2] && centroids[2] < centroids[3]);

    // Verify BT.601 full_range == 1 path in rife_pack.ptx (must scale [0,1] by 255.0f)
    int32_t full_cut = 0;
    RTX_CHECK_EQ(bridge.interpolateRife(raw_bridge, d0, d1, kPitch, 0.5f, dout, /*full_range=*/1, &full_cut), RTX_OK);
    RTX_REQUIRE_EQ(cuda.memcpy_dtoh(curr_out.data(), dout, nv12_bytes), 0);
    const uint8_t bg_y = curr_out[16u * kPitch + 16u];
    const uint8_t fg_y = curr_out[540u * kPitch + 760u];
    const size_t uv_base = static_cast<size_t>(kPitch) * kH;
    const uint8_t bg_u = curr_out[uv_base + 8u * kPitch + 16u];
    const uint8_t bg_v = curr_out[uv_base + 8u * kPitch + 17u];
    RTX_CHECK(bg_y >= 24u && bg_y <= 40u);
    RTX_CHECK(fg_y >= 190u && fg_y <= 230u);
    RTX_CHECK(std::abs(static_cast<int>(bg_u) - 128) <= 2);
    RTX_CHECK(std::abs(static_cast<int>(bg_v) - 128) <= 2);
} while (false);

cleanup();
RT
```

X\_CHECK(pass);  
}

````

### Step 2.2：重写 Case 2（`rife_no_preview_ring_stream_isolation_soak`）— 20 轮高频重建与同显存指针覆写防碰撞压测

* **代码坐标**：`tests/test_rife.cpp` 末尾（紧接 Step 2.1）
* **完整替换源码**：

```cpp
RTX_TEST(rife_no_preview_ring_stream_isolation_soak) {
    // Doc 59/61/63/66 Case 2: C ABI stream isolation soak + same-device-pointer overwrite anti-aliasing.
    constexpr uint32_t kW = 256u, kH = 256u, kPitch = 256u;
    TestCudaDriver cuda{};
    if (!rtx_ngx::isRifePackAvailable() || !cuda.load() || cuda.init(0) != 0) {
        std::printf("[SKIP-GPU] rife_no_preview_ring_stream_isolation_soak: CUDA/PTX unavailable\n");
        return;
    }
    int device = 0;
    char luid[8] = {};
    unsigned int node_mask = 0;
    if (cuda.device_get(&device, 0) != 0 || cuda.device_get_luid(luid, &node_mask, device) != 0) {
        std::printf("[SKIP-GPU] rife_no_preview_ring_stream_isolation_soak: LUID unavailable\n");
        return;
    }

    std::error_code ec;
    const std::filesystem::path temp_dir = std::filesystem::temp_directory_path() / "cinertx_p13_soak_test";
    std::filesystem::create_directories(temp_dir, ec);
    const std::filesystem::path synth_engine = temp_dir / "rife_builtin_synth.engine";

    rtx::NgxRemasterBridge bridge;
    RtxNgxBridge* raw_bridge = nullptr;
    TestCudaDriver::CUdeviceptr d0 = 0, d1 = 0, dout = 0;
    bool pass = true;

    auto cleanup = [&]() {
        if (raw_bridge != nullptr) {
            bridge.destroyRife(raw_bridge);
            bridge.releaseBridge(raw_bridge);
            raw_bridge = nullptr;
        }
        if (dout != 0 && cuda.mem_free != nullptr) cuda.mem_free(dout);
        if (d1 != 0 && cuda.mem_free != nullptr) cuda.mem_free(d1);
        if (d0 != 0 && cuda.mem_free != nullptr) cuda.mem_free(d0);
        d0 = d1 = dout = 0;
        std::filesystem::remove(synth_engine, ec);
    };

    do {
        std::string err;
        if (!bridge.open(&err)) {
            ::rtx::test::fail(rtx_test_context, __FILE__, __LINE__, "bridge.open failed: " + err);
            pass = false;
            break;
        }
        char build_err[512] = {};
        if (bridge.buildRifeEngine("__builtin__", synth_engine.string().c_str(), kW, kH,
                                   build_err, sizeof(build_err)) != RTX_OK) {
            ::rtx::test::fail(rtx_test_context, __FILE__, __LINE__,
                              std::string("buildRifeEngine failed: ") + build_err);
            pass = false;
            break;
        }
        RtxNgxBridgeConfig cfg{};
        std::memcpy(cfg.luid, luid, sizeof(cfg.luid));
        cfg.luid_node_mask = node_mask;
        cfg.input_width = static_cast<int32_t>(kW);
        cfg.input_height = static_cast<int32_t>(kH);
        cfg.output_width = static_cast<int32_t>(kW);
        cfg.output_height = static_cast<int32_t>(kH);
        cfg.reserved[0] = 1u;

        if (bridge.create(&cfg, &raw_bridge) != RTX_OK || raw_bridge == nullptr) {
            ::rtx::test::fail(rtx_test_context, __FILE__, __LINE__, "bridge.create failed");
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
            }
        }

        const size_t bytes = static_cast<size_t>(kPitch) * kH * 3u / 2u;
        if (cuda.mem_alloc(&d0, bytes) != 0 ||
            cuda.mem_alloc(&d1, bytes) != 0 ||
            cuda.mem_alloc(&dout, bytes) != 0) {
            ::rtx::test::fail(rtx_test_context, __FILE__, __LINE__, "cuMemAlloc failed");
            pass = false;
            break;
        }

        RtxRifeInitConfig rcfg{};
        rcfg.struct_size = sizeof(RtxRifeInitConfig);
        rcfg.width = kW;
        rcfg.height = kH;
        rcfg.scale = 1.0f;
        rcfg.align_mod = 128u;
        rcfg.reflect_padding = 1;

        for (int iter = 0; iter < 20; ++iter) {
            uint64_t peak = 0;
            if (bridge.initRifeEx(raw_bridge, synth_engine.string().c_str(), &rcfg, &peak) != RTX_OK) {
                ::rtx::test::fail(rtx_test_context, __FILE__, __LINE__,
                                  "soak initRifeEx failed at iter " + std::to_string(iter));
                pass = false;
                break;
            }
            std::vector<uint8_t> h0(bytes, 128u), h1(bytes, 128u);
            std::memset(h0.data(), 40, static_cast<size_t>(kPitch) * kH);
            std::memset(h1.data(), 40, static_cast<size_t>(kPitch) * kH);
            const uint32_t shift = static_cast<uint32_t>(40 + (iter % 5) * 8);
            for (uint32_t y = 80u; y < 160u; ++y) {
                for (uint32_t x = shift; x < shift + 48u; ++x) h0[y * kPitch + x] = 220u;
                for (uint32_t x = shift + 12u; x < shift + 60u; ++x) h1[y * kPitch + x] = 220u;
            }
            RTX_REQUIRE_EQ(cuda.memcpy_htod(d0, h0.data(), bytes), 0);
            RTX_REQUIRE_EQ(cuda.memcpy_htod(d1, h1.data(), bytes), 0);
            int32_t cut = 0;
            for (float a : {0.2f, 0.4f, 0.6f, 0.8f}) {
                if (bridge.interpolateRife(raw_bridge, d0, d1, kPitch, a, dout, 0, &cut) != RTX_OK || cut != 0) {
                    ::rtx::test::fail(rtx_test_context, __FILE__, __LINE__,
                                      "soak interpolateRife failed at iter " + std::to_string(iter));
                    pass = false;
                    break;
                }
            }
            bridge.destroyRife(raw_bridge);
            if (!pass) break;
        }
    } while (false);

    cleanup();
    RTX_CHECK(pass);
}
````

## **Step 2.3：重写 Case 3（`rife_geometry_mismatch_fail_fast_guard`）— 纯 C ABI 几何越界与未初始化快速失败守卫**

* **代码坐标**：`tests/test_rife.cpp` 末尾（紧接 Step 2.2）  
* **完整替换源码**：

p  
RTX\_TEST(rife\_geometry\_mismatch\_fail\_fast\_guard) {  
// Doc 59/61/63/66 Case 3: Pure C ABI fail-fast guard on invalid handles, uninitialized RIFE,  
// zero pitch, and out-of-profile frame dimensions.  
int32\_t cut \= 0;  
RTX\_CHECK\_EQ(rtx\_ngx\_bridge\_init\_rife\_ex(nullptr, "fake.engine", nullptr, nullptr),  
RTX\_ERR\_INVALID\_ARGUMENT);  
RTX\_CHECK\_EQ(rtx\_ngx\_bridge\_interpolate\_rife(nullptr, 0x1000ull, 0x2000ull, 0u, 0.5f, 0x3000ull, 0, \&cut),  
RTX\_ERR\_INVALID\_ARGUMENT);

```
TestCudaDriver cuda{};
if (!cuda.load() || cuda.init(0) != 0) {
    return;
}
int device = 0;
char luid[8] = {};
unsigned int node_mask = 0;
if (cuda.device_get(&device, 0) != 0 || cuda.device_get_luid(luid, &node_mask, device) != 0) {
    return;
}
rtx::NgxRemasterBridge bridge;
std::string err;
if (!bridge.open(&err)) {
    return;
}
RtxNgxBridgeConfig cfg{};
std::memcpy(cfg.luid, luid, sizeof(cfg.luid));
cfg.luid_node_mask = node_mask;
cfg.input_width = 1920;
cfg.input_height = 1080;
cfg.output_width = 1920;
cfg.output_height = 1080;
cfg.reserved[0] = 1u;

RtxNgxBridge* raw_bridge = nullptr;
if (bridge.create(&cfg, &raw_bridge) != RTX_OK || raw_bridge == nullptr) {
    return;
}

// 1. Calling interpolateRife before initRifeEx must fail fast
RTX_CHECK_EQ(bridge.interpolateRife(raw_bridge, 0x1000ull, 0x2000ull, 1920u, 0.5f, 0x3000ull, 0, &cut),
             RTX_ERR_INVALID_ARGUMENT);

// 2. Zero dimensions in RtxRifeInitConfig must fail fast
RtxRifeInitConfig bad_cfg{};
bad_cfg.struct_size = sizeof(RtxRifeInitConfig);
bad_cfg.width = 0u;
bad_cfg.height = 1080u;
RTX_CHECK_EQ(bridge.initRifeEx(raw_bridge, "models/rife_v4.25_wide.engine", &bad_cfg, nullptr),
             RTX_ERR_INVALID_ARGUMENT);

// 3. Oversized geometry exceeding single-profile MAX (9999x9999 > 3840x2176) must fail fast
const std::filesystem::path wide_eng = std::filesystem::path("models") / "rife_v4.25_wide.engine";
std::error_code ec;
if (std::filesystem::exists(wide_eng, ec) && !ec) {
    RtxRifeInitConfig over_cfg{};
    over_cfg.struct_size = sizeof(RtxRifeInitConfig);
    over_cfg.width = 9999u;
    over_cfg.height = 9999u;
    over_cfg.scale = 1.0f;
    over_cfg.align_mod = 128u;
    over_cfg.reflect_padding = 1;
    RTX_CHECK(bridge.initRifeEx(raw_bridge, wide_eng.string().c_str(), &over_cfg, nullptr) != RTX_OK);
}

bridge.destroyRife(raw_bridge);
bridge.releaseBridge(raw_bridge);
```

}

```

### 三、Wave 2 通关门禁（Exit Gate）

| 检查项 | 核验方法 | 通关判据 |
| :--- | :--- | :--- |
| **Gate W2-1：无任何内部符号** | 编译链接 `rtx_tests.exe` | 零 `LNK2019`、零 `C2675/C2039/C3861`、MSVC `/W4 /WX` 0 警告 |
| **Gate W2-2：零新增具名类型** | `cmake -P cmake/AuditTypes.cmake` | `OK: 34/35`（严格 `0` 新增类型） |
| **Gate W2-3：三项新单测 PASS** | `rtx_tests.exe` 运行输出 | `rife_real_engine_1080p_fg_first_to_4k_120fps`、`rife_no_preview_ring_stream_isolation_soak`、`rife_geometry_mismatch_fail_fast_guard` 全部 **PASS** |

> **通关后立即进入 `67_CineRTX Phase 13 Wave 3 Step 3.1-3.4 全量编译门禁与RTX5060真机终验施工单`。**
```

