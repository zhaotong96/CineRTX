# CineRTX

[English](README.md) | [中文](README_zh.md)

> Next-generation pure-GPU video remastering & NLE engine for NVIDIA RTX. Zero-copy hardware pipeline integrating NVDEC, RTX Video Super Resolution (VSR), TrueHDR inverse tone mapping, dual-path true frame interpolation (NVOFA optical flow + TensorRT RIFE), and NVENC AV1/HEVC 10-bit hardware encoding — delivered through three isomorphic workstations (Win32 native / WebUI / CLI) driven by one SSOT parameter contract.

---

## Highlights

- **Pure-GPU Zero-Copy Pipeline**: Video frames reside 100% in GPU device memory from decoding (`NVDEC`), through D3D12/CUDA zero-copy interop for RTX Video SDK (`VSR` + `TrueHDR`), fused CUDA P010 packing, to hardware encoding (`NVENC`). Zero PCIe round-trips for pixel buffers.
- **Dual-Path True Frame Interpolation**: Route A uses the driver-native NVOFA hardware optical flow with PTX motion compensation; Route B uses TensorRT 10.13.3.9 + RIFE v4.25 deep-learning inference. Both live inside the single `rtx_ngx_bridge.dll` plugin and share one `D3D12Interop` CUDA context, with hard scene-cut duplicate bypass.
- **Default AV1 10-bit Hardware Encoding**: Modern AV1 10-bit HDR10 encoding by default (on NVIDIA Ada Lovelace / Blackwell ≥ sm_89 architectures), with automatic graceful fallback to HEVC Main10 on earlier RTX architectures.
- **Three-End SSOT Architecture**: One `kParamSchema` contract of 19 professional parameters (`include/rtx/param_schema.h`) and one 113-entry bilingual dictionary (`include/rtx/i18n.h`) drive the Win32 native UI, the WebUI, and the CLI — no parameter drift between frontends.
- **Win32 "3+1" Four-State Pro Workstation**: NVIDIA Signature Green (`#76B900`) native UI with `[1] Encoder` transcode matrix, `[2] Player` review playback, `[3] NLE` four-track timeline on the 705,600,000 Hz Flicks timebase, and the `+1` fourth form: associate video files with `CineRTX.exe` and double-click to enter borderless fullscreen dynamic-VSR quick playback.
- **In-Place Dynamic Localization (`Ctrl+L`)**: Instant table-driven hot reload between English and Chinese across all frontends with `Ctrl+L` without restarting.
- **Modern C++26 Strict RAII**: 100% RAII resource management across the entire codebase. Zero two-step `initialize/shutdown/destroy` lifecycles, zero memory leaks.
- **Self-Contained Single Executable**: Surgery-trimmed static FFmpeg (`libavformat`/`libavcodec` bitstream filters) statically linked into `CineRTX.exe`. No external media DLL dependencies.
- **DebugView Zero-Dependency Kernel Logging**: All engine logs stream through `OutputDebugStringW` (`include/rtx/logger.h`); attach Sysinternals DebugView and filter `[CineRTX]` for live diagnostics — no disk log files, no env switches.
- **Ultra-Lightweight Standalone Release (~34 MB)**: Self-contained portable distribution package free of multi-gigabyte TensorRT bloat.

---

## Quick Start

### 1. Win32 Native "3+1" Workstation (Windows)

Simply run `CineRTX.exe` without arguments to launch the native four-state workstation:
```powershell
./CineRTX.exe
```
- `[1] Encoder`: 19-parameter SSOT transcode matrix with drag & drop.
- `[2] Player`: review playback; `[3] NLE`: four-track editing on the Flicks timeline.
- `+1` fourth form: after associating video files with `CineRTX.exe`, double-clicking a video opens borderless fullscreen dynamic VSR quick playback (viewport-adaptive scale such as `2.00x`, portrait `1.6875x`).
- **Hot Language Switch**: Press `Ctrl+L` to dynamically toggle between English and Chinese.

### 2. WebUI Browser Workstation (Encoder / Player / NLE)
```powershell
python webui/server.py --port 8787
```
The zero-dependency JSON bridge talks to the same C++26 backend via `--schema-json` and `--session-stdio` — identical parameters, identical states.

### 3. Command Line Interface (CLI)
CineRTX seamlessly attaches to the console when launched with arguments:

```powershell
# 1. Hardware probe and environment diagnostics
./CineRTX.exe --probe

# 2. Default out-of-the-box 4K 60fps HDR10 remaster
#    (AV1 10-bit + NVOFA optical flow + VSR 4 + TrueHDR 1000 nits)
./CineRTX.exe --input in.mp4 --output out_4k60_hdr10.mp4

# 3. Custom HDR peak luminance (1500 nits) and VSR tier
./CineRTX.exe --input in.mp4 --output out.mp4 --peak-nits 1500 --vsr-quality 4

# 4. TensorRT RIFE v4.25 deep-learning master interpolation
./CineRTX.exe --input in.mp4 --output out_rife.mp4 --rife-engine models/rife_v4.25.engine

# 5. Export the SSOT parameter schema and bilingual dictionary as JSON
./CineRTX.exe --schema-json

# 6. View full CLI options
./CineRTX.exe --help
```

### 4. DebugView Live Diagnostics
Launch Sysinternals DebugView, enable "Capture Win32 + Global Win32", and filter `[CineRTX]` — every decode/encode/probe event streams there in real time, in both UI languages.

---

## Building from Source

### Requirements
- **OS**: Windows 10/11 64-bit
- **Compiler**: Visual Studio 2026 / 2022 (MSVC with C++26 `/std:c++latest` and `/arch:AVX2`), or the bundled Zig 0.13 fallback toolchain
- **Build System**: CMake 3.25+ and Ninja
- **Hardware**: NVIDIA GeForce RTX 30 / 40 / 50 Series or RTX Professional GPU
- **Driver**: NVIDIA Game Ready / Studio Driver 550.00+

### Build Steps
```powershell
# 1. Configure (MSVC preset; on machines without VS use the zig-windows preset)
cmake --preset windows-msvc-ninja

# 2. Compile targets (CineRTX.exe, rtx_ngx_bridge.dll, rtx_tests.exe)
cmake --build --preset windows-msvc-ninja

# 3. Run test suite (75 tests)
./.vs/build/cmake/windows-msvc-ninja/rtx_tests.exe
```

Binaries land under `.vs/build/cmake/<preset>/` (see `CMakePresets.json`).

---

## Packaging Portable Release

To create a clean, standalone release package (~34 MB):
```powershell
cmake -DBUILD_DIR=.vs/build/cmake/windows-msvc-ninja -P cmake/PackageRelease.cmake
```
Generates `.vs/build/dist/CineRTX-v0.1.0-windows-x64.zip` accompanied by SHA-256 manifests and instructions.

---

## Architecture Overview

```
 [ Input Video Container (MP4/MKV) ]
                |
                v  (Static FFmpeg Demuxer)
   [ Compressed Bitstream Packets ]  -------->  [ Audio Stream Copy ]
                |                                          |
                v                                          |
        [ NVDEC Decoder ] (cuvidMapVideoFrame64)           |
                |                                          |
                v (Device Memory NV12)                     |
    [ Dual-Path True Interpolation ]                       |
     (NVOFA optical flow | TensorRT RIFE)                  |
                |                                          |
                v                                          |
    [ D3D12 / CUDA Zero-Copy Interop ]                     |
                |                                          |
                v                                          |
    [ RTX Video VSR + TrueHDR ] (NGX Bridge)               |
                |                                          |
                v (RGBA16F Surface)                        |
      [ Fused CUDA P010 Pack Kernel ]                      |
                |                                          |
                v (P010 10-bit BT.2020 / PQ)               |
        [ NVENC Hardware Encoder ]                         |
     (AV1 10-bit / HEVC Main10)                            |
                |                                          |
                v                                          v
 [ Static FFmpeg Muxer (av1C / HVCC Extradata + Audio Passthrough) ]
                |
                v
 [ Remastered 4K HDR10 Output (MP4/MKV) ]
```

---

## Licensing & Open-Source Compliance

- Core engine code is licensed under **GPL-3.0-or-later WITH Plugin-Exception** (see [LICENSE](LICENSE) and [COPYING.EXCEPTION](COPYING.EXCEPTION)).
- NVIDIA proprietary SDK components (`rtx_ngx_bridge.dll`, `nvngx_vsr.dll`, `nvngx_truehdr.dll`) run behind strict dynamic boundaries governed by the Plugin-Exception.
