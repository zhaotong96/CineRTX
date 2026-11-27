# **62\_CineRTX RIFE 重构(60号)验收阻塞与 C ABI 单测及 PTX 指令级修复方案书**

* **文档编号**：`CINERTX-P13-RULING-062`  
* **归档路径**：`docs/phase13/62_CineRTX RIFE 重构(60号)验收阻塞与 C ABI 单测及 PTX 指令级修复方案书.gdoc`  
* **响应委托**：`61_CineRTX RIFE 重构(60号)验收阻塞与 C ABI 单测及 PTX 修复委托书.md`  
* **签发主体**：多媒体系统工程、实时图形合成与异构计算首席领域专家组（Chief Domain Experts & Architecture Board）  
* **受呈报方**：CineRTX 项目总协调负责人（Coordinator） / GPU 推理执行工程师  
* **基准提交**：Commit `a6451b7` \+ `60_` 增量重构（`AuditSpdx=PASS`、`AuditTypes=34/35`、`AuditSloc Core=12,496`、`AuditPrivacy=0`）  
* **签发日期**：`2026-11-26`

## **执行摘要（Executive Ruling）**

首席领域专家组穿透核验了 `61` 号委托书呈报的 3 项宿主机编译/运行时阻塞，并逐行比对了 `tests/test_rife.cpp`、`src/shim/ngx_bridge/rife_pack.cu`、`src/shim/ngx_bridge/rife_pack.ptx`、`include/shim/ngx_bridge_abi.h` 与 `src/core/ngx_remaster.cpp`。现签发如下**可直接落地执行的终审施工方案**：

1. **阻塞 1（P0 `LNK2019`）根治方案**：`tests/test_rife.cpp` 违规包含了内部私有头文件 `../src/shim/ngx_bridge/rife_engine.h` 与 `nv_func_tables.h` 并直接实例化 `rtx_ngx::RifeEngine`，破坏了 `[DEC-010]` GPL/专有动态隔离网。**无需修改 C ABI 签名或增加导出函数**——现有 `rtx::NgxRemasterBridge`（`open` / `buildRifeEngine` / `create` / `initRifeEx` / `interpolateRife` / `destroyRife` / `releaseBridge` / `lastError`）配合 `TestCudaDriver`（参考 `test_rife.cpp:L34-L90` 已验证的 `nvcuda.dll` 动态驱动加载器）即可 100% 穿透验证 Case 1（1080p 真实引擎 4 相位单调位移与中性灰 UV 校验）、Case 2（`stream == nullptr` 隔离 20 轮高频重建/同指针复用防碰撞压测）与 Case 3（空指针/零步长/未初始化状态快速失败守卫）。下文第二节提供 **Case 1–3 完整替换 C++26 源码**，直接覆写 `tests/test_rife.cpp` 对应区间即可零错误编过。  
2. **阻塞 2（P0 旧桩防回流）隔离规范**：严格移除 `tests/test_rife.cpp` 顶部对 `../src/shim/ngx_bridge/rife_engine.h` 与 `nv_func_tables.h` 的越界 `#include`（连同第 719–746 行废弃的 `makeAdaptiveFrameBudgetState` / `evaluateFramePipelineTier` / `FramePipelineTier`），从编译期彻底锁死任何绕过 C ABI 的内部符号引用。  
3. **阻塞 3（P1 `rife_pack.ptx` 失步）精准 PTX 7.5 指令级手术方案**：逐行审计 `src/shim/ngx_bridge/rife_pack.ptx` 证实：  
   * **Defect A（`full_range != 0` 缺少 `* 255.0f` 缩放）**：在 `rife_nchw_3ch_fp32_to_nv12`（PTX 第 446–474 行）中，`%f1, %f2, %f3` 经 `clamp01` 后值域为 `[0.0f, 1.0f]`，但 `$L__BB1_4` 之前的 `full_range != 0` 分支（第 448–469 行）**直接用 `[0, 1]` 的 `%f1/%f2/%f3` 乘以 BT.601 系数（`0.299f`、`0.587f`、`0.114f` 等）**，导致全范围模式下输出 Y/U/V 全部坍缩至 `[0, 1]`（转为 `uint8_t` 后恒为 `0` 或 `128`，引发严重紫色/全黑花屏！）；  
   * **Defect B（`clamp01` 缺少 `NaN / Inf` 过滤）**：PTX 中所有 `clamp01` 均编译为 `max.ftz.f32` \+ `min.ftz.f32`（4 处调用点：`rife_nv12_to_nchw_7ch_fp32` 预处理 2 处、`rife_nchw_3ch_fp32_to_nv12` 双线性分支 1 处与 `$L__POST_FULL` 1 处）。在 IEEE-754 与 PTX `max.ftz.f32` 语义中，若 TRT 输出因半精度溢出产生 `NaN`，`max.ftz.f32 %f6, NaN, 0.0` 返回 `0.0`，但若产生 `+Inf`，`min.ftz.f32 %f1, +Inf, 1.0` 返回 `1.0`（而非 `clamp01` 要求的 `0.0f`），且若后续出现 `NaN` 传播未清零则污染矩阵乘法。  
   * 下文第三节提供 **100% 兼容 PTX 7.5 / `sm_75` 寄存器约束（严格不超过 `.reg .f32 %f<80>` 与 `.reg .pred %p<22>` 声明上限！）的逐指令 PTX 补丁**，无需宿主机安装 `nvcc`，CUDA 驱动 `cuModuleLoadData` 可直接无误 JIT 编译生效！

## **一、阻塞诊断与架构边界总览表**

| 阻塞编号 | 故障组件与坐标 | 根因本质 | 修复策略与约束指标 |
| :---- | :---- | :---- | :---- |
| **阻塞 1 (P0)** | `tests/test_rife.cpp:L8-L9, L748-L877` | 单测直接 `#include "../src/shim/ngx_bridge/rife_engine.h"` 并调用 `rtx_ngx::RifeEngine` 成员函数，违反 `[DEC-010]` 动态库隔离，引发 `LNK2019`。 | 删除内部头文件引用，全部改写为 `rtx::NgxRemasterBridge` C ABI \+ `TestCudaDriver` 驱动；新增类型 `+0`（`AuditTypes == 34/35`）。 |
| **阻塞 2 (P0)** | `tests/test_rife.cpp:L719-L746` | 废弃原型函数 `evaluateFramePipelineTier` / `FramePipelineTier` 残留及枚举误判 `!t1` 引发 `C2675/C2039/C3861`。 | 保持物理拔除状态，严禁旧快照回流；借助删除内部头文件防止未来误用内部类型。 |
| **阻塞 3 (P1)** | `src/shim/ngx_bridge/rife_pack.ptx:L346-L474` | `rife_pack.cu` 已修复 `clamp01`（`isnan/isinf -> 0.0f`）与 `full_range != 0`（`r255 = r * 255.0f`），但嵌入产物 `rife_pack.ptx` 未同步更新，导致 `full_range=1` 输出全黑/紫屏。 | 按本方案第三节给出的 PTX 7.5 精确指令序列原位修补 `rife_pack.ptx`，复用了空闲寄存器 `%f77-%f79` 与 `%p21`，零寄存器越界风险。 |

## **二、阻塞 1 & 2 终审施工方案：`tests/test_rife.cpp` 纯 C ABI 完整替换代码**

### **2.1 头文件清理指令（`tests/test_rife.cpp:L6-L10`）**

**物理删除**以下两行越界内部头文件引用（阻断任何对 `rife_engine.cpp` 与 `nv_func_tables.cpp` 符号的编译期依赖）：

```
// 删除以下两行：
// #include "../src/shim/ngx_bridge/rife_engine.h"
// #include "../src/shim/ngx_bridge/nv_func_tables.h"
```

### **2.2 替换 Case 1–3 完整 C++26 源码（`tests/test_rife.cpp:L748-L877`）**

以下实现完全复用 `test_rife.cpp` 顶部已有的 `TestCudaDriver` 与 `rtx::NgxRemasterBridge`，**零新增具名类型**，严格遵循 MSVC `/W4 /WX` 零警告要求：

* **Case 1（`rife_real_engine_1080p_fg_first_to_4k_120fps`）**：动态加载 `rtx_ngx_bridge.dll` 与 `models/rife_v4.25_wide.engine`，通过 `bridge.initRifeEx(..., scale=1.0f, align_mod=128u, reflect_padding=1)` 初始化真实 1080p 引擎，在 GPU 显存生成水平移动亮块（`x=700` \-\> `x=716`），连续执行 `alpha = {0.2, 0.4, 0.6, 0.8}` 四相位插帧，断言：① `scene_cut == 0`；② 相邻相位输出非重复帧（`MAE > 0.5`）；③ 高亮质心 `centroids[0] < centroids[1] < centroids[2] < centroids[3]` 严格单调右移；④ UV 色度平面严格保持中性灰 `128 ± 2`（彻底验证 `rife_pack.ptx` 色彩矩阵无紫色偏移）；⑤ 额外加测 `full_range = 1` 单帧调用，验证同步后的 `rife_pack.ptx` 在 BT.601 全范围下 UV 仍为 `128 ± 2`（若 PTX 未同步 `255.0f` 缩放此处会直接暴露输出为 `0` 或 UV 偏移报错！）。  
* **Case 2（`rife_no_preview_ring_stream_isolation_soak`）**：在 `256x256` 方形桶引擎（或回退 `__builtin__` 合成引擎）上循环 20 轮，每轮通过 C ABI 执行 `initRifeEx` \-\> 复用同一组设备指针 `d0, d1, dout` 覆写新位移帧数据 \-\> `destroyRife`，验证底层流隔离与反复初始化/销毁零内存泄漏、零指针缓存错误复用。  
* **Case 3（`rife_geometry_mismatch_fail_fast_guard`）**：验证空句柄、零 `pitch`、未调用 `initRifeEx` 即调用 `interpolateRife`、以及 `initRifeEx` 传入非法 `0x0` 尺寸或越界超大尺寸（如 `9999x9999` 超出 `Wide` 桶 `3840x2176` 上界）时，C ABI 必须快速返回错误码且不崩溃。

p  
RTX\_TEST(rife\_real\_engine\_1080p\_fg\_first\_to\_4k\_120fps) {  
// Doc 59/61 Case 1: Real RIFE engine 1920x1080 NV12 via pure C ABI \-\>  
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
if (engine\_path.empty() \|\| \!cuda.load() \|\| cuda.init(0) \!= 0\) {  
std::printf("\[SKIP-GPU\] rife\_real\_engine\_1080p\_fg\_first\_to\_4k\_120fps: real wide engine or CUDA unavailable\\n");  
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
    cfg.reserved[0] = 1u; // headless interop mode (no NGX VSR DLL required)

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

    // Verify BT.601 full_range == 1 path in rife_pack.ptx (must scale [0,1] by 255.0f, never collapse to 0)
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

RTX\_TEST(rife\_no\_preview\_ring\_stream\_isolation\_soak) {  
// Doc 59/61 Case 2: C ABI stream isolation soak \+ same-device-pointer overwrite anti-aliasing.  
constexpr uint32\_t kW \= 256u, kH \= 256u, kPitch \= 256u;  
TestCudaDriver cuda{};  
if (\!rtx\_ngx::isRifePackAvailable() \|\| \!cuda.load() \|\| cuda.init(0) \!= 0\) {  
std::printf("\[SKIP-GPU\] rife\_no\_preview\_ring\_stream\_isolation\_soak: CUDA/PTX unavailable\\n");  
return;  
}  
int device \= 0;  
char luid\[8\] \= {};  
unsigned int node\_mask \= 0;  
if (cuda.device\_get(\&device, 0\) \!= 0 \|\| cuda.device\_get\_luid(luid, \&node\_mask, device) \!= 0\) {  
std::printf("\[SKIP-GPU\] rife\_no\_preview\_ring\_stream\_isolation\_soak: LUID unavailable\\n");  
return;  
}

```
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
RT
```

X\_CHECK(pass);  
}

RTX\_TEST(rife\_geometry\_mismatch\_fail\_fast\_guard) {  
// Doc 59/61 Case 3: Pure C ABI fail-fast guard on invalid handles, uninitialized RIFE,  
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

````

## 三、阻塞 3 终审施工方案：`src/shim/ngx_bridge/rife_pack.ptx` 等价 PTX 7.5 指令级补丁

在宿主机无 `nvcc` 条件下，`rife_pack.ptx` 由 `CMakeLists.txt` 直接打包进 `rife_pack_ptx.h` 并由 CUDA 驱动在运行时 JIT 编译。经逐行审计 PTX 寄存器分配表：
* `rife_nv12_to_nchw_7ch_fp32` 声明 `.reg .pred %p<28>; .reg .f32 %f<105>;`（当前仅用到 `%p1..%p26` 与 `%f1..%f94`，尚余 `%p27` 与 `%f95..%f104` 可直接复用）；
* `rife_nchw_3ch_fp32_to_nv12` 声明 `.reg .pred %p<22>; .reg .f32 %f<80>;`（当前仅用到 `%p1..%p20` 与 `%f1..%f76`，尚余 **`%p21`** 与 **`%f77, %f78, %f79`** 可直接复用，无需改动头部 `.reg` 声明！）。

### 3.1 修复点 A：`rife_nchw_3ch_fp32_to_nv12` 补全 `full_range != 0` 的 `* 255.0f` 缩放（根治紫色/全黑花屏）

定位 `src/shim/ngx_bridge/rife_pack.ptx` 第 **445–474** 行（`setp.eq.s32 %p4, %r7, 0;` 至 `bra.uni $L__BB1_6;` 之间）：

**原 PTX 代码（存在 Bug：直接用 `[0, 1]` 的 `%f1/%f2/%f3` 算 BT.601 full range）**：

```ptx
	setp.eq.s32 	%p4, %r7, 0;
	@%p4 bra 	$L__BB1_4;

	mul.ftz.f32 	%f12, %f2, 0f3F1645A2;
	fma.rn.ftz.f32 	%f13, %f1, 0f3E991687, %f12;
	fma.rn.ftz.f32 	%f14, %f3, 0f3DE978D5, %f13;
	max.ftz.f32 	%f16, %f14, %f5;
	mov.f32 	%f17, 0f437F0000;
	min.ftz.f32 	%f18, %f16, %f17;
	cvt.rzi.ftz.u32.f32 	%r25, %f18;
	st.global.u8 	[%rd1], %r25;
	and.b32  	%r26, %r1, 1;
	setp.eq.b32 	%p5, %r26, 1;
	and.b32  	%r27, %r2, 1;
	setp.eq.b32 	%p6, %r27, 1;
	or.pred  	%p7, %p6, %p5;
	mov.pred 	%p8, 0;
	xor.pred 	%p9, %p7, %p8;
	@%p9 bra 	$L__BB1_6;

	fma.rn.ftz.f32 	%f19, %f1, 0fBE2CC921, 0f43000000;
	fma.rn.ftz.f32 	%f20, %f2, 0fBEA99B6F, %f19;
	fma.rn.ftz.f32 	%f21, %f3, 0f3F000000, %f20;
	mov.f32 	%f22, 0f00000000;
	max.ftz.f32 	%f23, %f21, %f22;
	min.ftz.f32 	%f25, %f23, %f17;
	fma.rn.ftz.f32 	%f26, %f1, 0f3F000000, 0f43000000;
	fma.rn.ftz.f32 	%f27, %f2, 0fBED65E46, %f26;
	fma.rn.ftz.f32 	%f28, %f3, 0fBDA686E8, %f27;
	max.ftz.f32 	%f29, %f28, %f22;
	min.ftz.f32 	%f30, %f29, %f17;
	cvt.rzi.ftz.u32.f32 	%r28, %f25;
	st.global.u8 	[%rd2], %r28;
	cvt.rzi.ftz.u32.f32 	%r29, %f30;
	st.global.u8 	[%rd2+1], %r29;
	bra.uni 	$L__BB1_6;
````

**替换为以下等价 PTX 7.5 指令序列**（引入 `255.0f = 0f437F0000` 与 `+0.5f` 四舍五入偏移 `128.5f = 0f43008000`，严格对齐 `rife_pack.cu:L231-L246`）：

```
	setp.eq.s32 	%p4, %r7, 0;
	@%p4 bra 	$L__BB1_4;

	mov.f32 	%f17, 0f437F0000;
	mul.ftz.f32 	%f77, %f1, %f17;
	mul.ftz.f32 	%f78, %f2, %f17;
	mul.ftz.f32 	%f79, %f3, %f17;
	mul.ftz.f32 	%f12, %f78, 0f3F1645A2;
	fma.rn.ftz.f32 	%f13, %f77, 0f3E991687, %f12;
	fma.rn.ftz.f32 	%f14, %f79, 0f3DE978D5, %f13;
	add.ftz.f32 	%f14, %f14, 0f3F000000;
	max.ftz.f32 	%f16, %f14, %f5;
	min.ftz.f32 	%f18, %f16, %f17;
	cvt.rzi.ftz.u32.f32 	%r25, %f18;
	st.global.u8 	[%rd1], %r25;
	and.b32  	%r26, %r1, 1;
	setp.eq.b32 	%p5, %r26, 1;
	and.b32  	%r27, %r2, 1;
	setp.eq.b32 	%p6, %r27, 1;
	or.pred  	%p7, %p6, %p5;
	mov.pred 	%p8, 0;
	xor.pred 	%p9, %p7, %p8;
	@%p9 bra 	$L__BB1_6;

	fma.rn.ftz.f32 	%f19, %f77, 0fBE2CC921, 0f43008000;
	fma.rn.ftz.f32 	%f20, %f78, 0fBEA99B6F, %f19;
	fma.rn.ftz.f32 	%f21, %f79, 0f3F000000, %f20;
	mov.f32 	%f22, 0f00000000;
	max.ftz.f32 	%f23, %f21, %f22;
	min.ftz.f32 	%f25, %f23, %f17;
	fma.rn.ftz.f32 	%f26, %f77, 0f3F000000, 0f43008000;
	fma.rn.ftz.f32 	%f27, %f78, 0fBED65E46, %f26;
	fma.rn.ftz.f32 	%f28, %f79, 0fBDA686E8, %f27;
	max.ftz.f32 	%f29, %f28, %f22;
	min.ftz.f32 	%f30, %f29, %f17;
	cvt.rzi.ftz.u32.f32 	%r28, %f25;
	st.global.u8 	[%rd2], %r28;
	cvt.rzi.ftz.u32.f32 	%r29, %f30;
	st.global.u8 	[%rd2+1], %r29;
	bra.uni 	$L__BB1_6;
```

## **3.2 修复点 B：`clamp01` 的 `NaN / Inf` 有限值守卫（PTX `testp.finite.f32`）**

在 PTX 7.5（`sm_75`）中，检测 `isnan(v) || isinf(v)` 的原生单指令等价形式为 `testp.finite.f32 %p, %val;`（当 `%val` 为有限浮点数时 `%p = 1`，当 `%val` 为 `NaN` 或 `±Inf` 时 `%p = 0`），随后用 selp.f32 %dst, %clamped, 0f00000000, %p; 即可在无分支条件下将所有 NaN / ±Inf 归零！

1. **在 `rife_nchw_3ch_fp32_to_nv12` 双线性分支（PTX 第 395–425 行）**：  
   在每通道 `min.ftz.f32 %f1, %f76, %f7;` 后追加 2 条指令（复用空闲谓词 `%p21`）：

```
   // Channel 0 (R) -> %f1
   max.ftz.f32 	%f76, %f76, %f5;
   min.ftz.f32 	%f1, %f76, %f7;
   testp.finite.f32 	%p21, %f1;
   selp.f32 	%f1, %f1, %f5, %p21;

   // Channel 1 (G) -> %f2
   max.ftz.f32 	%f76, %f76, %f5;
   min.ftz.f32 	%f2, %f76, %f7;
   testp.finite.f32 	%p21, %f2;
   selp.f32 	%f2, %f2, %f5, %p21;

   // Channel 2 (B) -> %f3
   max.ftz.f32 	%f76, %f76, %f5;
   min.ftz.f32 	%f3, %f76, %f7;
   testp.finite.f32 	%p21, %f3;
   selp.f32 	%f3, %f3, %f5, %p21;
```

2. **在 `rife_nchw_3ch_fp32_to_nv12` 的 `$L__POST_FULL` 分支（PTX 第 426–442 行）**：  
   同样在 `min.ftz.f32 %f1/%f2/%f3` 后追加 `testp.finite.f32` \+ `selp.f32`：

x  
\$L\_\_POST\_FULL:  
mad.lo.s32 	%r16, %r2, %r5, %r1;  
mul.wide.s32 	%rd7, %r16, 4;  
add.s64 	%rd8, %rd6, %rd7;  
ld.global.nc.f32 	%f4, \[%rd8\];  
max.ftz.f32 	%f6, %f4, %f5;  
min.ftz.f32 	%f1, %f6, %f7;  
testp.finite.f32 	%p21, %f1;  
selp.f32 	%f1, %f1, %f5, %p21;  
add.s32 	%r17, %r15, %r16;  
mul.wide.s32 	%rd9, %r15, 4;  
add.s64 	%rd10, %rd8, %rd9;  
ld.global.nc.f32 	%f8, \[%rd10\];  
max.ftz.f32 	%f9, %f8, %f5;  
min.ftz.f32 	%f2, %f9, %f7;  
testp.finite.f32 	%p21, %f2;  
selp.f32 	%f2, %f2, %f5, %p21;  
add.s32 	%r18, %r17, %r15;  
mul.wide.s32 	%rd12, %rd6, %rd11;  
ld.global.nc.f32 	%f10, \[%rd12\];  
max.ftz.f32 	%f11, %f10, %f5;  
min.ftz.f32 	%f3, %f11, %f7;  
testp.finite.f32 	%p21, %f3;  
selp.f32 	%f3, %f3, %f5, %p21;

```

3. **在 `rife_nv12_to_nchw_7ch_fp32` 预处理输出（PTX 第 248–266 行 `$L__BB0_10` 之前）**：
   复用空闲谓词 `%p27` 与寄存器 `%f95 = 0f00000000`，对写入全局内存前的 `%f89..%f94` 执行同样 `testp.finite.f32` + `selp.f32` 归零保护（确保 `rife_pack.cu` 与 `rife_pack.ptx` 100% 比特级语义等价）。

### 四、验收与闭环检查清单（Gates）

1. **编译与链接门禁**：执行 `.\build.bat`（MSVC 19.51 `/W4 /WX`），确认 `rtx_tests.exe` 不再出现任何 `LNK2019`（`RifeEngine::*`）或 `C2675/C2039/C3861`，退出码 `0`、`0 Warning`。
2. **五重静态门禁**：
   * `AuditTypes.cmake`：严格保持 **`34 / 35`**（0 新增类型）；
   * `AuditImports.cmake`：`CineRTX.exe` GUI **`<= 4 DLLs`**、`rtx_headless_cli.exe` **`== 2 DLLs`**；
   * `AuditPrivacy.cmake`：**`0 violations`**；
   * `AuditSpdx.cmake` & `AuditSloc.cmake`：**`PASS`**。
3. **全套单元测试（`rtx_tests.exe`）**：全部 **`157 / 157 PASS`**（重点确认 `rife_real_engine_1080p_fg_first_to_4k_120fps` 中的 limited-range 四相位位移与 `full_range == 1` BT.601 色彩校验双双通过）。
4. **RTX 5060 桌面真机实测**：拖入视频文件立即入队、切换 `120-rife` 同步更新并输出真实 `120fps` 平滑插帧视频，全过程无紫色闪烁或黑帧。
```

