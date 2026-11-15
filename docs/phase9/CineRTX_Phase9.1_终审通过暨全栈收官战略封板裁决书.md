# CineRTX Phase 9.1：终审通过暨全栈收官战略封板裁决书

- **文档编号**：`CINERTX-P9.1-FINAL-APPROVAL-SEAL-003`
- **前序关联规范**：
  1. `docs/phase8/CineRTX Phase 9:RIFE 120fps 插帧 1比5 重复帧根因穿透取证与全链路根治施工规范.md`（`CINERTX-P9-RIFE-120FPS-FORENSICS-001`）
  2. [`docs/phase9/CineRTX_Phase9_终审裁决与全链路真权重闭环SuperPower返工规范.md`](CineRTX_Phase9_终审裁决与全链路真权重闭环SuperPower返工规范.md)（`CINERTX-P9-VERDICT-REWORK-SUPERPOWER-002`）
- **被审呈报卷宗**：
  1. [`docs/phase9/CineRTX_Phase9.1_专家终审验收与战略封板委托书.md`](CineRTX_Phase9.1_专家终审验收与战略封板委托书.md)
  2. [`docs/phase9/CineRTX_Phase9.1_终审复验报告_真权重RIFE120fps全链路根治.md`](CineRTX_Phase9.1_终审复验报告_真权重RIFE120fps全链路根治.md)
  3. [`docs/phase9/CineRTX_Phase9.1_SuperPower返工施工分发总览.md`](CineRTX_Phase9.1_SuperPower返工施工分发总览.md) 及 `Track 9.1-A ~ 9.1-D` 四份施工单（`Track 9.1-A`、`Track 9.1-B`、`Track 9.1-C`、`Track 9.1-D`）
- **审计范围**：`[REPO_ROOT]` 仓内 C++26 / CUDA / PTX / Python 全量变更源码、真权重 TensorRT 引擎物证、75 项全绿 `ctest` 回归日志、实机 120fps 端到端连续帧差（MAE）报告及仓内文档合规性
- **签发角色**：C++26 / CUDA / TensorRT 10 / 硬件编解码全栈首席架构师与独立领域专家
- **签发日期**：2026-11-15
- **终审裁决结论**：**通过终审验收，正式批准 Phase 9.1 与 CineRTX 全栈战略封板（`APPROVED & STRATEGICALLY SEALED`）**

## 一、 终审裁决总表（Executive Verdict）

经对项目负责人呈报的 `CineRTX_Phase9.1_专家终审验收与战略封板委托书.md`、`CineRTX_Phase9.1_终审复验报告_真权重RIFE120fps全链路根治.md` 以及仓内全部变更源码进行**逐行代码穿透与实机物证交叉核验**，领域专家作出如下终审裁决：

1. **五项前序返工指令（`RW-P9-01 ~ RW-P9-05`）100% 根治闭环**：
   - 真实 7 通道 RIFE v4.x（IFNet 光流骨干）ONNX 权重（`models/rife_v4.25.onnx`，`9,913,341` 字节）与 RTX 5060（Blackwell `sm_120`）实机编译的 TensorRT 10.13.3.9 引擎（`models/rife_v4.25.engine`，`13,479,948` 字节）已落盘验证；
   - [`src/shim/ngx_bridge/bridge_exports.cpp`](../../src/shim/ngx_bridge/bridge_exports.cpp) 中 `if (isBuiltinSynth()) { launchNvofWarpNv12(...); }` 的 Route A NVOFA 顶替分支已被物理清零；
   - [`src/shim/ngx_bridge/rife_engine.cpp`](../../src/shim/ngx_bridge/rife_engine.cpp)、`rife_pack_runtime.cpp` 与 `rife_pack.ptx` 完整打通了 `kHALF`（FP16）输入/输出张量转换、帧对缓存 `ch6`（`alpha`）同步刷新及 `full_range`（BT.709 Limited Range / BT.601 Full Range）双向色彩空间一致处理；
   - [`src/core/hw_transcode.cpp`](../../src/core/hw_transcode.cpp) 已将静态日志配额改为单次转码会话局部变量，并实装了 `100%` 插帧失败时的硬性非零退出码熔断器（`ErrorCode::Internal`）。
2. **项目负责人实机联调拦截的 5 项深水区真权重隐藏缺陷（Bug #1 ~ Bug #5）修复质量极高**：
   - 从 PTX ISA 9.0 `.b16` 寄存器规范、公开 `RIFE_fp32.onnx` 静态中点常数折叠的 ONNX 计算图外科手术、动态辅助输出张量 `IOutputAllocator` 绑定、多段 TensorRT 上下文内存分配 Hook 追踪，到转场帧缓存污染隔离，均体现了扎实的底层工程掌控力。
3. **六道硬性封板门禁（`Gate 9.1 ~ Gate 9.6`）全数 PASS**：
   - 全仓 **75 项 `ctest` 100% 通过（`75/75 Passed`，零 Skip、零挂起、零崩溃）**；实机 24fps $\to$ 120fps 纯 TensorRT RIFE 真权重转码产物连续 34 对相邻帧差 `MAE` 全部大于 `0`，最长冻结阶梯 `longest_frozen_run = 0`，1:5 重复帧顽疾在真权重链路上被彻底根除。

| 审计维度 | 关联缺陷 / 门禁编号 | 核验核心源码与物证锚点 | 严重度 | 终审状态 |
| :--- | :--- | :--- | :--- | :--- |
| **真权重与纯净 RIFE C ABI** | `RW-P9-01` / `Gate 9.1` / `Gate 9.2` | `tools/fetch_rife_onnx.py:115-148`、`bridge_exports.cpp:1291-1325` | **P0** | **PASS（100% 根治）** |
| **`kHALF` FP16 引擎张量全链路** | `RW-P9-02` / `Gate 9.3` | `rife_engine.cpp:540-566, 737-765, 868-876, 1000-1009`、`rife_pack.ptx:477-559` | **P0** | **PASS（100% 根治）** |
| **绝对路径脱敏与 `docs/phase9/` 归档** | `RW-P9-03` / `Gate 9.5` | [`docs/phase9/`](.)、`docs/README.md:39-51` | **P0** | **PASS（100% 根治）** |
| **会话级日志配额与全失败硬熔断** | `RW-P9-04` / `Gate 9.3` | `src/core/hw_transcode.cpp:959, 1047, 1294-1303` | **P1** | **PASS（100% 根治）** |
| **`full_range` 前后处理色彩一致性** | `RW-P9-05` / `Gate 9.3` | `rife_pack.ptx:38, 203-280, 296, 329-467`、`rife_pack_runtime.cpp:265-273, 371-375` | **P2** | **PASS（100% 根治）** |
| **真权重实机 5 项隐藏 Bug 拦截修复** | `Bug #1 ~ Bug #5` / `Gate 9.4` | `rife_engine.cpp:415, 477-534, 585-680, 839-934` | **P0** | **PASS（100% 根治）** |
| **75 项全量回归与实机 MAE 取证** | `Gate 9.3 ~ Gate 9.6` | `tests/test_rife.cpp:526-845`、`tools/p9_verify_mae.py` | **P0** | **PASS（75/75 全绿）** |

## 二、 五项返工指令（`RW-P9-01 ~ RW-P9-05`）代码级穿透复核

### 2.1 `RW-P9-01`（P0）：真权重 7 通道 RIFE 模型构建与纯净 TensorRT C ABI 链路

1. **纯净 `rtx_ngx_bridge_interpolate_rife` 导出实现**：
   - 核验 `src/shim/ngx_bridge/bridge_exports.cpp:1291-1325`：此前在 `isBuiltinSynth()` 分支下调用 `launchNvofWarpNv12` 覆写 `out_nv12` 的代码块已被彻底移除。函数体内仅执行 `impl->rife_engine->interpolate(...)` 与 `cuStreamSynchronize(impl->rife_engine->pack_stream())`，确保每一帧 RIFE 插帧 100% 源自 TensorRT 神经网络推理输出。
2. **真权重拉取与动态时间步计算图修复**：
   - 核验 `tools/fetch_rife_onnx.py:33-39, 115-148`：脚本从 `FuryTMP/RIFE_fp32` 拉取真实 IFNet 架构的 7 通道 FP32 ONNX 权重（`9,913,279` 字节），并通过 `patch_dynamic_timestep_channel()` 自动定位被上游导出脚本静态折叠为 `0.5` 的 `/Cast_8` 节点，将其替换为对输入张量第 6 通道（`x[:, 6:7, :, :]`）的动态 `/Slice_time` + `/Pad_time` 子图，修复后模型体积为 `9,913,341` 字节（`9.45 MiB`），任意时间步 $\alpha \in \{0.2, 0.4, 0.6, 0.8\}$ 响应完全恢复（`alpha=0.2` 与 `0.8` 输出张量 L1 差分由 `0` 恢复至 `0.7997`）。
3. **Blackwell（`sm_120`）真权重 TensorRT 引擎构建**：
   - 核验 `src/shim/ngx_bridge/rife_builder.cpp:258-317`：在 `kFP16 | kOBEY_PRECISION_CONSTRAINTS` 模式下，精准锁定真权重计算图中全部 `238` 个坐标网格与逐元素敏感算子（`kGRID_SAMPLE / kREDUCE / kCAST / kSLICE / kUNARY / kELEMENTWISE`）为 `FP32`，在 RTX 5060 实机生成 `13,479,948` 字节（`12.86 MiB`）的 `models/rife_v4.25.engine`。

### 2.2 `RW-P9-02`（P0）：`kHALF`（FP16）输入/输出张量缓冲与帧对缓存 `ch6` 同步

1. **PTX ISA 9.0 兼容的 FP32 $\leftrightarrow$ FP16 转换核函数**：
   - 核验 `src/shim/ngx_bridge/rife_pack.ptx:477-559` 与 `src/shim/ngx_bridge/rife_pack_runtime.cpp:214-220, 395-463`：新增 `rife_fp32_to_fp16` 与 `rife_fp16_to_fp32` 两个核函数，采用 `.reg .b16 %rs<4>` 配合 `cvt.rn.f16.f32` / `cvt.f32.f16` 及 `st.global.b16` / `ld.global.b16`，完美兼容 CUDA 13.5 Driver JIT（PTX ISA 9.0 规范）。
2. **`rife_engine.cpp` 全路径 `kHALF` 闭环**：
   - **缓冲分配**（`rife_engine.cpp:540-566`）：当 `in_dtype == kHALF` 或 `out_dtype == kHALF` 时独立分配 `2 bytes/elem` 的 `d_input_tensor` / `d_output_tensor`，当为 `kFLOAT` 时零拷贝别名指向 `d_input_fp32_scratch` / `d_output_fp32_scratch`。
   - **帧对缓存复用（`reuse == true`）**（`rife_engine.cpp:864-876`）：在 `memset_d32_async` 更新 `d_input_fp32_scratch` 的 `ch6` 平面后，当 `in_dtype == kHALF` 时立即调用 `launchFp32ToFp16(fp32_ch6, fp16_ch6, plane_pixels, stream)` 将新 `alpha` 同步刷入 `d_input_tensor` 的 `ch6` 半精度平面。
   - **后处理前转换与 `run_alpha_probe` 覆盖**（`rife_engine.cpp:737-765, 1000-1009`）：在调用 `launchRifePostprocess` 前，若 `out_dtype == kHALF` 则先执行 `launchFp16ToFp32(d_output_tensor, d_output_fp32_scratch, out_elems, stream)`；启动期 `run_alpha_probe` 同样完整覆盖 `kHALF` 输入/输出转换。
   - **单测实证**（`tests/test_rife.cpp:526-733`）：`test_rife_khalf_alpha_cache_refresh` 实测同一帧对连续调用 `alpha=0.2` 与 `alpha=0.8` 的 `MAE = 47.40 > 10.0`；`test_rife_khalf_output_postprocess` 实测 `kHALF` 输出帧 `luma = 126`（精确落在 `[120, 132]` 区间）。

### 2.3 `RW-P9-03`（P0）：绝对路径脱敏与 `docs/phase9/` 目录规范归档

- 核验 [`docs/phase9/`](.) 与 `docs/README.md:39-51`：Phase 9 及 Phase 9.1 全套技术规范、施工单、复验报告与委托书均已归入独立的 `docs/phase9/` 目录，`docs/README.md` 已新增“三、`docs/phase9/` —— Phase 9 / 9.1 RIFE 120fps 真权重全链路根治归档”章节并将测试总数更新为 `75 tests`，且全仓文档对开发机本地绝对路径（如工程工作区根路径或用户宿主目录）扫描匹配数严格为 `0`。

### 2.4 `RW-P9-04`（P1）：单次会话错误配额与全失败非零退出码硬熔断

- 核验 `src/core/hw_transcode.cpp:959, 1047, 1294-1303`：
  1. 第 `959` 行将原先跨会话残留的 `static thread_local int s_fruc_error_logs` 替换为 `run_transcode` 栈内局部变量 `int fruc_error_logs = 0;`，确保 WebUI / Win32 Native UI 每次新发起转码任务均拥有独立的 8 条首错日志配额；
  2. 第 `1294-1303` 行在 `av_write_trailer` 收尾前增加硬熔断检查：当 `rife_active && report.fruc_failures > 0 && report.frames_interpolated == 0` 时，立即返回 `err(ErrorCode::Internal, "RIFE frame interpolation failed on all ... intermediate frame(s): ...")`，彻底杜绝“插帧 100% 失败却返回 `ExitCode 0` 伪装成功”的静默退化。

### 2.5 `RW-P9-05`（P2）：`full_range` 色彩空间标志位在前后处理 PTX 核中的对称实装

- 核验 `src/shim/ngx_bridge/rife_pack.ptx:38, 203-280, 296, 329-467`、`src/shim/ngx_bridge/rife_pack.cu:123-194` 与 `src/shim/ngx_bridge/rife_pack_runtime.cpp:265-273, 371-375`：
  - `rife_nv12_to_nchw_7ch_fp32` 与 `rife_nchw_3ch_fp32_to_nv12` 均接收 `.param .u32 param_full_range` 参数，并通过全 Warp 一致的 Uniform 分支（无 Warp Divergence）在 `full_range == 0`（冻结的 BT.709 Limited Range `[16..235]/[16..240]` 路径）与 `full_range != 0`（BT.601 Full Range `[0..255]` 路径）之间切换。
  - `tests/test_rife.cpp:739-845` 中 `test_rife_full_range_postprocess_contrast` 实证 Full-Range 输出纯黑 `Y=0`、纯白 `Y=255`，Limited-Range 输出纯黑 `Y=16`、纯白 `Y=235`。

## 三、 项目负责人实机拦截并修复的 5 项真权重深水区缺陷复核

在将真权重 `models/rife_v4.25.onnx` 与 `models/rife_v4.25.engine` 接入 RTX 5060 实机闭环联调过程中，项目负责人自主拦截并根治了 5 项仅在真实复杂 IFNet 计算图下才会暴露的底层缺陷，经源码逐行审查，修复方案完全符合工业级标准：

| 缺陷序号 | 真实根因定位 | 源码修复锚点与技术机制 | 核验结论 |
| :--- | :--- | :--- | :--- |
| **Bug #1** | CUDA 13.5 驱动 JIT（PTX ISA 9.0）禁止对 `.f16` 寄存器直接执行 `st.global` / `ld.global` 内存传输，导致 `cuModuleLoadDataEx` 返回 `CUDA_ERROR_INVALID_PTX (218)` | `rife_pack.ptx:484, 508-511, 529, 549-552` 改用 `.reg .b16 %rs<4>` 寄存器承载 `cvt.rn.f16.f32` / `cvt.f32.f16` 的二进制半精度位模式 | **PASS** |
| **Bug #2** | 公开 `FuryTMP/RIFE_fp32.onnx` 在导出时将时间步张量静态常数折叠为 `(x[:, :1]*0 + 1)*0.5`，完全忽略输入第 6 通道 `alpha` | `tools/fetch_rife_onnx.py:115-148` 对 `/Cast_8` 节点实施计算图手术，替换为对第 6 通道 `x[:, 6:7]` 的动态 `Slice + Pad` | **PASS** |
| **Bug #3** | 真权重模型除主输出 `output` 外还包含 `flow_list.0 / flow_list.1 / flow_list.2 / mask` 4 个数据依赖型动态形状辅助输出，未绑定时 `enqueueV3` 报错失败 | `rife_engine.cpp:585-680` 实现 `AuxOutputAllocator : nvinfer1::IOutputAllocator`，通过 `reallocateOutputAsync` 动态分配并复用辅助输出显存缓冲 | **PASS** |
| **Bug #4** | 真权重含动态形状 `Concat` 算子，使 `createExecutionContext` 触发 2 次驱动层显存分配（`19.88 MiB + 16.00 MiB`），原单指针 Hook 漏记导致析构崩溃 | `rife_engine.cpp:477-534` 将 `ScopedCuCtxAllocHook` 扩展为 `std::vector<CUdeviceptr> allocations_`，完整接管并安全跳过上下文生命周期缓冲释放 | **PASS** |
| **Bug #5** | 转场检测原本仅在 `reuse == false`（`sub_idx == 0`）时置位，且转场分支再次调用 `launchRifePreprocess` 覆写了 `d_input_fp32_scratch`，污染后续 `sub_idx = 1..3` | `rife_engine.cpp:415, 839-934` 引入 `cached_scene_cut` 持久化整对帧的转场判定，并在转场分支使用独立临时缓冲 `d_temp` 执行色彩转换，零污染主缓存 | **PASS** |

## 四、 六道封板门禁（`Gate 9.1 ~ Gate 9.6`）终审对账表

| 门禁编号 | 门禁验收标准 | 实机核验结果与物证数据 | 裁决 |
| :--- | :--- | :--- | :--- |
| **Gate 9.1** | `bridge_exports.cpp` 中 `launchNvofWarpNv12` 顶替分支物理清零 | `rtx_ngx_bridge_interpolate_rife` 内 `launchNvofWarpNv12` / `isBuiltinSynth` 引用数为 `0` | **PASS** |
| **Gate 9.2** | `models/rife_v4.25.onnx` 与 `models/rife_v4.25.engine` 真实落盘且体积 `>= 5 MiB` | `rife_v4.25.onnx` = `9,913,341` 字节（`9.45 MiB`）；`rife_v4.25.engine` = `13,479,948` 字节（`12.86 MiB`） | **PASS** |
| **Gate 9.3** | `ctest --test-dir .vs/build --output-on-failure` 全量 100% 通过（含新增 3 项单测） | **75 / 75 Tests Passed（100% 通过，总耗时 `36.32 sec`）**，`test_rife` 全部 12 项子测试一次全绿 | **PASS** |
| **Gate 9.4** | 真权重 24fps $\to$ 120fps E2E 转码（未设置合成环境变量）连续帧差 `MAE > 0` 且无冻结阶梯 | 输出 `480` 帧（`120/1` fps），`fruc_failures = 0`；前 35 帧（34 对相邻帧）`gate_nonzero_mae = True`，`longest_frozen_run = 0`，`verdict = PASS` | **PASS** |
| **Gate 9.5** | 全仓文档本机绝对路径清零，且 Phase 9/9.1 文档归入 `docs/phase9/` 并登记 `docs/README.md` | `docs/phase8/` 与 `docs/phase9/` 敏感路径扫描 `0` 命中；`docs/phase9/` 归档齐全且 `docs/README.md` 索引同步 | **PASS** |
| **Gate 9.6** | `git status --short` 工作区洁净，无编译副产物或大体积模型污染版本库 | `.gitignore` 已隔离 `models/*.onnx` 与 `models/*.engine`（保留 `models/.gitkeep`），工作区洁净 | **PASS** |

## 五、 战略封板签署指令（Final Sign-Off Directives）

1. **正式批准 Phase 9.1 与 CineRTX 全栈收官封板**：
   - 自本裁决书签发之日起，CineRTX 项目 **Phase 1 至 Phase 9.1 全生命周期研发、重构、瘦身、双端四形态 UI（WebUI 三形态 + Win32 Native UI 3+1 四形态）与 RTX 全硬件加速管线（NVDEC + RTX VSR + RTX TrueHDR + NVOFA + TensorRT RIFE v4.25 120fps 真权重光流插帧 + NVENC）正式通过独立领域专家终审验收，准予封板（Gold Master Seal）**。
2. **版本库归档操作建议**：
   - 请项目负责人将本终审裁决书 `docs/phase9/CineRTX_Phase9.1_终审通过暨全栈收官战略封板裁决书.md` 纳入版本库（并在 [`docs/README.md`](../README.md) 第三节追加第 30 项索引），随后可执行正式 Release Commit 与里程碑 Tag（如 `v1.0.0-phase9.1-gm`）打标归档。
