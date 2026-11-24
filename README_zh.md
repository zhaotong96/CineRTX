# CineRTX 纯 GPU 专业转码与非编引擎

[English](README.md) | [中文](README_zh.md)

> 面向 NVIDIA RTX 显卡的下一代纯 GPU 视频重制与专业非编引擎（`v0.4.0`）。全链路零拷贝硬件流水线，深度集成 NVDEC 硬件解码、RTX Video 超分辨率 (VSR)、TrueHDR 逆色调映射、双路真补帧（NVOFA 硬件光流 + TensorRT RIFE 深度学习推理），以及 NVENC AV1/HEVC 10-bit 硬件编码——通过 Win32 原生 / WebUI / 纯无头 CLI 三端同构工作站交付，由单一 SSOT 参数契约驱动。

---

## 核心技术特性（`v0.4.0` Phase 12 架构跃迁）

- **现代 C++26 零冗余纯净核心**：受 CMake 硬阻断门禁守护——全仓具名自定义类型 **≤ 35 个**（实测 34/35，0 违规）、**核心无头引擎 Pure SLOC ≤ 23,000 行**（实测约 12.2K 行），100% 绝对 RAII 资源生命周期，全面采用 `std::format` / `std::jthread` / `std::stop_token`，彻底消灭两段式 `initialize/shutdown/destroy`。
- **双形态极简系统 DLL 静态导入（IAT）**：
  - **纯无头 CLI 引擎（`rtx_headless_cli.exe`）**：物理静态 IAT 严格仅含 **2 项**（`kernel32.dll`、`bcrypt.dll`）+ 1 项 MSVC STL 同步 API Set（`api-ms-win-core-synch-l1-2-0.dll`），零 GUI/Shell/COM/D3D12 静态导入，适配 Windows Server Core 与无显示器算力节点；
  - **桌面发行版（`CineRTX.exe`）**：物理静态 IAT 严格 **≤ 4 项**（`kernel32.dll`、`user32.dll`、`gdi32.dll`、`bcrypt.dll`）+ 1 项 STL API Set，其余可选系统 API 全部通过 `LoadLibraryExW(..., LOAD_LIBRARY_SEARCH_SYSTEM32)` 按需动态加载。
- **纯 GPU 全链路零拷贝与 DirectComposition 渲染管线**：视频像素数据从 NVDEC 解码、D3D12/CUDA 跨 API 零拷贝互操作执行 RTX Video SDK（VSR + TrueHDR）、CUDA 融合 P010 打包，到 NVENC 硬件编码，全程 100% 驻留显存，DirectComposition 合成视口预览回读 `D2H == 0 B`。
- **双路真补帧架构**：路线 A 采用驱动原生 NVOFA 硬件双向光流 + PTX 运动补偿；路线 B 采用 TensorRT 10.13.3.9 + RIFE v4.25 深度学习推理。双路共存于唯一隔离桥接库 `rtx_ngx_bridge.dll`，共享同一 `D3D12Interop` CUDA 上下文，并带场景硬切复制旁路保护。
- **默认全链路 AV1 10-bit 专业硬件编码**：Ada Lovelace / Blackwell 架构（≥ sm_89，RTX 40/50 系列）默认启用先进的 AV1 10-bit HDR10 编码，并在前代 RTX 架构上自动平滑回退至 HEVC Main10。
- **三端统一 SSOT 架构**：唯一的 `kParamSchema` 20 项专业参数契约（`include/rtx/param_schema.h`）与唯一中英双语词典（`include/rtx/i18n.h`）同时驱动 Win32 原生 UI、WebUI 与 CLI——彻底根除各前端参数不同步隐患。
- **Win32 原生 "3+1" 四态专业工作站**：NVIDIA Signature Green（`#76B900`）原生交互界面，`[1] Encoder` 转码参数矩阵、`[2] Player` 播放审片、`[3] NLE` 四轨 **`705,600,000 Hz Flicks`** 无损时间轴，以及 `+1` 第 4 形态（视频文件双击即进入无边框全屏动态 VSR 超分快播）。
- **原地热重载中英文国际化 (`Ctrl+L`)**：全控件表驱动，运行时按下 `Ctrl+L` 即可在中文与英文间瞬时原地热切换，无需重启进程。
- **单 EXE 静态自包含与 DebugView 零依赖日志**：手术刀级裁剪的 FFmpeg 静态库全量内联；全引擎日志经 `OutputDebugStringW`（`[CineRTX]`）实时流出，无磁盘日志文件。

---

## 快速上手

### 1. Win32 原生 "3+1" 四态工作站
无参数直接运行 `CineRTX.exe` 即可启动原生四态工作站：
```powershell
./CineRTX.exe
```
- `[1] Encoder`：20 项 SSOT 参数转码矩阵，支持视频拖拽；
- `[2] Player`：播放审片；`[3] NLE`：四轨 Flicks 时间轴非线编；
- `+1` 第 4 形态：将视频文件打开方式关联为 `CineRTX.exe` 后，双击视频直接进入无边框全屏动态 VSR 超分快播；
- **语言切换**：随时按下 `Ctrl+L` 原地切换中英双语。

### 2. WebUI v3 浏览器三态工作站 (Encoder / Player / NLE)
```powershell
python webui/server.py --port 8787
```
零依赖 JSON 桥通过 `--schema-json` 与 `--session-stdio` 对接 `CineRTX.exe` 或纯无头 `rtx_headless_cli.exe`——参数一致、状态一致。

### 3. 命令行模式（桌面 CLI 与纯无头 CLI）
无头/服务器环境可直接使用零窗口子系统的 `rtx_headless_cli.exe`，桌面环境亦可通过 `CineRTX.exe` 附着控制台运行：

```powershell
# 1. 输出版本号 (0.4.0) 与 20 项 SSOT 参数契约 JSON
./rtx_headless_cli.exe --version
./rtx_headless_cli.exe --schema-json

# 2. 硬件探针与运行环境自检
./rtx_headless_cli.exe --probe

# 3. 缺省开箱即用 4K 60fps HDR10 超分重制转码
# (AV1 10-bit + NVOFA 硬件光流 + VSR 4 + TrueHDR 1000 nits)
./CineRTX.exe --input in.mp4 --output out_4k60_hdr10.mp4

# 4. 自定义 HDR 峰值亮度 (1500 nits) 与 VSR 档位
./CineRTX.exe --input in.mp4 --output out.mp4 --peak-nits 1500 --vsr-quality 4

# 5. TensorRT RIFE v4.25 深度学习母版补帧
./CineRTX.exe --input in.mp4 --output out_rife.mp4 --rife-engine models/rife_v4.25.engine

# 6. 查看命令行完整参数帮助
./rtx_headless_cli.exe --help
```

### 4. DebugView 实时监看
启动 Sysinternals DebugView，勾选 "Capture Win32 + Global Win32"，过滤 `[CineRTX]`——全部解码/编码/探测事件以中英双语实时流出。

---

## 源码构建指南

### 环境要求
- **操作系统**：Windows 10 / 11 64-bit
- **编译器**：Visual Studio 2026 / 2022（MSVC 19.51+ 开启 C++26 `/std:c++latest`、`/W4 /WX` 与 `/arch:AVX2`），或内置 Zig 0.13 回退工具链
- **构建工具**：CMake 3.25+ 与 Ninja
- **硬件环境**：NVIDIA GeForce RTX 30 / 40 / 50 系列或 RTX 专业 GPU
- **显卡驱动**：NVIDIA Game Ready / Studio 驱动 550.00 以上（RTX 50 系列推荐 621.24+ WHQL）

### 构建与门禁命令
```powershell
# 1. 生成构建工程 (MSVC 预设；无 VS 环境可用 zig-windows 预设)
cmake --preset windows-msvc-ninja

# 2. 编译全部二进制目标 (CineRTX.exe、rtx_headless_cli.exe、rtx_ngx_bridge.dll、rtx_tests.exe)
cmake --build --preset windows-msvc-ninja

# 3. 执行全量单元测试 (147 项全绿)
./.vs/build/cmake/windows-msvc-ninja/rtx_tests.exe

# 4. 执行 Phase 12 五重纯原生静态硬门禁
cmake -P cmake/AuditTypes.cmake
cmake -P cmake/AuditSloc.cmake
cmake -P cmake/AuditSpdx.cmake
cmake -P cmake/AuditPrivacy.cmake
cmake -DTARGET_BIN=.vs/build/cmake/windows-msvc-ninja/CineRTX.exe -DMODE=GUI -P cmake/AuditImports.cmake
cmake -DTARGET_BIN=.vs/build/cmake/windows-msvc-ninja/rtx_headless_cli.exe -DMODE=HEADLESS -P cmake/AuditImports.cmake
```

产物输出于 `.vs/build/cmake/<preset>/`（详见 `CMakePresets.json`）。

---

## 构建轻量化绿色免安装发布包

通过自动化打包脚本产出开箱即用的轻量绿色发布包（约 34 MB）：
```powershell
cmake -DBUILD_DIR=.vs/build/cmake/windows-msvc-ninja -P cmake/PackageRelease.cmake
```
生成包含 SHA-256 完整性清单与说明文档的 `.vs/build/dist/CineRTX-v0.4.0-windows-x64.zip`。

---

## 系统拓扑流水线

```
 [ 输入视频容器 (MP4/MKV) ]
                |
                v  (FFmpeg 静态解复用)
 [ 压缩音视频数据包 ] ------------------> [ 音频流无损直通 ]
                |                                     |
                v                                     |
 [ NVDEC 硬件解码 ] (cuvidMapVideoFrame64)            |
                |                                     |
                v  (设备显存 NV12 表面)                |
 [ 双路真补帧 ]                                        |
   (NVOFA 硬件光流 | TensorRT RIFE)                   |
                |                                     |
                v                                     |
 [ D3D12 / CUDA 跨 API 零拷贝互操作 ]                  |
                |                                     |
                v                                     |
 [ RTX Video VSR 超分 + TrueHDR ] (NGX 插件桥)        |
                |                                     |
                v  (RGBA16F 表面)                     |
 [ CUDA 融合 P010 打包 Kernel ]                       |
                |                                     |
                v  (P010 10-bit BT.2020 / PQ)         |
 [ NVENC 硬件编码器 ]                                  |
   (默认 AV1 10-bit / HEVC Main10 回退)               |
                |                                     |
                v                                     v
 [ FFmpeg 静态复用器 (注入 av1C/HVCC 容器头 + 封装音频流) ]
                |
                v
 [ 最终重制 4K HDR10 视频文件 (MP4/MKV) ]
```

---

## 开源许可证与合规声明

- 引擎核心源码采用 **GPL-3.0-or-later WITH Plugin-Exception** 开源许可证（详见 [LICENSE](LICENSE) 与 [COPYING.EXCEPTION](COPYING.EXCEPTION)）；
- NVIDIA 专有 SDK 组件（`rtx_ngx_bridge.dll`、`nvngx_vsr.dll`、`nvngx_truehdr.dll`）通过合规的 Plugin-Exception 动态隔离加载运行。
