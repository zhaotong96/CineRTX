# **63\_CineRTX Phase 13 RIFE 重构与 PTX 指令级合龙多波次多阶段施工 Superpower 规约**

* **文档编号**：`CINERTX-P13-SUPERPOWER-063`  
* **归档路径**：`docs/phase13/63_CineRTX Phase 13 RIFE 重构与 PTX 指令级合龙多波次多阶段施工Superpower规约.gdoc`  
* **承接基线**：Commit `a6451b7` \+ `60_` 增量重构（响应 `61` 号委托书与 `62` 号专家终审方案）  
* **适用角色**：GPU 推理与异构计算执行工程师 / 项目总协调负责人（Coordinator）  
* **目标产物**：`.\build.bat` 0 Error 0 Warning、五重静态门禁全绿、`rtx_tests.exe` **157 / 157 PASS**、RTX 5060 桌面 `120-rife` 真机零紫色闪烁闭环  
* **签发日期**：`2026-11-26`

## **一、施工总纲与不可逾越的宪法红线（Constitutional Directives）**

本 Superpower 规约采用\*\*四波次原子流水线（Wave 0 \-\> Wave 1 \-\> Wave 2 \-\> Wave 3）\*\*将 `60_` 号 RIFE 重构的 3 项编译/运行时阻塞彻底清零。每波次严格遵守 TDD"红灯断言 \-\> 最小精准外科修改 \-\> 门禁绿灯校验"闭环，**严禁跳波次盲改、严禁以旧版本文件整体覆写、严禁在无 `nvcc` 宿主机上伪造 PTX 编译成功**。

| 宪法红线 | 审计工具 / 依据 | 硬性量化指标 | 违宪一票否决条款 |
| :---- | :---- | :---- | :---- |
| **GPL 与专有 SDK 物理隔离** | `[DEC-010]` / `CMakeLists.txt` | `rtx_tests.exe`、`rtx_core.lib`、`CineRTX.exe` 严禁包含 `src/shim/ngx_bridge/` 内部私有头（`rife_engine.h`、`nv_func_tables.h`）或链接 `nvinfer_10.lib` | 出现任何 `rtx_ngx::RifeEngine::*` 直接引用或 `LNK2019` 即刻驳回 |
| **具名类型配额锁死** | `cmake/AuditTypes.cmake` / `[DEC-009]` | 全仓 `include/` \+ `src/` 具名 `struct`/`class` 严格保持 **`34 / 35`（`+0` 新增）** | 新增任何具名结构体或类导致 `> 34` 即刻拦截 |
| **静态 IAT 导入表净空** | `cmake/AuditImports.cmake` / `[DEC-018]` | `CineRTX.exe` GUI **`<= 4 DLLs`**；`rtx_headless_cli.exe` **`== 2 DLLs`** | 新增任何静态 `.lib` 或隐式 `#pragma comment(lib)` 即刻拦截 |
| **PTX 7.5 寄存器与产物同步** | `CMakeLists.txt:L213, L237` / `sm_75` | `rife_pack.ptx` 必须与 `rife_pack.cu` 比特级语义等价，且严格不超出原 `.reg` 声明上限 | `full_range == 1` 输出全黑/紫屏或 `cuModuleLoadData` JIT 报错即刻拦截 |
| **隐私与代码规模红线** | `AuditPrivacy.cmake` / `AuditSloc.cmake` | `AuditPrivacy == 0 violations`；Core SLOC `<= 23,000`（当前 `12,496`） | 出现本机盘符/路径或无意义样板膨胀即刻拦截 |

## **二、多波次施工拓扑与文件隔离矩阵**

```
[Wave 0: 卫生拔除与依赖隔离]
 └─ tests/test_rife.cpp (删除 rife_engine.h / nv_func_tables.h 越界引用 + 负向防回流核验)
        │
        ▼
[Wave 1: PTX 7.5 指令级同步手术]
 └─ src/shim/ngx_bridge/rife_pack.ptx (补全 full_range * 255.0f 缩放 + testp.finite.f32 NaN/Inf 守卫)
        │
        ▼
[Wave 2: 纯 C ABI 单元测试重构 (Case 1–3)]
 └─ tests/test_rife.cpp (改用 rtx::NgxRemasterBridge + TestCudaDriver 穿透验证 1080p/256p/Fail-Fast)
        │
        ▼
[Wave 3: MSVC 全量编译、五重静态门禁与 RTX 5060 真机终验]
 └─ .\build.bat -> Audit*.cmake -> rtx_tests.exe (157/157) -> CineRTX.exe 120-rife 实片验证
```

### **物理修改文件边界（严格锁定仅触碰以下 2 个文件）**

1. `tests/test_rife.cpp`（移除越界 `#include`，纯 C ABI 重写 Case 1–3）  
2. `src/shim/ngx_bridge/rife_pack.ptx`（等价指令级同步 `rife_pack.cu` 修复）  
* **严禁触碰**：`include/shim/ngx_bridge_abi.h`、`src/shim/ngx_bridge/bridge_exports.cpp`、`src/shim/ngx_bridge/rife_engine.h/.cpp`、`src/core/ngx_remaster.h/.cpp`、`CMakeLists.txt`（上述文件在 `60_` 增量重构后已编译通过，无需任何 ABI 改动）。

## **三、分波次原子施工单（Wave 0 – Wave 3）**

### **Wave 0：越界内部头文件拔除与废弃旧桩防回流防线**

#### **3.1 目标与根因**

`tests/test_rife.cpp` 顶部违规包含 `../src/shim/ngx_bridge/rife_engine.h` 与 `../src/shim/ngx_bridge/nv_func_tables.h`，导致测试目标 `rtx_tests.exe` 直接引用未链接进测试目标的 `rtx_ngx::RifeEngine` 与 `rtx_ngx::nv_cuda` 符号，触发 `LNK2019`；同时防堵旧快照中的废弃原型函数 `makeAdaptiveFrameBudgetState` / `evaluateFramePipelineTier` / `FramePipelineTier` 回流。

#### **3.2 原子操作步骤**

1. **清理顶部越界 `#include`（`tests/test_rife.cpp:L6-L10`）**：  
   物理删除以下两行：

```
// 物理删除以下两行：
#include "../src/shim/ngx_bridge/rife_engine.h"
#include "../src/shim/ngx_bridge/nv_func_tables.h"
```

保留合法公共边界头文件：`#include "rife_pack_runtime.h"`、`#include "rtx/ngx_remaster.h"`、`#include "shim/ngx_bridge_abi.h"`。

2. **执行负向防回流扫描（Gate W0-1）**：  
   确认 `tests/test_rife.cpp` 全文对以下符号返回 **0 命中**：  
   * `rife_engine.h` / `nv_func_tables.h`  
   * `rtx_ngx::RifeEngine` / `rtx_ngx::nv_cuda`  
   * `evaluateFramePipelineTier` / `makeAdaptiveFrameBudgetState` / `FramePipelineTier`

---

### **Wave 1：`rife_pack.ptx` 等价 PTX 7.5（`sm_75`）指令级同步补丁**

#### **4.1 底层原理与寄存器安全性证明**

`CMakeLists.txt` 直接将 `src/shim/ngx_bridge/rife_pack.ptx` 转换为 `rife_pack_ptx.h` 嵌入 `rtx_ngx_bridge.dll`。因宿主机无 `nvcc`，必须直接更新 `rife_pack.ptx` 使其与 `rife_pack.cu` 保持比特级等价：

* **漏洞 1（`full_range != 0` 紫屏/全黑）**：在 `rife_nchw_3ch_fp32_to_nv12` 中，采样后的 `%f1, %f2, %f3` 位于 `[0.0f, 1.0f]`，但 `$L__BB1_4` 前的 `full_range != 0` 分支未乘 `255.0f`（`0f437F0000`），直接用 `[0, 1]` 计算 BT.601 矩阵，导致输出 Y/U 坍缩为 `0`，引发严重偏色与黑屏。  
* **漏洞 2（`clamp01` 缺失 `NaN / ±Inf` 归零）**：PTX 原 `max.ftz.f32` \+ `min.ftz.f32` 在输入为 `+Inf` 时输出 `1.0f` 而非 `0.0f`。PTX 7.5 原生指令 `testp.finite.f32 %p, %val` \+ `selp.f32 %dst, %val, 0f00000000, %p` 可无分支过滤所有 `NaN` 与 `±Inf`。  
* **寄存器零越界证明**：  
  * `rife_nv12_to_nchw_7ch_fp32` 声明 `.reg .pred %p<28>; .reg .f32 %f<105>;`（当前最高用到 `%p26` 与 `%f94`，空闲 `%p27` 与 `%f95` 可安全使用）；  
  * `rife_nchw_3ch_fp32_to_nv12` 声明 `.reg .pred %p<22>; .reg .f32 %f<80>;`（当前最高用到 `%p20` 与 `%f76`，空闲 `%p21` 与 `%f77, %f78, %f79` 可安全使用）。

#### **4.2 原子补丁 A：修补 `rife_nv12_to_nchw_7ch_fp32` 的 `clamp01`（`rife_pack.ptx:L247-L270`）**

在 `$L__BB0_10:` 标签前（存储 `%f89..%f94` 至全局内存之前）插入有限值归零守卫（复用空闲谓词 `%p27` 与寄存器 `%f95`）：

```
	mov.f32 	%f95, 0f00000000;
	testp.finite.f32 	%p27, %f89;
	selp.f32 	%f89, %f89, %f95, %p27;
	testp.finite.f32 	%p27, %f90;
	selp.f32 	%f90, %f90, %f95, %p27;
	testp.finite.f32 	%p27, %f91;
	selp.f32 	%f91, %f91, %f95, %p27;
	testp.finite.f32 	%p27, %f92;
	selp.f32 	%f92, %f92, %f95, %p27;
	testp.finite.f32 	%p27, %f93;
	selp.f32 	%f93, %f93, %f95, %p27;
	testp.finite.f32 	%p27, %f94;
	selp.f32 	%f94, %f94, %f95, %p27;

$L__BB0_10:
```

#### **4.3 原子补丁 B：修补 `rife_nchw_3ch_fp32_to_nv12` 的 `clamp01` 与 `full_range != 0`（`rife_pack.ptx:L395-L474`）**

1. **双线性分支与 `$L__POST_FULL` 分支的 `clamp01` 有限值守卫（`rife_pack.ptx:L395-L442`）**：  
   在每处 `min.ftz.f32 %f1/%f2/%f3, ..., %f7;` 后追加两行（复用空闲谓词 `%p21` 与常量寄存器 `%f5 = 0f00000000`）：

```
// 对 %f1（R 通道）追加：
testp.finite.f32 	%p21, %f1;
selp.f32 	%f1, %f1, %f5, %p21;

// 对 %f2（G 通道）追加：
testp.finite.f32 	%p21, %f2;
selp.f32 	%f2, %f2, %f5, %p21;

// 对 %f3（B 通道）追加：
testp.finite.f32 	%p21, %f3;
selp.f32 	%f3, %f3, %f5, %p21;
```

*(注：双线性路径 3 处 \+ `$L__POST_FULL` 路径 3 处，共 6 处均需追加)*。

2. **BT.601 `full_range != 0` 分支补全 `* 255.0f` 与 `+0.5f` 舍入（`rife_pack.ptx:L445-L474`）**：  
   将 `setp.eq.s32 %p4, %r7, 0;` 至 `bra.uni $L__BB1_6;` 整段替换为：

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

---

### **Wave 2：纯 C ABI 驱动重写 `tests/test_rife.cpp` Case 1–3**

将 `tests/test_rife.cpp` 末尾 3 个测试用例（原第 748–877 行）完整替换为以下纯 C ABI 实现（复用顶部 `TestCudaDriver` 与 `rtx::NgxRemasterBridge`，零新增具名类型，涵盖 `full_range = 0/1` 双色域回归）：p  
RTX\_TEST(rife\_real\_engine\_1080p\_fg\_first\_to\_4k\_120fps) {  
// Doc 59/61/63 Case 1: Real RIFE engine 1920x1080 NV12 via pure C ABI \-\>  
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
// Doc 59/61/63 Case 2: C ABI stream isolation soak \+ same-device-pointer overwrite anti-aliasing.  
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
// Doc 59/61/63 Case 3: Pure C ABI fail-fast guard on invalid handles, uninitialized RIFE,  
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

---

### Wave 3：宿主机全量构建、五重静态门禁与 RTX 5060 真机终审验收

按顺序在 Windows 11 + MSVC 19.51 + RTX 5060 宿主机执行以下验收序列（全部 PASS 方可签发封板）：

| 验收序号 | 执行命令 / 实测场景 | 强制通过标准 |
| :---: | :--- | :--- |
| **Gate W3-1** | `.\build.bat` | MSVC `/W4 /WX` 全目标（含 `rife_pack_ptx.h` 重新生成与 `rtx_tests.exe` 链接）**`0 Error, 0 Warning`**，退出码 `0`。 |
| **Gate W3-2** | `cmake -P cmake/AuditTypes.cmake`<br>`cmake -P cmake/AuditImports.cmake`<br>`cmake -P cmake/AuditPrivacy.cmake`<br>`cmake -P cmake/AuditSpdx.cmake`<br>`cmake -P cmake/AuditSloc.cmake` | `AuditTypes == 34 / 35`；`AuditImports`（GUI `<= 4` / HEADLESS `== 2`）；`AuditPrivacy == 0 violations`；`AuditSpdx == PASS`；`AuditSloc Core <= 23,000`。 |
| **Gate W3-3** | `.\.vs\build\cmake\windows-msvc-ninja\rtx_tests.exe` | **`157 test(s) run, 0 failed`**（确认 `rife_real_engine_1080p_fg_first_to_4k_120fps` 真实 `1920x1080` 四相位非重复运动插值、limited/full 双色域及中性灰 UV 校验全部实跑 PASS）。 |
| **Gate W3-4** | 启动桌面 `CineRTX.exe` 实片压测 | 拖入视频文件立即入队，切换 `120-rife` 同步更新，RTX 5060 转码输出真实 `120fps` 平滑插帧视频，全片无紫色闪烁、无边缘黑框、无帧率冻结。 |

---

**施工 Superpower 规约签发完毕。严格按 Wave 0 -> Wave 1 -> Wave 2 -> Wave 3 顺序落地并回传四重门禁物证。**
```

