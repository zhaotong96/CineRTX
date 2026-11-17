# CineRTX Phase 10 领域专家技术评审裁决书暨全链路接线闭环 SuperPower 返工规范

- **文档编号**：`CINERTX-P10-REJECT-REWORK-007`
- **评审对象**：[`docs/phase10/06_CineRTX_Phase10_领域专家技术评审与验收委托函.md`](06_CineRTX_Phase10_领域专家技术评审与验收委托函.md) 及全仓对应实现
- **终审裁决**：**REJECT（驳回验收，强制返工）**
- **核心定性**：**孤岛式伪闭环（Isolated Unwired Scaffolds）**——执行团队新建了 `cpu_video_encoder`、`shot_segmenter`、`job_queue`、`string_utils.h` 四个独立模块及两枚 CUDA 核函数并跑通了孤立单测（78/78 PASS），**但完全没有将它们接入真实生产执行链路**（`param_schema.h`、`hw_transcode.cpp`、`rife_engine.cpp`、`ngx_d3d12_bridge.cpp`、`rtx_gui.cpp`、`main.cpp`、`webui/` 均未完成主干接线）。

---

## 一、 四大施工轨道实证穿透审计总表（物证级核验）

| 轨道 | 已落地部分（PASS） | 实证捕获的致命断链 / 未接线缺陷（REJECT 根因） | 返工编号 |
| :--- | :--- | :--- | :--- |
| **Track 10A**<br>代码卫生与零附属 DLL | 1. `CMakeLists.txt` 已移除 `rtx_ngx_bridge SHARED` 并开启 `RTX_EMBEDDED_NGX_BRIDGE=1`。<br>2. `p010_pack.cu` 已删除 2 枚死核并合入 `rtx_nvof_warp_*`。<br>3. `include/rtx/string_utils.h` 已创建。 | 1. **`p010_pack.ptx` 未重新编译**：云端文件仍停留在 `2026-11-13` 的 **`1,320,609 B`（1.32 MB）**，死核汇编依然驻留二进制。<br>2. **`p010_pack_runtime.cpp` 未清理**：仍保留 `launchFusedRemaster` 及内联 PTX 字符串字面量 `.visible .entry rtx_nvof_warp_nv12`。<br>3. **`bridge_exports.cpp` 7 个死导出未删**，且 `ngx_remaster.cpp` 仍保留 `LoadLibraryW` / `GetProcAddress` 动态反射壳。 | **RW-P10-01** |
| **Track 10B**<br>NVENC `CQ`/`QP` 与 CPU `CRF` | 1. `rtx_shim_abi.h` 与 `nv_encoder.cpp` 已正确实现 `RTX_RC_CQ`（`targetQuality` + `enableAQ=1`）、阶梯 `QP` 与 `FORCEIDR`。<br>2. `RtxFfmpegStatic.cmake` 已接入 `libsvtav1` / `libx265`。<br>3. `cpu_video_encoder.h/.cpp` 已实现 AVX2 `P010 -> YUV420P10LE`。 | 1. **`param_schema.h/.cpp` 完全未更新**：`rate_control` 仍默认为 `"cbr"` 且枚举仅 `"cbr,vbr,qp"`；`codec` 仅 `"av1,hevc"`；**完全缺失 `cq`、`crf`、`av1_cpu`、`hevc_cpu`**！<br>2. **`CpuVideoEncoder` 在 `hw_transcode.cpp` 中 0 引用**：`--rate-control crf` / `--codec av1_cpu` 在真实转码管线中完全不可达。<br>3. **`cpu_video_encoder.cpp` 存在两处隐藏 Bug**：逐帧复用 `impl_->frame` 时未先调用 `av_frame_remove_side_data` 会导致 SideData 累积，且 `(void)hdr10plus_payload;` 丢弃了 HDR10+ 动态元数据。 | **RW-P10-02** |
| **Track 10C**<br>ShotGroup 防果冻与 `VSR -> FG` | 1. `shot_segmenter.h/.cpp` 已实现 $8\times 8$ 分块 + EMA 双指标判定。<br>2. `rife_pack.cu` 已新增 `rtx_eval_block_sad_8x8` 与 `rtx_p010_guided_interp`。 | 1. **`hw_transcode.cpp`（1,386 行）完全未重构**：无 `RemasterPipeline` 类，无 `ShotGroupSegmenter` 接线，无 `vsr-first`（`Decode -> VSR -> FG`）拓扑，仍死守 `FG -> VSR`！<br>2. **`rife_engine.cpp` 仍死守 `diff_norm > 0.35f`**：未调用 `rtx_eval_block_sad_8x8`，NVOFA 路径仍 0 切镜检测。<br>3. **`ngx_d3d12_bridge.cpp:372` 伪实现**：`process(..., bool reset_temporal)` 函数签名加了参数，但函数体内 **0 处使用 `reset_temporal`**（未设置 `NVSDK_NGX_Parameter_Reset`）。 | **RW-P10-03** |
| **Track 10D**<br>Encoder 动态多文件队列 | 1. `job_queue.h/.cpp` 已完整实现 `EncoderJobQueue` 状态机与多线程调度。<br>2. `rtx_gui.cpp` 将全局变量收敛进 `WorkstationApp g_app`。 | 1. **`g_app.jobQueue` 是死成员**：`rtx_gui.cpp` 中 `onStartTranscode`、`onQueueAdd`、`onQueueRemove`、`onQueueClear` **仍在跑旧版 `g_app.batchQueue` + `g_app.workerThread`**，对 `g_app.jobQueue` 的调用次数为 **0**！<br>2. **`main.cpp`（`--session-stdio`）与 `webui/` 完全未更新**：无 `enqueue`/`remove`/`move`/`skip` 协议，无 `--pipeline-order`。<br>3. **`test_gui_workstation.cpp` 对 `EncoderJobQueue` 覆盖率为 0**。 | **RW-P10-04** |

---

## 二、 SuperPower 返工施工指令（RW-P10-01 ~ RW-P10-04）

> **铁律**：本次返工严禁再提交“只写孤立类与单测、不接主干管线”的半成品。以下 4 项返工必须全部在真实生产路径打通，并通过端到端实机验证门禁。

### RW-P10-01：完成 PTX 重编译与死符号彻底清零（Track 10A 闭环）
1. **重新编译 `p010_pack.ptx` 与 `rife_pack.ptx`**：
   - 使用 `nvcc -ptx -arch=sm_75 src/shim/ngx_bridge/p010_pack.cu -o src/shim/ngx_bridge/p010_pack.ptx` 重新生成 `p010_pack.ptx`，硬性验收其体积 **$\le 180\text{ KB}$**（当前 `1,320,609 B`）。
   - 同步编译 `rife_pack.ptx`，确保 `rtx_eval_block_sad_8x8` 与 `rtx_p010_guided_interp` 进入 PTX 导出表。
2. **清理 `p010_pack_runtime.cpp/.h`、`bridge_exports.cpp` 与 `ngx_remaster.cpp`**：
   - 删除 `p010_pack_runtime.cpp` 中的 `launchFusedRemaster` 与内联 PTX 字符串字面量（直接从 `pack.module` 加载 `rtx_nvof_warp_nv12` / `rtx_nvof_warp_p010`）。
   - 删除 `bridge_exports.cpp` 中 7 个废弃导出函数及 `include/shim/ngx_bridge_abi.h` 中对应声明。
   - 删除 `src/core/ngx_remaster.cpp` 中所有 `LoadLibraryW` / `GetProcAddress` 动态反射代码，直接通过 `bindEmbedded()` 静态绑定。

### RW-P10-02：打通 SSOT 参数契约与 `CpuVideoEncoder` 主干接线（Track 10B 闭环）
1. **更新 `include/rtx/param_schema.h`、`src/core/param_schema.cpp` 与 `tests/test_i18n.cpp`**：
   - `TranscodeProfile` 默认值更新为：`rate_control{"cq"}`，`qp = 22`，`pipeline_order{"vsr-first"}`。
   - `kParamSchema` 扩展为 20 项：
     - `codec` `enum_csv = "av1,hevc,av1_cpu,hevc_cpu"`
     - `rateControl` `enum_csv = "cq,vbr,cbr,qp,crf"`，`default_str = "cq"`
     - 新增 `pipelineOrder`（`--pipeline-order`，`enum_csv = "vsr-first,fg-first"`，`default_str = "vsr-first"`）
   - 更新 `profileToHwOptions()`：当 `codec` 为 `av1_cpu` / `hevc_cpu` 或 `rate_control == "crf"` 时自动路由至 CPU 软编标记（`use_cpu_encoder = true`, `rate_control = RTX_RC_CRF`）。
2. **修复 `src/io/cpu_video_encoder.cpp` 并接入 `hw_transcode.cpp`**：
   - 在 `encodeP010Frame` 每次填充 HDR10 SideData 前，先执行 `av_frame_remove_side_data(impl_->frame, AV_FRAME_DATA_MASTERING_DISPLAY_METADATA)` 与 `AV_FRAME_DATA_CONTENT_LIGHT_LEVEL`（以及 `AV_FRAME_DATA_DYNAMIC_HDR_PLUS`），防止复用 `AVFrame` 时元数据重复堆积。
   - 在 `hw_transcode.cpp` 消费端编码线程中增加 `CpuVideoEncoder` 分支：通过双缓冲 Pinned Host Memory（`cuMemAllocHost` + `cuMemcpyDtoHAsync`）异步拉取 GPU `P010` 帧送入 `CpuVideoEncoder::encodeP010Frame` 并写入 `IMuxer`。

### RW-P10-03：打通 `ShotGroupSegmenter`、`NVSDK_NGX_Parameter_Reset` 与 `vsr-first` 双拓扑（Track 10C 闭环）
1. **修复 `src/shim/ngx_bridge/ngx_d3d12_bridge.cpp` 的时域重置空实现**：
   - 在 `NgxD3D12Bridge::process(..., bool reset_temporal)` 中真正写入 NGX 参数：
     ```cpp
     if (ngx_params_ != nullptr) {
         auto* params = static_cast<NVSDK_NGX_Parameter*>(ngx_params_);
         NVSDK_NGX_Parameter_SetI(params, NVSDK_NGX_Parameter_Reset, reset_temporal ? 1 : 0);
     }
     ```
2. **打通 GPU $8\times 8$ 分块 SAD 检测（替换 `0.35f` 死阈值并覆盖 NVOFA）**：
   - 在 `rife_engine.cpp` 与 `nvof_engine.cpp` 中调用 `rtx_eval_block_sad_8x8` 计算 `uint32_t block_sad[64]` 并导出至 C ABI（或由 `bridge_exports.cpp` 在插帧前统一执行）。
   - 彻底删除 `rife_engine.cpp:962` 的 `diff_norm > 0.35f` 硬编码判断，改由 `rtx::ShotGroupSegmenter::evaluate(block_sad, w, h)` 统一裁决！
3. **重构 `src/core/hw_transcode.cpp` 为 `RemasterPipeline` 类并实现 `vsr-first`（先超分后补帧）**：
   - 将 705 行巨型 `runNvidiaRemasterTranscode` 拆解为良构的 `RemasterPipeline` 成员方法（`openSession` / `processDecodedFrame` / `runVsrFirstStep` / `runFgFirstStep` / `flushAndFinalize`，单函数严禁超过 120 行）。
   - 实现 `vsr-first` 拓扑：
     1. 原始解码帧经 `ShotGroupSegmenter` 判定切镜；若切镜，本帧设置 `reset_temporal = true` 与 `force_idr = true`；
     2. 每个原始解码帧仅调用 **1 次** `remaster`（`NGX VSR -> TrueHDR -> P010`），结果轮转存入 4K `P010` 双锚点环 `anchor_ring[2]`；
     3. 组内时域插值槽位 $\alpha \in (0, 1)$ 调用 `rtx_p010_guided_interp` 直接在 `anchor_ring[2]` 上合成 4K 10-bit `P010` 中间帧；切镜边界槽位 100% 重复输出旧镜头末帧 `anchor_ring[prev]`（**Shot-Tail Hold，零光流计算，零果冻**）。

### RW-P10-04：彻底激活 `rtx::EncoderJobQueue` 三端动态队列并补充单测（Track 10D 闭环）
1. **删除 `rtx_gui.cpp` 中的 `batchQueue` / `batchIndex` / `batchMutex` / `workerThread`**：
   - 将左侧队列控件升级为多列 `WC_LISTVIEWW`（`ID` | `Inputs` | `Profile Summary` | `Status` | `Progress`）。
   - `IDC_BTN_QUEUE_ADD` 调用 `g_app.jobQueue.enqueueJob(selected_files, collectProfile(hwnd))`；支持运行期不锁死参数面板随时追加异构参数任务；支持 `updatePendingJob`、`removeJob`、`moveJob(±1)`、`skipCurrentJob`。
2. **同步更新 `src/cli/main.cpp` 与 `webui/`**：
   - `main.cpp` 解析 `--pipeline-order vsr-first|fg-first` 及 `--codec av1_cpu|hevc_cpu`、`--rate-control cq|crf`。
   - `--session-stdio` 与 `webui/server.py` / `webui/app.js` / `webui/index.html` 对接 `EncoderJobQueue` 动态队列指令与多文件异构任务展示。
3. **补充 `tests/test_gui_workstation.cpp`**：
   - 补齐 `test_encoder_job_queue_heterogeneous_dynamic_mutation` 单测，验证运行期并发加任务、改参数、上下移动与多文件异构执行顺序。

---

## 三、 复验准出硬门禁（下次提交验收必须同时满足）

1. `p010_pack.ptx` 文件体积 `<= 180 KB`，`rg "launchFusedRemaster|rtx_ngx_bridge_get_textures|0\.35f" src/` 返回 **0 匹配**；
2. `rg "batchQueue" src/gui/` 返回 **0 匹配**，`rg "jobQueue\.enqueueJob" src/gui/` 至少 **1 处真实调用**；
3. `rg "ShotGroupSegmenter|CpuVideoEncoder|vsr-first" src/core/hw_transcode.cpp` 全部命中且函数拆解良构（单函数 `<= 120` 行）；
4. `rg "NVSDK_NGX_Parameter_Reset" src/shim/ngx_bridge/ngx_d3d12_bridge.cpp` 命中；
5. 全量 `ctest --preset windows-msvc-ninja --output-on-failure`（含新增 `EncoderJobQueue` 与 `vsr-first` 单测）100% PASS，且 `AuditSpdx.cmake` / `AuditImports.cmake` 0 违规。
