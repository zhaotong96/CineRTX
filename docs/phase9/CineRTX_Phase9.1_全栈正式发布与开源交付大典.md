# CineRTX Phase 9.1：全栈正式发布与开源交付大典（v0.1.0 Gold Master）

- **文档编号**：`CINERTX-P9.1-RELEASE-MANIFESTO-007`
- **发布版本**：`v0.1.0`（Phase 9.1 Full-Stack Gold Master）
- **关联裁决书**：
  - [`docs/phase9/CineRTX_Phase9.1_终审通过暨全栈收官战略封板裁决书.md`](CineRTX_Phase9.1_终审通过暨全栈收官战略封板裁决书.md)（`CINERTX-P9.1-FINAL-APPROVAL-SEAL-003`）
  - [`docs/phase9/CineRTX_Phase9.1_推送前仓库卫生完善与CMake全原生闭环专家裁决暨SuperPower施工规范.md`](CineRTX_Phase9.1_推送前仓库卫生完善与CMake全原生闭环专家裁决暨SuperPower施工规范.md)（`CINERTX-P9.1-REPO-HYGIENE-SUPERPOWER-006`）
- **开源许可协议**：Apache License 2.0（100% 源码 SPDX 合规）

## 一、 工程哲学与破局宣言：以极简机械同情重塑 RTX AI 视频生产力

在当代桌面与多媒体工业软件领域，臃肿的 Electron 壳层、动辄数百兆的 Python/PyTorch 虚拟环境、碎片化的动态 DLL 地狱以及不可复现的脚本胶水，正在吞噬现代硬件的原生算力。**CineRTX v0.1.0** 的诞生，是对“最小完备机制（Minimally Sufficient Mechanism）”与“奥卡姆剃刀定律（Occam's Razor）”的工程实践：

1. **零外部脚本、100% C++26 与 CMake 原生闭环**：
   全仓仅保留根目录唯一构建入口 [`build.bat`](../../build.bat) 与声明式 [`CMakeLists.txt`](../../CMakeLists.txt)。从 CUDA Driver PTX 核函数内嵌、FFmpeg 7.1.1 静态编译、SPDX 许可证门禁、PE 导入表零泄漏审计，到 RIFE v4.25 ONNX `/Cast_8` 动态时间步计算图修补与便携版打包，**100% 由 C++26 与原生 CMake 完成，彻底清零 Python/PowerShell/Bash/Node.js 外部构建脚本依赖**。
2. **双二进制静态微内核闭包（`CineRTX.exe` + `rtx_ngx_bridge.dll`）**：
   主程序 `CineRTX.exe`（约 `4.54 MB`）将 MSVC 静态运行时（`/MT`）与裁剪后的 `libavformat` / `libavcodec` / `libavutil` / `libswresample` 静态熔铸一体；唯一可选插件 `rtx_ngx_bridge.dll` 通过纯 C ABI 函数表及 `LoadLibraryExW(..., LOAD_LIBRARY_SEARCH_SYSTEM32)` 动态桥接底层驱动，在无 NVIDIA 独显的机器与云端无头 CI 上仍能 100% 优雅降级启动。
3. **显存零拷贝（VRAM Zero-Copy）全链路直通**：
   从 NVDEC 硬解 P010/NV12 帧入显存开始，历经 RTX Video Super Resolution（VSR）、RTX TrueHDR、3D LUT 三线性色彩映射、双路径光流/AI 插帧（NVOFA / TensorRT RIFE v4.25），直至 NVENC AV1/HEVC 硬件编码输出，**全程像素数据驻留 GPU VRAM，主机 PCIe 显存-内存往返拷贝为零**。

## 二、 核心技术里程碑与架构全景矩阵

| 架构维度 | 核心实现模块 | 关键技术指标与工程突破 |
| :--- | :--- | :--- |
| **真 AI 120fps 插帧引擎（TensorRT RIFE v4.25）** | [`src/shim/ngx_bridge/rife_engine.cpp`](../../src/shim/ngx_bridge/rife_engine.cpp)<br>[`src/shim/ngx_bridge/rife_builder.cpp`](../../src/shim/ngx_bridge/rife_builder.cpp)<br>`src/cuda/rife_kernels.ptx` | 攻克开源 `RIFE_fp32.onnx` 第 7 通道时间步被 `/Cast_8` 静态折叠为 $0.5$ 的行业顽疾；内置纯 C++26 动态时间步图重写器与 `nvinfer_builder_resource_10.dll` 显式预加载；在 RTX 5060 Laptop GPU 上实现 $24\text{fps} \to 120\text{fps}$（$5\times$，$\alpha \in \{0.0, 0.2, 0.4, 0.6, 0.8\}$）全帧唯一运动内插（相邻帧像素互差 $\text{MAE} \in [0.87, 3.31]$，零重复帧） |
| **硬件光流任意时间步插帧（NVOFA）** | [`src/shim/ngx_bridge/nvof_engine.cpp`](../../src/shim/ngx_bridge/nvof_engine.cpp) | 摒弃锁死 $2\times$ 中点插帧的 `NvOFFRUC.dll`，直接通过 `%SystemRoot%\System32\nvofapi64.dll` 驱动硬件光流计算 $4\times 4$ 双向运动矢量场，配合 PTX 双向遮挡感知 Warp 核函数与 `strideXInBytes`/`strideYInBytes` 精确 2D 步长对齐，支持任意帧率倍增 |
| **10-bit HDR 与 Flicks 零漂移时间基** | [`include/rtx/flicks.h`](../../include/rtx/flicks.h)<br>`src/cuda/p010_convert.ptx` | 采用工业级 $705,600,000\text{ Hz}$ Flicks 整数时间基，彻底消灭 $23.976 / 29.97 / 59.94 / 119.88\text{fps}$ NTSC 分数帧率在长视频转码中的浮点累积误差；原生支持 P010LE 10-bit、BT.2020 与 ST.2084 PQ 元数据无损透传 |
| **三端一体工作站与极速播放器** | [`src/gui/rtx_gui.cpp`](../../src/gui/rtx_gui.cpp)<br>`src/gui/webui_html.h` | 集成 NVIDIA 暗黑绿主题三端一体工作站（`Transcoder` 批处理转码中心 + `Player` 实时画质对比播放器 + `NLE` 非编时间线工作台）与 Win32 原生 `--mode player` 零边框极速预览模式；19 项核心参数与 5 国语言（113 词条）由单一事实源（SSOT）驱动 |
| **全自动自举供给与 10 大 C++26 门禁** | [`cmake/RtxProvision.cmake`](../../cmake/RtxProvision.cmake)<br>[`cmake/RtxFfmpegStatic.cmake`](../../cmake/RtxFfmpegStatic.cmake)<br>[`tests/`](../../tests) | 首次构建自动完成 CUDA/NGX/TensorRT Apache-2.0 头文件校验、FFmpeg 静态库编译与 RIFE ONNX 模型供给；10 个纯 C++26 测试套件全方位守护 ABI 边界、时间基数学、音视频流拷贝、PE 导入表闭包与真机 120fps 插帧质量 |

## 三、 实机黄金验收基线（RTX 5060 Laptop GPU + Ryzen 9 8945HX）

| 验收指标项 | 门禁阈值要求 | Phase 9.1 实机实测结果 | 状态 |
| :--- | :--- | :--- | :--- |
| **主程序体积（`CineRTX.exe`）** | $1.2\text{ MB} \sim 12.0\text{ MB}$ | **`4,540,928` Bytes（`4.33 MB`）** | **PASS** |
| **动态库零泄漏审计** | 仅允许 Win32 系统 DLL | **0 外部动态 DLL 依赖（通过 `AuditImports.cmake` 与 PE 门禁）** | **PASS** |
| **RIFE 120fps 5 槽时间步非退化** | 相邻槽位像素互差 $\text{MAE} \ge 0.50\text{ LSB}$ | `diff(0.0,0.2)=3.310`, `diff(0.2,0.4)=1.347`, `diff(0.4,0.6)=0.870`, `diff(0.6,0.8)=0.903` | **PASS** |
| **24fps $\to$ 120fps 端到端真机转码** | 输出严格 120fps CFR 且帧间无重复 | 输入 29 帧 $24\text{fps} \to$ 输出 **145 帧 `120/1` fps**，前 5 帧互差 $3.26 \sim 4.92$（零重复帧） | **PASS** |
| **CTest 全量 C++26 回归测试** | 100% 通过率 | **100% tests passed, 0 tests failed** | **PASS** |

## 四、 极简开箱即用与一键构建指南

### 4.1 开发者一键构建（Windows 11 + VS 2022/2026）

```powershell
git clone https://github.com/zhaotong96/CineRTX.git
cd CineRTX
.\build.bat
```
- 构建产物统一收敛于 `.vs\build\cmake\windows-msvc-ninja\`（源码树保持 100% 零污染）；
- 首次运行 `CineRTX.exe` 时，若检测到 `models/rife_v4.25.onnx` 尚未针对本机 GPU 架构编译为 TensorRT Engine，将自动在后台完成 `/Cast_8` 动态时间步核验与 FP16 Engine 编译并缓存至 `models/rife_v4.25.engine`。

### 4.2 命令行与三端工作站快速上手

```powershell
# 1. 启动三端一体图形工作站（默认进入 Transcoder / Player / NLE 综合界面）
.\.vs\build\cmake\windows-msvc-ninja\CineRTX.exe

# 2. 启动 Win32 极速播放器模式并直接预览媒体文件
.\.vs\build\cmake\windows-msvc-ninja\CineRTX.exe --mode player --input sample.mp4

# 3. 命令行批处理：RTX VSR 4K 超分 + TrueHDR + RIFE 120fps AI 插帧 + AV1 10-bit 硬件编码
.\.vs\build\cmake\windows-msvc-ninja\CineRTX.exe -i input_24fps.mp4 -o output_120fps_av1.mp4 ^
    --scale 2 --true-hdr --interp rife --fps 120 --codec av1 --bitrate 25M
```
