# CineRTX 工程施工规范（第三阶段）：全仓文档漂移清零、历史阶段单文档提炼与 CI 契约闭环

**文档标识**：`DOC-SUPERPOWER-STAGE3-DOC-DRIFT-AND-CI-CLOSURE-003`  
**演进阶段**：Stage 3 / 共 3 阶段（收官合拢：清零 58+12 项文档/实现漂移、**提炼归并 Phase 1–7 历史阶段文档为单一摘要**、拔除死文件与重复文档、修复必败 CI）  
**遵循方法论**：`SuperPower` 工程施工范式（`Plan` + `Tasks` + `Checklists` 三位一体）  
**关联基准文档**：
- 工程师初筛报告：`docs/phase8/CineRTX_文档实现漂移审计报告_20261113.md`（检出 `A1–A35`、`B1–B25` 共 58 项）
- 专家终审补遗：检出工程师漏检的 `C1–C12` 共 12 项核心漂移与冗余文件 + 历史文档上下文瘦身战略  
**适用团队**：执行工程师 A（CI 工作流、打包脚本与构建死模块清理组）、执行工程师 B（根级 `README`、`docs/spec/` 权威规范重写与 Phase 1–7 历史文档单文件提炼归并组）

---

## 一、 Plan（顶层规划与架构不变量契约）

### 1.1 本阶段核心目标
将现有工程师初筛报告（`docs/phase8/CineRTX_文档实现漂移审计报告_20261113.md`）查出的 58 项历史漂移、专家二次排查补充的 **12 项漏检问题（C1–C12）** 以及 **历史旧文档膨胀导致的上下文污染问题** 一次性彻底解决：
1. **修复 100% 必败的 GitHub Actions CI 工作流（`.github/workflows/ci.yml`）**：拔除调用已删除参数 `--self-test`、`--simulate 2 --width 64 --height 64` 与断言 `cpu-reference` 的三处无效 smoke 步骤，对齐真实 CLI 契约（`--probe --json` 与 `--schema-json`）。
2. **拔除全仓无意义冗余文件、缓存垃圾与死模块**：物理删除 100% 重复的 `docs/phase8/report.md`、死模块 `cmake/RtxNvidiaSdks.cmake`、入库的字节码缓存 `webui/test/__pycache__/`，并将错放在 `src/shim/ngx_bridge/` 下的 D3D12 视口着色器 `sdr_blit_cs.h` / `sdr_blit_rs.h` 归位至 `src/gui/shaders/`。
3. **重写权威技术规范（`README.md`、`README_zh.md`、`docs/README.md`、`docs/spec/*`、`docs/TODO/*`）**：清除 `--truehdr`、`--backend`、`--bench`、`--trace`、`CpuReferenceBackend`、`SurfacePool`、`BackpressureController`、`TraceRecorder` 等幽灵契约，使全仓文档与 Stage 1/2 完工后的真实代码 100% 严丝合缝。
4. **历史阶段文档（`docs/phase1` ～ `docs/phase7`）单一摘要提炼与物理瘦身（节省 >85% 上下文）**：将每个旧阶段目录下繁杂的委托书、过程返工单、阶段初稿与过时设计文档，按阶段提炼浓缩为**每个旧阶段唯一一份 `<PhaseN>_Summary_Digest.md`**（或统一归入 `docs/archive/Phase1_to_Phase7_Evolution_Digest.md`），物理移除已被推翻的过程废稿，彻底杜绝后续工程师或 AI Agent 误读旧阶段废弃契约！

### 1.2 多工程师并行防撞文件矩阵（File Ownership Matrix）

| 工程师泳道 | 独占修改 / 删除文件清单 | 职责边界 |
| :--- | :--- | :--- |
| **工程师 A（CI、工具脚本与源码树卫生泳道）** | - 修改 `.github/workflows/ci.yml`<br>- 修改 `tools/package_release.py`<br>- 修改 `tests/test_main.cpp` & `src/shim/loader/nv_runtime.cpp`<br>- 移动 `src/shim/ngx_bridge/sdr_blit_*.h` $\to$ `src/gui/shaders/`（同步更新 `src/gui/preview_viewport.cpp`）<br>- 删除 `cmake/RtxNvidiaSdks.cmake`、`webui/test/__pycache__/`、`docs/phase8/report.md` | 负责全部脚本、CI、文件迁移与垃圾清除 |
| **工程师 B（根文档、`docs/spec/` 重写与历史阶段文档提炼归并泳道）** | - 修改 `README.md` & `README_zh.md`<br>- 修改 `docs/README.md`<br>- 修改 `docs/spec/architecture.md`、`docs/spec/abi.md`、`docs/spec/testing.md`、`docs/spec/compliance.md`、`docs/spec/gate1.md`、`docs/spec/gate3.md`<br>- 修改 `docs/TODO/README.md` 及各分册状态<br>- **提炼并收敛 `docs/phase1/` ～ `docs/phase7/` 为单一阶段摘要文档** | 负责全部 Markdown 文档的重写、历史阶段浓缩提炼与索引闭环 |

---

## 二、 Tasks（原子级 TDD 施工任务集）

### Track A（工程师 A）：CI 修复、打包脚本修正与冗余死文件物理拔除

#### Task 3A-1：修复 `.github/workflows/ci.yml` 三处必败的 Smoke 测试（对应漂移项 A30）
- **目标位置**：`.github/workflows/ci.yml`（第 81–88、128–134、177–183 行）
- **缺陷根因**：CI 在 `windows-msvc`、`windows-zig`、`linux-gcc` 三个 Job 中调用了早已删除的 `--self-test` 和 `--simulate 2 --width 64 --height 64` 并检查 `"cpu-reference"`，导致每次运行必然触发 `src/cli/main.cpp` 的 `unknown argument` 退出码 `2` 失败！
- **施工动作**：将三处 Smoke 步骤统一替换为验证真实契约：
  1. `CineRTX --version` 与 `CineRTX --help`（断言退出码 `0`）；
  2. `CineRTX --schema-json`（断言输出合法 JSON 且包含 `"codec"`、`"i18n"`）；
  3. `CineRTX --probe --json`（在无 NVIDIA 驱动的 CI 容器中允许退出码 `0`、`4` 或 `5`，断言输出合法 JSON 且包含 `"abi_version": 2`）。

#### Task 3A-2：清除 `tools/package_release.py` 中的幽灵参数 `--truehdr`（对应漏检项 C5）
- **目标位置**：`tools/package_release.py`（第 9、99、118–124 行）
- **施工动作**：
  1. 将第 118–124 行生成的 `README.txt` 内容修正为当前真实 CLI 用法（删除 `--truehdr`，补充 `--rife-engine` 与 `python webui/server.py` 启动说明）：
     ```python
     "  3. 缺省 4K 60fps HDR10 超分重制转码 (默认 AV1 10-bit + NVOFA 光流 + VSR 4 + TrueHDR 1000nits):\n"
     "     CineRTX.exe --input in.mp4 --output out.mp4\n\n"
     "  4. TensorRT RIFE 深度学习母版补帧转码:\n"
     "     CineRTX.exe --input in.mp4 --output out_rife.mp4 --rife-engine models/rife_v4.25.engine\n\n"
     ```
  2. 将第 9、99 行关于发布包体积的描述统一为 `~34 MB`（与根目录 `README.md` 保持一致）。

#### Task 3A-3：物理删除冗余死文件与归位着色器头文件（对应漏检项 C9、C11 及 A33/A34/C8）
- **目标位置**：
  1. **物理删除**：
     - `cmake/RtxNvidiaSdks.cmake`（未被任何 `CMakeLists.txt` 引用的 16.2 KB 死模块，含 `trt_dynamic.cpp` 与 `NvOFFRUC` 残留）
     - `docs/phase8/report.md`（与阶段一验收报告重复）
     - `webui/test/__pycache__/`（Python `.pyc` 字节码缓存目录）
  2. **模块归位**：
     - 将 `src/shim/ngx_bridge/sdr_blit_cs.h` 与 `src/shim/ngx_bridge/sdr_blit_rs.h` 移动至 `src/gui/shaders/sdr_blit_cs.h` 与 `src/gui/shaders/sdr_blit_rs.h`；
     - 在 `src/gui/preview_viewport.cpp`（第 15–16 行）中将 `#include "../shim/ngx_bridge/sdr_blit_cs.h"` 更新为 `#include "shaders/sdr_blit_cs.h"` 与 `#include "shaders/sdr_blit_rs.h"`。
  3. **清理代码侧陈旧注释**：
     - `src/shim/loader/nv_runtime.cpp`（第 4–6 行）：删除 `"the engine degrades to the CPU reference path when a component is absent"`；
     - `tests/test_main.cpp`（第 56 行）：将注释示例中的 `--filter=fruc_abi_*` 更新为真实存在的 `--filter=rife_*`。

---

### Track B（工程师 B）：根级 `README`、`docs/spec/` 权威规范重写与旧阶段文档单文件提炼

#### Task 3B-1：同步更新根目录 `README.md` 与 `README_zh.md`（对应 A1、A2、A32、A35）
- **目标位置**：`README.md` 与 `README_zh.md`
- **施工动作**：
  1. **删除第 43 行的 `--truehdr` 示例**：改为展示默认开箱即用 4K 60fps HDR10 重制命令（`./CineRTX.exe --input in.mp4 --output out_4k60_hdr10.mp4`）、自定义峰值亮度（`--peak-nits 1500`）以及 RIFE 深度学习补帧（`--rife-engine models/rife_v4.25.engine`）。
  2. **新增第 3 种前端模式（WebUI）与 DebugView 监看指南**：补充 `python webui/server.py --port 8787` 浏览器控制台说明，以及使用 Sysinternals DebugView 过滤 `[CineRTX]` 实时监看运行态日志的说明。
  3. **更新系统拓扑图与测试计数**：在 Architecture Overview 拓扑图中加入 `NVDEC -> Dual-Path FRUC (NVOFA / TensorRT RIFE) -> NGX VSR + TrueHDR` 节点；将第 71 行的 `(79 tests)` 更新为实际 `rtx_tests.exe` 单元测试数量（`63 tests`）。

#### Task 3B-2：重写 `docs/spec/` 六大规范文档，彻底拔除幽灵子系统与幽灵 CLI 契约（对应 A3–A29）
- **目标位置**：`docs/spec/architecture.md`、`docs/spec/abi.md`、`docs/spec/testing.md`、`docs/spec/compliance.md`、`docs/spec/gate1.md`、`docs/spec/gate3.md`
- **施工动作**：
  1. **`docs/spec/architecture.md`**：
     - 删除“RIFE/TensorRT 已手术切除”的过时表述，改为描述当前 `rtx_ngx_bridge.dll` 内聚的双路补帧（路线 A：NVOFA 硬件光流；路线 B：TensorRT 10.13.3.9 RIFE v4.25）+ VSR + TrueHDR 架构；
     - **整节删除 §4–§7 中早已不存在的 4 个幽灵子系统**：`CpuReferenceBackend`、`defaultPoolSpecs()` / `SurfacePool`、`BackpressureController` (75/25/90)、`PipelineScheduler`、`TraceRecorder`、`ResolutionGuard`，替换为当前 `NvidiaTranscodeSession` + 3-Slot P010 Ring + `TranscodeProfile` 三端统一 SSOT + `rtx::logWrite` DebugView 日志架构说明。
  2. **`docs/spec/abi.md`**：
     - 删除不存在的 `include/shim/i_engine_backend.h`、`offruc_bridge.cpp`、`runNvidiaContainerTranscode` / `runNvidiaRawTranscode`；
     - 将 `RTX_ERR_TENSORRT_VERSION (=11)` 更正为 `RTX_ERR_RESERVED_11 = 11`；
     - 将 `RTX_NGX_BRIDGE_ABI_VERSION` 从 `2u` 更新为 **`4u`**，并完整列出 ABI v3 与 ABI v4 导出表。
  3. **`docs/spec/testing.md`、`docs/spec/compliance.md`、`docs/spec/gate1.md`、`docs/spec/gate3.md`**：
     - 删除全部幽灵 CLI 参数（`--backend nvidia`、`--input-raw`、`--output-raw`、`--bench`、`--viewport-self-test`、`--trace`）；
     - 将 `testing.md` 测试文件覆盖表按 `tests/` 目录下实存的 12 个测试文件重写，统一 CTest 条目数为 `1`（`rtx_unit_tests`）。

#### Task 3B-3：将 `docs/phase1/` ～ `docs/phase7/` 繁杂历史文档按阶段提炼为单一摘要文档（上下文大瘦身）
- **目标位置**：`docs/phase1/`、`docs/phase2/`、`docs/phase3/`、`docs/phase4/`、`docs/phase5/`、`docs/phase6/`、`docs/phase7/`、`docs/handover/`
- **设计背景**：当前 `docs/phase1` 至 `docs/phase7` 散落了超过 50 份早期过程草稿、返工委托书、阶段性验收单与已被推翻的旧架构方案（如 `CpuReferenceBackend`、`rtx_fruc_bridge.dll`、`multi_gpu_scheduler`、`--sdr` 开关等），不仅占用超过 80% 的文档体积，而且极易污染后续工程师与 AI 代理的上下文窗口。
- **施工动作**：
  1. **每个旧阶段提炼为唯一一份精华摘要（`<PhaseN>_Summary_Digest.md`，每份控制在 80～120 行以内）**：
     - 每个阶段的摘要仅保留四项核心工程资产：
       - **① 阶段核心目标与里程碑结论**；
       - **② 至今仍存活生效的架构遗产与代码模块**（例如 Phase 5 的静态 FFmpeg 零拷贝封包、Phase 6 的単 EXE + `rtx_ngx_bridge.dll` 插件隔离、Phase 7 的 `Flicks` 705.6MHz 时钟与 3D LUT / D3D12 实时预览视口）；
       - **③ 已被后续阶段正式废除/替代的设计清单（防回潮黑名单）**（明确标注哪些类、开关和 DLL 已被拔除，严禁恢复）；
       - **④ 关键排障经验与硬件踩坑记录**（如 Blackwell `sm_120` PTX JIT、`/MT` 静态 CRT 链接、NVOFA stride 步幅对齐等）。
  2. **物理清理旧阶段过程中的冗余草稿**：在每个 `docs/phase1` ～ `docs/phase7` 目录生成对应的 `PhaseN_Summary_Digest.md` 后，删除该目录下其余重复的过程施工单、返工单与旧版委托书（仅保留每阶段唯一的 `PhaseN_Summary_Digest.md`），并在 `docs/README.md` 中将历史演进索引指向这 7 份精华摘要文档。
  3. **更新 `docs/TODO/` 状态**：将 `docs/TODO/` 下已完工分册全部更新为 `✅ 已闭环`，并清理 `docs/TODO/README.md` 中的悬空死链。

---

## 三、 Checklists（第三阶段量化验收门禁清单）

- [ ] **门禁 3.1（幽灵 CLI 参数全仓清零扫描）**：
  - 执行命令：`rg -n "(--truehdr|--self-test|--simulate|--input-raw|--output-raw|--viewport-self-test|--trace\b)" README.md README_zh.md .github/ tools/ docs/spec/`
  - 通过标准：输出严格为 **0 命中**。
- [ ] **门禁 3.2（已删幽灵子系统全规范清零扫描）**：
  - 执行命令：`rg -n "(CpuReferenceBackend|SurfacePool|BackpressureController|TraceRecorder|i_engine_backend|offruc_bridge|trt_dynamic)" docs/spec/ cmake/`
  - 通过标准：输出严格为 **0 命中**。
- [ ] **门禁 3.3（冗余文件、编译垃圾与旧阶段过程草稿收敛验证）**：
  - 执行命令：检查 `cmake/RtxNvidiaSdks.cmake`、`docs/phase8/report.md`、`webui/test/__pycache__`、`src/shim/ngx_bridge/sdr_blit_cs.h`、`src/shim/ngx_bridge/sdr_blit_rs.h` 是否已移除，以及 `docs/phase1` ～ `docs/phase7` 是否已收敛为单一 `PhaseN_Summary_Digest.md`
  - 通过标准：5 项冗余/错位文件在原路径全部为 `False`；`docs/phase1` ～ `docs/phase7` 每目录仅保留 1 份精华摘要文档，全仓 Markdown 总行数下降 $\ge 75\%$。
- [ ] **门禁 3.4（CI Smoke 命令本地实机复跑）**：
  - 执行命令：依次执行 `.github/workflows/ci.yml` 中的 `CineRTX.exe --version`、`CineRTX.exe --schema-json`、`CineRTX.exe --probe --json` 与 `ctest --test-dir build/cmake/windows-msvc-ninja --output-on-failure`
  - 通过标准：100% 零报错通过。
- [ ] **门禁 3.5（SPDX 头、单文件行数与全仓脱敏终审门禁）**：
  - 执行命令：`pwsh tools/check_spdx.ps1` 与 `pwsh tools/audit_imports.ps1`
  - 通过标准：两个脚本全部输出 **`PASS`**（全仓单文件 $\le 1,200$ 行、0 隐私路径泄露、0 外部媒体/CRT/网络 DLL 污染）。
