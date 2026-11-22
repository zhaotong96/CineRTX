# CineRTX Phase 11.4-F SuperPower 施工单：物理双引擎方向解耦与 60fps 实机吞吐闭环

- **文档编号**：`CINERTX-P11-WO-060`
- **归档路径**：[`docs/phase11/60_CineRTX Phase 11.4-F SuperPower 施工单_物理双引擎方向解耦与60fps实机吞吐闭环.md`](./60_CineRTX%20Phase%2011.4-F%20SuperPower%20施工单_物理双引擎方向解耦与60fps实机吞吐闭环.md)
- **签发主体**：CineRTX 项目总协调负责人（Coordinator）
- **受指派方**：外部高级执行工程师（Senior Implementation Engineer）
- **当前状态**：🟡 **Phase 11.4-F 紧急抢险施工单已签发，提请执行工程师进场施工（ASSIGNED TO IMPLEMENTATION ENGINEER）**
- **执行基线**：Phase 11.4-F 抢险分支（Windows MSVC 19.51 / Ninja / RTX 5060 Laptop GPU / Driver `621.24 WHQL`）
- **输入与法定依据**：
  * [`docs/phase11/59_CineRTX Phase 11.4 RIFE双Profile性能崩塌法医终审裁决书暨Phase 11.4-F抢险与Phase 12准入方案.md`](./59_CineRTX%20Phase%2011.4%20RIFE双Profile性能崩塌法医终审裁决书暨Phase%2011.4-F抢险与Phase%2012准入方案.md)（`CINERTX-P11-FORENSIC-RULING-059`）
  * [`docs/phase11/58_CineRTX Phase 11.4 RIFE双Profile重构性能崩塌与转码速率低于1fps法医排查专家咨询委托书.md`](./58_CineRTX%20Phase%2011.4%20RIFE双Profile重构性能崩塌与转码速率低于1fps法医排查专家咨询委托书.md)（`CINERTX-P11-DELEGATION-058`）
  * [`DECISIONS.md`](../../DECISIONS.md)（`[DEC-008]`, `[DEC-011]`, `[DEC-012]`）
  * [`ISSUES.md`](../../ISSUES.md)（`[ISSUE-009]`）

---

## 一、 施工背景与战略抢险定位

在 Phase 11.4 封板前夕，为兼容 4K 竖屏（`2160x3840`），前序改动在单一 TensorRT Engine 中强塞横竖屏双正交 Optimization Profile，导致构建期工作区显存撞击 7.55 GB 触顶（抛出 `Error Code 2: OutOfMemory` 告警），TensorRT 10.13 Autotuner 对 Tactic 进行毁灭性剪枝，使得单帧推理耗时飙升至 $> 1000\text{ ms}$，横竖屏转码速率均跌破 **1 fps**，构成 P0 级严重灾难。

首席领域专家组在 [59 号法医终审裁决书](./59_CineRTX%20Phase%2011.4%20RIFE双Profile性能崩塌法医终审裁决书暨Phase%2011.4-F抢险与Phase%2012准入方案.md) 中正式拍板采纳**「方案 A：物理双引擎方向解耦架构 + 编译期单一静态 Profile 锁死」**，并批准设立 `Phase 11.4-F` 紧急抢险轨。

根据 `[DEC-008]` 铁律，协调负责人坚守门禁审计与物证验收职责，严禁代写业务代码。本施工单将专家裁决完整拆解为四大正交执行任务包，交由外部高级执行工程师集中攻关，**预计工期 0.5 工作日（约 3.5 小时）**。

---

## 二、 任务拆解与物理文件隔离矩阵

为确保施工互不踩踏、职责清晰，本次抢险任务划分为 4 个高度聚焦的执行包：

| 任务包编号 | 任务包名称 | 核心改造目标 | 物理隔离文件清单（仓库相对路径） | 预计工期 |
| :---: | :--- | :--- | :--- | :---: |
| **PKG-F1** | **脏引擎隔离与物证固化** | 记录污染引擎 SHA-256 哈希，物理隔离当前 42.3MB 脏工件，防范污染。 | `.vs/build/cmake/windows-msvc-ninja/models/`<br>`.vs/build/external/models/` | 10 min |
| **PKG-F2** | **构建端物理双引擎解耦** | 拔除双 Profile 逻辑，恢复单一 Profile；实现横竖屏串行独立构建与零 OOM 约束。 | [`src/shim/ngx_bridge/rife_builder.cpp`](../../src/shim/ngx_bridge/rife_builder.cpp)<br>[`CMakeLists.txt`](../../CMakeLists.txt) | 60 min |
| **PKG-F3** | **推理端方向路由与模数修复** | 建立姿态路由加载机制；拔除动态切换逻辑；修复 `align_mod_`（128u）模数断裂；规范专属流。 | [`src/shim/ngx_bridge/rife_engine.cpp`](../../src/shim/ngx_bridge/rife_engine.cpp) | 60 min |
| **PKG-F4** | **GPU 60fps 吞吐硬门禁与实测** | 补齐单测真实 GPU 吞吐计时；执行横竖屏 4K 全链路压测，达成 60+ fps 满血达标。 | [`tests/test_rife_scale05.cpp`](../../tests/test_rife_scale05.cpp) | 80 min |

---

## 三、 细化施工规范与代码级重构细则

### 3.1 PKG-F1：脏工件物证封存与隔离规范

1. **计算并记录 SHA-256**：
   在物理清理前，计算并记录 `.vs/build/cmake/windows-msvc-ninja/models/rife_v4.25.engine`（`42,294,308` 字节）的 SHA-256 哈希值，记录在施工汇报中；
2. **物理清除脏文件**：
   从构建目录与部署目录删除 `rife_v4.25.engine`，确保干净的构建环境，防止旧的污染引擎被隐式加载。

---

### 3.2 PKG-F2：构建端物理双引擎解耦细则（`rife_builder.cpp`）

1. **彻底拔除双 Profile 代码**：
   删除 `rife_builder.cpp` 中新增的 `add_profile` 双次调用与双 Profile 逻辑，**坚决恢复单一 `IOptimizationProfile`**；
2. **方向分流与静态 Profile 参数配置**：
   通过入参 `width` 与 `height`（或根据 CLI 传入的显式参数）判定生成方向：
   - **横屏专用引擎（`width >= height`，或默认 `width == 0 && height == 0`）**：
     * Profile 0：
       * `min = [1, c, 128, 128]`
       * `opt = [1, c, 1152, 1920]`
       * `max = [1, c, 2176, 3840]`（注意：Dims4 顺序为 `[N, C, H, W]`，高为 2176，宽为 3840）
     * 默认命名目标：**`rife_v4.25_landscape.engine`**
   - **竖屏专用引擎（`height > width`）**：
     * Profile 0：
       * `min = [1, c, 128, 128]`
       * `opt = [1, c, 1920, 1152]`
       * `max = [1, c, 3840, 2176]`（高为 3840，宽为 2176）
     * 默认命名目标：**`rife_v4.25_portrait.engine`**
3. **工作区显存防爆与零 OOM 保证**：
   - 保持 `kWORKSPACE = 1 GiB`（`1ULL << 30`），在单一 Profile 下，1GB 显存预算足以容纳 Tensor Core FP16 Winograd 与全部融合算子，**单次构建峰值显存必须受控在 $\le 2.6\text{ GB}$**；
   - 严禁在构建日志中出现任何 `virtualMemoryBuffer.cpp...resizePhysical::154` OOM 告警。
4. **CLI 支持与串行构建契约**：
   确保 `CineRTX.exe --build-rife-engine --onnx <model.onnx> --engine <target.engine> [--width W] [--height H]` 能够分别被调用两次，先后独立生成横屏与竖屏引擎。

---

### 3.3 PKG-F3：推理端方向路由与模数修复细则（`rife_engine.cpp`）

1. **方向感知与引擎自动寻址分流**：
   在 `RifeEngine::loadEngineEx()` 中：
   ```cpp
   const bool is_portrait = (height > width && width > 0);
   const std::string preferred_name = is_portrait ? "rife_v4.25_portrait.engine" : "rife_v4.25_landscape.engine";
   ```
   - 自动在模型搜索路径中检索 `preferred_name`；
   - **向下兼容回退逻辑**：若检索横屏引擎且未发现 `rife_v4.25_landscape.engine`，但发现了历史 `rife_v4.25.engine`（且其文件大小在 `20 MB ~ 32 MB` 区间，非 `> 35 MB` 污染产物），则允许向下兼容加载；若检测到 `> 35 MB` 则拒绝加载并触发重新构建。
2. **彻底拔除动态 Profile 切换逻辑**：
   - 删除 `setOptimizationProfileAsync` 调用；
   - 校验引擎 `impl_->engine->getNbOptimizationProfiles() == 1`；
   - 默认激活 Profile 0，终身不变；
3. **修复对齐模数断裂漏洞**：
   - 物理删除 `rife_engine.cpp:526` 写死的 `constexpr uint32_t kAlign = 64;`；
   - 统一收口至成员变量 `align_mod_`（默认 `128u`）：
     ```cpp
     const uint32_t align = (align_mod_ > 0) ? align_mod_ : 128u;
     const uint32_t aligned_w = ((width + align - 1u) / align) * align;
     const uint32_t aligned_h = ((height + align - 1u) / align) * align;
     ```
   - 确保 `scale = 0.5f` 下，所有输入下采样后均严格保持 $64$ 字节对齐。
4. **规范 CUDA 流透传**：
   - 确保 `run_alpha_probe()` 仅在初始化加载期执行一次；
   - `interpolate()` 热循环中所有 `enqueueV3`、核函数启动与异步拷贝必须透传管线专属 `cudaStream_t`，严禁传 `nullptr` 默认流。

---

### 3.4 PKG-F4：GPU 60fps 吞吐硬门禁与实测压测细则（`test_rife_scale05.cpp`）

1. **补齐真实 GPU 推理吞吐硬门禁单测**：
   在 `tests/test_rife_scale05.cpp` 中追加 `RTX_TEST(rife_trt_throughput_dual_engine_gate)`：
   - **横屏门禁**：
     * 加载 `rife_v4.25_landscape.engine`；
     * 输入尺寸：`3840x2160`，`scale = 0.5f`，`align_mod = 128`；
     * 预热 5 帧后连续执行 60 帧真实 `interpolate()`；
     * **硬断言**：平均单帧耗时 $\le 16.67\text{ ms}$（吞吐 $\ge 60\text{ fps}$）；
     * **硬断言**：引擎文件体积 $< 32\text{ MB}$。
   - **竖屏门禁**：
     * 加载 `rife_v4.25_portrait.engine`；
     * 输入尺寸：`2160x3840`，`scale = 0.5f`，`align_mod = 128`；
     * 预热 5 帧后连续执行 60 帧真实 `interpolate()`；
     * **硬断言**：平均单帧耗时 $\le 16.67\text{ ms}$（吞吐 $\ge 60\text{ fps}$）；
     * **硬断言**：输出边缘 `reflect101` 零拉伸黑边；
     * **硬断言**：TRT 峰值显存 $< 3,200\text{ MiB}$。
2. **端到端实机压测（CineRTX.exe）**：
   在 RTX 5060 Laptop GPU 上执行实际转码压测：
   - 场景 A：横屏 4K（`3840x2160`）+ RIFE + AV1 编码，实测全链路速率 $\ge 60\text{ fps}$，`D2H == 0 B`；
   - 场景 B：竖屏 4K（`2160x3840`）+ RIFE + AV1 编码，实测全链路速率 $\ge 60\text{ fps}$，`D2H == 0 B`，显存受控在 5,986 MiB 以下。

---

## 四、 协调负责人（Coordinator）验收硬指标（Gates）

执行工程师交付后，协调负责人将逐一穿透核验以下 5 大验收门禁，任何一项未达标直接熔断返工：

| 门禁编号 | 门禁名称 | 穿透检验方式与合格准则 | 一票否决红线 |
| :---: | :--- | :--- | :--- |
| **GATE-F1** | **引擎物理瘦身门禁** | `Get-Item rife_v4.25_landscape.engine, rife_v4.25_portrait.engine`<br>两份独立引擎体积均在 **`20 MB ~ 30 MB`** 区间（单引擎约 25.3 MB）。 | 单引擎体积 $> 32\text{ MB}$ 直接否决。 |
| **GATE-F2** | **构建期零 OOM 门禁** | 审查串行构建日志，单次构建显存峰值 $\le 2.6\text{ GB}$。 | 出现任何 `OutOfMemory (7549747200 bytes)` 直接否决。 |
| **GATE-F3** | **单测 60fps 硬门禁** | `rtx_tests.exe` 执行全量单测（146+ 项），新吞吐门禁单测全绿。 | 单帧推理耗时 $> 16.67\text{ ms}$ 直接否决。 |
| **GATE-F4** | **端到端 60fps 实机门禁** | 实机执行 4K 横竖屏硬件转码，控制台日志打印速率 $\ge 60\text{ fps}$。 | 转码速率 $< 60\text{ fps}$ 直接否决。 |
| **GATE-F5** | **四门禁平账核验** | `AuditTypes`（0 孤立类型）、`AuditSloc`、`AuditSpdx`（100% 合规）、`AuditImports`（零专有符号泄漏）。 | 破坏任何一项既有门禁直接否决。 |

---

## 五、 执行工程师接单与实施指令

请执行工程师严格遵循本施工单规范进场施工，严守职责边界，不得修改与本案无关的业务逻辑。

施工完成后，请按照标准模板提交《Phase 11.4-F 施工汇报》，附带构建日志、引擎大小、单测结果与端到端实测数据，提请协调负责人穿透复验！

---
*(正文完)*
