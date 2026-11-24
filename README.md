# CineRTX

[English](README.md) | [中文](README_zh.md)

> Next-generation pure-GPU video remastering & NLE engine for NVIDIA RTX (`v0.4.0`). Zero-copy hardware pipeline integrating NVDEC, RTX Video Super Resolution (VSR), TrueHDR inverse tone mapping, dual-path true frame interpolation (NVOFA optical flow + TensorRT RIFE), and NVENC AV1/HEVC 10-bit hardware encoding — delivered through three isomorphic workstations (Win32 native / WebUI / headless CLI) driven by one SSOT parameter contract.

---

## Highlights (`v0.4.0` Phase 12 Architecture)

- **Modern C++26 Zero-Redundancy Core**: Enforced by hard CMake audit gates — **<= 35 justified named types** (34/35 verified, `0` violations), **Core Engine Pure SLOC <= 23,000** (~12.2K verified), 100% RAII resource management, `std::format` / `std::jthread` / `std::stop_token`, zero two-step `initialize/shutdown/destroy` lifecycles.
- **Dual-Mode Minimal Static System DLL Footprint**:
  - **Headless CLI Engine (`rtx_headless_cli.exe`)**: Exactly **2 physical system DLLs** (`kernel32.dll`, `bcrypt.dll`) + 1 MSVC STL synchronization API Set (`api-ms-win-core-synch-l1-2-0.dll`) — zero GUI/Shell/COM/D3D12 static imports for Windows Server Core and headless compute nodes.
  - **Desktop Workstation (`CineRTX.exe`)**: Strictly **<= 4 physical system DLLs** (`kernel32.dll`, `user32.dll`, `gdi32.dll`, `bcrypt.dll`) + 1 STL API Set, with optional OS APIs loaded on demand via `LoadLibraryExW(..., LOAD_LIBRARY_SEARCH_SYSTEM32)`.
- **Pure-GPU Zero-Copy Pipeline**: Video frames reside 100% in GPU device memory from decoding (`NVDEC`), through D3D12/CUDA zero-copy interop for RTX Video SDK (`VSR` + `TrueHDR`), fused CUDA P010 packing, to hardware encoding (`NVENC`), with zero preview readback (`D2H == 0 B`) via DirectComposition.
- **Dual-Path True Frame Interpolation**: Route A uses driver-native NVOFA hardware optical flow with PTX motion compensation; Route B uses TensorRT 10.13.3.9 + RIFE v4.25 deep-learning inference. Both live inside the isolated `rtx_ngx_bridge.dll` plugin and share one `D3D12Interop` CUDA context, with hard scene-cut duplicate bypass.
- **Default AV1 10-bit Hardware Encoding**: Modern AV1 10-bit HDR10 encoding by default on NVIDIA Ada Lovelace / Blackwell (>= sm_89), with automatic graceful fallback to HEVC Main10 on earlier RTX architectures.
- **Three-End SSOT Architecture**: One `kParamSchema` contract of 20 professional parameters (`include/rtx/param_schema.h`) and one bilingual dictionary (`include/rtx/i18n.h`) drive the Win32 native UI, WebUI, and CLI — zero parameter drift between frontends.
- **Win32 "3+1" Four-State Pro Workstation**: NVIDIA Signature Green (`#76B900`) native UI with `[1] Encoder` transcode matrix, `[2] Player` review playback, `[3] NLE` four-track timeline on the **705,600,000 Hz Flicks** lossless timebase, and the `+1` fourth form (double-click video association for borderless fullscreen dynamic-VSR quick playback).
- **In-Place Dynamic Localization (`Ctrl+L`)**: Instant table-driven hot reload between English and Chinese across all frontends without restarting.
- **Self-Contained Single Executable & DebugView Logging**: Surgery-trimmed static FFmpeg (`libavformat`/`libavcodec` bitstream filters) linked statically; live diagnostics stream via `OutputDebugStringW` (`[CineRTX]`) with zero disk log files.

---

## Quick Start

### 1. Win32 Native "3+1" Workstation (Windows)

Run `CineRTX.exe` without arguments to launch the native four-state workstation:
```powershell
./CineRTX.exe
```
- `[1] Encoder`: 20-parameter SSOT transcode matrix with drag & drop.
- `[2] Player`: review playback; `[3] NLE`: four-track editing on the Flicks timeline.
- `+1` fourth form: associate video files with `CineRTX.exe` and double-click to open borderless fullscreen dynamic VSR quick playback.
- **Hot Language Switch**: Press `Ctrl+L` to toggle between English and Chinese in place.

### 2. WebUI v3 Browser Workstation (Encoder / Player / NLE)
```powershell
python webui/server.py --port 8787
```
The zero-dependency JSON bridge talks to `CineRTX.exe` or `rtx_headless_cli.exe` via `--schema-json` and `--session-stdio` — identical parameters, identical states.

### 3. Command Line Interface (Desktop CLI & Pure Headless CLI)
Use `rtx_headless_cli.exe` on headless/server environments (zero GUI subsystem) or `CineRTX.exe` with arguments on desktop:

```powershell
# 1. Print version (0.4.0) and export 20-parameter SSOT schema JSON
./rtx_headless_cli.exe --version
./rtx_headless_cli.exe --schema-json

# 2. Hardware probe and environment diagnostics
./rtx_headless_cli.exe --probe

# 3. Default out-of-the-box 4K 60fps HDR10 remaster
# (AV1 10-bit + NVOFA optical flow + VSR 4 + TrueHDR 1000 nits)
./CineRTX.exe --input in.mp4 --output out_4k60_hdr10.mp4

# 4. Custom HDR peak luminance (1500 nits) and VSR tier
./CineRTX.exe --input in.mp4 --output out.mp4 --peak-nits 1500 --vsr-quality 4

# 5. TensorRT RIFE v4.25 deep-learning master interpolation
./CineRTX.exe --input in.mp4 --output out_rife.mp4 --rife-engine models/rife_v4.25.engine

# 6. View full CLI options
./rtx_headless_cli.exe --help
```

### 4. DebugView Live Diagnostics
Launch Sysinternals DebugView, enable "Capture Win32 + Global Win32", and filter `[CineRTX]` — every decode/encode/probe event streams there in real time, in both UI languages.

---

## Building from Source

### Requirements
- **OS**: Windows 10/11 64-bit
- **Compiler**: Visual Studio 2026 / 2022 (MSVC 19.51+ with C++26 `/std:c++latest`, `/W4 /WX`, and `/arch:AVX2`), or the bundled Zig 0.13 fallback toolchain
- **Build System**: CMake 3.25+ and Ninja
- **Hardware**: NVIDIA GeForce RTX 30 / 40 / 50 Series or RTX Professional GPU
- **Driver**: NVIDIA Game Ready / Studio Driver 550.00+ (621.24+ WHQL recommended for RTX 50 Series)

### Build & Audit Steps
```powershell
# 1. Configure (MSVC preset; on machines without VS use the zig-windows preset)
cmake --preset windows-msvc-ninja

# 2. Compile targets (CineRTX.exe, rtx_headless_cli.exe, rtx_ngx_bridge.dll, rtx_tests.exe)
cmake --build --preset windows-msvc-ninja

# 3. Run full unit test suite (147 tests)
./.vs/build/cmake/windows-msvc-ninja/rtx_tests.exe

# 4. Run Phase 12 five native CMake hard audit gates
cmake -P cmake/AuditTypes.cmake
cmake -P cmake/AuditSloc.cmake
cmake -P cmake/AuditSpdx.cmake
cmake -P cmake/AuditPrivacy.cmake
cmake -DTARGET_BIN=.vs/build/cmake/windows-msvc-ninja/CineRTX.exe -DMODE=GUI -P cmake/AuditImports.cmake
cmake -DTARGET_BIN=.vs/build/cmake/windows-msvc-ninja/rtx_headless_cli.exe -DMODE=HEADLESS -P cmake/AuditImports.cmake
```

Binaries land under `.vs/build/cmake/<preset>/` (see `CMakePresets.json`).

---

## Packaging Portable Release

To create a clean, standalone release package (~34 MB):
```powershell
cmake -DBUILD_DIR=.vs/build/cmake/windows-msvc-ninja -P cmake/PackageRelease.cmake
```
Generates `.vs/build/dist/CineRTX-v0.4.0-windows-x64.zip` accompanied by SHA-256 manifests and instructions.

---

## Architecture Overview

```
 [ Input Video Container (MP4/MKV) ]
                |
                v  (Static FFmpeg Demuxer)
 [ Compressed Bitstream Packets ] --------> [ Audio Stream Copy ]
                |                                     |
                v                                     |
 [ NVDEC Decoder ] (cuvidMapVideoFrame64)             |
                |                                     |
                v  (Device Memory NV12)               |
 [ Dual-Path True Interpolation ]                     |
   (NVOFA optical flow | TensorRT RIFE)               |
                |                                     |
                v                                     |
 [ D3D12 / CUDA Zero-Copy Interop ]                   |
                |                                     |
                v                                     |
 [ RTX Video VSR + TrueHDR ] (NGX Bridge)             |
                |                                     |
                v  (RGBA16F Surface)                  |
 [ Fused CUDA P010 Pack Kernel ]                      |
                |                                     |
                v  (P010 10-bit BT.2020 / PQ)         |
 [ NVENC Hardware Encoder ]                           |
   (AV1 10-bit / HEVC Main10)                         |
                |                                     |
                v                                     v
 [ Static FFmpeg Muxer (av1C / HVCC Extradata + Audio Passthrough) ]
                |
                v
 [ Remastered 4K HDR10 Output (MP4/MKV) ]
```

---

## Licensing & Open-Source Compliance

- Core engine code is licensed under **GPL-3.0-or-later WITH Plugin-Exception** (see [LICENSE](LICENSE) and [COPYING.EXCEPTION](COPYING.EXCEPTION)).
- NVIDIA proprietary SDK components (`rtx_ngx_bridge.dll`, `nvngx_vsr.dll`, `nvngx_truehdr.dll`) run behind strict dynamic boundaries governed by the Plugin-Exception.
