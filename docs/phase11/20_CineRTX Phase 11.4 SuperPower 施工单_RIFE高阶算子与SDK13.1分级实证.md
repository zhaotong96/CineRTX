# CineRTX Phase 11.4 SuperPower 施工单：RIFE 高阶算子与 SDK 13.1 分级实证封板 v0.3.0（终审修订版）

- **施工单号**：`CINERTX-P11-WORKORDER-020-REV2`
- **归档路径**：`docs/phase11/20_CineRTX Phase 11.4 SuperPower 施工单_RIFE高阶算子与SDK13.1分级实证.md`
- **承接角色**：GPU AI 推理与 NVENC 编解码优化工程师（外部执行工程师）
- **前置基线**：Phase 11.3 剪辑与音频总线合龙基线
- **依据母本**：[`16_CineRTX Phase 11 演进计划.md`](./16_CineRTX%20Phase%2011%20演进计划.md)、[`21_CineRTX Phase11 施工方案深度审查.md`](./21_CineRTX%20Phase11%20施工方案深度审查.md)
- **建议工期**：4–5 个有效工作日
- **核心目标**：RIFE scale=0.5 像素对齐与 PTX 同步更新（C-06）、基于 7300 MiB 实际 DXGI 显存预算建模防 OOM、重构 `nv_encoder.cpp` 多缓冲池并实测 SDK 13.1 分层 B 帧（C-07），收官封板 **v0.3.0**。

---

## 一、 施工背景与输入资产

Phase 11.4 是 Phase 11（v0.3.0 里程碑）的收官决胜切片。21 号穿透审查指出，原计划在 CMake PTX 产物更新（C-06）以及 SDK 13.1 单缓冲状态机（C-07）上存在严重阻断风险。本施工单已全量收敛上述缺陷，确保外部工程师能够顺利开展真机实测。

### 核心参考与输入文件
- 历史参考源：`archive/phase11-attempt-1` 中的 `src/core/vram_scheduler.cpp`、`src/shim/ngx_bridge/rife_engine.cpp`
- 架构约束与技术裁决：[`21_CineRTX Phase11 施工方案深度审查.md`](./21_CineRTX%20Phase11%20施工方案深度审查.md) 第五节

---

## 二、 核心开发任务与实施规范

### 任务 1：RIFE scale=0.5 像素对齐、PTX 编译同步与管线拓扑（C-06）
修改/新建文件：`include/shim/ngx_bridge_abi.h`、`src/shim/ngx_bridge/bridge_exports.cpp`、`src/shim/ngx_bridge/rife_engine.h/.cpp`、`src/shim/ngx_bridge/rife_builder.cpp`、`src/shim/ngx_bridge/rife_pack_runtime.h/.cpp`、`src/shim/ngx_bridge/rife_pack.cu`、`src/shim/ngx_bridge/rife_pack.ptx`、`src/core/hw_transcode_stages.cpp`、`tests/test_rife_scale05.cpp`

1. **ABI v5 向后兼容扩充**：
   在 `include/shim/ngx_bridge_abi.h`（严守 `MIT OR Apache-2.0`）中追加 `RtxRifeConfig` 结构体及 `rtx_ngx_bridge_init_rife_ex` 入口，并在 `bridge_exports.cpp` 与 `NgxRemasterBridge` 绑定。
2. **PTX 编译同步与保守 ISA 目标（C-06）**：
   修改 `rife_pack.cu` 增加 A=128 对齐及 `reflect101` 镜像填充核函数；必须采用保守兼容的 PTX ISA 目标版本重新生成，并**同步更新 `src/shim/ngx_bridge/rife_pack.ptx` 与 `rife_pack_runtime.h/.cpp`**，防止低版本驱动发生 JIT 拒载事故。
3. **数学对齐与 4K 动态 Profile**：
   将 `rife_engine.cpp` 与 `rife_builder.cpp` 的对齐模数升级为 $A=128$，确保经 2× 光流降采样后仍整数对齐；动态 Profile `kMAX` 扩展至 4K 对齐尺寸 $3840 \times 2176$；`reflect101` 采样在越界时对称折返（如 -1 映射至 1，W 映射至 W-2），输出回写时裁回原始尺寸，消除边缘黑边与伪影。
4. **管线尺寸传递与 vsr-first 合龙**：
   在 `src/core/hw_transcode_stages.cpp` 中按 `TopologyTier` 传递真实输入分辨率调用 `initRife`，并将闲置的 `anchor_p010_` 真正接入 `vsr-first` 超分后补帧流转中。
5. **新建独立验证用例**：
   落地 `tests/test_rife_scale05.cpp`，断言输出边缘像素无黑边，并真实记录输出 `trt_peak_allocation_bytes`。

---

### 任务 2：显存滞后自适应调度器（基于 7300 MiB 实际 DXGI 预算建模）
修改/新建文件：`include/rtx/vram_scheduler.h`、`src/core/vram_scheduler.cpp`、`src/core/hw_transcode.cpp`、`tests/test_vram_scheduler.cpp`

1. **基于实际可用预算建模**：
   在 Windows WDDM 环境下，8 GB 显卡扣除系统 DWM 占用后，实际可用 `dxgi_budget_bytes` 约为 **7200~7600 MiB**（按 7300 MiB 估算）：
   - **82% 降级高水位线** $\approx \mathbf{5986.0\text{ MiB}}$；
   - **68% 恢复低水位线** $\approx \mathbf{4964.0\text{ MiB}}$；
   - **最小驻留周期**：**120 帧**，防临界点频繁抖动。
2. **多指标联合判定与平滑降级**：
   以 `dxgi_budget_bytes` 为分母，联合 `cuMemGetInfo` 空闲显存、RIFE 实际 `trt_peak_allocation_bytes` 与编解码参考池，严格按以下梯队平滑降级：
   `VsrFirst_RifeFull` $\rightarrow$ `VsrFirst_RifeHalf` $\rightarrow$ `VsrFirst_Nvofa` $\rightarrow$ `FgFirst_RifeFull` $\rightarrow$ `PassThrough_NoFruc`。
3. **8 GB 显卡 4K 压制防 OOM 实证**：
   在 RTX 5060 Laptop GPU 上执行 4K 压制全流程压力测试，实证全程显存受控在 82% 水位线以下，零 OOM 崩溃。

---

### 任务 3：Video Codec SDK 13.1 双模协商与多缓冲池重构（C-07）
修改文件：`src/shim/loader/nv_encoder.cpp`

1. **运行时 API 版本双模协商**：
   生产二进制保持 `.gitmodules` 中的 `nv-codec-headers n13.0.19.1`。创建会话前调用 `NvEncodeAPIGetMaxSupportedVersion(&max_ver)`：驱动支持 $\ge 13.1$ 时以高阶版本打开，并通过局部隔离常量补齐 `NV_ENC_BFRAME_REF_MODE_HIERARCHICAL = 0x420` 与 UHQ Tuning 定义；若 $< 610.0$ 驱动或硬件不支持则优雅回退至 13.0 Default 配置。
2. **多缓冲环形池与排空重写（C-07）**：
   - 彻底废除单一输出缓冲设计，将输出槽升级为 `std::vector<BitstreamSlot>` 环形池（容量至少 $4 + \text{B帧数} + \text{Lookahead深度}$），配套各自异步完成事件。
   - 当 `nvEncEncodePicture` 返回 `NV_ENC_ERR_NEED_MORE_INPUT` 时，合法返回缓冲蓄水信号，绝不误判为 `RTX_ERR_INTERNAL`。
   - 重写 `rtx_shim_encoder_flush()`：提交 `NV_ENC_PIC_FLAG_EOS` 后，循环锁定并排空所有待重排的延迟编码包。
3. **分层 B 帧与 UHQ 矩阵实测**：
   在驱动 $\ge 621.14$ WHQL 环境下，实测并输出 AV1 分层 B 帧（1 帧、3 帧、7 帧、15 帧、31 帧）在 Default、HighQuality-7B 与 Master-UHQ-15B/31B 配置下的对比数据。完整记录测试口径，输出 FPS、VRAM 峰值、PSNR、SSIM 与 VMAF 对比全表。

---

## 三、 外部工程师交付验收检查清单（Gates）

- [ ] **Check 1：高阶算子单测全绿**：`tests/test_rife_scale05` 与 `tests/test_vram_scheduler` 100% PASS。
- [ ] **Check 2：8 GB 显卡 4K 压制零 OOM**：实机 4K 60fps/120fps 重制压制全程显存稳定受控于 82% 阈值以下。
- [ ] **Check 3：完整实测矩阵如实落盘**：交付包含真实硬件测量数据的 Markdown 测试矩阵表。
- [ ] **Check 4：合规与体积封板**：
  - `cmake -P cmake/AuditSpdx.cmake` 100% PASS。
  - `cmake -P cmake/AuditImports.cmake` 确认 `CineRTX.exe` 体积位于 `[1.2 MB, 12.0 MB]` 范围之内，零泄漏动态伴生库。
  - 全量 `rtx_tests.exe` 100% PASS。
