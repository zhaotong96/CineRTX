# 68_CineRTX Phase 13 Wave 0-2 越界清理、PTX 7.5 指令同步与纯 C ABI 单测重构汇报

> **汇报编号**：`CINERTX-P13-REWORK-REPORT-068`  
> **施工依据**：`61_CineRTX RIFE 重构(60号)验收阻塞与 C ABI 单测及 PTX 修复委托书.md`、`62–63` 号方案与总纲、`64` 号（Wave 0）、`65` 号（Wave 1）、`66` 号（Wave 2）、`67` 号（Wave 3）  
> **精准变更范围**：严格仅触碰 `src/shim/ngx_bridge/rife_pack.ptx` 与 `tests/test_rife.cpp` 两份文件（旧版均已归档至 `work/_CineRTX_bak_archive/*.p13r10bak`），60 号已通过宿主机编译的其余 12 份增量重构成果 100% 保留。

---

## 一、 Wave 0：越界头文件拔除与废弃旧桩防回流（Doc 64）

1. **物理拔除越界内部头文件**（`tests/test_rife.cpp:L6-L22`）：删除 `#include "../src/shim/ngx_bridge/rife_engine.h"` 与 `#include "../src/shim/ngx_bridge/nv_func_tables.h"`，仅保留合法公共边界头文件（`test_framework.h`、`rtx/playback_session.h`、`rife_pack_runtime.h`、`rtx/ngx_remaster.h`、`shim/ngx_bridge_abi.h`），彻底阻断 `LNK2019` 与内部符号渗透（`[DEC-010]`）。
2. **负向正则全文件扫描**：扫描 `tests/test_rife.cpp` 确认 `rife_engine.h`、`nv_func_tables.h`、`rtx_ngx::RifeEngine`、`rtx_ngx::nv_cuda`、`evaluateFramePipelineTier`、`makeAdaptiveFrameBudgetState`、`FramePipelineTier` 全部 **0 命中**。

---

## 二、 Wave 1：PTX 7.5（`sm_75`）指令级同步手术（Doc 65）

对 `src/shim/ngx_bridge/rife_pack.ptx`（原 1027 行 -> 更新后 1056 行）执行无新增 `.reg` 声明的寄存器安全指令级手术：

1. **Step 1.1 — 预处理 `rife_nv12_to_nchw_7ch_fp32` 有限值归零守卫**（`src/shim/ngx_bridge/rife_pack.ptx:L332-L345`）：
   * 核验 `L289` 存在 `bra.uni $L__BB0_10;`（`full_range != 0` 分支汇合点），因此将 13 条指令精准植入 `$L__BB0_10:` 入口之后、`cvta.to.global.u64 %rd16, %rd3;` 之前，复用空闲 `%p27`（`.reg .pred %p<28>`）与 `%f95`（`.reg .f32 %f<105>`），一次性对 **Limited Range 与 Full Range 双路径**的 `%f89..%f94` 执行 `testp.finite.f32` + `selp.f32` 将 `NaN / ±Inf` 归零。
2. **Step 1.2 — 后处理 `rife_nchw_3ch_fp32_to_nv12` 双线性与全分辨率 `clamp01` 守卫**（`src/shim/ngx_bridge/rife_pack.ptx:L494-L555`）：
   * 复用空闲 `%p21`（`.reg .pred %p<22>`）与 `%f5 = 0f00000000`，在双线性路径（`L494, L511, L528`）与 `$L__POST_FULL:` 路径（`L538, L546, L554`）的 R/G/B 三通道 `min.ftz.f32` 后各追加 2 条 `testp.finite.f32` + `selp.f32` 有限值归零指令。
3. **Step 1.3 — `full_range != 0` 补全 `* 255.0f` 缩放与 `+ 0.5f / 128.5f` 偏移**（`src/shim/ngx_bridge/rife_pack.ptx:L567-L606`）：
   * 复用空闲 `%f77, %f78, %f79`（`.reg .f32 %f<80>`），将 `[0, 1]` 的 `%f1, %f2, %f3` 乘以 `0f437F0000` (`255.0f`)（`L571-L573`），Y 通道补全 `+ 0.5f`（`L577`），U/V 通道偏移修正为 `128.5f` (`0f43008000`，`L591, L597`)，根除 `full_range == 1` 下 Y/U 坍缩导致的黑屏/紫闪。

---

## 三、 Wave 2：纯 C ABI 驱动 RIFE 单测重构（Doc 66）

在 `tests/test_rife.cpp` 中将全部 3 项新增用例重构为 **100% 纯 C ABI（`rtx::NgxRemasterBridge` + `TestCudaDriver`）** 驱动：

1. **Step 2.1 — Case 1 (`rife_real_engine_1080p_fg_first_to_4k_120fps`)**（`tests/test_rife.cpp:L717-L906`）：
   * 经 `bridge.open/create/initRifeEx/interpolateRife/destroyRife/releaseBridge` 驱动真实 `models/rife_v4.25_wide.engine`（`1920x1080` NV12），验证 `full_range = 0` 四相位（`0.2/0.4/0.6/0.8`）`scene_cut == 0`、相邻帧 `MAE > 0.5`、亮块质心严格单调递增、中性灰 UV `128 ± 2`；
   * 同步执行 `full_range = 1` 推理校验 `bg_y ∈ [24, 40]`、`fg_y ∈ [190, 230]`、`bg_u/bg_v ∈ [126, 130]`，直接端到端验证 Wave 1 `rife_pack.ptx` `* 255.0f` 补丁在 GPU 上真实生效。
2. **Step 2.2 — Case 2 (`rife_no_preview_ring_stream_isolation_soak`)**（`tests/test_rife.cpp:L908-L1037`）：
   * 经 `bridge.buildRifeEngine("__builtin__", ...)` + 20 轮 `initRifeEx` / 4 相位 `interpolateRife` / `destroyRife` 循环复用同一组 GPU 设备指针 `d0/d1/dout` 覆写不同位移内容，验证专属非阻塞流隔离与防指针别名零误判。
3. **Step 2.3 — Case 3 (`rife_geometry_mismatch_fail_fast_guard`)**（`tests/test_rife.cpp:L1039-L1105`）：
   * 经纯 C ABI 验证空句柄、未初始化 `initRifeEx` 直接调 `interpolateRife`、零宽高配置及超限尺寸 `9999x9999` 快速返回错误码（`RTX_ERR_INVALID_ARGUMENT` / 非 `RTX_OK`）。

---

## 四、 门禁 CheckList 勾选与实证

### 4.1 Wave 0 – Wave 2 代码级与离线门禁（已实证勾选）

| 门禁编号 | 核验项 | 状态 | 实证坐标与结果 |
| :---: | :--- | :---: | :--- |
| **Gate W0-1** | `tests/test_rife.cpp` 越界内部头清零 | `[x]` | `tests/test_rife.cpp:L6-L22`；负向扫描 `rife_engine.h` / `nv_func_tables.h` 命中 `0` |
| **Gate W0-2** | 废弃旧桩与内部符号清零 | `[x]` | 负向扫描 `rtx_ngx::RifeEngine` / `rtx_ngx::nv_cuda` / `evaluateFramePipelineTier` / `FramePipelineTier` 命中 `0` |
| **Gate W1-1** | PTX `testp.finite.f32` 精确 12 处 & 零新增 `.reg` | `[x]` | `src/shim/ngx_bridge/rife_pack.ptx:L334-L344`（6 处）、`L494,L511,L528,L538,L546,L554`（6 处），合计精确 `12` 处 |
| **Gate W1-2** | PTX Full-Range `* 255.0f` 缩放精确 3 处 | `[x]` | `src/shim/ngx_bridge/rife_pack.ptx:L571-L573`（`%f77/%f78/%f79` 乘 `0f437F0000`）+ `L591,L597`（`128.5f`） |
| **Gate W2-1** | Case 1–3 纯 C ABI 重构与语法平衡 | `[x]` | `tests/test_rife.cpp:L717,L908,L1039`；两文件花括号/小括号平衡均为 `0` |
| **Gate W2-2** | 离线宪法红线自检 | `[x]` | 新增具名类型 `+0`（`AuditTypes == 34/35`）、新增静态 DLL `+0`、`AuditPrivacy == 0`、`AuditSpdx == PASS` |

### 4.2 Wave 3（Doc 67）Windows 11 + RTX 5060 宿主机终验项（严格遵循 `[DEC-004]` / `[DEC-008]` 如实留待）

> **诚实声明**：当前执行环境为离线 Linux 沙箱，无 Windows 11 / MSVC 19.51 / RTX 5060 物理宿主机。严禁伪造任何构建、单测、导入表或实机截图日志，以下 Wave 3 终验项如实保持 `[ ]`，请于 Windows 11 宿主机拉取最新两份文件后依次执行封板：

- [ ] **Step 3.1（MSVC `/W4 /WX` 全量构建）**：执行 `.\build.bat`，确认触发 `rife_pack.ptx` -> `rife_pack_ptx.h` 重新生成，全目标 `0 Error, 0 Warning`，退出码 `0`。
- [ ] **Step 3.2（五重静态门禁）**：执行 `AuditTypes`（`34/35`）、`AuditImports`（GUI `<= 4` / HEADLESS `== 2`）、`AuditPrivacy`（`0`）、`AuditSpdx`（`PASS`）、`AuditSloc`（`<= 23,000`）。
- [ ] **Step 3.3（全套单元测试 `157/157 PASS`）**：执行 `.\.vs\build\cmake\windows-msvc-ninja\rtx_tests.exe`，确认 `157 test(s) run, 0 failed` 且 3 项 RIFE C ABI 用例均无 `[SKIP-GPU]` 全绿通过。
- [ ] **Step 3.4（RTX 5060 桌面真机实片终验）**：启动 `CineRTX.exe`，拖拽视频入队 -> 切换 `120-rife` -> 转码导出，验证 `120 fps` 真实非 dup 运动插帧且全片无紫绿闪烁。
