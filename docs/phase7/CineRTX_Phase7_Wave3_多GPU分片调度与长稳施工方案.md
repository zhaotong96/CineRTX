# CineRTX Phase 8：多 GPU 拓扑分片调度与广电级长稳鲁棒性 SuperPower 施工文档

- **文档编号**：`PLAN-SUPERPOWER-PHASE8-MULTIGPU-ROBUSTNESS`
- **前置依据**：《CineRTX 阶段六收官终审验收、深层瓶颈剖析与 Phase 7–9 战略演进专家裁决报告》 (`DOC-VERDICT-PHASE6-CLOSURE-AND-ROADMAP-001` §3.3, §4.2, §5) · `PLAN-SUPERPOWER-PHASE7-VIEWPORT-COLOR`
- **目标版本**：`CineRTX v0.4.0`
- **预计工期与人员配置**：`2 周（Sprint 3–4）` · **3 名执行工程师并行开发**
  - **工程师 A（多 GPU 拓扑枚举与并行分片调度组）**：负责多卡硬件探测 (`enumerateDevices`)、多文件跨卡工作窃取调度器 (`MultiGpuBatchScheduler`) 与单大文件 IDR-GOP 切片跨卡并行合成器 (`GopSegmentDispatcher`)。
  - **工程师 B（广电专业容器解码与 VFR 时间基平滑组）**：负责手术刀静态 FFmpeg 扩展（`ProRes`、`DNxHR`、`MXF`、`MOV`）及 `705,600,000 Hz` Flicks 严格单调时间戳平滑器 (`FlicksMonotonicSmoother`)。
  - **工程师 C（抗 TDR 事务自愈与长稳模糊压测组）**：负责 `DXGI_ERROR_DEVICE_REMOVED` / `RTX_ERR_DEVICE_LOST` 自动重建会话与 GOP 级断点续转 (`TranscodeCheckpoint`)、损坏码流 Fuzzing 测试及 100,000 帧浸泡工装。

---

## Part I: Plan（总体施工蓝图与架构契约）

### 1.1 多 GPU 零 PCIe 像素传输分片架构（为什么拒绝跨卡传未压缩像素？）

正如专家裁决报告 §4.2 所证：4K `P010` 未压缩帧每帧高达 `24.88 MB`，在无 NVLink 的 PCIe 主板上做“卡 0 超分 $\to$ 卡 1 编码”会产生 ~3 GB/s 的 PCIe 流量，且因 NVENC 仅占总耗时的 10%，收益不足 10%。因此，Phase 8 确立 **“零跨卡未压缩像素传输”的两级多 GPU 分片架构**：

```
+------------------------------------------------------------------------------------------------------+
|                      CineRTX Phase 8 (v0.4.0) 零跨卡像素拷贝多 GPU 并行拓扑                            |
+------------------------------------------------------------------------------------------------------+
  [ 输入源判断 ]
       ├── 场景 1: 批处理队列含 M 个视频文件 (M >= 2) ──► [ 模式 A: 文件级工作窃取 (File-Level Sharding) ]
       │     ├── GPU 0 (sm_120 RTX 5080): 独立 NvidiaTranscodeSession 消费文件 1, 3, 5...
       │     └── GPU 1 (sm_89  RTX 4090): 独立 NvidiaTranscodeSession 消费文件 2, 4, 6...
       │
       └── 场景 2: 单个超长/大型视频文件 (时长 >= 30s) ──► [ 模式 B: IDR-GOP 关键帧分片 (GOP-Segment Sharding) ]
             ├── 1. 主解复用器快速扫描视频流数据包索引，按 IDR 关键帧边界切分为 K 个闭合 GOP 段 (每段 ~10 秒)
             ├── 2. GPU 0 独立完成段 0, 2, 4 的 [NVDEC -> VSR+TrueHDR -> Pack -> NVENC] 产出编码包队列
             ├── 3. GPU 1 独立完成段 1, 3, 5 的 [NVDEC -> VSR+TrueHDR -> Pack -> NVENC] 产出编码包队列
             └── 4. 主复用器按段序无损拼接压缩后的 AV1/HEVC OBU/NAL 数据包 (跨卡传输的仅是几 MB 的压缩码流！)
```

### 1.2 架构红线与边界约束
1. **单实例设备显式绑定铁律**：严禁依赖进程全局默认 CUDA Context 0！`RtxDecoderConfig`、`RtxEncoderConfig` 与 `RtxNgxBridgeConfig` 必须显式携带目标 GPU 的 `device_index` 与 8 字节硬件 `luid`。
2. **主二进制体积红线**：扩展 `ProRes` / `DNxHR` / `MXF` 静态解复用与软解后，`CineRTX.exe` 单文件体积仍必须严格控制在 **`<= 12.0 MB`** 红线内（`ProRes` 与 `DNxHD` 解码器纯机器码仅约 `280 KB`）。
3. **时间戳绝对单调不变量**：送入 `av_interleaved_write_frame` 的任何音视频包必须满足 $\text{DTS}_{i} > \text{DTS}_{i-1}$（或相等当且仅当跨不同流），杜绝 VFR 源导致复用器报错退出。

---

## Part II: Tasks（原子化 TDD 施工任务集）

### Track A：多 GPU 拓扑枚举与双模并行分片调度器（执行工程师 A）

#### Task 8-A1：扩展 `rtx_shim_abi.h` 以支持多 GPU 拓扑枚举与指定设备会话创建
- **目标文件**：[`include/shim/rtx_shim_abi.h`](../../include/shim/rtx_shim_abi.h)、`src/shim/loader/nv_backend_shim.cpp`、`src/shim/loader/nv_cuda_context.cpp`
- **施工内容**：
  1. 在 [`include/shim/rtx_shim_abi.h`](../../include/shim/rtx_shim_abi.h) 中利用 `RtxDecoderConfig` 与 `RtxEncoderConfig` 末尾或新增字段加入 `int32_t device_index;`（默认 `0`）；
  2. 新增 C ABI 导出函数 `rtx_shim_enumerate_devices`：
     ```c
     #define RTX_MAX_GPUS 8
     typedef struct RtxGpuTopology {
       uint32_t device_count;
       RtxDeviceInfo devices[RTX_MAX_GPUS];
     } RtxGpuTopology;

     RTX_SHIM_API RtxResult RTX_SHIM_CALL rtx_shim_enumerate_devices(RtxGpuTopology* topology);
     ```
  3. 在 `nv_cuda_context.cpp` 中调用 `cuDeviceGetCount` 遍历所有 NVIDIA GPU，填充每张卡的 `name`、`luid`、`compute_major/minor`、`vram_bytes` 与 `capabilities`（如 `RTX_CAP_NVENC_AV1`）。

#### Task 8-A2：实现 `MultiGpuDispatcher`（支持多文件工作窃取与单文件 IDR-GOP 切片并行）
- **新增文件**：`include/rtx/multi_gpu_scheduler.h`、`src/core/multi_gpu_scheduler.cpp`
- **核心算法与接口契约**：
  ```cpp
  // include/rtx/multi_gpu_scheduler.h — 100% RAII 多卡分片调度器
  #pragma once
  #include <span>
  #include <vector>
  #include "rtx/hw_transcode.h"

  namespace rtx {

  struct GopSegmentRange {
      int64_t start_pts = 0;
      int64_t end_pts   = 0; // std::numeric_limits<int64_t>::max() for last segment
      int     assigned_gpu = 0;
  };

  class MultiGpuScheduler {
  public:
      [[nodiscard]] static Result<MultiGpuScheduler> create(std::span<const int> gpu_indices = {});

      // 模式 A：多文件批处理工作窃取（按各卡算力权重动态领取下一个待转码文件）
      [[nodiscard]] Result<std::vector<HwTranscodeReport>> runBatchJobs(
          std::span<const HwTranscodeOptions> jobs);

      // 模式 B：单大文件按 IDR 关键帧边界切分 GOP 并行重制，最后在内存/临时分段流中按序无损合流
      [[nodiscard]] Result<HwTranscodeReport> runSegmentedSingleJob(
          const HwTranscodeOptions& options);

  private:
      std::vector<DeviceInfo> active_gpus_;
  };

  } // namespace rtx
  ```
- **单卡机器上的确定性回退验证**：当宿主机仅有 1 张 GPU（或用户传入 `--gpu 0`）时，自动支持以“虚拟双分片（同一 GPU 双会话顺序/交错测试模式 `--test-gop-split`）”在单卡机器上 100% 覆盖测试 GOP 切片与合流逻辑！

---

### Track B：广电专业格式支持与 Flicks VFR 时间戳单调平滑器（执行工程师 B）

#### Task 8-B1：扩展静态 FFmpeg 编译白名单支持 `MXF/MOV` 与 `ProRes/DNxHR` 输入
- **目标文件**：`tools/build_ffmpeg_static.bat`、`cmake/RtxFfmpegStatic.cmake`、[`src/io/ffmpeg_io.cpp`](../../src/io/ffmpeg_io.cpp)
- **施工内容**：
  1. 在 `tools/build_ffmpeg_static.bat` 的 `--enable-demuxer` 白名单中追加 `mxf,mov,avi,mpegts`，在 `--enable-decoder` 中追加 `prores,dnxhd,dvvideo`（由于 NVDEC 硬件不支持解码 Apple ProRes 与 Avid DNxHR，当 `ffmpeg_io.cpp` 检测到输入流为 `AV_CODEC_ID_PRORES` 或 `AV_CODEC_ID_DNXHD` 时，由内联的轻量级 FFmpeg 多线程帧级软解直接解码至 `HostPinned` 锁页内存 `P010` 平面，再经异步 DMA 零阻塞送入 `rtx_ngx_bridge` 进行 4K VSR + TrueHDR + NVENC AV1 硬件编码！）；
  2. 在 [`src/io/ffmpeg_io.cpp`](../../src/io/ffmpeg_io.cpp) 的 `pathExtension` 与探测逻辑中放行 `.mxf` 与 `.mov`。

#### Task 8-B2：在 [`src/io/ffmpeg_io.cpp`](../../src/io/ffmpeg_io.cpp) 与 [`src/core/hw_transcode.cpp`](../../src/core/hw_transcode.cpp) 实装 VFR 时间戳单调修复器
- **目标文件**：[`src/io/ffmpeg_io.cpp`](../../src/io/ffmpeg_io.cpp)、[`src/core/hw_transcode.cpp`](../../src/core/hw_transcode.cpp)
- **核心算法规范**：
  ```cpp
  // VFR / 损坏时间戳严格单调夹具 (Monotonicity Sanitizer)
  struct StreamTimestampGuard {
      int64_t last_dts = std::numeric_limits<int64_t>::min();
      int64_t min_step = 1;

      constexpr void sanitize(int64_t& pts, int64_t& dts, int64_t fallback_step) noexcept {
          if (dts == std::numeric_limits<int64_t>::min()) {
              dts = (last_dts == std::numeric_limits<int64_t>::min()) ? 0 : (last_dts + fallback_step);
          }
          if (last_dts != std::numeric_limits<int64_t>::min() && dts <= last_dts) {
              dts = last_dts + min_step; // 强制修正非单调回退的损坏 DTS
          }
          if (pts == std::numeric_limits<int64_t>::min() || pts < dts) {
              pts = dts; // 确保无 B 帧重排或异常倒挂时 PTS >= DTS
          }
          last_dts = dts;
      }
  };
  ```

---

### Track C：抗 TDR 事务级自愈与极端畸形码流 Fuzzing 测试套件（执行工程师 C）

#### Task 8-C1：在 [`src/core/hw_transcode.cpp`](../../src/core/hw_transcode.cpp) 中实现 `RTX_ERR_DEVICE_LOST` 自动软重启与 GOP 级断点续转
- **目标文件**：[`src/core/hw_transcode.cpp`](../../src/core/hw_transcode.cpp)
- **施工内容**：
  当 `remaster()` 或 `encoderEncode()` 返回 `RTX_ERR_DEVICE_LOST`（例如显卡驱动发生瞬态重置或超时）时：
  1. 捕获当前已成功写入 Muxer 的最后一个关键帧 PTS $\text{PTS}_{\text{safe}}$；
  2. 自动销毁当前失效的 ` bridge_`、`decoder_`、`encoder_` 句柄（保持已打开的 `muxer_` 与输出文件不中断！）；
  3. 休眠退避 `250 ms` 等待 WDDM 驱动完成重置，重新调用 `bridge_loader_.create(...)`、`shim_.decoderCreate(...)`、`createEncoder(...)`（最多重试 3 次）；
  4. 调用 `demuxer_->seekToKeyframeBefore(pts_safe)` 从最近一个 IDR 关键帧重新解码推进，跳过已写入的帧，实现 **7×24 小时无人值守长周期转码的 TDR 零中断自愈**！

#### Task 8-C2：新增畸形容器/码流模糊测试 (`tests/test_fuzz_resilience.cpp`)
- **新增文件**：`tests/test_fuzz_resilience.cpp`（注册至 [`CMakeLists.txt`](../../CMakeLists.txt) `RTX_TEST_SOURCES`）
- **测试覆盖**：
  1. 利用 `MemoryAvioContext` 构造 1,000 组随机位翻转（Bit-Flip）、截断头部、乱序负值 PTS/DTS (`INT64_MIN`, `INT64_MAX`, 倒退序列)、奇偶非法宽高（如 `1921x1079`、`0x0`）的内存 MP4/MKV 字节流；
  2. 断言 `FfmpegDemuxer` 与 `StreamTimestampGuard` 100% 返回结构化 `Status` 错误或安全平滑修复，**0 崩溃、0 除零异常、0 内存泄漏**。

---

## Part III: Checklists（执行工程师完工自检与门禁核销表）

- [ ] **门禁 1（GOP 分片并行无缝合流校验）**：
  ```powershell
  ./build/cmake/windows-msvc-ninja/CineRTX.exe --input test_30s.mp4 --output out_gop_split.mp4 --test-gop-split 4
  ```
  断言：将 30 秒视频切分为 4 个 GOP 段并行重制并合流后，输出视频总帧数与单次连续转码 100% 严格相等，交界帧无花屏、无 PTS 跳变（`ffprobe -v error -show_entries packet=pts_time,dts_time` 严格单调递增）。
- [ ] **门禁 2（VFR 与乱序时间戳抗压验证）**：
  ```powershell
  ./build/cmake/windows-msvc-ninja/rtx_tests.exe --filter vfr
  ```
  断言：模拟 24fps $\leftrightarrow$ 60fps 剧烈抖动及 100 处倒退 DTS 的 24 小时虚拟流测试 100% PASS，累积音画同步漂移 `== 0 Flicks`。
- [ ] **门禁 3（抗 TDR 故障注入自愈验证）**：
  ```powershell
  ./build/cmake/windows-msvc-ninja/rtx_tests.exe --filter tdr_recovery
  ```
  断言：在第 50 帧人工注入模拟 `RTX_ERR_DEVICE_LOST` 错误码时，引擎自动销毁并重建编解码/桥接上下文，最终输出完整 100 帧视频且返回 `Ok`。
- [ ] **门禁 4（1,000 次畸形码流 Fuzzing 零崩溃门禁）**：
  ```powershell
  ./build/cmake/windows-msvc-ninja/rtx_tests.exe --filter fuzz
  ```
  断言：1,000 次变异容器/码流注入测试在 `< 3 秒` 内 100% PASS，0 未捕获异常。
- [ ] **门禁 5（单 EXE 体积红线核查）**：
  ```powershell
  powershell -ExecutionPolicy Bypass -File tools/audit_imports.ps1
  ```
  断言：`CineRTX.exe` 体积依然严格 `< 12.0 MB`，无任何外部 FFmpeg DLL 依赖。
