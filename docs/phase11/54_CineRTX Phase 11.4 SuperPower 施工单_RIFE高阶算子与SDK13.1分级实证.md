# CineRTX Phase 11.4 SuperPower 施工单：RIFE 高阶算子与 SDK 13.1 分级实证（v0.3.0 决胜封板）

**工单编号**：`CINERTX-P11-WO-054`（承接并升维 `CINERTX-P11-WORKORDER-020-REV2`）  
**项目阶段**：`Phase 11.4 (v0.3.0 Milestone Closure)`  
**下达日期**：2026-11-22  
**建议工期**：3–4 个有效工作日  
**执行角色**：GPU AI 推理与 NVENC 编解码优化高级工程师（External Senior GPU AI & Codec Engineer）  
**协调负责人 / 门禁终审人**：Coordinator（负责 MSVC 编译自愈、门禁平账、RTX 5060 硬件物证复核与方案 B 历史重写组织）  
**前置基线**：Commit `1e55a06`（`feat(phase11.3): seal phase 11.3 strategic baseline with ngx dynamic isolation and full portrait adaptive transcode`）  
**上位指导与输入资产**：
- [`docs/phase11/53_CineRTX Phase 11.3 止血与竖屏修复终审验收裁决书暨 Phase 12 极限现代 C++ 重构与 DLL 净空总方案.md`](53_CineRTX%20Phase%2011.3%20止血与竖屏修复终审验收裁决书暨%20Phase%2012%20极限现代%20C++%20重构与%20DLL%20净空总方案.md)
- [`docs/phase11/20_CineRTX Phase 11.4 SuperPower 施工单_RIFE高阶算子与SDK13.1分级实证.md`](20_CineRTX%20Phase%2011.4%20SuperPower%20施工单_RIFE高阶算子与SDK13.1分级实证.md)
- [`docs/phase11/21_CineRTX Phase11 施工方案深度审查.md`](21_CineRTX%20Phase11%20施工方案深度审查.md)
- [`DECISIONS.md`](../../DECISIONS.md)（`[DEC-008]`, `[DEC-009]`, `[DEC-010]`）

---

## 一、 施工背景与收官定位

Phase 11.4 是 CineRTX 迈向 **v0.3.0 正式发版** 的收官决胜战役。在 Phase 11.3 止血与竖屏自适应阶段，工程团队已彻底攻克了 GPL 开源隔离净空（`rtx_ngx_bridge.dll` 独立动态加载）、WSOLA 多声道混音、DisplayMatrix 36 字节容器透传与横竖屏全档位零拷贝转码（141/141 单测全绿，`D2H == 0 B`），并获得了首席领域专家组在 [53 号裁决书](53_CineRTX%20Phase%2011.3%20止血与竖屏修复终审验收裁决书暨%20Phase%2012%20极限现代%20C++%20重构与%20DLL%20净空总方案.md) 中的正式战略封板授权。

本施工单旨在完成 Phase 11 规划的最后三大高难度算法与底层驱动突破，彻底消除以下历史断环：
1. **C-06 断环**：RIFE scale=0.5 像素对齐（$A=128$ 模数）、reflect101 边界镜像折返与保守 PTX ISA 同步生成；
2. **显存 OOM 断环**：基于 RTX 5060 宿主机实际 7,300 MiB DXGI 显存预算建模的滞后自适应平滑降级调度器；
3. **C-07 断环**：Video Codec SDK 13.1 运行时双模协商、环形多缓冲池（多 B 帧延迟重排）与分层 B 帧/UHQ 矩阵实测。

---

## 二、 核心外科手术任务分解

### 任务 1：RIFE scale=0.5 算子升级、PTX 编译同步与 4K 拓扑合龙 (C-06)

- **涉及源码与测试**：
  - `include/shim/ngx_bridge_abi.h`
  - `src/shim/ngx_bridge/bridge_exports.cpp`
  - `src/shim/ngx_bridge/rife_engine.h` / `rife_engine.cpp`
  - `src/shim/ngx_bridge/rife_builder.cpp`
  - `src/shim/ngx_bridge/rife_pack_runtime.h` / `rife_pack_runtime.cpp`
  - `src/shim/ngx_bridge/rife_pack.cu` / `rife_pack.ptx`
  - `src/core/hw_transcode_stages.cpp`
  - `tests/test_rife_scale05.cpp`（新建）
- **改造指令**：
  1. **ABI 向后兼容扩充（纯 C 边界）**：
     在 `include/shim/ngx_bridge_abi.h` 中追加扩展结构体（严守 MIT/Apache-2.0 许可），新增扩展初始化入口 `rtx_ngx_bridge_init_rife_ex`，支持外部显式配置 `scale`（1.0 / 0.5）、`align_mod` 及 `reflect_padding` 参数；在 `bridge_exports.cpp` 导出并绑定。
  2. **数学对齐与 4K 动态 Profile 拓宽**：
     将 `rife_builder.cpp` 与 `rife_engine.cpp` 中的对齐模数升级为 $A=128$（确保经 2× 光流降采样后仍严格 64 字节对齐）；动态优化 Profile 上限拓展至 4K 对齐尺寸 $3840 \times 2176$。
  3. **reflect101 镜像填充核函数与 PTX ISA 同步更新（C-06 核心）**：
     修改 `rife_pack.cu`，在预处理与后处理核函数中实现 `reflect101` 对称折返映射（越界时折返采样，输出写回时裁回原始尺寸，彻底消除边缘伪影与黑边）；以保守兼容的 PTX ISA 目标重新编译，并**同步更新 `src/shim/ngx_bridge/rife_pack.ptx` 与 `rife_pack_runtime.h/.cpp`**，绝不允许低版本驱动 JIT 拒载。
  4. **管线尺寸传递与 vsr-first 超分后补帧流转**：
     在 `hw_transcode_stages.cpp` 中按当前拓扑梯队传递准确尺寸，将 `anchor_p010_` 真正接入 `vsr-first`（先超分至 4K 再进行插帧）流转中。
  5. **单元测试落地**：
     新建 `tests/test_rife_scale05.cpp`，断言输出边缘像素无黑边，记录真实 `trt_peak_allocation_bytes`。

---

### 任务 2：显存滞后自适应调度器（基于 7300 MiB 实际 DXGI 预算建模）

- **涉及源码与测试**：
  - `include/rtx/vram_scheduler.h`（新建）
  - `src/core/vram_scheduler.cpp`（新建）
  - `src/core/hw_transcode.cpp`
  - `src/core/hw_transcode_stages.cpp`
  - `tests/test_vram_scheduler.cpp`（新建）
- **改造指令**：
  1. **基于实际可用预算建模**：
     在 Windows WDDM 环境下，8 GB 显卡扣除系统 DWM 占用后，实际可用 `dxgi_budget_bytes` 约为 **7200~7600 MiB**（基准锁定为 **7300 MiB**）：
     - **82% 降级高水位线** $\approx \mathbf{5986.0\text{ MiB}}$；
     - **68% 恢复低水位线** $\approx \mathbf{4964.0\text{ MiB}}$；
     - **最小驻留周期**：**120 帧**，防临界点频繁抖动。
  2. **多指标联合判定与平滑降级状态机**：
     以 `dxgi_budget_bytes` 为分母，联合 `cuMemGetInfo` 空闲显存、RIFE 实际 `trt_peak_allocation_bytes` 与编解码参考帧显存占用，严格按以下梯队平滑降级：
     `VsrFirst_RifeFull` $\rightarrow$ `VsrFirst_RifeHalf` $\rightarrow$ `VsrFirst_Nvofa` $\rightarrow$ `FgFirst_RifeFull` $\rightarrow$ `PassThrough_NoFruc`。
  3. **单元测试与防 OOM 实证**：
     新建 `tests/test_vram_scheduler.cpp` 覆盖 5 级降级状态转换与 120 帧滞后防抖；在宿主机 RTX 5060 执行 4K 长时间压测，实证显存全程在 82% 水位线以下，零 OOM 崩溃。

---

### 任务 3：Video Codec SDK 13.1 双模协商与环形多缓冲池重构 (C-07)

- **涉及源码**：
  - `src/shim/loader/nv_encoder.cpp`
- **改造指令**：
  1. **运行时 API 版本双模协商**：
     生产源码保持引用现有的 `nv-codec-headers`。创建编码会话前调用 `NvEncodeAPIGetMaxSupportedVersion(&max_ver)`：
     - 若驱动版本支持 $\ge 13.1$（如宿主机驱动 `621.24 WHQL`），以高阶版本打开，并通过局部安全常量补齐 `NV_ENC_BFRAME_REF_MODE_HIERARCHICAL = 0x420` 与 UHQ Tuning 定义；
     - 若低版本驱动或硬件不支持，平滑回退至 13.0 默认配置，绝不强退。
  2. **环形多缓冲池重构（消灭 C-07 单缓冲状态机阻断）**：
     - 彻底废除单一输出缓冲设计，将输出槽升级为 `std::vector<BitstreamSlot>` 环形池（容量至少 $4 + \text{B帧数} + \text{Lookahead深度}$），配套各自异步完成事件；
     - 当 `nvEncEncodePicture` 返回 `NV_ENC_ERR_NEED_MORE_INPUT` 时，判定为合法缓冲蓄水信号，绝不误判为 `RTX_ERR_INTERNAL`；
     - 重写 `rtx_shim_encoder_flush()`：提交 `NV_ENC_PIC_FLAG_EOS` 后，循环锁定并排空所有待重排的延迟编码包。
  3. **分层 B 帧与 UHQ 矩阵实测全表**：
     在宿主机驱动 `621.24 WHQL` 环境下，实测并输出 AV1 分层 B 帧（1 帧、3 帧、7 帧、15 帧、31 帧）在 Default、HighQuality-7B 与 Master-UHQ-15B/31B 配置下的对比数据全表，包含 FPS、VRAM 峰值、PSNR/SSIM/VMAF 对比。

---

## 三、 外部工程师交付验收标准（准出硬性门禁）

外部高级工程师在完成开发后，必须提交 `docs/phase11/55_CineRTX Phase 11.4 施工汇报_RIFE高阶算子与SDK13.1分级实证.md` 并达成以下硬性指标：

| 准出硬门禁项 | 验证命令 / 指标口径 | 必须达成结果 |
| :--- | :--- | :--- |
| **C++ 单元测试全绿** | `ninja -C build rtx_tests && .\build\rtx_tests.exe` | **143+ 项单元测试 100% 全部通过 (PASS)** |
| **类型门禁** | `cmake -P cmake/AuditTypes.cmake` | **PASS (守死 153 类型红线，零无谓包装类)** |
| **SLOC 门禁** | `cmake -P cmake/AuditSloc.cmake` | **PASS (严格控制增量，不得肆意扩张样板代码)** |
| **SPDX 开源门禁** | `cmake -P cmake/AuditSpdx.cmake` | **PASS (新增文件 100% 具备合法开源头)** |
| **动态隔离门禁** | `cmake -P cmake/AuditImports.cmake` | **PASS (CineRTX.exe 零专有符号，仅宿主合规依赖)** |
| **真机防 OOM 穿透** | RTX 5060 4K 压制全流程压力实测 | **全程显存占用 $\le 82\%$ (5,986 MiB)，零 OOM 崩溃** |
| **分层 B 帧实证全表** | 交付 Markdown 格式实测对比表 | **数据完整翔实，具备 100% 独立可复现性** |

---

## 四、 协调负责人（Coordinator）后续封板与方案 B 执行规程

1. **环境与跨平台编译自愈**：负责人接管交付代码，在 Windows MSVC 19.51 下验证编译并执行门禁平账。
2. **RTX 5060 实机物证终验**：复核 82% 水位线与分层 B 帧画质数据。
3. **出具 56 号终审委托书**：向首席领域专家呈报，申请签发 `v0.3.0` 最终封板印章。
4. **方案 B 原子化执行（清洗远端 Git 历史）**：
   在获得专家终审封板与用户最高授权后，负责人立即执行：
   ```bash
   git rebase -i 74d4141  # 修正 Phase 10 Track 10A 的 CMakeLists.txt，彻底拔除静态直链
   git tag -d v0.2.0      # 重建纯净 v0.2.0 标签
   git tag v0.3.0         # 正式打标 v0.3.0
   git push --force origin main --tags  # 原子化清洗远端违规历史并推送
   ```
5. **正式跨入 Phase 12**：开启极限现代 C++ 全仓重构与系统 DLL 极限剥离。

---

> [!NOTE]
> 本工单是 Phase 11（v0.3.0）的最后一份施工单，承接并更新了 20 号施工单内容，全面纳入了 Phase 11.3 战略封板成果与驱动 `621.24 WHQL` 实测环境。

> [!TIP]
> 架构纪律：外部工程师必须严格遵守 `[DEC-008]` 正交分工，禁止修改已封板的 Phase 11.3 音频与竖屏几何核心逻辑；专有头文件与实现严格封印在 `rtx_ngx_bridge.dll` 内部。

> [!IMPORTANT]
> 核心任务攻坚：显存滞后自适应调度器与 SDK 13.1 环形多缓冲池是保证 8 GB 显卡 4K 稳定压制与下一代分层 B 帧高效输出的决定性特性，必须以硬件物证为唯一准出标准。

> [!WARNING]
> 显存基线红线：严禁采用粗暴的固定 8 GB 计算水位线，必须严格基于 DXGI 实际报告的约 7,300 MiB 可用预算计算 82% 阈值（5,986 MiB），避免系统 DWM 争抢导致的黑屏或驱动崩溃。

> [!CAUTION]
> 严防合规与 Git 倒退：绝对禁止在施工期间向 `origin/main` 执行 `git push`；远端历史清洗（方案 B）必须由负责人在 Phase 11.4 最终验收完成并获用户明确授权后统一执行。
