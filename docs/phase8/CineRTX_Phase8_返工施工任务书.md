# Phase 8 返工施工任务书：开箱即用 60 帧全栈重构与极致代码瘦身

**任务单号**：`TASK-PHASE8-REWORK-ZERO-CONFIG-60FPS-SLIMMING-001`  
**下达机构**：CineRTX 项目工程管理与架构评审委员会  
**接收对象**：Phase 8 执行工程师  
**依据文档**：
- [docs/phase8/CineRTX 最新代码深度验收报告暨“开箱即用 60 帧 + 大刀阔斧极简瘦身”全栈重构方案.md](./CineRTX%20最新代码深度验收报告暨“开箱即用%2060%20帧%20+%20大刀阔斧极简瘦身”全栈重构方案.md)
- [docs/phase8/CineRTX_Phase8_双路补帧架构工程线索与实机环境证据报告.md](./CineRTX_Phase8_双路补帧架构工程线索与实机环境证据报告.md)

---

## 一、 返工背景与严重问题定性通报

在用户端对 [gemini_generated_video_6eaef4a7_remaster_av1.mp4](../../.vs/gemini_generated_video_6eaef4a7_remaster_av1.mp4) 进行实测时，由于**体验与规格严重不符**，本次 Wave 1~4 交付已被**全盘打回**。经项目负责人穿透式取证，发现以下不可接受的严重问题：

1. **违背“开箱即用与零非必要开关”原则（四道阻断导致默认转码仍为 24fps）**：
   - [include/rtx/hw_transcode.h](../../include/rtx/hw_transcode.h#L90) 与 [src/cli/main.cpp](../../src/cli/main.cpp#L105) 将 `fruc_mode` 默认设为 `FrucMode::Off`；
   - [src/gui/rtx_gui.cpp](../../src/gui/rtx_gui.cpp) 完全未接线补帧参数；
   - [src/core/hw_transcode.cpp](../../src/core/hw_transcode.cpp#L201-L215) 的 `chooseFrameRate` 算法计算为 48fps 而非 60fps。
   导致用户直接拖入或运行默认命令时，输出视频仍为 **24fps 240 帧**，补帧形同虚设。
2. **欺骗性插帧交付（`duplicateBypass` 偷梁换柱）**：
   - [src/shim/backends/offruc/offruc_bridge.cpp](../../src/shim/backends/offruc/offruc_bridge.cpp#L518-L519) 的 `interpolateFrame` 内部直接调用 `return duplicateBypass(bridge, in);`，将中间插值帧替换为内存硬拷贝前一帧，仅虚增计数器伪造遥测。肉眼播放呈现严重 3:2 丢步卡顿，完全无光流内插。
3. **架构泡沫严重反弹（全仓源码膨胀至 ~17,500 行 / 660 KB）**：
   - 存在 `vram_ring_pool`、`color.cpp`、`multi_gpu_scheduler`、`daemon_rpc`（带 `ws2_32`）等 15 个生产路径 0 调用的悬空死代码文件；
   - 搞出三套 Bridge 与三套 Loader 重复造轮子，并将已被裁撤的 TensorRT RIFE（外部 1.4GB 依赖）重新带回，违背奥卡姆剃刀与 KISS 原则。

---

## 二、 核心重构目标与三大工程支柱

1. **开箱即用黄金体验（Zero-Config Best Experience）**：
   - CLI 运行 `CineRTX --input in.mp4 --output out.mp4` 或 GUI 拖入点开始，**100% 自动输出 4K UHD + 60.0 FPS（NVOFA 硬件真光流）+ VSR Ultra (Level 4) + TrueHDR 1000 nits + AV1 10-bit**。
   - 色彩自动感知：SDR 自动开启 TrueHDR，HDR 原片自动旁路逆色调映射并透传原片 HDR10 元数据；输入宽高/帧率 100% 由解封装器获取，杜绝用户手工输入。
2. **大刀阔斧极简瘦身（Radical Codebase Slimming）**：
   - 彻底删除 15 个死代码文件与死测试；
   - **三桥归一**：消灭 `rtx_fruc_bridge.dll` 独立插件，将驱动原生 NVOFA 硬件光流（`C:\Windows\System32\nvofapi64.dll`）直接并入 [rtx_ngx_bridge.dll](../../build/cmake/windows-msvc-ninja/rtx_ngx_bridge.dll)，复用其 CUDA context/stream 与 PTX 加载器；
   - 生产代码压回 **7,500 行以内**（全仓净削减 45%+）。
3. **全局纯净度**：
   - 全局静态 CRT（`/MT`），消除 `MSVCP140.dll` / `VCRUNTIME140*.dll` / `WS2_32.dll` 泄漏，单 EXE 仅依赖系统基础 DLL。

---

## 三、 返工施工任务分解（Atomic Work Breakdown）

### Track A：开箱即用 60 帧与黄金默认值闭环

* **Task A1（底层结构体 SSOT 黄金默认值）**：修改 [include/rtx/hw_transcode.h](../../include/rtx/hw_transcode.h)：
  1. 移除 `enum class FrucMode`、`rife_model_path`、`fruc_scene_threshold`、`fault_inject_frame`；
  2. 确立黄金默认值：
     ```cpp
     int vsr_quality = 4;                // 默认 Level 4 Ultra
     bool fruc_enabled = true;           // 默认开启驱动原生 NVOFA 硬件光流
     Rational target_fps{60, 1};         // 默认锁定 60 FPS
     VideoCodec codec = VideoCodec::AV1; // 默认 AV1 10-bit
     ```
* **Task A2（帧率推导与 HDR 自适应决策）**：修改 [src/core/hw_transcode.cpp](../../src/core/hw_transcode.cpp)：
  1. `chooseFrameRate` 重写：若 `options.fruc_enabled == true`，源视频帧率 $< 59.9$ fps 时，100% 返回 `options.target_fps`（`Rational{60, 1}`）；原生 $\ge 60$ fps 则原样直通；
  2. HDR 自动感知：读取源流 `info_.hdr_source`，SDR 源自动开启 `enable_truehdr = 1`；HDR10/HLG 源自动置 `enable_truehdr = 0` 并透传原生元数据。
* **Task A3（GUI 控件与 CLI 参数清洗）**：
  1. 修改 [src/gui/rtx_gui.cpp](../../src/gui/rtx_gui.cpp)：VSR 默认勾选 `Level 4 Ultra`；替换 `IDC_CHK_TRUEHDR` 为 `IDC_CMB_FPS`（默认 `60 FPS (NVOFA 硬件光流 - 默认)`），并在 `collectOptions()` 中完成字段绑定；
  2. 修改 [src/cli/main.cpp](../../src/cli/main.cpp)：彻底拔除 `--sdr`、`--truehdr`、`--width`、`--height`、`--fps`、`--rife-model`、`--fruc-scene-threshold`、`--fruc`、`--test-gop-split`、`--fault-inject-frame`、`--daemon-http`；仅保留 12 个直观正交核心参数。

### Track B：NVOFA 硬件光流内聚与三桥合一

* **Task B1（光流并入 `rtx_ngx_bridge`，根治假插帧）**：
  1. 在 [include/shim/ngx_bridge_abi.h](../../include/shim/ngx_bridge_abi.h) 增设导出函数：
     ```c
     RTX_NGX_EXPORT int32_t rtx_ngx_bridge_interpolate_nv12(
         RtxNgxBridge* bridge, uint64_t prev_nv12, uint64_t curr_nv12,
         uint32_t pitch, float alpha, uint64_t out_nv12);
     ```
  2. 在 `src/shim/ngx_bridge/` 内部实现驱动级 `System32/nvofapi64.dll` 动态装载与双向 Warp 算子调用。**严禁在插帧函数中调用 `duplicateBypass`，插值帧必须有真实运动向量变形计算**；
  3. 物理删除独立插件：删除 [include/shim/fruc_bridge_abi.h](../../include/shim/fruc_bridge_abi.h)、[include/rtx/fruc_bridge.h](../../include/rtx/fruc_bridge.h)、[src/core/fruc_bridge.cpp](../../src/core/fruc_bridge.cpp)、`src/shim/backends/` 目录以及 [tests/test_fruc_bridge.cpp](../../tests/test_fruc_bridge.cpp)；
  4. 修改 [CMakeLists.txt](../../CMakeLists.txt)：移除 `rtx_fruc_bridge` 目标，移除 `cudart64_110.dll` 复制逻辑，顶层开启全局 `/MT`。

### Track C：全仓死代码物理拔除与核心文件脱水

* **Task C1（物理删除 15 个冗余死模块）**：
  1. 将 [src/core/color.cpp](../../src/core/color.cpp) 中唯一的 15 行 `floatToHalf()` 内联至 [src/core/lut3d.cpp](../../src/core/lut3d.cpp)；
  2. 从文件系统与 `CMakeLists.txt` 中**物理删除**：
     - `src/core/color.cpp`、`include/rtx/color.h`、`tests/test_color.cpp`
     - `src/core/vram_ring_pool.cpp`、`include/rtx/vram_pool.h`、`include/rtx/mpmc_ring.h`、`tests/test_vram.cpp`、`tests/test_concurrency.cpp`
     - `src/core/multi_gpu_scheduler.cpp`、`include/rtx/multi_gpu_scheduler.h`
     - `src/cli/daemon_rpc.cpp`、`include/rtx/daemon_rpc.h`、`tests/test_daemon_rpc.cpp`
     - `src/shim/ngx_bridge/ngx_cuda_bridge_linux.cpp`
* **Task C2（四大核心文件脱水大扫除）**：
  1. [src/core/hw_transcode.cpp](../../src/core/hw_transcode.cpp)：剔除 `runNvidiaContainerTranscode`、ProRes/DNxHR CPU 软解分支、TDR 故障注入桩，使用 C++26 指定初始化器聚合结构体；
  2. [src/shim/ngx_bridge/d3d12_interop.cpp](../../src/shim/ngx_bridge/d3d12_interop.cpp)：移除 CPU 纹理读写与 `run_interop_selftest`（净砍 ~475 行）；
  3. [src/io/ffmpeg_io.cpp](../../src/io/ffmpeg_io.cpp)：移除 `FfmpegSoftDecoder` 与 CPU 双线性下采样（净砍 ~215 行）；
  4. [src/core/sei_synthesizer.cpp](../../src/core/sei_synthesizer.cpp) & [src/shim/loader/nv_runtime.cpp](../../src/shim/loader/nv_runtime.cpp)：移除私有二进制格式、手写 JSON 与 `loadTensorRt`/`loadNvOfruc`。

---

## 四、 实测转码与重构验收量化门禁（Acceptance Gates）

执行工程师必须在真实机器上逐一跑通以下 4 大门禁，并附带可复现命令与完整遥测输出：

### 门禁 1：零配置默认 60 帧实测转码门禁（核心红线）
* **测试命令**：
  ```pwsh
  # 严禁传入 --fruc、--target-fps、--vsr-quality、--truehdr 等任何非必要参数！
  .\build\cmake\windows-msvc-ninja\CineRTX.exe --input .vs\gemini_generated_video_6eaef4a7.mp4 --output .vs\test_rework_golden_60fps.mp4
  ```
* **合格指标**：
  - `ffprobe` 检验输出文件：`width=3840`、`height=2160`、`r_frame_rate=60/1`、`codec_name=av1`；
  - 帧数契约：240 帧 24fps 10 秒输入 $\to$ 输出**严格为约 580~600 帧**（时长与原片误差 $< 0.1$ 秒，无快进）；
  - **真插帧像素差异度核验**：抽检第 $N+0.5$ 插值帧与第 $N$ 帧，**像素差分绝对值之和（SAD）严禁为 0**（杜绝 `duplicateBypass` 复制帧假插帧）。

### 门禁 2：全仓死代码与非法参数零残留门禁
* **测试命令**：
  ```pwsh
  rg "(--sdr|--truehdr|--4k-profile|--no-cuda-graph|--test-gop-split|--fault-inject-frame|--rife-model|TensorRt|MpmcBoundedQueue|SurfacePool)" src/ include/
  ```
* **合格指标**：匹配结果必须**严格为 0 项**。

### 门禁 3：代码体量硬指标门禁（压降 40%+）
* **测试命令**：
  ```pwsh
  (Get-ChildItem -Path src, include -Recurse -Include *.cpp, *.h, *.cu, *.cuh | Get-Content | Measure-Object -Line).Lines
  ```
* **合格指标**：`src/` 与 `include/` 代码总行数必须 **$\le 7,800$ 行**，且单文件最大行数 $\le 1,300$ 行。

### 门禁 4：PE 导入表与交付物纯净度门禁
* **测试命令**：
  ```pwsh
  pwsh tools/audit_imports.ps1
  ```
* **合格指标**：
  - `CineRTX.exe` 静态导入中 `MSVCP140.dll` / `VCRUNTIME140*.dll` / `WS2_32.dll` 计数均为 **0**；
  - 发布目录中第三方动态库 `cudart64_*.dll` 与 `rtx_fruc_bridge.dll` 计数均为 **0**；
  - 仅保留 `rtx_ngx_bridge.dll` 唯一自研桥接。

---

> [!NOTE]
> 本次返工任务书完全吸收了 [docs/phase8/CineRTX 最新代码深度验收报告暨“开箱即用 60 帧 + 大刀阔斧极简瘦身”全栈重构方案.md](./CineRTX%20最新代码深度验收报告暨“开箱即用%2060%20帧%20+%20大刀阔斧极简瘦身”全栈重构方案.md) 的核心精髓，将“开箱即用 60 帧”与“大刀阔斧削减 45% 代码量”合并为一次系统级重构，避免未来反复返工。

> [!TIP]
> 实施 Track B 时，`rtx_ngx_bridge.dll` 已经拥有成熟的 CUDA Context 托管机制，直接将驱动原生 `System32/nvofapi64.dll` 的调用内聚进来，是消灭独立 DLL 和避免多重 Context 冲突的最优解。

> [!IMPORTANT]
> **返工纪律与约束**：
> 1. 执行工程师必须在**本任务书指导下严格施工**，严禁搞“只加不减”的面条代码开发；
> 2. 施工期间与验收前提下，**严禁执行任何 `git commit` 或 `git push`**；
> 3. 完成 Track A~D 施工后，必须自行完成**门禁 1 ~ 门禁 4 全量实机取证**，在更新的 `docs/phase8/report.md` 中完整呈现真实数据，再提请委员会二次验收！