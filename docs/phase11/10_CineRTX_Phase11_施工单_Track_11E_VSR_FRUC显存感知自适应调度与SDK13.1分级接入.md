# CineRTX Phase 11 施工单 Track 11E：VSR/FRUC 显存感知自适应调度与 Video Codec SDK 13.1 分级接入

- **施工单编号**：`CINERTX-P11-TASK-11E`
- **优先级与依赖**：**P2/P3（依赖 Track 11A 与 Track 11D 稳固后启动）**
- **承接工程师**：GPU AI 推理与视频编解码优化工程师（GPU AI Inference & Video Codec Engineer）
- **母本依据**：[`04_CineRTX Phase 11 冲突评判与方案.md`](./04_CineRTX Phase 11 冲突评判与方案.md) §3（第四、五点）、§4（Track 11E）、§5（Gate 11E-1）
- **独占文件边界**：
  - 修改：`include/shim/ngx_bridge_abi.h`、`include/rtx/ngx_remaster.h`、`src/core/ngx_remaster.cpp`、`src/shim/ngx_bridge/bridge_exports.cpp`、`src/shim/ngx_bridge/rife_engine.cpp`、`src/shim/ngx_bridge/rife_pack.cu`、`src/core/hw_transcode_stages.cpp`、`src/shim/loader/nv_encoder.cpp`、`.gitmodules`（仅在驱动基线获批升版至 R610 时触碰）
  - 新建：`include/rtx/vram_scheduler.h`、`src/core/vram_scheduler.cpp`、`tests/test_vram_scheduler.cpp`、`tests/test_rife_scale05.cpp`
  - **严禁触碰**：`schema/*`、`src/core/nle_project.*`、`src/core/wasapi_*`

---

## 一、 施工目标与物理边界约束

1. **打通 RIFE `scale=0.5`（128 像素对齐 + `reflect101`）桥接 ABI 与运行时**：
   - 修复当前 [`src/core/hw_transcode_stages.cpp`](../../src/core/hw_transcode_stages.cpp) 调用 `initRife` / `getInterpBuffer` 固定写死源分辨率 `session_->info().width/height` 的缺陷；
   - 严禁未经实测宣称“节省 75% 显存”，所有内存占用以 TensorRT 真实峰值分配为准；
   - 保留 NVOFA 与 RIFE **互斥按需初始化**，**严禁**对黑盒管理工作区的 NvOFFRUC 做跨库 `cuMemMap` 物理页别名。
2. **落地带滞后防抖（Hysteresis）的多维 VRAM 调度器**：
   - 取代拍脑袋的“8GB × 88% 一刀切”静态阈值，联合采集 DXGI 本地显存预算/占用、CUDA 空闲显存、TRT 峰值分配、编码/解码参考池与视口占用，在 `vsr-first`（NVOFA / RIFE 全分 / RIFE `scale=0.5`）与 `fg-first` / 跳过插帧间平滑切换，杜绝单帧边界抖动与 OOM。
3. **Video Codec SDK 13.1 驱动基线分级接入与实证矩阵**：
   - 当前 `.gitmodules` 锁定 `nv-codec-headers n13.0.19.1`（对应 R580 驱动）；[SDK 13.1.15 要求最低驱动 ≥ 610.0](https://docs.nvidia.com/video-technologies/video-codec-sdk/13.1/read-me/index.html)；
   - 任何 13.1 高级特性（AV1 分层 B 帧 7/15/31 帧、UHQ 迭代/Lookahead、时域滤波）均须经 `NvEncGetEncodeCaps` 运行时能力探测，并在低版本驱动上平滑回退，同时实测输出完整口径的 FPS / 编码器占用 / VRAM 峰值 / 画质矩阵。

---

## 二、 原子 TDD 施工任务（Atomic Tasks）

### Task 11E-1：扩展 RIFE 桥接 ABI（仅追加，严格向后兼容）
1. 在 `include/shim/ngx_bridge_abi.h`（`MIT OR Apache-2.0`）追加结构体与版本化入口（不破坏现有 ABI v4）：
   ```c
   typedef struct RtxRifeConfig {
       uint32_t struct_size;
       uint32_t input_width;   // 实际输入给 RIFE 的分辨率（vsr-first 时为目标分辨率，fg-first 时为源分辨率）
       uint32_t input_height;
       float    flow_scale;    // 1.0f (A=64 对齐) 或 0.5f (A=128 对齐 + reflect101 边缘填充)
       uint32_t reserved[4];
   } RtxRifeConfig;

   // 追加导出（ABI v5）：rtx_ngx_bridge_init_rife_ex(bridge, engine_path, const RtxRifeConfig* cfg)
   ```
2. 修改 `src/shim/ngx_bridge/rife_engine.cpp` 与 `rife_pack.cu`：
   - 当 `flow_scale == 0.5f` 时，强制按 `A = 128` 向上对齐张量尺寸，预处理核函数采用 `reflect101` 镜像边缘填充（避免零填充黑边污染光流边界），输出回写时精确裁剪回 `input_width × input_height`；
   - 确保 TensorRT Optimization Profile 覆盖 `1280×720`、`1920×1080`、`2560×1440`、`3840×2160` 对应对齐尺寸，并向调用方返回 `trt_peak_allocation_bytes` 供遥测记录。
3. 修改 `src/core/hw_transcode_stages.cpp`：按实际拓扑（`vsr-first` vs `fg-first`）与调度器选定的 `flow_scale` 传递真实尺寸调用 `initRifeEx`。

### Task 11E-2：多维滞后 VRAM 自适应调度器（`include/rtx/vram_scheduler.h` & `src/core/vram_scheduler.cpp`）
```cpp
// include/rtx/vram_scheduler.h (SPDX: GPL-3.0-or-later WITH Plugin-Exception)
#pragma once
#include <cstdint>

namespace rtx {

enum class TopologyTier : uint8_t {
    VsrFirst_RifeFull = 0,     // 最高画质：VSR -> RIFE scale=1.0
    VsrFirst_RifeHalf = 1,     // 高画质抗大位移：VSR -> RIFE scale=0.5 (128-align)
    VsrFirst_Nvofa    = 2,     // 硬件光流超分先行
    FgFirst_RifeFull  = 3,     // 显存紧张回退：先源分辨率插帧再 VSR
    PassThrough_NoFruc = 4,    // 极度紧张保护：跳过插帧保底不 OOM
};

struct VramTelemetry {
    uint64_t dxgi_budget_bytes = 0;
    uint64_t dxgi_current_usage_bytes = 0;
    uint64_t cuda_free_bytes = 0;
    uint64_t cuda_total_bytes = 0;
    uint64_t trt_estimated_peak_bytes = 0;
    uint64_t codec_ref_pool_bytes = 0;  // 解码 + NVENC B-frame/Lookahead 缓冲
    uint64_t viewport_bytes = 0;
};

class VramAdaptiveScheduler {
public:
    // 滞后防抖参数：降级水印（如 82%）与升级恢复水印（如 68%）+ 最小驻留窗口（>= 120 帧）
    explicit VramAdaptiveScheduler(double degrade_ratio = 0.82,
                                   double recover_ratio = 0.68,
                                   uint32_t min_dwell_frames = 120) noexcept;
    [[nodiscard]] TopologyTier selectInitialTier(TopologyTier requested,
                                                 const VramTelemetry& snap) noexcept;
    [[nodiscard]] TopologyTier evaluateStep(const VramTelemetry& snap) noexcept;
};

} // namespace rtx
```

### Task 11E-3：Video Codec SDK 13.1 运行时能力探测与 Profile 矩阵（`src/shim/loader/nv_encoder.cpp`）
1. 在 `nv_encoder.cpp` 中封装 `queryAv1AdvancedCaps()`，通过 `NvEncGetEncodeCaps` 运行时探测：
   - `NV_ENC_CAPS_SUPPORT_BFRAME_REF_MODE`（确认硬件与驱动是否支持分层参考 B 帧）；
   - `NV_ENC_CAPS_SUPPORT_LOOKAHEAD` 与 `NV_ENC_CAPS_SUPPORT_TEMPORAL_FILTERING`（注意时域滤波要求 `frameIntervalP >= 5` 且与超低延迟零重排互斥）。
2. 定义 3 档透明编码 Profile（不作未经实测的性能承诺）：
   - `Default (R580/13.0 兼容)`：现有 CQ / P5 默认基线；
   - `HighQuality-7B`：7 层级 B 帧候选（需实测 8GB 显卡参考缓冲增量约 ~166 MiB）；
   - `Master-UHQ-15B/31B`：离线高压缩母版档（启用 UHQ 迭代/Lookahead + 15 或 31 分层 B 帧，显式检查 VRAM 预算并在 8GB 显卡上限制 Lookahead 深度，低驱动自动回退 `Default` 并记录告警日志）。
3. 仅当项目负责人正式批准升版最低驱动基线至 **R610（≥ 610.0）** 时，才将 `.gitmodules` 中的 `nv-codec-headers` 从 `n13.0.19.1` 升级至 `13.1.15`；否则通过条件编译宏 `RTX_NVENC_SDK_13_1` 隔离在可选实验开关之后。
4. （可选探索子项）在**纯同分辨率无 VSR 直通转码**分支独立评估 `AppTransZeroCopy`（`CUDA_ARRAY3D_VIDEO_ENCODE_DECODE`）与 H.264/HEVC 宏块 MV 统计辅助 `ShotGroupSegmenter`。

---

## 三、 量化验收门禁 Checklist（Gate 11E-1）

- [ ] **RIFE `scale=0.5` 闭环测试（`tests/test_rife_scale05.cpp`）**：
  - 验证 `128` 像素对齐与 `reflect101` 填充裁剪在 `1080p`、`2K`、`4K` 下边缘像素误差无黑边/撕裂；
  - 验证非整除高度（如 `1080` $\rightarrow$ `1152` 对齐再裁回 `1080`）输出尺寸 100% 精确；
  - 真机实测记录 `scale=1.0` 与 `scale=0.5` 的真实 `trt_peak_allocation_bytes` 与整机 VRAM 峰值。
- [ ] **滞后 VRAM 调度器测试（`tests/test_vram_scheduler.cpp`）**：
  - 模拟显存占用在 `75%–85%` 区间高频震荡，验证调度器在 `min_dwell_frames` 窗口内零抖动切换；
  - 8GB GPU 4K 连续压测无 OOM、无 `CUDA_ERROR_OUT_OF_MEMORY`。
- [ ] **SDK 13.1 实测基准矩阵（严禁缺失口径）**：
  - 输出包含 **GPU 型号、驱动版本、CUDA/TRT/RTX Video SDK/Video Codec SDK 版本、输入素材规格、编码参数、预热时长、统计窗口、重复次数** 的完整对比表，覆盖 `Default` vs `7B` vs `15B/31B + UHQ` 的 FPS、编码器占用、VRAM 峰值与客观画质（PSNR/SSIM/VMAF）；
  - 在 R580 低驱动环境下验证高级 Profile 优雅回退至 `Default`，零崩溃。
- [ ] **全局合规门禁**：`AuditSpdx.cmake`、`AuditImports.cmake` 与全量 `rtx_tests.exe` 100% PASS。
