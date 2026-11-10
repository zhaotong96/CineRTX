# CineRTX

[English](README.md) | [中文](README_zh.md)

> Next-generation pure-GPU video remastering & NLE engine for NVIDIA RTX. Zero-copy hardware pipeline integrating NVDEC, RTX Video Super Resolution (VSR), TrueHDR inverse tone mapping, and NVENC AV1/HEVC 10-bit hardware encoding.

---

## Highlights

- **Pure-GPU Zero-Copy Pipeline**: Video frames reside 100% in GPU device memory from decoding (`NVDEC`), through D3D12/CUDA zero-copy interop for RTX Video SDK (`VSR` + `TrueHDR`), fused CUDA P010 packing, to hardware encoding (`NVENC`). Zero PCIe round-trips for pixel buffers.
- **Default AV1 10-bit Hardware Encoding**: Modern AV1 10-bit HDR10 encoding by default (on NVIDIA Ada Lovelace / Blackwell $\ge \text{sm\_89}$ architectures), with automatic graceful fallback to HEVC Main10 on earlier RTX architectures.
- **Native Win32 Pro Parameter Matrix**: Clean, fast Win32 Common Controls v6 UI with native dark mode support. Exposes 13 professional parameters (Codec, Preset, Rate Control, Bitrate, QP, Target Resolution, Geometry/Aspect Ratio, VSR Quality, TrueHDR Peak Nits, Audio Passthrough).
- **In-Place Dynamic Localization (`Ctrl+L`)**: Instant table-driven hot reload between English and Chinese across all labels and controls with `Ctrl+L` without restarting.
- **Modern C++26 Strict RAII**: 100% RAII resource management across the entire codebase. Zero two-step `initialize/shutdown/destroy` lifecycles, zero memory leaks.
- **Self-Contained Single Executable**: Surgery-trimmed static FFmpeg (`libavformat`/`libavcodec` bitstream filters) statically linked into `CineRTX.exe`. No external media DLL dependencies.
- **Ultra-Lightweight Standalone Release (~34 MB)**: Self-contained portable distribution package free of multi-gigabyte TensorRT bloat.

---

## Quick Start

### 1. Run Native GUI (Windows)
Simply run `CineRTX.exe` without arguments to launch the Native Dark Pro GUI:
```powershell
./CineRTX.exe
```
- **Drag & Drop**: Drag any video file directly into the window.
- **13 Pro Parameters**: Full control over encoder codec, bitrate/QP, resolution, VSR quality, and TrueHDR peak nits.
- **Hot Language Switch**: Press `Ctrl+L` to dynamically toggle between English and Chinese.

### 2. Command Line Interface (CLI)
CineRTX seamlessly attaches to the console when launched with arguments:

```powershell
# 1. Hardware probe and environment diagnostics
./CineRTX.exe --probe

# 2. Default 4K Remaster (AV1 10-bit + RTX VSR + Audio Passthrough)
./CineRTX.exe --input input.mp4 --output output_4k_av1.mp4

# 3. SDR to HDR10 4K Remaster with RTX TrueHDR inverse tone mapping
./CineRTX.exe --input input.mp4 --output output_4k_hdr10.mp4 --truehdr

# 4. View full CLI options
./CineRTX.exe --help
```

---

## Building from Source

### Requirements
- **OS**: Windows 10/11 64-bit
- **Compiler**: Visual Studio 2026 / 2022 (MSVC with C++26 `/std:c++latest` and `/arch:AVX2`)
- **Build System**: CMake 3.25+ and Ninja
- **Hardware**: NVIDIA GeForce RTX 30 / 40 / 50 Series or RTX Professional GPU
- **Driver**: NVIDIA Game Ready / Studio Driver 550.00+

### Build Steps
```powershell
# 1. Open Visual Studio Developer PowerShell (or execute Launch-VsDevShell.ps1)
# Launch-VsDevShell.ps1 -Arch amd64

# 2. Configure with Ninja
cmake -B build/cmake/windows-msvc-ninja -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo

# 3. Compile targets (CineRTX.exe, rtx_ngx_bridge.dll, rtx_tests.exe)
ninja -C build/cmake/windows-msvc-ninja

# 4. Run test suite (79 tests)
./build/cmake/windows-msvc-ninja/rtx_tests.exe
```

---

## Packaging Portable Release

To create a clean, standalone release package (~34 MB):
```powershell
python tools/package_release.py --build-dir build/cmake/windows-msvc-ninja --output-dir dist/CineRTX-v0.2.0-win64
```
Generates `dist/CineRTX-v0.2.0-win64.zip` accompanied by SHA-256 manifests and instructions.

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
