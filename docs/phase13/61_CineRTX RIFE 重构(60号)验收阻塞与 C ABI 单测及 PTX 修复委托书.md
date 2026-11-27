# 61_CineRTX RIFE 重构(60号)验收阻塞与 C ABI 单测及 PTX 修复委托书

- **基线与状态**：基于 `a6451b7` 合入 `60_` 增量重构（13 份文件无旧快照覆写，`AuditSpdx=PASS`、`AuditTypes=34/35`、`AuditSloc Core=12496`、`AuditPrivacy=0`，且 `rtx_ngx_bridge.dll` / `rtx_core.lib` / `rtx_headless_cli.exe` / `CineRTX.exe` 均已编译通过）。
- **委托目标**：请领域专家针对宿主机实机编译暴露的 **3 项阻塞** 出具精准施工指令（供工程师一次性修复或由协调负责人宿主机直接落地），确保 `rtx_tests.exe` 达到 `157/157 PASS`。

---

## 一、宿主机实测阻塞清单

### 阻塞 1（P0）：`tests/test_rife.cpp` Case 1–3 越界直调 `rtx_ngx::RifeEngine` 导致 `LNK2019`
- **实测报错**：`rtx_tests.exe` 链接失败 `LNK2019: unresolved external symbol RifeEngine::~RifeEngine / loadEngineEx / interpolate`。
- **根因**：`rtx_tests.exe` 严守 GPL 隔离边界，不编译 `rife_engine.cpp` 也不链接 TensorRT `nvinfer_10.lib`；RIFE 能力仅通过 `rtx_ngx_bridge.dll` 的 C ABI（`rtx::NgxRemasterBridge`：`open` / `create` / `initRifeEx` / `interpolateRife` / `destroyRife`）暴露。
- **需裁决/施工要求**：
  - 将 `tests/test_rife.cpp:L748-L877` 的 Case 1（`rife_real_engine_1080p_fg_first_to_4k_120fps`）、Case 2（`rife_no_preview_ring_stream_isolation_soak`）、Case 3（`rife_geometry_mismatch_fail_fast_guard`）全部改写为经由 `rtx::NgxRemasterBridge` C ABI 驱动；
  - Case 2 需验证当传入 `stream == nullptr` 或通过 C ABI 调用 `interpolateRife` 时，底层的 `rife_stream_` 非阻塞流隔离与 `resetFramePairCache` / 同指针复用防碰撞逻辑均能通过 C ABI（或补充 C ABI 测试入口）完整穿透验证。

### 阻塞 2（P0）：`tests/test_rife.cpp` 残留旧桩代码（已在宿主机清理，需防回流）
- **实测报错**：原 `tests/test_rife.cpp:L719-L746` 存在未实现的 `makeAdaptiveFrameBudgetState` / `evaluateFramePipelineTier` / `FramePipelineTier` 及误将 `RifeAspectBucket` 当布尔值 `!t1` 的编译错误（`C2675` / `C2039` / `C3861`）。
- **要求**：严禁工程师再次以旧版本 `test_rife.cpp` 覆写引入上述已废弃桩代码。

### 阻塞 3（P1）：`rife_pack.cu` 修复未同步至预编译 `rife_pack.ptx`
- **实测事实**：`CMakeLists.txt:L213, L237` 直接将 `src/shim/ngx_bridge/rife_pack.ptx` 嵌入为 `rife_pack_ptx.h`；宿主机未安装 `nvcc`，因此 `60_` 仅修改 `rife_pack.cu`（`clamp01` `isnan`/`isinf` 与 `full_range != 0` 补充 `* 255.0f`）**不会**在运行时生效。
- **需裁决/施工要求**：
  - 请专家给出 `rife_pack.ptx` 对应 kernel（`rife_nchw_3ch_fp32_to_nv12` 与 `clamp01`）的等价 PTX 指令级修复方案，或确认交付已重编的 `rife_pack.ptx`，确保 `full_range == 1` 与 `NaN/Inf` 钳位在真机上实际生效。

---

## 二、验收标准

1. `.\build.bat`（MSVC `/W4 /WX`）：`0 Error, 0 Warning`，退出码 `0`；
2. 五重静态门禁全绿：`AuditTypes == 34/35`、`AuditImports`（GUI `<= 4` / HEADLESS `== 2`）、`AuditPrivacy == 0`、`AuditSpdx == PASS`、`AuditSloc` 预算内；
3. `rtx_tests.exe`：全部 `157/157 PASS`（含真实 `rife_v4.25_wide.engine` `1920x1080` 非 dup 运动插值与合法中性灰 UV 色度校验）；
4. RTX 5060 GUI 实测：拖入文件立即入队、切换 `120-rife` 同步更新、转码输出真实 `120fps` RIFE 产物且无紫色闪烁。
