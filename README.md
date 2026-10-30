# CineRTX

> Next-generation pure-GPU video remastering & NLE engine for NVIDIA RTX. Zero-copy pipeline integrating RTX Video Super Resolution (VSR) and TrueHDR inverse tone mapping, with OFA optical-flow and TensorRT RIFE bridge contracts for frame interpolation.

This repository contains a buildable, tested implementation of the engine's
portable core and its runtime plugin boundary. It deliberately separates what
can be verified on any machine from what requires proprietary hardware.

**Product contract.** `CineRTX.exe` is the only product target. Both its GUI
and its CLI container mode execute exactly one pixel path: NVDEC decode →
RTX VSR + TrueHDR remaster in device memory → 4K HDR10 HEVC Main10 encode.
There is no passthrough mode, no feature switch, and no per-frontend
difference. Feature availability is decided only by the presence of the
NVIDIA runtime components at run time.

| Area | State in this source drop |
| --- | --- |
| Product container transcode (NVDEC → VSR + TrueHDR → NVENC HEVC Main10) | Implemented; shared by GUI and CLI, zero host pixel copies |
| CPU reference pipeline (decode → FRUC → enhance → P010 → encode → mux) | Verification harness only, deterministic, covered by 137 tests |
| HDR10 metadata synthesis (VUI, SEI 137/144, AV1 OBU, JSON sidecar) | Implemented and unit-tested |
| VRAM pool, watermark backpressure, TDR guard, DAG scheduler | Implemented and tested with simulated device memory |
| NVDEC hardware decode (`cuvidCreateDecoder` / `cuvidDecodePicture` / `cuvidMapVideoFrame64`) | Implemented; runtime-loaded, exercised on real Maxwell hardware |
| NVENC hardware encode (`NvEncRegisterResource` + `CUdeviceptr` direct path) | Implemented; HEVC Main/Main10 and H.264 High |
| Zero-copy `NVDEC → VRAM pool → NVENC` main loop | Implemented; host pixel traffic is measured and reported as 0 |
| FFmpeg demux/mux (`libavformat` + `*_mp4toannexb` bitstream filters) | Implemented; always linked from the static FFmpeg build (no prefix, no switch) |
| RIFE/TensorRT configuration and tensor marshalling | Implemented; TensorRT 10.13.3.9 is provisioned at configure time and resolved with `GetProcAddress` at run time, so `--build-rife` builds the engine in-process and falls back to the sanctioned `trtexec` command when the runtime is not resolvable |
| Pure C shim ABI + loader (`LoadLibraryExW`/`dlopen`) | Implemented; shim ABI bound in-image, NVIDIA driver modules runtime-loaded |
| NVIDIA shim probe and component diagnostics | Implemented; exercised on the development machine |
| D3D12/NGX bridge | Implemented; every Windows build links the RTX Video SDK, stages `rtx_ngx_bridge.dll` with NVIDIA's runtime DLLs under `build/external/plugins/nvidia_rtx`, and mirrors them to `plugins/nvidia_rtx` beside `CineRTX.exe` so the executable loads them at run time |
| RTX VSR + TrueHDR execution | Implemented in the SDK-backed NGX bridge; this is the product path, Turing or newer |
| NvOFFRUC / RIFE bridges | ABI, loader and TensorRT engine builder compiled into every image; the Optical Flow and TensorRT runtimes are staged beside `CineRTX.exe` and reported by `--probe`, but no interpolation execution is wired into the product remaster loop yet |

The zero-copy codec foundation (NVDEC decode + NVENC encode + VRAM hand-off)
runs on Maxwell (sm_52) and newer. RTX Video (VSR, TrueHDR) is a mandatory part
of the product with no build-time or command-line switch: at run time it
requires Turing (sm_75) or newer plus the installed NVIDIA runtimes, and
`--probe` reports a structured diagnostic instead of silently running a
different colour pipeline. The NvOFFRUC/RIFE execution gap is tracked in
[Limitations](#limitations); the bridge itself is always compiled in.

## Quick Start (快速上手)

### 1. Build from Source (源码编译)

**Requirements**: CMake 3.25+ and a C++20 compiler (Visual Studio 2022, GCC 13+, Clang 16+, or Zig 0.13).

```bash
# 1. 一键配置工程 (自动静默拉取 nv-codec-headers MIT 子模块与 RTX Video SDK；Windows 始终构建 NGX 插件，规范落点 build/external/plugins/nvidia_rtx，并镜像到 CineRTX.exe 同级的 plugins/nvidia_rtx)
cmake -B build -S . -DCMAKE_BUILD_TYPE=RelWithDebInfo

# 2. 并行编译全部目标 (单一双模式 GUI/CLI 可执行文件 CineRTX 与测试集)
cmake --build build --config RelWithDebInfo -j
```

### 2. Run Native GUI (运行极简原生单 exe GUI)

```bash
# 启动 Win32 原生 GDI 即时态自绘界面 (无参数即进入 GUI；18.6 ms 极速启动，1.2 MB 超低显存)
./build/RelWithDebInfo/CineRTX.exe
```

### 3. Verification & Diagnostic Probe (自检与硬件探针)

```bash
# 运行全量单元与集成测试套件 (137/137 项全通过)
./build/RelWithDebInfo/rtx_tests.exe

# 探测本地 GPU 架构、NVENC/NVDEC 引擎与 NGX VSR / TrueHDR 插件状态
./build/RelWithDebInfo/CineRTX.exe --probe
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

The diagram is the reference stage order. The shipped product executes
demux → NVDEC → VSR + TrueHDR → P010 pack → NVENC; the FRUC stage is present in
the CPU verification pipeline only until NvOFFRUC/RIFE execution is wired in
(see [Limitations](#limitations)). Stage boundaries are otherwise identical in
both backends. The CPU reference backend performs the same colour math with
host memory so that ordering, timestamps, scene-cut behaviour, and metadata can
be tested without a GPU. The NVIDIA backend is reached only through the
versioned C ABI in [`include/shim/rtx_shim_abi.h`](include/shim/rtx_shim_abi.h);
the shim is linked into the image as a static archive and resolves NVIDIA
driver modules at runtime, so `CineRTX` never links an NVIDIA import library.

Key design points:

- **Interpolate before upscaling (reference design).** The reference pipeline
  interpolates at 1080p, so a 4K RGBA16F intermediate is never cached across
  frames. The product remaster loop currently has no FRUC stage.
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
- Ninja for the Zig/native presets (provisioned when missing on Windows)

The `zig-windows` and `zig-linux` presets prefer the compiler already installed
on the host: MSVC, Clang and GCC drive the build whenever one is configured, so
Visual Studio 2022/2026 configures with the MSVC toolset it supplies and needs
nothing else. Zig 0.13 is the fallback for dependency-free hosts: when no
native compiler exists, the toolchain downloads the pinned kit into
`build\external\tools\zig` (SHA-256 verified) and reuses it from then on. Name
a Zig explicitly with `RTX_ZIG` or `-DRTX_ZIG_EXECUTABLE=...` to force the
portable build (the configuration CI uses). On Windows a missing Ninja is
provisioned the same way before `project()`; set `HTTPS_PROXY`/`HTTP_PROXY`
before configuring if the network needs a proxy (curl honours them, and the
same settings cover the nasm/make downloads during the FFmpeg build).

`build.bat` is only for hosts without CMake: it downloads that single tool and
hands the rest to CMake.

```powershell
# Windows: MSVC/Clang/GCC when present, otherwise portable Zig 0.13
cmake --preset zig-windows
cmake --build --preset zig-windows
ctest --preset zig-windows
```

```powershell
# Windows, Visual Studio 2022 or 2026 with the MSVC toolset
# Run from a Developer PowerShell (cl.exe must be on PATH).
cmake --preset windows-msvc-ninja
cmake --build --preset windows-msvc-ninja
ctest --preset windows-msvc-ninja
```

```powershell
# Windows, Visual Studio 2022 multi-config generator
cmake --preset windows-msvc
cmake --build --preset windows-msvc
ctest --preset windows-msvc
```

In Visual Studio 2022/2026, open the folder and configure: Visual Studio picks
the first applicable preset, **Windows MSVC (Ninja, VS 2022/2026)**, which uses
the MSVC environment Visual Studio provides and builds with `cl.exe`, so
breakpoints and PDB symbols work out of the box. The `zig-windows` preset also
prefers that same native compiler when selected explicitly. A forced Zig build
(`RTX_ZIG=...`) produces a GNU-ABI binary whose debug information is DWARF
rather than PDB.

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
`out/CineRTX-<version>-<platform>.zip`, a `SHA256SUMS.txt`, a per-file
`manifest.json`, and a `.sha256` sidecar for the archive. The script refuses
to package a binary with a static NVIDIA import or a missing required file.

Useful CMake options:

| Option | Default | Meaning |
| --- | --- | --- |
| `RTX_BUILD_TESTS` | `ON` | Build the unit/integration suite |
| `RTX_BUILD_GUI` | `ON` | Build the Win32 DirectComposition GUI front end into `CineRTX` (Windows only) |
| `RTX_WARNINGS_AS_ERRORS` | `OFF` | Promote compiler warnings to errors |
| `RTX_OF_SDK_ROOT` | provisioned | Optical Flow SDK 5.0.7 root; defaults to the extracted `build/external/optical_flow_sdk` |
| `RTX_TENSORRT_ROOT` | provisioned | TensorRT 10.13.3.9 root; defaults to the extracted `build/external/tensorrt` |

The media container layer is **not optional**. `cmake/RtxFfmpegStatic.cmake`
resolves the surgically trimmed static archives (`avformat`, `avcodec`,
`avutil`, `swresample`) at configure time and binds them to `rtx_core`, so every
executable is a single self-contained file. On Windows, CMake automatically
initializes the `third_party/ffmpeg` git submodule, provisions NASM and Make
tooling, and runs `tools/build_ffmpeg_static.bat` to build the static archives;
on Linux, it resolves the development libraries via `pkg-config`. There is no build
in which the engine lacks container I/O.

## Running

```text
CineRTX --simulate 12 --json
CineRTX --input-raw source.nv12 --width 1920 --height 1080 \
        --output-raw master.p010 --hdr-sidecar master.hdr.json
CineRTX --probe --json
CineRTX --self-test
CineRTX --build-rife --onnx rife.onnx --engine rife.engine
```

The raw path consumes a headerless NV12 file and writes tight 10-bit P010. It
is intended for conformance vectors and CI, not as a substitute for the FFmpeg
demuxer.

### Product transcode and verification paths

With an NVIDIA driver installed (the shim is linked into `CineRTX`; no
separate `rtx_shim.dll` ships), the product path is the container input path:

```powershell
# The product path: NVDEC -> VSR + TrueHDR -> P010 -> NVENC HEVC Main10
# --backend is optional here; a missing NVIDIA runtime is reported as an error,
# never replaced by a CPU or passthrough result.
CineRTX --input source.mp4 --output out.mp4

# Verification harness only: raw NV12 -> P010 artefact, explicitly accounted
# host staging, used to exercise NVENC/NVDEC without the remaster bridge.
CineRTX --input-raw source.nv12 --width 1920 --height 1080 \
        --output-raw out.p010 --backend nvidia
```

`--codec`, `--bit-depth`, `--input-raw`, `--simulate` and the CPU reference
pipeline belong to the verification harness. They cannot change the product
container output, which is always 4K HDR10 HEVC Main10.

The report prints the measured PCIe traffic for the main loop:

```text
zero-copy main loop: yes
host pixel copies: decoder=0, encoder=0; D2H=0 bytes, H2D=0 bytes
device-to-device bytes: 3110400
```

`--input-raw` uses the explicitly accounted host staging surface (it is a
bitstream verification artefact, not the zero-copy claim); the product
container path never transfers a pixel through host memory.

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
137/137 tests, 6/6 CTest, and 4/4 Gate-1 checks for both the 8-bit
(241 frames) and 10-bit Main10 verification transcode paths, each reporting
`D2H=0`, `H2D=0`. (The phase-2 rework re-confirmed all of the above after the
`ulNumOutputSurfaces` fix; see [`docs/phase2/rework_phase2_verification.md`](docs/phase2/rework_phase2_verification.md).)

## Phase 2 (W4–W6) status

Phase 2 construction and validation are complete and formally approved by the domain expert committee (see
[`docs/spec/gate2.md`](docs/spec/gate2.md), [`docs/phase2/phase2_w5_w6_completion_report.md`](docs/phase2/phase2_w5_w6_completion_report.md), and
[`docs/phase2/CineRTX 开源终审与验收评估.md`](docs/phase2/CineRTX%20开源终审与验收评估.md)). Highlights:

- **Done and verified on hardware (RTX 5060 Laptop GPU, sm_120)**: Full end-to-end 4K HDR10 remaster pipeline (decode → D3D12/CUDA interop → NGX VSR / TrueHDR → P010 packing → NVENC HEVC Main10) sustained at **128.7 FPS**, with zero host copy (`D2H=0`, `H2D=0`), zero frame drop, and zero VRAM leak across a 174-minute continuous soak run.
- **Native Single-exe GUI**: Win32 immediate-mode GDI GUI (now the no-argument mode of the single `CineRTX.exe`; the Phase-2 standalone GUI measured ~331 KB, 18.6 ms cold startup, 1.2 MB VRAM footprint) with real-time visual playback, interactive pan/zoom canvas, dual-pane split comparison, and dynamic parameter hot-reloading.
- **D3D12 ↔ CUDA Interop & NGX Bridge**: Headless D3D12 device with shared textures imported into CUDA as `CUsurfObject`, bidirectional texture round trip verified in CTest, and clean dynamic plugin loading of NVIDIA RTX Video SDK runtime components (`_nvngx.dll`, `nvngx_vsr.dll`, `nvngx_truehdr.dll`).
- **Comprehensive Quality Gates**: 137/137 unit/integration tests pass (100%), 6/6 CTest gates pass, and automated SPDX / PE import table audits report 0 violations.

Gate 2 is **fully approved (Grade A - Industrial Production Ready)**; Gate 1 and Gate 2 are all green.

### Backend selection

Container input has exactly one backend: the NVIDIA remaster path. `--input`
does not require `--backend nvidia`, and `--backend cpu` is rejected for
container input because no CPU product path exists. `--backend` is a
verification selector for `--simulate` and `--input-raw`:

- `--backend cpu` runs the deterministic CPU reference harness.
- `--backend auto` (the default) runs the CPU harness for `--simulate`.
- `--backend nvidia` is strict: it probes the shim and refuses to continue if
  the device, driver, or required components are missing. It is required for
  `--input-raw`.

The RTX Video features (VSR, TrueHDR) need Turing (sm_75) or newer plus the
installed NVIDIA runtimes. A Maxwell GPU can still run the verification codec
path but rejects an upscaling request:

```text
$ CineRTX --simulate 2 --width 64 --height 64 --backend nvidia
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
probe itself is a diagnostic. Automation that needs a pass/fail gate should
inspect `result`/`capabilities` in the JSON, run `--input` (which fails when
the remaster is unavailable), or use `--backend nvidia` with `--simulate` /
`--input-raw`.

`--probe --json` escapes every non-ASCII code point as `\uXXXX`, so localized
component notes remain valid JSON even when a shell decodes a child process's
stdout with a legacy code page. Human-readable output is still UTF-8; use a
UTF-8 terminal for localized diagnostics. The human-readable probe also prints
an `NGX bridge probe` block with the resolved bridge path (`RTX_NGX_BRIDGE_PATH`
or the sidecar) and the per-feature VSR/TrueHDR/D3D12/CUDA caps, which is the
fastest way to tell a staging problem from a driver problem. JSON mode keeps
the shim schema unchanged.

### Runtime discovery

The shim is compiled into `CineRTX` as a static archive and binds its C ABI
in-image (`NvidiaShim::loadEmbedded()`); no `rtx_shim.dll` exists. The NGX
bridge is different by design: the proprietary RTX Video SDK stays inside
`rtx_ngx_bridge.dll`, which is loaded at run time from
`RTX_NGX_BRIDGE_PATH`, then `<exe>/plugins/nvidia_rtx/rtx_ngx_bridge.dll`,
then `<exe>/rtx_ngx_bridge.dll`. Non-Windows hosts embed the SDK-free bridge
instead of producing a sidecar.

The bridge finds `nvngx_vsr.dll` and `nvngx_truehdr.dll` next to itself, in
`<bridge dir>/plugins/nvidia_rtx/`, or in the directory named by
`RTX_NGX_PLUGIN_DIR`. An override only wins when it actually contains a feature
DLL, so a stale environment variable cannot hide the copies sitting beside the
bridge. The shim probe reports those feature DLLs directly, including their
paths, so `--probe` says "available" whenever the files are actually present.

Every Windows build re-runs the staging step and copies `rtx_ngx_bridge.dll`,
`nvngx_vsr.dll` and `nvngx_truehdr.dll` into `<exe>/plugins/nvidia_rtx`; the
build fails if any of the three is missing. An incremental build whose
`CineRTX.exe` was already up to date can therefore no longer keep a stale or
empty plugin directory.

The two SDKs behind frame interpolation are provisioned the same way as the RTX
Video SDK, by `cmake/RtxNvidiaSdks.cmake`:

| SDK | Archive (cached in `build/external`) | Extracted to |
| --- | --- | --- |
| Optical Flow 5.0.7 | `Optical_Flow_SDK_5.0.7.zip` | `build/external/optical_flow_sdk` |
| TensorRT 10.13.3.9 | `TensorRT-10.13.3.9.Windows.win10.cuda-13.0.zip` | `build/external/tensorrt` |

Both archives are pinned by SHA-256 and verified on every configure, so a
cached copy is reused offline and the placeholder download URL is only tried
when the archive is missing. A cached archive whose digest does not match stops
the configure step with both digests printed; the file is never deleted by the
build. The RTX Video SDK block still treats a download failure as fatal, while a
failed Optical Flow or TensorRT download only warns, because NVIDIA gates those
endpoints behind a browser session and the affected features already report
themselves as unavailable at run time.

Once both SDKs resolve, every Windows build stages the runtime modules beside
`CineRTX.exe` and fails if one is missing:

- `NvOFFRUC.dll` and `cudart64_110.dll` — the FRUC DLL imports the CUDA runtime,
  and loading it on its own fails with error 126.
- `nvinfer_10.dll`, `nvonnxparser_10.dll`, `nvinfer_plugin_10.dll` and
  `nvinfer_builder_resource_10.dll` — the TensorRT runtime, the ONNX parser the
  in-process engine build calls, and the builder's kernel resource.

TensorRT is **not linked**. The Zone C bridge (`rtx_ngx_bridge.dll`, implemented in
`src/shim/ngx_bridge/rife_builder.cpp`) dynamically resolves
`createInferBuilder_INTERNAL`, `createNvOnnxParser_INTERNAL` and
`getInferLibVersion` with `GetProcAddress` at run time; those are the plain C
entry points behind the inline `nvinfer1::createInferBuilder` and
`nvonnxparser::createParser` helpers, so no C++ symbol has to be decorated by
hand. Two consequences are deliberate: `CineRTX.exe` imports no NVIDIA DLL
(`docs/spec/gate3.md` §1.2 stays green), and a host without the TensorRT runtime
still starts and falls back to the sanctioned `trtexec` command instead of
failing in the loader.

What else is resolved at runtime are the NVIDIA driver modules, through a
deliberately narrow search order:

1. `%SystemRoot%\System32` — bare driver names (`nvcuda.dll`, `d3d12.dll`,
   `dxgi.dll`, and the system fallback for `nvcuvid.dll` / `nvEncodeAPI64.dll`)
   are opened with `LoadLibraryExW(LOAD_LIBRARY_SEARCH_SYSTEM32)` +
   `GetProcAddress`, so they never become static imports.
2. `RTX_NVIDIA_DIR` (semicolon/colon-separated directories), then the
   executable's directory — for operator-supplied SDK modules.

`NvOFFRUC.dll` and `nvinfer_10.dll` are in the second group only: neither has a
System32 fallback, so a copy that is not beside the executable (or listed in
`RTX_NVIDIA_DIR`) is reported missing by `--probe` no matter what the driver
package installs. `NvOFFRUC.dll` also has to sit next to `cudart64_110.dll`,
because the loader resolves the sibling CUDA runtime from the DLL's own
directory. `nvinfer_11.dll` is probed as a fallback so an installed TensorRT
11.x is reported rather than hidden, but the RIFE engine builder still refuses
any version other than 10.13.3.9.

The loader never searches the current working directory or an unqualified
`PATH`; SDK components that ship beside the application must be opened by
explicit path, which blocks the classic working-directory DLL-planting attack.

## Testing

The suite is dependency-free and runs on machines without a GPU:

```text
[==========] 137 test(s) run, 0 failed (612 ms)
100% tests passed, 0 tests failed out of 6
```

CTest entries:

- `rtx_unit_tests` — 137 cases across colour, HDR metadata, rational/timestamp
  mapping (including the Flicks 705.6 MHz timeline), zero-disk container I/O,
  scene detection, scheduling, VRAM pools and the three-tier watermark, RIFE,
  shim loading, status codes, the end-to-end pipeline, the headless
  D3D12↔CUDA interop round trip, the strongly-typed i18n table, the FRUC bridge
  ABI, the even/odd ping-pong scheduler and the AVX2 audio mixer.
- `cli_self_test` — 4 input frames → 8 output frames with a scene cut.
- `cli_simulate_json` — JSON report fields and frame accounting.
- `cli_rife_command` — TensorRT version lock and the `--build-rife` path with
  its `.engine.sha256` integrity sidecar.
- `rtx_fruc_abi_contract_test` — Gate 5: pure C ABI isolation and the hard-cut
  duplicate bypass (`rtx_tests --filter=fruc_abi_*`).
- `cli_gui_viewport_smoke_test` — Gate 6: DirectComposition viewport throttle,
  occlusion bypass and split clamp (`--viewport-self-test`).

The shim, NGX bridge and FRUC bridge are static archives (`rtx_shim_static`,
`rtx_ngx_bridge_static`, `rtx_fruc_bridge_static`) linked into `rtx_tests`, so
`test_shim.cpp` and `test_offruc_bridge.cpp` call the C ABI entry points
directly; loader behaviour is not mocked, and the former `RTX_TEST_SHIM_PATH`,
`RTX_TEST_NGX_PATH` and `RTX_TEST_FRUC_BRIDGE_PATH` variables no longer exist.
`CineRTX` binds the embedded shim; on Windows the NGX bridge is always built
and mirrored to `<exe>/plugins/nvidia_rtx` as a separately loaded plugin, while
non-Windows hosts embed the SDK-free bridge.

CI runs the same gates on Linux GCC, Windows MSVC, and Zig 0.13 for Windows:
build, CTest, SPDX audit, and import-table audit. A `v*` tag additionally
builds the audited Windows archive and publishes it with its SHA-256 sidecar;
see [`.github/workflows/ci.yml`](.github/workflows/ci.yml). The archive contains
the GPLv3 `CineRTX.exe`; the separately licensed NGX plugin is deployed by the
build/install layout (`plugins/nvidia_rtx/`) and is not embedded in the image.

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

## Licensing & Open-Source Compliance

CineRTX employs a dual-mechanism compliance architecture combining a GPLv3 Section 7 runtime plugin exception with a pure C ABI physical boundary:

- **Core Engine, I/O, CLI, Native GUI, Tests & Build System**: Licensed under the **GNU General Public License v3.0 or later** ([`LICENSE`](LICENSE)), supplemented by the **CineRTX Plugin-Exception** ([`COPYING.EXCEPTION`](COPYING.EXCEPTION)) under GPLv3 Section 7 (`SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception`). The `LICENSE` file is kept 100% verbatim as published by the Free Software Foundation so automated scanners (including GitHub Licensee) accurately identify the primary GPL-3.0 license.
- **Runtime Shim, Loader, Pure C ABI Headers & NGX Bridge (`include/shim/`, `src/shim/`)**: Permissively dual-licensed under **MIT OR Apache-2.0** ([`LICENSE-MIT`](LICENSE-MIT) / [`LICENSE-APACHE`](LICENSE-APACHE)).
- **Vendored Codec Headers (`third_party/nv-codec-headers`, git submodule pinned to `n13.0.19.1`)**: Licensed under the **MIT License** (declarations only; zero proprietary binaries or static import libraries).

No NVIDIA proprietary header, `.lib`, or `.a` is ever included in or statically linked by the GPLv3 Covered Work. Runtime plugin loading crosses only the pure C ABI (`rtx_shim_abi.h` and `ngx_bridge_abi.h`). Run `pwsh tools/check_spdx.ps1` and `pwsh tools/audit_imports.ps1` to verify full source and binary compliance. See [`docs/spec/compliance.md`](docs/spec/compliance.md) for complete legal and architectural details.

## Limitations

- The product transcode needs an NVIDIA GPU with an NVDEC engine for the input
  codec and an NVENC HEVC engine for the output codec. `cuvidGetDecoderCaps` is
  queried up front, so an unsupported combination fails with
  `RTX_ERR_ARCH_UNSUPPORTED` instead of stalling. Maxwell GM204 (GTX 970/970M),
  for instance, has no HEVC decoder and the raw verification path falls back to
  H.264 with a warning.
- RTX VSR and TrueHDR require Turing (sm_75) or newer plus the installed RTX
  Video runtime DLLs. They are reported honestly rather than emulated.
- **NvOFFRUC/RIFE execution is not yet wired into the product loop.** The
  FRUC bridge ABI, the hard-cut duplicate bypass, the RIFE tensor layout and
  the TensorRT engine builder are implemented and tested; the Optical Flow and
  TensorRT 10.13.3.9 runtimes are provisioned, staged beside `CineRTX.exe` and
  reported as present by `--probe`. What is still missing is the dispatch:
  `rtx_fruc_bridge_interpolate` returns `RTX_FRUC_ERR_UNSUPPORTED` for a real
  interpolated frame and `nv_backend_shim` returns `RTX_ERR_NOT_IMPLEMENTED`
  when `enable_rife` is requested, so neither backend is called from the
  remaster loop. Frame interpolation cannot be claimed as a product feature
  until that call site exists.
- The D3D12/NGX bridge requires the RTX Video SDK and the Windows SDK. Every
  Windows configure fetches the pinned SDK archive into `build/external`,
  stages `rtx_ngx_bridge.dll` plus NVIDIA's runtime DLLs under
  `build/external/plugins/nvidia_rtx`, and mirrors them to
  `plugins/nvidia_rtx` beside `CineRTX.exe`. At run time the executable
  searches `RTX_NGX_BRIDGE_PATH`, then `<exe>/plugins/nvidia_rtx`, then the
  executable directory; the bridge is never linked into the GPLv3 image.
  There is no SDK-free Windows build and no switch that removes VSR or TrueHDR
  from the product.
- Container transcode needs no external FFmpeg installation: the container layer
  is statically inlined from the self-contained `third_party/ffmpeg` submodule (Gate 3.0).
  CMake compiles and binds the surgical static archives automatically at configure time.
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
src/shim/ngx_bridge/ D3D12/NGX bridge (SDK-backed plugin on Windows, SDK-free stub elsewhere)
src/shim/backends/   RIFE/TensorRT configuration and tensor packing
third_party/nv-codec-headers/  MIT nv-codec-headers submodule (decode + encode ABI)
tests/               Dependency-free test harness and 137 cases
cmake/               Zig toolchain file
tools/               Build, SPDX, import-table, packaging, and Gate-1 verification scripts
docs/                Architecture, ABI, compliance, and test notes
```

