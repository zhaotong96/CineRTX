# CineRTX 最新代码深度验收报告暨“开箱即用 60 帧 + 大刀阔斧极简瘦身”全栈重构方案

**文档标识**：`VERDICT-PHASE8-ACCEPTANCE-AND-RADICAL-SLIMMING-001`  
**审计基线**：2026-11-12 最新同步代码库（含 Phase 7 Wave 1~4 与 Phase 8 FRUC Wave 1~4 落地变更，见 [`docs/phase8/report.md`](./report.md)）  
**核心目标**：  
1. **穿透式代码验收**：查明为什么 Phase 8 竣工后“默认转码仍然不是 60 帧”，以及为什么全仓代码量再次反弹膨胀至 **~17,500 行（约 660 KB 源码）**。  
2. **开箱即用最佳体验（Zero-Config Best Experience）**：无论用户从 GUI 拖入视频点开始，还是从 CLI 仅传 `--input in.mp4 --output out.mp4`，无需拨动任何开关，**100% 默认输出 4K UHD + 60 FPS（NVOFA 硬件光流）+ VSR Ultra（Level 4）+ TrueHDR 1000 nits + AV1 10-bit**。  
3. **大刀阔斧代码精简（Radical Codebase Slimming）**：遵循 **KISS 原则、奥卡姆剃刀定律（如无必要，勿增实体）、单一事实源（SSOT）与现代 C++26 高可读性规范**，直接砍除 **15 个冗余/死代码文件与 ~7,800 行（超过 45%）的架构泡沫**，将核心生产代码压回 **7,500 行以内**的精悍体量。

## 一、 最新代码深度验收：两大核心问题根因取证

### 1.1 为什么“目前默认转码仍然不是 60 帧”？（四道阻断锁死定位）

虽然 [`docs/phase8/report.md`](./report.md) 报告在测试中跑通了 `24fps -> 60fps` 插帧，但那是在测试命令中**显式传入了 `--fruc nvof --target-fps 60`** 才触发的。在真实用户的默认使用路径上，以下 **4 道代码级阻断** 导致默认转码依然是原帧率（24 帧进 → 24 帧出）：

| 阻断层级 | 源码定位 | 当前错误代码现状 | 导致的实际后果 |
| :--- | :--- | :--- | :--- |
| **阻断 1：底层结构体默认关闭补帧** | [`include/rtx/hw_transcode.h`](../../include/rtx/hw_transcode.h)（第 79–80 行） | `FrucMode fruc_mode = FrucMode::Off;`<br>`Rational target_fps{0, 1};` | 违背了 [`docs/phase8/CineRTX_Phase8_双路补帧架构工程线索与实机环境证据报告.md`](./CineRTX_Phase8_双路补帧架构工程线索与实机环境证据报告.md) 中“默认开启 `FrucMode::Auto`”的指令，默认直接走 1:1 透传分支。 |
| **阻断 2：CLI 参数层默认关闭补帧** | [`src/cli/main.cpp`](../../src/cli/main.cpp)（第 86–87 行） | `FrucMode fruc_mode = FrucMode::Off;`<br>`Rational target_fps{0, 1};` | 用户执行 `CineRTX --input in.mp4 --output out.mp4` 时，补帧器完全不启动，24fps 进去出来仍是 24fps。 |
| **阻断 3：GUI 界面层完全未接线** | [`src/gui/rtx_gui.cpp`](../../src/gui/rtx_gui.cpp)（第 354–426 行 `collectOptions`） | 既没有补帧控件，也没有在 `collectOptions()` 中给 `opt.fruc_mode` 或 `opt.target_fps` 赋值 | **GUI 用户永远无法获得 60 帧输出**！在 GUI 里点“开始转码”100% 沿用 `FrucMode::Off`，24fps 进去出来永远是 24fps。 |
| **阻断 4：帧率推导函数算成了 48 帧而非 60 帧** | [`src/core/hw_transcode.cpp`](../../src/core/hw_transcode.cpp)（第 201–215 行 `chooseFrameRate`） | 当 `target_fps` 为 `{0, 1}` 时，对 `<= 30.5 fps` 的源执行 `Rational{base.num * 2, base.den}` | 即便把 `fruc_mode` 改成 `Auto`，24fps 视频进去算出来也是 **48fps**（23.976fps 算出来是 47.952fps，25fps 算出来是 50fps），**根本不是 60 帧**！ |

此外，影响“开箱即用最佳体验”的还有两处未对齐的次优默认值：
* **VSR 画质档位默认仍是 Level 2 而非 Level 4（Ultra）**：在 [`include/rtx/hw_transcode.h`](../../include/rtx/hw_transcode.h)（第 62 行）、[`src/cli/main.cpp`](../../src/cli/main.cpp)（第 61 行）与 [`src/gui/rtx_gui.cpp`](../../src/gui/rtx_gui.cpp)（第 231 行 `selVsr = 1`）中，NGX VSR 默认档位全部停留在 `2`，未发挥 RTX 显卡最高画质档位 `4 (Ultra)` 的实力。
* **构建脚本重新混入了 `cudart64_110.dll` 与 `NvOFFRUC.dll`**：在 [`CMakeLists.txt`](../../CMakeLists.txt)（第 494–498 行）中，竟然把旧版 CUDA 11 运行时 `cudart64_110.dll` 和外部 `NvOFFRUC.dll` 塞进了发布包复制列表，一旦脱离开发机 `build/external/` 目录，光流即告失效并退化为复制帧（假 60 帧、真 24 帧卡顿）。

### 1.2 为什么“目前的代码量又膨胀了”？（全仓 660 KB / ~17,500 行膨胀病灶拆解）

在 Phase 6 Wave 1 瘦身时，代码库曾被压缩至约 6,800 行。然而经过 Phase 7、Phase 8、Phase 9 的多轮叠加，执行团队在“不删旧代码、只往上叠新抽象和新分支”的惯性下，导致全仓 C++/CUDA 源码与测试再次膨胀至 **~660 KB（约 17,500 行，其中 `src/` + `include/` 约 13,200 行，`tests/` 约 4,300 行）**。

通过全仓逐文件穿透式审计，我定位出导致代码膨胀的 **7 大核心病灶（共计可净砍 ~7,800 行代码）**：

| 膨胀病灶编号与模块 | 涉及文件与当前体量 | 为什么它是违背 KISS / 奥卡姆剃刀的冗余膨胀？ | 精简目标（净砍行数） |
| :--- | :--- | :--- | :--- |
| **病灶 1：完全未被调用的“显存环池与三级水位控制器”** | [`src/core/vram_ring_pool.cpp`](../../src/core/vram_ring_pool.cpp) (21.3 KB)<br>[`include/rtx/vram_pool.h`](../../include/rtx/vram_pool.h) (14.4 KB)<br>[`include/rtx/mpmc_ring.h`](../../include/rtx/mpmc_ring.h) (6.3 KB)<br>[`tests/test_vram.cpp`](../../tests/test_vram.cpp) (20.0 KB)<br>[`tests/test_concurrency.cpp`](../../tests/test_concurrency.cpp) (5.6 KB) | **100% 悬空死代码（共 67.6 KB / ~1,820 行）**！全仓 [`src/core/hw_transcode.cpp`](../../src/core/hw_transcode.cpp) 根本没有 `#include "rtx/vram_pool.h"`，也没有 `#include "rtx/mpmc_ring.h"`！NVDEC 自己在解码器内管槽位，NGX Bridge 自己管 `p010_ring[3]`，主循环用的是 20 行 `std::deque`。这 5 个文件纯粹是“自产自测”的空中楼阁，生产环境 0 调用！ | **整文件拔除 5 个文件**<br>**净砍 ~1,820 行** |
| **病灶 2：完全未被调用的“CPU 浮点色彩数学库”** | [`src/core/color.cpp`](../../src/core/color.cpp) (18.8 KB)<br>[`include/rtx/color.h`](../../include/rtx/color.h) (3.4 KB)<br>[`tests/test_color.cpp`](../../tests/test_color.cpp) (14.3 KB) | **97% 悬空死代码（共 36.5 KB / ~1,010 行）**！CineRTX 是纯 GPU 管线，PQ/BT.709/BT.2020 转换全部在 GPU 内完成。整个 `src/` 中唯一用到的只有 [`src/core/lut3d.cpp`](../../src/core/lut3d.cpp) 调用的一个 15 行 `floatToHalf()` 函数，其余 500 行 CPU 色彩转换与双线性采样函数在生产代码中 0 调用！ | **将 15 行 `floatToHalf` 内联至 `lut3d.cpp`，整文件拔除 3 个文件**<br>**净砍 ~990 行** |
| **病灶 3：写了却根本没接线的“多 GPU GOP 切片调度器”** | [`src/core/multi_gpu_scheduler.cpp`](../../src/core/multi_gpu_scheduler.cpp) (16.3 KB)<br>[`include/rtx/multi_gpu_scheduler.h`](../../include/rtx/multi_gpu_scheduler.h) (2.7 KB) | **100% 悬空死代码（共 19.0 KB / ~510 行）**！[`src/cli/main.cpp`](../../src/cli/main.cpp) 虽然包含了头文件并解析了 `--test-gop-split`，但在 `runHardwareMode()` 中根本没有调用 `MultiGpuScheduler`，GUI 也没有调用它！ | **整文件拔除 2 个文件**<br>**净砍 ~510 行** |
| **病灶 4：三套 Bridge / 三套 Loader 重复造轮子与 TensorRT/RIFE 复辟** | [`include/shim/fruc_bridge_abi.h`](../../include/shim/fruc_bridge_abi.h) (5.3 KB)<br>[`include/rtx/fruc_bridge.h`](../../include/rtx/fruc_bridge.h) (0.9 KB)<br>[`src/core/fruc_bridge.cpp`](../../src/core/fruc_bridge.cpp) (5.0 KB)<br>`src/shim/backends/offruc/offruc_bridge.cpp`<br>[`tests/test_fruc_bridge.cpp`](../../tests/test_fruc_bridge.cpp) (10.7 KB)<br>[`src/core/nvidia_shim.cpp`](../../src/core/nvidia_shim.cpp) (13.3 KB)<br>[`src/core/ngx_remaster.cpp`](../../src/core/ngx_remaster.cpp) (12.6 KB)<br>[`src/shim/loader/nv_runtime.cpp`](../../src/shim/loader/nv_runtime.cpp) (13.1 KB) | 1. **一模一样的 `widen()`、`executableDirectory()`、`LoadLibraryExW` 样板代码在 5 个 `.cpp` 里复制粘贴了 5 遍**。<br>2. `rtx_shim` 早已静态内联（`RTX_EMBEDDED_SHIM=1`），但 [`src/core/nvidia_shim.cpp`](../../src/core/nvidia_shim.cpp) 仍保留着 300 行去磁盘找 `rtx_shim.dll` 的无用动态解析代码。<br>3. 为补帧单独新建了第 3 个 DLL（`rtx_fruc_bridge.dll`），还把已被裁撤的 TensorRT RIFE（`RTX_FRUC_BACKEND_TRT_RIFE`、`build_engine`、`--rife-model`）又塞了回来！ | **砍除 TensorRT/RIFE 分支；将 NVOFA 硬件光流直接并入 `rtx_ngx_bridge`；消灭 `rtx_fruc_bridge` 独立 DLL 与重复 Loader**<br>**净砍 ~1,450 行** |
| **病灶 5：`hw_transcode.cpp` 单文件膨胀至 92.4 KB（~2,350 行）** | [`src/core/hw_transcode.cpp`](../../src/core/hw_transcode.cpp) (92.4 KB) | 1. 第 715–926 行保留了 212 行非重制路径 `runNvidiaContainerTranscode`（注释自承非产品路径）。<br>2. 混入了约 220 行用于响应 `--fault-inject-frame` 测试桩的模拟 TDR 注入与恢复状态机。<br>3. 混入了约 120 行违背“纯 GPU 零拷贝”哲学的 ProRes/DNxHR CPU 软解上传分支。 | **拔除 Stage-1 非重制路径、拔除 CPU 软解、拔除故障注入桩，精简主循环**<br>**净砍 ~950 行** |
| **病灶 6：`d3d12_interop.cpp` (89.1 KB) 与 `ffmpeg_io.cpp` (68.9 KB) 夹带大量非生产自检与软解代码** | [`src/shim/ngx_bridge/d3d12_interop.cpp`](../../src/shim/ngx_bridge/d3d12_interop.cpp) (89.1 KB)<br>[`src/io/ffmpeg_io.cpp`](../../src/io/ffmpeg_io.cpp) (68.9 KB)<br>[`src/core/sei_synthesizer.cpp`](../../src/core/sei_synthesizer.cpp) (20.0 KB) | 1. `d3d12_interop.cpp` 第 685–1160 行包含 **475 行** CPU 纹理上传/下载与 `run_interop_selftest` 渐变图自检函数，生产零拷贝管线根本不用。<br>2. `ffmpeg_io.cpp` 第 688–900 行包含 **215 行** `FfmpegSoftDecoder` 与手写 CPU 双线性色度下采样。<br>3. `sei_synthesizer.cpp` 包含 **180 行** 从未被调用的 `"RTXH"` 私有二进制格式与手写 JSON 解析器。 | **砍除 `d3d12_interop.cpp` CPU 上传下载与自检、砍除 `FfmpegSoftDecoder`、砍除 `sei_synthesizer` 死函数**<br>**净砍 ~1,100 行** |
| **病灶 7：跨平台/守护进程伪需求膨胀（Linux 软超分桥接与手写 HTTP Socket 服务）** | [`src/shim/ngx_bridge/ngx_cuda_bridge_linux.cpp`](../../src/shim/ngx_bridge/ngx_cuda_bridge_linux.cpp) (24.9 KB)<br>[`src/cli/daemon_rpc.cpp`](../../src/cli/daemon_rpc.cpp) (12.4 KB)<br>[`include/rtx/daemon_rpc.h`](../../include/rtx/daemon_rpc.h) (2.1 KB)<br>[`tests/test_daemon_rpc.cpp`](../../tests/test_daemon_rpc.cpp) (3.8 KB) | CineRTX 的核心壁垒是 Windows 专属的 **NVIDIA RTX Video SDK (`nvsdk_ngx_s.lib` + D3D12 + Win32 GUI)**。为根本不存在的 Linux 无头容器维护 680 行 CUDA 手写 Bicubic 假超分桥接，以及为了 `--daemon-http` 引入 `WS2_32.dll` 和手写 HTTP/1.1 Socket 循环，严重违背奥卡姆剃刀原则（若需前后端通信，100 行 `--daemon-stdio` 管道即够用，甚至在双轨 UI 启动前完全无需提前塞入主分支）。 | **移除 Linux 假超分桥接与 HTTP Socket 守护分支（及 `ws2_32` 依赖）**<br>**净砍 ~1,200 行** |

## 二、 解决方案总纲：三大核心支柱

### 支柱一：确保用户“开箱即用，永远是最佳体验”（Zero-Config Golden Defaults）

将“最佳体验”直接刻入 [`include/rtx/hw_transcode.h`](../../include/rtx/hw_transcode.h) 的唯一事实源（SSOT）默认值中，消灭 CLI 与 GUI 的默认值分裂，确保用户**不传任何可选参数、不调任何下拉框**时，自动运行以下黄金管线：

```
输入任意视频 (23.976 / 24 / 25 / 29.97 / 30 / 50 fps, SDR 或 HDR)
   ↓ [NVDEC 硬件解码 (零主机拷贝 NV12/P010)]
   ↓ [NVOFA 驱动原生硬件光流补帧 (System32/nvofapi64.dll + 内嵌 PTX 双向 Warp 核) -> 锁定 60 FPS]
   ↓ [NGX VSR Level 4 (Ultra 最高画质档位) -> 4K UHD (3840x2160 / 竖屏自动 2160x3840)]
   ↓ [NGX TrueHDR 1000 nits (若源视频已是 HDR10/HLG 则自动旁路逆色调映射并保留原片 HDR 元数据)]
   ↓ [NVENC AV1 10-bit P5 硬件编码 (非 Ada/Blackwell 显卡自动平滑降级 HEVC Main10)]
输出 4K 60fps 10-bit HDR10 MP4/MKV (音轨与字幕无损流拷贝直通)
```

#### 关键代码级修正点：

1. **统一 `HwTranscodeOptions` 黄金默认值（[`include/rtx/hw_transcode.h`](../../include/rtx/hw_transcode.h)）**：
   * `VideoCodec codec = VideoCodec::AV1;`（默认 AV1 10-bit）
   * `int vsr_quality = 4;`（**从 `2` 提升为 `4` Ultra 最高画质**）
   * `int target_width = 3840; int target_height = 2160;`（默认 4K UHD，配合 `AutoDirect` 竖屏自适应为 `2160x3840`）
   * `bool fruc_enabled = true;`（**废除复杂的 `FrucMode { Off, Auto, NvOf, Rife }` 四态枚举，简化为单一 `bool fruc_enabled = true`！默认永远开启驱动原生 `nvofapi64.dll` 硬件光流补帧**）
   * `Rational target_fps{60, 1};`（**默认目标帧率直接锁定为 `60/1` 即 60 FPS**，不再使用 `{0, 1}` 模糊占位）

2. **修正 `chooseFrameRate` 帧率决策逻辑（[`src/core/hw_transcode.cpp`](../../src/core/hw_transcode.cpp) 第 201–215 行）**：
   ```cpp
   Rational chooseFrameRate(const HwTranscodeOptions& options, const MediaInfo& info) {
     const Rational src_fps = (info.frame_rate.valid() && info.frame_rate.num > 0)
                                  ? info.frame_rate
                                  : Rational{24, 1};
     if (!options.fruc_enabled) {
       return src_fps;
     }
     const Rational desired = (options.target_fps.valid() && options.target_fps.num > 0)
                                  ? options.target_fps
                                  : Rational{60, 1};
     // 若源视频帧率已达到或超过目标帧率（如原生 60fps / 120fps），保持源帧率直通；否则升频至 desired (默认 60fps)
     return (src_fps.toDouble() >= desired.toDouble() - 0.05) ? src_fps : desired;
   }
   ```
   * **效果**：23.976fps、24fps、25fps、29.97fps、30fps、50fps 视频拖进来，**100% 自动升频输出为丝滑的 60.0 FPS**；原生 60fps/120fps 视频拖进来，自动免插帧直通！

3. **HDR 源视频智能免配置感知（消灭 `--sdr` 和 `--truehdr` 开关）**：
   * 在 [`src/core/hw_transcode.cpp`](../../src/core/hw_transcode.cpp) 配置阶段，直接读取 `info_.hdr_source`（[`src/io/ffmpeg_io.cpp`](../../src/io/ffmpeg_io.cpp) 已实装自动探测 SMPTE2084 PQ / HLG）：
     * 若输入是 **SDR 源**（BT.709）：自动激活 `enable_truehdr = 1`（SDR → HDR10 1000 nits 逆色调映射）。
     * 若输入本身已是 **HDR10 / HLG 源**：自动将 `enable_truehdr = 0`（仅执行 NVOFA 60fps 补帧 + VSR 4K 超分，并原样透传原片 HDR10 元数据，防止对 HDR 源二次套用 TrueHDR 导致过曝）。
     * 若用户在编码器中选择了仅支持 8-bit 的 `H.264`：自动以 8-bit SDR 输出。**全程零开关、零心智负担！**

4. **GUI 补帧与默认档位同步（[`src/gui/rtx_gui.cpp`](../../src/gui/rtx_gui.cpp)）**：
   * 将 VSR Quality 下拉框默认选中项从 `selVsr = 1`（Level 2）改为 `selVsr = 3`（**Level 4 Ultra**）。
   * 新增一个极简的目标帧率下拉框 `IDC_CMB_FPS`（默认选中 **`60 FPS (NVOFA 硬件光流补帧 - 默认推荐)`**，备选 `120 FPS` 与 `保持原片帧率 (关闭补帧)`），并在 `collectOptions()` 中直接绑定到 `opt.fruc_enabled` 与 `opt.target_fps`。

### 支柱二：大刀阔斧的代码精简方案（净砍 ~7,800 行，削减率 > 45%）

遵循 **“如无必要，勿增实体（Occam's Razor）”** 与 **“拔除所有未接线代码、拔除所有 CPU 模拟参考实现、拔除所有为未来假想保留的过度抽象”** 的铁律，执行以下 **三大外科手术式清理**：

#### 手术 1：整文件拔除 15 个悬空/冗余模块（立减 ~4,950 行）

| 序号 | 待删除文件路径 | 当前字节数 / 行数 | 删除理由（精准代码级定性） |
| :---: | :--- | :---: | :--- |
| 1 | [`src/core/vram_ring_pool.cpp`](../../src/core/vram_ring_pool.cpp) | 21,345 B (~580 行) | 生产路径 0 调用。NVDEC 与 NGX Bridge 各自管理显存环，无需此中间层。 |
| 2 | [`include/rtx/vram_pool.h`](../../include/rtx/vram_pool.h) | 14,448 B (~390 行) | 同上，`hw_transcode.cpp` 甚至未 `#include` 该头文件。 |
| 3 | [`include/rtx/mpmc_ring.h`](../../include/rtx/mpmc_ring.h) | 6,315 B (~180 行) | 仅被未使用的 `SurfacePool` 包含，生产主循环使用标准库 `std::deque` + `std::mutex`。 |
| 4 | [`tests/test_vram.cpp`](../../tests/test_vram.cpp) | 19,976 B (~520 行) | 仅为测试已废弃的 `SurfacePool` 而存在的自嗨测试。 |
| 5 | [`tests/test_concurrency.cpp`](../../tests/test_concurrency.cpp) | 5,593 B (~150 行) | 仅为测试已废弃的 `MpmcBoundedQueue` 而存在的测试。 |
| 6 | [`src/core/color.cpp`](../../src/core/color.cpp) | 18,805 B (~520 行) | 纯 CPU 软件色彩转换参考实现；将 15 行 `floatToHalf` 移入 [`src/core/lut3d.cpp`](../../src/core/lut3d.cpp) 后即可彻底删除。 |
| 7 | [`include/rtx/color.h`](../../include/rtx/color.h) | 3,449 B (~100 行) | 同上，拔除 CPU 色彩库头文件。 |
| 8 | [`tests/test_color.cpp`](../../tests/test_color.cpp) | 14,318 B (~390 行) | 同上，拔除 CPU 色彩库单测。 |
| 9 | [`src/core/multi_gpu_scheduler.cpp`](../../src/core/multi_gpu_scheduler.cpp) | 16,288 B (~430 行) | 在 `main.cpp` 与 `rtx_gui.cpp` 中 0 调用，属于典型的未接线过度设计。 |
| 10 | [`include/rtx/multi_gpu_scheduler.h`](../../include/rtx/multi_gpu_scheduler.h) | 2,652 B (~80 行) | 同上。 |
| 11 | [`src/shim/ngx_bridge/ngx_cuda_bridge_linux.cpp`](../../src/shim/ngx_bridge/ngx_cuda_bridge_linux.cpp) | 24,928 B (~680 行) | CineRTX 是基于 Windows D3D12 + NVIDIA RTX Video SDK 的桌面级产品，无需维护 Linux 假超分桥接。 |
| 12 | [`src/cli/daemon_rpc.cpp`](../../src/cli/daemon_rpc.cpp) | 12,404 B (~350 行) | 手写 HTTP Socket 守护进程，不仅增加 350 行代码还拉入了 `WS2_32.dll` 依赖。 |
| 13 | [`include/rtx/daemon_rpc.h`](../../include/rtx/daemon_rpc.h) | 2,116 B (~70 行) | 同上。 |
| 14 | [`tests/test_daemon_rpc.cpp`](../../tests/test_daemon_rpc.cpp) | 3,780 B (~110 行) | 同上。 |
| 15 | [`docs/TODO/rife_cli_test_fixture.md`](../TODO/rife_cli_test_fixture.md) 及 `scratch/` 目录 | ~2,600 B | 彻底清除 ONNX/RIFE 历史遗留文档与临时草稿目录。 |

#### 手术 2：合并桥接层（消灭 `rtx_fruc_bridge.dll` 与 TensorRT RIFE，三桥合一，立减 ~1,200 行）

当前仓库同时维护着 **3 套 C ABI 与加载器**：`NvidiaShim`（静态内联却保留着动态加载代码）、`NgxRemasterBridge`（加载 `rtx_ngx_bridge.dll`）、`FrucBridgeLoader`（加载 `rtx_fruc_bridge.dll`）。
* **砍除 RIFE / TensorRT 路线 B**：既然用户核心诉求是“开箱即用、代码精简、零外部 1.4GB 依赖”，保留需要用户自己去找 `.onnx`/`.engine` 模型和 200MB TensorRT DLL 的 RIFE 路线完全违背奥卡姆剃刀原则。**只保留驱动原生零依赖的 NVOFA 硬件光流（`System32/nvofapi64.dll` + PTX 双向 Warp 核）作为唯一补帧引擎**！
* **将 NVOFA 光流插帧直接并入 `rtx_ngx_bridge.dll`**：
  * `rtx_ngx_bridge.dll` 原本就已经加载了 `nvcuda.dll`、持有了 `CUcontext` / `CUstream` 并管理着 PTX 模块加载（[`src/shim/ngx_bridge/p010_pack_runtime.cpp`](../../src/shim/ngx_bridge/p010_pack_runtime.cpp)）。
  * 只需在 [`include/shim/ngx_bridge_abi.h`](../../include/shim/ngx_bridge_abi.h) 中增加 1 个函数 `rtx_ngx_bridge_interpolate_nv12(...)`，在 `rtx_ngx_bridge.dll` 内部用 180 行代码直调 `System32/nvofapi64.dll` + PTX Warp 核即可！
  * **直接删除** [`include/shim/fruc_bridge_abi.h`](../../include/shim/fruc_bridge_abi.h)、[`include/rtx/fruc_bridge.h`](../../include/rtx/fruc_bridge.h)、[`src/core/fruc_bridge.cpp`](../../src/core/fruc_bridge.cpp) 以及独立的 `rtx_fruc_bridge` CMake 目标！
* **删除 `nvidia_shim.cpp` 中冗余的动态 DLL 查找代码**：因为 `rtx_shim` 已 100% 静态链接进 `rtx_core`，[`src/core/nvidia_shim.cpp`](../../src/core/nvidia_shim.cpp) 直接绑定 `embeddedShimApi()` 即可，从 350 行直接浓缩为 **60 行**！
* **删除 [`src/shim/loader/nv_runtime.cpp`](../../src/shim/loader/nv_runtime.cpp) 中的 `loadTensorRt` 与 `loadNvOfruc`**：砍掉 120 行探测废弃 DLL 的死代码。

#### 手术 3：四大核心大文件内部脱水（立减 ~1,650 行）

1. **[`src/core/hw_transcode.cpp`](../../src/core/hw_transcode.cpp)（从 2,350 行压降至 ~1,100 行，净砍 ~1,250 行）**：
   * 删除 `runNvidiaContainerTranscode()`（第 715–926 行，212 行非重制死代码；相关测试直接合并到 `runNvidiaRemasterTranscode`）。
   * 删除 `soft_decode_`、`processSoftFrame()` 及 ProRes/DNxHR CPU 软解分支（120 行，坚守纯 GPU 零拷贝定位）。
   * 删除 `fault_inject_frame`、`recoverAfterDeviceLost`、`recreateEncoderAfterDeviceLost`、`recovery_generation` 等为单测故障注入服务的复杂状态机（220 行；硬件级防 TDR 依靠 `d3d12_interop.cpp` 已实装的 `DXGIDeclareAdapterRemovalSupport` + 队列深度 <= 2 门限 + 高优先级 Pack Stream 即可稳如磐石）。
   * 将 `runScrubPreview()` 与主会话初始化逻辑复用，消除 100 行重复的 `decoderSubmit`/`remaster` 样板代码。

2. **[`src/shim/ngx_bridge/d3d12_interop.cpp`](../../src/shim/ngx_bridge/d3d12_interop.cpp)（从 2,420 行压降至 ~1,650 行，净砍 ~770 行）**：
   * 删除 `D3D12Interop::upload()`、`D3D12Interop::download()` 以及 `run_interop_selftest()`（第 685–1160 行，共 475 行 CPU 读写测试代码，生产管线全走 CUDA External Memory 零拷贝，从不调用这几个函数）。

3. **[`src/io/ffmpeg_io.cpp`](../../src/io/ffmpeg_io.cpp)（从 1,900 行压降至 ~1,550 行，净砍 ~350 行）**：
   * 删除第 688–900 行的 `FfmpegSoftDecoder` 与 CPU 端 `convertFrameToP010()` 双线性下采样器（215 行）。
   * 删除 `FfmpegMuxer::open` 中第 465–512 行的 `// Legacy behaviour` 兼容回退分支（48 行）。

4. **[`src/core/sei_synthesizer.cpp`](../../src/core/sei_synthesizer.cpp)（从 550 行压降至 ~280 行，净砍 ~270 行）**：
   * 删除未使用的 `makeNvencSettings`、`prefixSeiNal`、`av1MetadataObu`、`containerSideData`（私有 `"RTXH"` 格式）、`toJson` 与 `fromJson`。

### 支柱三：回归 KISS 与奥卡姆剃刀设计哲学（CLI/GUI 极简契约与 C++26 规范）

#### 1. CLI 参数表大清洗（从 29 个杂乱开关精简为 12 个核心正交参数）

在 [`src/cli/main.cpp`](../../src/cli/main.cpp) 中，**拔除以下 14 个违反 KISS 原则或早已失效的开关**：
* **拔除色彩矛盾开关**：`--sdr`、`--truehdr`（由引擎根据源视频色彩特性自动决策：SDR 源自动开启 TrueHDR 10-bit 重制，HDR10/HLG 源自动透传原片 HDR 元数据）。
* **拔除僵尸死开关**：`--4k-profile`、`--no-cuda-graph`。
* **拔除误导性输入几何开关**：`--width`、`--height`、`--fps`（输入分辨率和输入帧率 100% 由 FFmpeg 解封装器从源视频头部精准读取，绝不让用户手填源视频宽高）。
* **拔除 RIFE/过度调参开关**：`--rife-model`、`--fruc-scene-threshold`、`--fruc`（简化为默认开启 60fps 硬件光流补帧；若用户想关闭补帧或指定其他帧率，仅需传 `--target-fps 0`（保持原帧率）或 `--target-fps 60` / `120`）。
* **拔除内部测试桩与守护进程开关**：`--test-gop-split`、`--fault-inject-frame`、`--daemon-http`。

清洗后的 `CineRTX` 命令行仅保留 **12 个极其直观、正交的参数**：
* 核心 I/O：`--input <path>`、`--output <path>`
* 编码控制：`--codec av1|hevc|h264`（默认 `av1`）、`--preset 1..7`（默认 `5`）、`--rate-control cbr|vbr|qp`（默认 `cbr`）、`--bitrate <kbps>`、`--qp <0..51>`
* 画质与帧率重制：`--target-width <W>` / `--target-height <H>`（默认 `3840x2160`）、`--target-fps <fps>`（默认 `60`，传 `0` 保持源帧率）、`--vsr-quality 1..4`（默认 `4` Ultra）、`--peak-nits 400..4000`（默认 `1000`）
* 高级色彩与辅助：`--lut <file.cube>`、`--hdr10plus`、`--pillarbox`、`--no-audio`、`--probe`、`--json`、`--lang zh|en`

#### 2. 全仓 C++26 指定初始化器（Designated Initializers）与 100% RAII 贯彻

在 [`src/core/hw_transcode.cpp`](../../src/core/hw_transcode.cpp) 中，当前仍有大量 C 风格逐字段赋值块（如 `RtxNgxBridgeConfig`、`RtxDecoderConfig`、`RtxEncoderConfig`、`RtxNgxRemasterInput` 连续 15 行 `config.xxx = yyy;`）。全部重构为 C++26 聚合指定初始化（Designated Initializers）：

```cpp
const RtxEncoderConfig config{
    .codec = encoderCodec(options_.codec),
    .width = target_width_,
    .height = target_height_,
    .fps_num = static_cast<int32_t>(frame_rate_.num),
    .fps_den = static_cast<int32_t>(frame_rate_.den),
    .bitrate_kbps = options_.bitrate_kbps,
    .preset = std::clamp(options_.preset, 1, 7),
    .hdr = toShimHdr(hdr_),
    .input_format = RTX_PIXEL_P010,
    .gop_length = static_cast<int32_t>(std::max<int64_t>(1, frame_rate_.num > 0 ? frame_rate_.den * 2 : 60)),
    .rate_control = std::clamp(options_.rate_control, 0, 2),
    .qp = options_.qp,
    .device_index = options_.device_index,
};
```
这不仅消除了未初始化成员隐患，还能在每个结构体构造点直接砍掉 40% 的样板行数。

## 三、 SuperPower 落地执行方案（Plan / Tasks / Checklists）

### 3.1 Plan（架构契约与瘦身前后对比指标）

| 量化维度 | 当前代码库状态（Phase 8 刚交付） | 本方案落地后目标值 | 收益说明 |
| :--- | :--- | :--- | :--- |
| **开箱默认输出规格** | 4K + **24fps（未补帧）** + VSR **Level 2** + AV1 10-bit | 4K + **60fps（NVOFA 硬件光流）** + VSR **Level 4 (Ultra)** + AV1 10-bit | 拖入视频直接获得满血 4K60 HDR10 母版体验 |
| **全仓源码文件数** | 64 个（含 `src/`、`include/`、`tests/`） | **46 个（净删 18 个文件）** | 消除所有悬空模块、三桥重复加载器与死测试 |
| **全仓代码总行数 (LOC)** | ~17,500 行（~660 KB） | **~9,600 行（其中核心生产代码 ~7,200 行，净砍 ~7,900 行）** | 代码量削减 **45%+**，单文件最大不超过 1,200 行 |
| **外部插件 DLL 数量** | 2 个自研桥接（`rtx_ngx_bridge.dll` + `rtx_fruc_bridge.dll`）+ 混入 `cudart64_110.dll` | **仅 1 个自研桥接（`rtx_ngx_bridge.dll`）** + `nvngx_vsr.dll` / `nvngx_truehdr.dll` | 零 `cudart`、零 `TensorRT`、零 `rtx_fruc_bridge.dll` |
| **`CineRTX.exe` PE 静态导入** | 16 个 DLL（含 `MSVCP140`、`VCRUNTIME140*`、`WS2_32`、`d3d12`、`dxgi`） | **8 个纯 Windows 系统原生 DLL**（全局 `/MT` 静态 CRT） | 彻底摆脱 VC++ 运行库弹窗与 Winsock/D3D12 静态泄漏 |

### 3.2 Tasks（按轨拆解的原子施工任务）

#### Track A：开箱即用 60 帧与黄金默认值闭环（涉及 `hw_transcode.h`、`hw_transcode.cpp`、`main.cpp`、`rtx_gui.cpp`）

* **Task A1（锁定黄金默认值 SSOT）**：修改 [`include/rtx/hw_transcode.h`](../../include/rtx/hw_transcode.h)：
  1. 删除 `enum class FrucMode`、`rife_model_path`、`fruc_scene_threshold`、`seek_start_ms`、`fault_inject_frame`。
  2. 设置 `int vsr_quality = 4;`、`bool fruc_enabled = true;`、`Rational target_fps{60, 1};`。
  3. 删除 `runNvidiaContainerTranscode` 声明。
* **Task A2（修正 60fps 帧率推导与 HDR 自动感知）**：修改 [`src/core/hw_transcode.cpp`](../../src/core/hw_transcode.cpp)：
  1. 在 `chooseFrameRate()` 中，当 `options.fruc_enabled` 为 `true` 时，若源视频帧率 `< options.target_fps`（默认 60fps），直接返回 `options.target_fps`（`Rational{60, 1}`），彻底终结 `24 -> 48` 的半吊子翻倍。
  2. 在 `NvidiaTranscodeSession::configure()` 中，当 `info_.hdr_source` 为 `true`（源视频已是 HDR10/HLG）时，自动将 `bridge_config.enable_truehdr = 0` 并沿用 `info_.hdr` 元数据；当源视频为 SDR 时，自动启用 `enable_truehdr = 1`。
* **Task A3（GUI 补帧控件接线与 VSR Level 4 对齐）**：修改 [`src/gui/rtx_gui.cpp`](../../src/gui/rtx_gui.cpp)：
  1. `populateComboBoxes()` 中 VSR 默认选中索引改为 `selVsr = 3`（`Level 4 (Ultra Quality)`）。
  2. 移除 `IDC_CHK_TRUEHDR` 复选框（色彩空间由引擎自动感知，不再让用户手动开关 SDR/HDR），在该位置替换为 `IDC_CMB_FPS`（目标帧率下拉框：`60 FPS (NVOFA 硬件光流 - 默认)` / `120 FPS (NVOFA 极速高帧率)` / `保持源视频帧率 (关闭补帧)`）。
  3. 在 `collectOptions()` 中将 `IDC_CMB_FPS` 映射至 `opt.fruc_enabled` 与 `opt.target_fps`。

#### Track B：NVOFA 硬件光流内聚与桥接层归一（涉及 `ngx_bridge_abi.h`、`bridge_exports.cpp`、`CMakeLists.txt`）

* **Task B1（NVOFA 并入 `rtx_ngx_bridge`）**：
  1. 在 [`include/shim/ngx_bridge_abi.h`](../../include/shim/ngx_bridge_abi.h) 中新增 `rtx_ngx_bridge_interpolate(RtxNgxBridge* bridge, uint64_t prev_nv12, uint64_t curr_nv12, uint32_t pitch, float alpha, uint64_t* out_nv12)`。
  2. 将 `offruc_bridge.cpp` 中直调 `System32/nvofapi64.dll`（或回退插值）的精简逻辑（~180 行，彻底剥离 TensorRT RIFE 和 `build_engine`）迁入 `src/shim/ngx_bridge/`，复用 `D3D12Interop` 已激活的 `CUcontext` 与 `nvcuda.dll` 函数表。
  3. 物理删除 [`include/shim/fruc_bridge_abi.h`](../../include/shim/fruc_bridge_abi.h)、[`include/rtx/fruc_bridge.h`](../../include/rtx/fruc_bridge.h)、[`src/core/fruc_bridge.cpp`](../../src/core/fruc_bridge.cpp)、`src/shim/backends/` 目录及 [`tests/test_fruc_bridge.cpp`](../../tests/test_fruc_bridge.cpp)。
  4. 修改 [`CMakeLists.txt`](../../CMakeLists.txt)：删除 `rtx_fruc_bridge` / `rtx_fruc_bridge_static` 目标，删除第 494–498 行复制 `cudart64_110.dll` 的污染逻辑，并在顶层加入 `set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")` 统一静态 CRT（`/MT`）。

#### Track C：全仓死代码与过度抽象大扫除（15 个文件删除 + 4 个大文件脱水）

* **Task C1（物理删除悬空模块）**：
  1. 从 [`CMakeLists.txt`](../../CMakeLists.txt) 的 `RTX_CORE_SOURCES`、`RTX_CINERTX_SOURCES`、`RTX_TEST_SOURCES` 中移除并物理删除：
     * `src/core/color.cpp`、`include/rtx/color.h`、`tests/test_color.cpp`（先将 15 行 `floatToHalf` 放入 [`src/core/lut3d.cpp`](../../src/core/lut3d.cpp)）
     * `src/core/vram_ring_pool.cpp`、`include/rtx/vram_pool.h`、`include/rtx/mpmc_ring.h`、`tests/test_vram.cpp`、`tests/test_concurrency.cpp`
     * `src/core/multi_gpu_scheduler.cpp`、`include/rtx/multi_gpu_scheduler.h`
     * `src/cli/daemon_rpc.cpp`、`include/rtx/daemon_rpc.h`、`tests/test_daemon_rpc.cpp`（同时从 `CMakeLists.txt` 移除 `ws2_32` 链接）
     * `src/shim/ngx_bridge/ngx_cuda_bridge_linux.cpp`
* **Task C2（四大核心文件内部脱水）**：
  1. [`src/core/hw_transcode.cpp`](../../src/core/hw_transcode.cpp)：删除 `runNvidiaContainerTranscode`、删除 `soft_decode_` 分支、删除 `fault_inject_frame` 模拟恢复分支。
  2. [`src/shim/ngx_bridge/d3d12_interop.cpp`](../../src/shim/ngx_bridge/d3d12_interop.cpp)：删除 `upload`、`download`、`run_interop_selftest`。
  3. [`src/io/ffmpeg_io.cpp`](../../src/io/ffmpeg_io.cpp)：删除 `FfmpegSoftDecoder` 与 `convertFrameToP010`。
  4. [`src/core/sei_synthesizer.cpp`](../../src/core/sei_synthesizer.cpp) & [`src/shim/loader/nv_runtime.cpp`](../../src/shim/loader/nv_runtime.cpp)：删除 `containerSideData`/`toJson`/`fromJson` 及 `loadTensorRt`/`loadNvOfruc`。

### 3.3 Checklists（量化验收门禁）

- [ ] **门禁 1（开箱即用 60 帧实证）**：不传任何可选参数运行 `CineRTX.exe --input test_24fps.mp4 --output out.mp4`（或直接在 GUI 拖入 24fps 视频点击开始），输出视频必须严格为 **3840x2160、60.0 fps、AV1 10-bit、HDR10（PQ BT.2020）、VSR Quality = 4**，且输出总时长与输入误差 `< 1` 帧。
- [ ] **门禁 2（零隐藏坑与 CLI 极简审计）**：运行 `rg "(--sdr|--truehdr|--4k-profile|--no-cuda-graph|--test-gop-split|--fault-inject-frame|--rife-model|TensorRt)" src/ include/`，匹配结果必须为 **0**。
- [ ] **门禁 3（代码量硬指标门禁）**：统计 `src/` + `include/` 总行数必须 **`<= 7,800` 行**（相比当前 ~13,200 行净削减 40% 以上），且无任何超过 1,500 行的巨型 `.cpp` 文件。
- [ ] **门禁 4（PE 导入表与零外部运行时污染门禁）**：运行 `dumpbin /imports CineRTX.exe`，确认 **0 个 `MSVCP140.dll` / `VCRUNTIME140*.dll` / `WS2_32.dll`** 导入，且发布包目录中 **0 个 `cudart64_*.dll` / `rtx_fruc_bridge.dll`**。
