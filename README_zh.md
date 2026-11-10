# CineRTX 纯 GPU 专业转码与非编引擎

[English](README.md) | [中文](README_zh.md)

> 面向 NVIDIA RTX 显卡的下一代纯 GPU 视频重制与专业非编引擎。全链路零拷贝硬件流水线，深度集成 NVDEC 硬件解码、RTX Video 超分辨率 (VSR)、TrueHDR 逆色调映射，以及 NVENC AV1/HEVC 10-bit 硬件编码。

---

## 核心技术特性

- **纯 GPU 全链路零拷贝流水线**：视频像素数据从解码（NVDEC）、D3D12/CUDA 跨 API 零拷贝显存互操作执行 RTX Video SDK（VSR 超分辨率 + TrueHDR 色调映射）、CUDA 融合 P010 打包，到硬件编码（NVENC），全程 100% 驻留设备显存，消除 PCIe 主机端内存往返拷贝开销。
- **默认全链路 AV1 10-bit 专业硬件编码**：Ada Lovelace / Blackwell 架构（$\ge \text{sm\_89}$，RTX 40/50 系列）默认启用先进的 AV1 10-bit HDR10 编码，并在前代 RTX 架构上自动平滑回退至 HEVC Main10。
- **Win32 原生 Common Controls 13 项专业参数矩阵**：彻底推翻纯 GDI 手绘教条，接入 Windows 原生控件库与暗黑主题。全量暴露 13 项专业参数（编码格式、预设、码率控制、目标码率、质量 QP、目标分辨率、画幅旋转适配、VSR 质量档位、TrueHDR 峰值亮度、音频流直通等）。
- **原地热重载中英文国际化 (`Ctrl+L`)**：全控件表驱动，在 GUI 运行时按下 `Ctrl+L` 即可在中文与英文间瞬时原地热切换，无需重启进程。
- **现代 C++26 100% 绝对 RAII**：全仓消灭两段式 `initialize/shutdown/destroy` 生命周期，强制推行私有构造 + `create(...)` 静态工厂与析构自动销毁，彻底根除显存与句柄泄漏隐患。
- **单 EXE 静态自包含单体架构**：手术刀级裁剪的 FFmpeg 静态库（解封装器、封装器、比特流滤镜）全量内联进 `CineRTX.exe`，免除任何第三方媒体 DLL 依赖。
- **极致轻量化绿色免安装发布包 (~34 MB)**：剔除数 GB 级冗余 AI 运行时，自包含绿色归档仅约 34 MB。

---

## 快速上手

### 1. 启动原生暗黑专业界面 (GUI)
无参数直接运行 `CineRTX.exe` 即可启动原生图形界面：
```powershell
./CineRTX.exe
```
- **文件拖拽**：直接拖拽视频文件至窗口即可自动加载并智能填充输出路径；
- **13 项专业参数调节**：自由设定分辨率、画幅、码率与 HDR 参数；
- **语言切换**：随时按下 `Ctrl+L` 原地切换中英双语。

### 2. 命令行模式 (CLI)
带参数运行时自动附着控制台输出：

```powershell
# 1. 硬件探针与运行环境自检
./CineRTX.exe --probe

# 2. 缺省 4K 超分重制转码 (默认 AV1 10-bit + RTX VSR + 音频无损直通)
./CineRTX.exe --input in.mp4 --output out_4k_av1.mp4

# 3. SDR 转 HDR10 逆色调映射 4K 重制转码
./CineRTX.exe --input in.mp4 --output out_4k_hdr10.mp4 --truehdr

# 4. 查看命令行完整参数帮助
./CineRTX.exe --help
```

---

## 源码构建指南

### 环境要求
- **操作系统**：Windows 10 / 11 64-bit
- **编译器**：Visual Studio 2026 / 2022 (MSVC 开启 C++26 `/std:c++latest` 与 `/arch:AVX2`)
- **构建工具**：CMake 3.25+ 与 Ninja
- **硬件环境**：NVIDIA GeForce RTX 30 / 40 / 50 系列或 RTX 专业 GPU
- **显卡驱动**：NVIDIA Game Ready / Studio 驱动 550.00 以上

### 构建命令
```powershell
# 1. 启动 Visual Studio Developer PowerShell (或执行 Launch-VsDevShell.ps1)
# Launch-VsDevShell.ps1 -Arch amd64

# 2. 生成 Ninja 构建工程
cmake -B build/cmake/windows-msvc-ninja -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo

# 3. 编译全部二进制目标
ninja -C build/cmake/windows-msvc-ninja

# 4. 执行全量单元测试 (79 项单测全绿)
./build/cmake/windows-msvc-ninja/rtx_tests.exe
```

---

## 构建轻量化绿色免安装发布包

通过自动化打包脚本产出开箱即用的轻量绿色发布包（约 34 MB）：
```powershell
python tools/package_release.py --build-dir build/cmake/windows-msvc-ninja --output-dir dist/CineRTX-v0.2.0-win64
```
生成包含 SHA-256 完整性清单与说明文档的 `dist/CineRTX-v0.2.0-win64.zip`。

---

## 系统拓扑流水线

```
 [ 输入视频容器 (MP4/MKV) ]
              |
              v  (FFmpeg 静态解复用)
     [ 压缩音视频数据包 ]  ------------------>  [ 音频流无损直通 ]
              |                                         |
              v                                         |
      [ NVDEC 硬件解码 ] (cuvidMapVideoFrame64)         |
              |                                         |
              v (设备显存 NV12 表面)                     |
   [ D3D12 / CUDA 跨 API 零拷贝互操作 ]                 |
              |                                         |
              v                                         |
   [ RTX Video VSR 超分 + TrueHDR ] (NGX 插件桥)        |
              |                                         |
              v (RGBA16F 表面)                          |
    [ CUDA 融合 P010 打包 Kernel ]                      |
              |                                         |
              v (P010 10-bit BT.2020 / PQ)              |
      [ NVENC 硬件编码器 ]                              |
   (默认 AV1 10-bit / HEVC Main10 回退)                 |
              |                                         |
              v                                         v
 [ FFmpeg 静态复用器 (注入 av1C/HVCC 容器头 + 封装音频流) ]
              |
              v
 [ 最终重制 4K HDR10 视频文件 (MP4/MKV) ]
```

---

## 开源许可证与合规声明

- 引擎核心源码采用 **GPL-3.0-or-later WITH Plugin-Exception** 开源许可证（详见 [LICENSE](LICENSE) 与 [COPYING.EXCEPTION](COPYING.EXCEPTION)）；
- NVIDIA 专有 SDK 组件（`rtx_ngx_bridge.dll`、`nvngx_vsr.dll`、`nvngx_truehdr.dll`）通过合规的 Plugin-Exception 动态隔离加载运行。
