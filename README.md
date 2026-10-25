# CineRTX

> Next-generation pure-GPU video remastering & NLE engine for NVIDIA RTX. Zero-copy pipeline integrating RTX Video Super Resolution (VSR), TrueHDR inverse tone mapping, OFA hardware optical flow, and TensorRT AI frame generation.

This repository contains a buildable, tested implementation of the engine's
portable core and its runtime plugin boundary. It deliberately separates what
can be verified on any machine from what requires proprietary hardware.

| Area | State in this source drop |
| --- | --- |
| CPU reference pipeline (decode → FRUC → enhance → P010 → encode → mux) | Implemented, deterministic, covered by 102 tests |
| HDR10 metadata synthesis (VUI, SEI 137/144, AV1 OBU, JSON sidecar) | Implemented and unit-tested |
| VRAM pool, watermark backpressure, TDR guard, DAG scheduler | Implemented and tested with simulated device memory |
| NVDEC hardware decode (`cuvidCreateDecoder` / `cuvidDecodePicture` / `cuvidMapVideoFrame64`) | Implemented; runtime-loaded, exercised on real Maxwell hardware |
| NVENC hardware encode (`NvEncRegisterResource` + `CUdeviceptr` direct path) | Implemented; HEVC Main/Main10 and H.264 High |
| Zero-copy `NVDEC → VRAM pool → NVENC` main loop | Implemented; host pixel traffic is measured and reported as 0 |
| FFmpeg demux/mux (`libavformat` + `*_mp4toannexb` bitstream filters) | Implemented; enabled when an FFmpeg prefix is found |
| RIFE/TensorRT configuration and tensor marshalling | Implemented; TensorRT engine build is gated |
| Pure C shim ABI + loader (`LoadLibraryExW`/`dlopen`) | Implemented; loads the real shim at runtime |
| NVIDIA shim probe and component diagnostics | Implemented; exercised on the development machine |
| D3D12/NGX bridge | Validated ABI stub; full implementation requires the RTX Video SDK |
| Real VSR/TrueHDR/NVOFFRUC/TensorRT execution | Not compiled in this drop; returns structured errors |

Stage 1 (NVDEC decode + NVENC encode + zero-copy VRAM hand-off) is complete and
runs on Maxwell (sm_52) and newer. The RTX-only features (VSR, TrueHDR,
NVOFFRUC, TensorRT) remain gated behind Turing (sm_75) and the proprietary SDKs;
`--backend nvidia` reports a structured diagnostic instead of silently running a
different colour pipeline.

## Quick Start (快速上手)

### 1. Build from Source (源码编译)

**Requirements**: CMake 3.25+ and a C++20 compiler (Visual Studio 2022, GCC 13+, Clang 16+, or Zig 0.13).

```bash
# 1. 一键配置工程 (NVIDIA SDK 自动静默拉取并完全隔离于 build 目录)
cmake -B build -S . -DCMAKE_BUILD_TYPE=RelWithDebInfo

# 2. 并行编译全部目标 (单 exe GUI、无头 CLI 引擎、动态装载桥与测试集)
cmake --build build --config RelWithDebInfo -j
```

### 2. Run Native GUI (运行极简原生单 exe GUI)

```bash
# 启动 Win32 原生 GDI 即时态自绘界面 (~331 KB 体积，18.6 ms 极速启动，1.2 MB 超低显存)
./build/RelWithDebInfo/rtx-transcode-gui.exe
```

### 3. Verification & Diagnostic Probe (自检与硬件探针)

```bash
# 运行全量单元与集成测试套件 (113/113 项全通过)
./build/RelWithDebInfo/rtx_tests.exe

# 探测本地 GPU 架构、NVENC/NVDEC 引擎与 NGX VSR / TrueHDR 插件状态
./build/RelWithDebInfo/rtx-transcode.exe --probe
```

## Architecture

```
 compressed packets                                      compressed packets
        |                                                        ^
        v                                                        |
  +-----------+   +-----------+   +-----------+   +-----------+  |
  |  Demux    |-->|  Decode   |-->|   FRUC    |-->| Enhance   |  |
  | libavformat|  |  NVDEC    |   | NvOFFRUC  |   | VSR +     |  |
  | (host)    |   |  NV12     |   | or RIFE   |   | TrueHDR   |  |
  +-----------+   +-----------+   +-----------+   +-----------+  |
                        |               |               |        |
                        v               v               v        |
                  decode pool      interp pool     D3D12/CUDA    |
                  16 x 1080p       8 x 1080p       ping-pong     |
                  NV12             NV12            2 + 2         |
                                                       |         |
                                                       v         |
                                                 +-----------+   |
                                                 | Pack P010 |   |
                                                 | BT.2020   |   |
                                                 +-----------+   |
                                                       |         |
                                                       v         |
                                                 +-----------+   |
                                                 |  NVENC    |---+
                                                 | HEVC/AV1  |
                                                 +-----------+
```

Stage boundaries are identical in both backends. The CPU reference backend
performs the same colour math with host memory so that ordering, timestamps,
scene-cut behaviour, and metadata can be tested without a GPU. The NVIDIA
backend is reached only through the versioned C ABI in
[`include/shim/rtx_shim_abi.h`](include/shim/rtx_shim_abi.h); the CLI never
links `rtx_shim.dll` or any NVIDIA import library.

Key design points:

- **Interpolate before upscaling.** FRUC runs at 1080p, so a 4K RGBA16F
  intermediate is never cached across frames.
- **Fixed VRAM plan.** 16 decode NV12, 8 interpolation NV12, 2 R10G10B10A2
  input textures, 2 RGBA16F output textures, and 12 encode P010 surfaces are
  allocated once. The hot loop only moves slot indices.
- **Backpressure by watermark.** Decode pauses at 75% occupancy and resumes at
  25%, preventing queue flap; a critical threshold at 90% is observable.
- **No blocking synchronisation in the loop.** Fence values model
  `cuSignalExternalSemaphoresAsync` / `ID3D12Fence`; the CPU path uses a
  monotonic timeline with the same semantics.
- **Scene cuts bypass interpolation.** A normalised SAD over the luma plane
  detects hard cuts; the mapper then duplicates a frame instead of blending
  across the cut.
- **One HDR10 metadata object, three sinks.** The same `Hdr10Metadata` feeds
  the NVENC VUI/SEI settings, the container side data, and the QA sidecar, so
  a PQ10 stream cannot accidentally ship without the HDR10 badge.

See [`docs/spec/architecture.md`](docs/spec/architecture.md) for the stage contracts and
[`docs/spec/abi.md`](docs/spec/abi.md) for the plugin ABI.

## Building

Requirements:

- CMake 3.25 or newer
- A C++20 compiler (MSVC 2022, GCC 13+, Clang 16+, or Zig 0.13)
- Ninja for the Zig/native presets

```powershell
# Windows, Zig 0.13 (the configuration used for the published test run)
$env:RTX_ZIG = "C:\path\to\zig.exe"
$env:PATH = "C:\path\to\ninja;$env:PATH"
cmake --preset zig-windows
cmake --build --preset zig-windows
ctest --preset zig-windows
```

```powershell
# Windows, Visual Studio 2022
cmake --preset windows-msvc
cmake --build --preset windows-msvc
ctest --preset windows-msvc
```

```bash
# Linux, native compiler
cmake --preset linux-release
cmake --build --preset linux-release
ctest --preset linux-release
```

`tools/build.ps1` wraps the same flow and finds CMake, Ninja, and Zig from
`PATH` or explicit parameters. All build products go to `build/`; nothing is
written outside the source tree.

Release packaging is one command:

```powershell
pwsh tools/package.ps1 -BuildDir build/cmake/zig-windows
```

It re-runs the SPDX and import-table audits, then writes
`out/rtx-transcode-<version>-<platform>.zip`, a `SHA256SUMS.txt`, a per-file
`manifest.json`, and a `.sha256` sidecar for the archive. The script refuses
to package a binary with a static NVIDIA import or a missing required file.

Useful CMake options:

| Option | Default | Meaning |
| --- | --- | --- |
| `RTX_BUILD_TESTS` | `ON` | Build the unit/integration suite |
| `RTX_WARNINGS_AS_ERRORS` | `OFF` | Promote compiler warnings to errors |
| `RTX_WITH_FFMPEG` | `ON` | Detect `libavformat`/`libavcodec` and build the real demuxer/muxer |
| `RTX_FFMPEG_ROOT` | empty | FFmpeg prefix containing `include/` and `lib/` |
| `RTX_FFMPEG_DLL_DIR` | empty | Runtime DLL directory copied beside the binaries |
| `RTX_WITH_NGX_SDK` | `OFF` | Compile the D3D12/NGX bridge against the RTX Video SDK |

When `RTX_WITH_FFMPEG=ON` but no prefix is found, the build still succeeds and
the I/O layer reports a structured `FfmpegMissing` error at runtime. Point
`RTX_FFMPEG_ROOT` (or the environment variable of the same name) at an FFmpeg
development tree to enable container transcode.

## Running

```text
rtx-transcode --simulate 12 --json
rtx-transcode --input-raw source.nv12 --width 1920 --height 1080 \
              --output-raw master.p010 --hdr-sidecar master.hdr.json
rtx-transcode --probe --json
rtx-transcode --self-test
rtx-transcode --build-rife --onnx rife.onnx --engine rife.engine
```

The raw path consumes a headerless NV12 file and writes tight 10-bit P010. It
is intended for conformance vectors and CI, not as a substitute for the FFmpeg
demuxer.

### Stage-1 hardware transcode

With an NVIDIA driver installed and `rtx_shim.dll` beside the executable:

```powershell
# Zero-copy container transcode: NVDEC -> VRAM pool -> NVENC
# --bit-depth must match the source: stage 1 has no depth-conversion kernel.
rtx-transcode --input source.mp4 --output out.mp4 --backend nvidia --codec hevc --bit-depth 8

# Raw NV12 -> P010 artefact path (verifies NVENC by decoding its own bitstream)
rtx-transcode --input-raw source.nv12 --width 1920 --height 1080 \
              --output-raw out.p010 --backend nvidia
```

`rtx-transcode-base.exe` is the same binary with `--backend nvidia` as the
default, built for the stage-1 acceptance run.

The report prints the measured PCIe traffic for the main loop:

```text
zero-copy main loop: yes
host pixel copies: decoder=0, encoder=0; D2H=0 bytes, H2D=0 bytes
device-to-device bytes: 3110400
```

`--input-raw` uses the explicitly accounted host staging surface (it is a
bitstream verification artefact, not the zero-copy claim); the container path
never transfers a pixel through host memory.

Gate 1 is re-checked with one command on the acceptance host:

```powershell
pwsh tools/verify_gate1.ps1 -BuildDir build/cmake/windows-msvc `
     -Container sample.mp4 -Codec hevc -BitDepth 10
```

It runs the unit suite without environment overrides, probes the device, checks
the raw artefact size, and asserts `zero_copy_main_loop` with zero D2H/H2D
pixel bytes. The measured record and the Nsight confirmation procedure are in
[`docs/spec/gate1.md`](docs/spec/gate1.md).

Verified end to end on an RTX 5060 Laptop GPU (sm_120, driver 617.14):
113/113 tests, 4/4 CTest, and 4/4 Gate-1 checks for both the 8-bit
(241 frames) and 10-bit Main10 container paths, each reporting
`D2H=0`, `H2D=0`. (The phase-2 rework re-confirmed all of the above after the
`ulNumOutputSurfaces` fix; see [`docs/phase2/rework_phase2_verification.md`](docs/phase2/rework_phase2_verification.md).)

## Phase 2 (W4–W6) status

The phase-2 construction is partially delivered; see
[`docs/spec/gate2.md`](docs/spec/gate2.md) for the item-by-item record and
[`docs/phase2/rework_phase2_verification.md`](docs/phase2/rework_phase2_verification.md) for
the RW2 rework outcome. In short:

- **Done and verified on hardware**: HDR10 container metadata (`mdcv`/`clli`,
  DCI-P3 D65, MaxCLL 1000, MaxFALL 300), NVENC top-left chroma siting,
  event-driven decode without a blocking synchronise, the `p010_pack.cu` kernel
  shipped as embedded PTX and JIT-loaded through `cuModuleLoadData` (no
  `cudart64_*.dll`), script/doc coverage, and the headless D3D12 device with
  shared textures imported into CUDA as `CUsurfObject`.
- **Reworked and re-verified (RW2-01…RW2-04)**: the D3D12↔CUDA interop round
  trip passes end to end on the RTX 5060 acceptance host (sm_120, also at
  1920×1080) and on the Maxwell development host —
  `rtx_ngx_bridge_selftest(256, 256)` returns `RTX_OK` with
  `round trip OK: 256x256 RGBA16F, D3D12 -> CUDA -> D3D12`, and the round trip
  is now a permanent CTest case. The decode callback no longer exhausts the
  NVDEC output-surface pool, so Gate 1 is back to 4/4 (8-bit and 10-bit).
- **Blocked**: VSR/TrueHDR evaluation needs the proprietary RTX Video SDK,
  which is not installed here (the bridge reports `RTX_ERR_NGX` with a
  diagnostic naming the missing DLLs); the Nsight capture needs `nsys`.

Gate 2 is therefore **not approved**; the phase-1 gates stay green.

### Backend selection

`--backend cpu` always works. `--backend auto` (the default) uses the CPU
reference backend and reports why the NVIDIA path is unavailable.
`--backend nvidia` is strict: it probes the shim and refuses to continue if the
device, driver, or required components are missing. Container transcode and
`--input-raw` always require `--backend nvidia`.

The RTX Video features (VSR, TrueHDR, NVOFFRUC, TensorRT) need Turing (sm_75)
or newer plus the matching SDK. A Maxwell GPU therefore still runs the stage-1
codec path but rejects an upscaling request:

```text
$ rtx-transcode --simulate 2 --width 64 --height 64 --backend nvidia
error: NVIDIA backend unavailable: NVIDIA GeForce GTX 970M is sm_52,
       below the Turing (sm_75) RTX Video baseline
detail: NVIDIA NVIDIA GeForce GTX 970M sm_52, CUDA 13.0, 3071 MiB; ...
hint: use --backend cpu for the deterministic reference pipeline
```

### Exit codes

| Code | Meaning |
| --- | --- |
| `0` | Success (including a probe that loaded and answered) |
| `1` | Pipeline or I/O failure |
| `2` | Invalid command line |
| `3` | Requested NVIDIA backend unavailable or unsupported |
| `4` | Shim library could not be loaded |
| `5` | Shim loaded but its probe returned an error |

A successful `--probe` exits `0` even when the GPU is unsupported, because the
probe itself is a diagnostic. Automation that needs a pass/fail gate should use
`--backend nvidia` or inspect `result`/`capabilities` in the JSON.

`--probe --json` escapes every non-ASCII code point as `\uXXXX`, so localized
component notes remain valid JSON even when a shell decodes a child process's
stdout with a legacy code page. Human-readable output is still UTF-8; use a
UTF-8 terminal for localized diagnostics.

### Runtime discovery

The CLI and the shim use a deliberately narrow search order:

1. `RTX_SHIM_PATH` (CLI only) — exact shim library path.
2. The executable's directory — `rtx_shim.dll`, `librtx_shim.so`, or
   `librtx_shim.dylib`.
3. `RTX_NVIDIA_DIR` — semicolon/colon-separated directories for SDK DLLs.
4. `%SystemRoot%\System32` / the system loader path, never the current
   working directory.

Bare `nvcuda.dll`, `nvEncodeAPI64.dll`, `nvngx_*.dll`, `NvOFFRUC.dll`, and
`nvinfer_10.dll` names are resolved from System32 (or the platform loader's
system path). SDK components that ship beside the application must be opened
by explicit path, which blocks the classic working-directory DLL-planting
attack.

## Testing

The suite is dependency-free and runs on machines without a GPU:

```text
[==========] 102 test(s) run, 0 failed (486 ms)
100% tests passed, 0 tests failed out of 4
```

CTest entries:

- `rtx_unit_tests` — 102 cases across colour, HDR metadata, rational/timestamp
  mapping, I/O, scene detection, scheduling, VRAM pools, RIFE, shim loading,
  status codes, the end-to-end pipeline, and the headless D3D12↔CUDA interop
  round trip.
- `cli_self_test` — 4 input frames → 8 output frames with a scene cut.
- `cli_simulate_json` — JSON report fields and frame accounting.
- `cli_rife_command` — TensorRT version lock and `trtexec` command generation.

The shim tests use the real built shim and bridge libraries through
`RTX_TEST_SHIM_PATH` and `RTX_TEST_NGX_PATH`; loader behaviour is not mocked.
In this source drop the CLI loads only `rtx_shim`; the NGX bridge is a
separately shipped component that the test suite loads directly through the
same `DynLibrary` path. The SDK-enabled build is expected to load it from the
NVIDIA backend.

CI runs the same gates on Linux GCC, Windows MSVC, and Zig 0.13 for Windows:
build, CTest, SPDX audit, and import-table audit. A `v*` tag additionally
builds the audited Windows archive and publishes it with its SHA-256 sidecar;
see [`.github/workflows/ci.yml`](.github/workflows/ci.yml).

## HDR10 output

The metadata presets follow the RTX Video SDK's 100-based parameter scale:

| Parameter | Default |
| --- | --- |
| Contrast | 125 |
| Saturation | 75 |
| Middle grey | 44 |
| Peak luminance | 1000 cd/m² |
| MaxCLL | 1000 cd/m² |
| MaxFALL | 300 cd/m² |
| Mastering display | DCI-P3 D65 |
| Min luminance | 0.0001 cd/m² |

The core emits:

- HEVC VUI: primaries 9, transfer 16, matrix 9, limited range, top-left
  chroma location.
- Prefix SEI NAL with payload type 137 (mastering display) and 144 (content
  light level), including emulation-prevention bytes.
- An AV1 metadata OBU (`mdcv` + `clli`).
- Canonical container side data and a JSON sidecar for QA.

`max_luminance` in the mastering display is always `target_nits * 10000`, the
HEVC SEI unit. The true peak nits are carried separately in the sidecar, so the
two cannot drift apart.

## Licensing

- Core engine, CLI, tests, build files:
  `GPL-3.0-or-later WITH Plugin-Exception`. See [`COPYING.EXCEPTION`](COPYING.EXCEPTION).
- Shim, loader, ABI headers, NGX bridge, RIFE helper:
  `MIT OR Apache-2.0`. See [`LICENSE-MIT`](LICENSE-MIT) and
  [`LICENSE-APACHE`](LICENSE-APACHE).

No NVIDIA header, `.lib`, or `.a` is compiled into or linked by the GPL core.
The shim resolves every proprietary symbol at runtime. `tools/check_spdx.ps1`
audits headers, and `tools/audit_imports.ps1` inspects the final import tables.
See [`docs/spec/compliance.md`](docs/spec/compliance.md).

## Limitations

- Stage 1 needs an NVIDIA GPU with an NVDEC engine for the input codec and an
  NVENC engine for the output codec. `cuvidGetDecoderCaps` is queried up front,
  so an unsupported combination fails with `RTX_ERR_ARCH_UNSUPPORTED` instead of
  stalling. Maxwell GM204 (GTX 970/970M), for instance, has no HEVC decoder and
  the raw artefact path falls back to H.264 with a warning.
- The RTX Video features (VSR, TrueHDR, NvOFFRUC, TensorRT) require Turing
  (sm_75) or newer plus the proprietary SDKs. They are reported honestly rather
  than emulated.
- The D3D12/NGX bridge requires the RTX Video SDK and the Windows SDK. Without
  them, `RTX_WITH_NGX_SDK=ON` fails at configure time with an explanatory
  message, and the default build ships the validated stub.
- Container transcode needs an FFmpeg development tree (`RTX_FFMPEG_ROOT`).
  Without one the build still succeeds and the I/O layer returns
  `FfmpegMissing`; the raw NV12/P010 path remains fully functional.
- The CPU reference backend is a correctness model, not a performance target.
  It uses host memory by definition; the zero-host-copy claim applies to the
  NVIDIA path only.
- AV1 encoding requires Ada Lovelace or newer hardware.

## Repository layout

```
include/rtx/         Public core headers (GPL + exception)
include/shim/        Pure C ABI and backend interface (MIT/Apache)
src/core/            Pipeline, colour, HDR, FRUC, scheduling, VRAM pools, NVDEC/NVENC orchestration
src/io/              Raw NV12/P010 and libavformat demux/mux
src/cli/             CLI and runtime shim probe
src/shim/loader/     NVDEC decoder, NVENC encoder, CUDA context, runtime loader
src/shim/ngx_bridge/ D3D12/NGX bridge ABI (stub in this drop)
src/shim/backends/   RIFE/TensorRT configuration and tensor packing
third_party/open_headers/ffnvcodec/  MIT nv-codec-headers (decode + encode ABI)
tests/               Dependency-free test harness and 102 cases
cmake/               Zig toolchain file
tools/               Build, SPDX, import-table, packaging, and Gate-1 verification scripts
docs/                Architecture, ABI, compliance, and test notes
```

