# Architecture

## 1. Scope

The engine remasters 720p/1080p SDR sources into 4K HDR10 HEVC Main10 or AV1
Main10. Its distinguishing constraint is that uncompressed frames must never
cross the PCIe bus on the NVIDIA path. That constraint drives the stage order,
the memory plan, and the plugin boundary.

This document describes the architecture as implemented. The single product
container path is NVDEC decode → RTX VSR + TrueHDR in device memory → P010 pack
→ NVENC HEVC Main10, and it is shared by the GUI and the CLI. The zero-copy
codec foundation (NVDEC decode, NVENC encode, and the VRAM hand-off) executes
on Maxwell (sm_52) and newer. VSR and TrueHDR are always built into the product
(there is no build or command-line switch that removes them); executing them
requires the proprietary NVIDIA runtimes and a Turing or newer GPU.

NvOFFRUC and TensorRT RIFE are also unconditional parts of the source tree —
their ABIs, loaders, tensor layout and engine builder are always compiled in —
but their interpolation calls are not yet wired into the product loop:
`rtx_fruc_bridge_interpolate` reports `RTX_FRUC_ERR_UNSUPPORTED` for real
interpolation and the shim reports `RTX_ERR_NOT_IMPLEMENTED` when RIFE is
requested. See [§7](#7-fruc-paths), which documents the contract that the
integration must satisfy. The verification harness still exercises the
duplicate bypass and the RIFE engine builder.

### 1.2 Single-EXE absolute self-containment (first principle)

**【单 EXE 绝对自包含第一原则】** CineRTX 主程序（单一 `CineRTX.exe`：无参数时
是 Win32 DirectComposition GUI，带参数时附着父控制台运行完整 CLI）必须构建为
100% 自包含的原生 Win32 单体可执行文件（体积上限 12.0 MB）。媒体容器解封装、
交织复用、码流过滤与音频编解码均为不可分割的第一公民核心组件，必须通过手术刀级
裁剪的 FFmpeg 静态库（`.lib` / `.a`）全量内联进主可执行文件内部。代码库中永久
严禁出现任何将媒体容器层降级为可选特性的条件编译宏（如 `RTX_WITH_FFMPEG` /
`RTX_HAVE_FFMPEG`）或向产物目录拷贝外部媒体 DLL 的构建逻辑。`rtx_shim` 与
`rtx_fruc_bridge` 为静态库并由 `rtx_core` 直接链接，产物目录中不得出现任何
sidecar DLL；`rtx_ngx_bridge` 因 Plugin-Exception 必须保持独立加载的 DLL：
规范落点为 `build/external/plugins/nvidia_rtx/`，构建时镜像到 `CineRTX.exe`
同级的 `plugins/nvidia_rtx/`，安装布局相同；它不并入 GPLv3 可执行文件
（非 Windows 宿主内联 SDK-free 静态桥接，不产生 DLL）。

Consequences that are enforced mechanically, not by convention:

1. `cmake/RtxFfmpegStatic.cmake` resolves the surgical static archives at
   configure time and binds them to `rtx_core` through `rtx::ffmpeg_static`.
   Every executable and test inherits the container layer by dependency
   propagation; there is no per-target opt-in.
2. A missing archive, or an `avformat.lib`/`avcodec.lib` smaller than 450 KB
   (i.e. a DLL import stub), is a `FATAL_ERROR`. A binary without container I/O
   is never produced, so the engine can never degrade to a runtime error code.
3. `tools/audit_imports.ps1` fails the build if any PE file imports
   `avcodec*`/`avformat*`/`avutil*`/`swresample*`/`swscale*`, if the output
   directory contains an `av*.dll`/`sw*.dll`, if any module other than
   `CineRTX.exe` / `rtx_tests.exe` is present in the output directory
   (`Test-OutputClosure`), or if `CineRTX.exe` leaves the
   `1.2 MB .. 12.0 MB` envelope.
4. `src/io/ffmpeg_io.cpp` contains no conditional compilation. The zero-disk
   unit tests exercise the real static code through
   `rtx::io::MemoryAvioContext` (`avio_alloc_context`), so "the tests pass but
   the shipped binary cannot demux" is structurally impossible.

## 2. Stage order

| Stage | Input | Output | Reference implementation | NVIDIA implementation |
| --- | --- | --- | --- | --- |
| 0. Demux | Container / raw | NV12 frames | Raw reader or libavformat | libavformat + `*_mp4toannexb` bitstream filter |
| 1. Decode | Compressed packets | NV12/P010 | Raw NV12 reader | NVDEC + `cuvidMapVideoFrame64` |
| 2. Scene detect | Consecutive luma planes | Cut flag | Subsampled SAD | CUDA SAD reduction kernel |
| 3. FRUC (reference only) | Two 1080p frames | 1080p interpolated frames | Linear blend + duplicate on cut | NvOFFRUC / RIFE bridge contracts; execution not yet wired into the product loop |
| 4. Enhance | 1080p SDR | 4K HDR10 RGBA16F | BT.709 → BT.2020 + PQ + upscale | NGX VSR + TrueHDR on D3D12 |
| 5. Pack | RGBA16F | P010 | BT.2020 NCL matrix + 10-bit quantisation | Fused CUDA kernel writing `CUsurfObject` |
| 6. Encode | P010 | Packet | Deterministic packet metadata | NVENC `CUDADEVICEPTR` input |
| 7. Mux | Packets + side data | MP4/MKV | P010 writer + JSON sidecar | libavformat + container side data |

The shipped product executes stages 0 → 1 → 4 → 5 → 6 → 7. Stage 3 remains in
the CPU verification pipeline until the vendor interpolation backend is
implemented; the product loop does not synthesise frames today. The stage order
is still the target contract: interpolating at 1080p keeps RIFE activations
inside the 8 GB budget, and TrueHDR runs after VSR so that the inverse tone
mapping sees reconstructed detail rather than upscaled compression artefacts.

## 3. Frame and timestamp model

`Frame` carries host planes and an optional device view. On the NVIDIA path
`storage` is empty and `device_planes[]` holds `CUdeviceptr` values that are
never mapped into host virtual memory.

`Rational` timestamps are mapped with a 128-bit intermediate when the compiler
provides `__int128`, with a `long double` fallback otherwise. The mapper emits
every output interval for every input frame:

- For each input frame at index `i`, the output interval is
  `[rescale(i), rescale(i+1))`.
- The number of output frames is the size of that interval, computed with
  rational arithmetic, not floating point.
- The final input frame has no successor, so its interval is filled by
  duplicating it. This is what makes 24 → 60 produce exactly 60 frames for
  every 24 input frames over a long clip.

Scene cuts set `bypass_interpolation`; the interval is filled with duplicates
rather than blended frames. This avoids the classic cross-cut ghost frame.

## 4. CPU reference backend

`CpuReferenceBackend` is a real implementation, not a mock:

- NV12 → RGB with BT.709 limited-range expansion.
- Linear-light BT.709 → BT.2020 primaries conversion.
- BT.2020 NCL luma/chroma encoding.
- PQ (SMPTE ST 2084) transfer with a 1000-nit target.
- Bilinear spatial scaling to the target geometry.
- 10-bit quantisation and P010 packing with correct high-bit alignment.

It exists so that colour, HDR metadata, timestamp, scene-cut, and scheduler
behaviour can be verified deterministically on any machine. It is explicitly
not a throughput baseline.

The CPU path allocates host frames with 256-byte aligned planes. `FrameBuffer`
keeps a logical `size_` separate from its aligned `offset_`, so `size()`
reports the usable byte count, not the padded allocation.

## 5. VRAM plan

`defaultPoolSpecs()` encodes the static budget:

| Pool | Geometry | Format | Slots | Bytes |
| --- | --- | --- | ---: | ---: |
| `Pool_Decode_1080p` | 1920×1080 | NV12 | 16 | 49,766,400 |
| `Pool_Interp_1080p` | 1920×1088 | NV12 | 8 | 25,067,520 |
| `PingPong_NGX_In` | 1920×1080 | R10G10B10A2 | 2 | 16,588,800 |
| `PingPong_NGX_Out` | 3840×2160 | RGBA16F | 2 | 132,710,400 |
| `Pool_Encode_4K_P010` | 3840×2160 | P010 | 12 | 298,598,400 |
| **Total** | | | | **522,731,520 (~498.5 MiB)** |

`validateBudget()` requires the plan plus a 384 MiB reserve (driver, NGX
scratch buffers, D3D12 heap) to fit in the driver-reported free memory. A plan
that does not fit is rejected before any allocation. Managed memory
(`cudaMallocManaged`) is never used: silent oversubscription into shared host
memory would destroy the throughput guarantee.

`SurfacePool` is allocation-free after `initialize()`. Acquisition pops a slot
index from an SPSC bounded queue; release pushes it back. A lease is
move-only and returns the slot on destruction, including during error unwinding.
`capacity()` returns zero after `shutdown()` or a failed initialization.

## 6. Backpressure and scheduling

`BackpressureController` uses hysteresis:

- high watermark 75% → pause decode;
- low watermark 25% → resume;
- critical watermark 90% → observable critical state.

Pausing at the high mark and resuming only at the low mark prevents the queue
from oscillating every frame. The controller records pause, resume, and
critical counts for the report.

`PipelineScheduler` runs an ordered list of `StageBinding`s with at most
`max_in_flight` frames. It supports per-frame command-list flush and a TDR
guard with a configurable frame budget. The hot loop never calls a device-wide
synchronise; draining is explicit and bounded.

Per-stage timing has one rule: a stage that reports its own duration in
`FrameContext::stage_ms` wins, and only a stage that reports nothing is measured
by the scheduler. A GPU stage reports a timestamp-query delta, which is the real
kernel cost, while the wall clock around the call would only capture launch
overhead. The measured fallback keeps sub-millisecond resolution instead of
truncating to whole microseconds, so a stage that finishes inside one clock tick
is not reported as `0 ms`.

`TraceRecorder` writes Chrome tracing JSON (`"ph":"X"`) for every stage, so a
run can be inspected in `chrome://tracing` without Nsight.

## 7. FRUC paths

Both paths are unconditional in the source tree and are exercised by the test
suite at the ABI/builder level. Neither is dispatched from the product remaster
loop yet: `rtx_fruc_bridge_interpolate` returns `RTX_FRUC_ERR_UNSUPPORTED`
after the vendor check, and `nv_backend_shim` returns `RTX_ERR_NOT_IMPLEMENTED`
for `enable_rife != 0`. The contracts below describe what the completed
integration must preserve.

### Path A: NvOFFRUC

NvOFFRUC consumes NV12 `CUdeviceptr` surfaces and maintains the previous frame
internally. The state machine forbids calling `NvOFFRUCProcess` twice with the
same input frame. The shim exposes the function table; execution requires the
Optical Flow SDK and an NVOFA-capable GPU. The bridge currently resolves
`NvOFFRUCCreate`/`NvOFFRUCProcess` but has no argument marshalling, so it
reports `RTX_FRUC_ERR_UNSUPPORTED` instead of guessing at the SDK structs.

`NvOFFRUC.dll` is opened by explicit path — `RTX_NVIDIA_DIR` first, then the
executable directory, then the bare name — because it is an SDK redistributable
with no System32 fallback. It imports `cudart64_110.dll`, so both are staged
beside `CineRTX.exe` by the build (see README, *Runtime discovery*).

### Path B: TensorRT RIFE

`RifeConfig` locks TensorRT to `10.13.3.9`. Versions in the 10.14/10.15 line
are rejected because the dynamic-shape RIFE graph regresses from roughly 2.7 GB
to roughly 10 GB of activation memory. The tensor is 1×7×1088×1920 FP16:

- channels 0–2: previous RGB frame;
- channels 3–5: next RGB frame;
- channel 6: timestep scalar.

Height is padded from 1080 to 1088 (multiple of 32). The output is clamped to
[0, 1] before conversion back to NV12, because residual FP16 overflow reaching
TrueHDR produces black patches and blown highlights. `buildTrtexecCommand()`
emits the documented flags: `--fp16 --noTF32 --precisionConstraints=obey`,
FP16 CHW I/O, and FP32 overrides for the sensitive cast/reciprocal/arithmetic
layers.

The SDK is provisioned at configure time from a SHA-256 pinned archive
(`cmake/RtxNvidiaSdks.cmake` → `build/external/tensorrt`) and its runtime is
staged beside `CineRTX.exe`. Nothing is linked: the Zone C bridge (`src/shim/ngx_bridge/rife_builder.cpp` inside `rtx_ngx_bridge.dll`) resolves
`createInferBuilder_INTERNAL`, `createNvOnnxParser_INTERNAL` and
`getInferLibVersion` with `GetProcAddress`, so the PE import table stays free of
NVIDIA DLLs and a host without the runtime falls back to the `trtexec` command
instead of failing to start. The four-part `10.13.3.9` gate judges the SDK these
sources were compiled against; a loaded library must additionally report the
same `NV_TENSORRT_VERSION`.

The engine builder and tensor packing kernels are implemented. A TensorRT
inference runtime (deserialize the engine, bind the FP16 tensors, enqueue on
the caller's stream, and synchronise with the decode/encode timeline) is the
missing piece before this path can run.

## 8. HDR10 metadata

`SeiSynthesizer` produces one `Hdr10Metadata` object and derives every sink
from it:

- `NvencHevcHdrSettings`: `pixelBitDepthMinus8 = 2`,
  `outputMasteringDisplay = 1`, `outputMaxCll = 1`, and VUI fields
  (primaries 9, transfer 16, matrix 9, top-left chroma location).
- `masteringDisplayPayload()`: SEI payload type 137 body.
- `contentLightPayload()`: SEI payload type 144 body.
- `prefixSeiNal()`: Annex-B NAL with both payloads and emulation prevention.
- `av1MetadataObu()`: `mdcv` and `clli` OBU payload.
- `containerSideData()` and `toJson()`: endian-independent QA forms.

The mastering-display maximum is stored in 0.0001 cd/m² units, so 1000 nits is
`10,000,000`. `TrueHdrParameters::validate()` enforces the 100-based ranges and
the relationship between the tone-mapping target and the metadata peak.

## 9. Zero-copy invariants (NVIDIA path)

The following are architectural requirements, enforced by review and audited
in the import table:

1. Decoded surfaces are copied device-to-device into the decode pool and the
   NVDEC surface is unmapped immediately, so the decoder's DPB is never
   blocked.
2. When FRUC is wired in, it must consume and produce device pointers with no
   host staging buffer; the current product loop has no FRUC stage.
3. NGX input/output textures are D3D12 committed resources exported as shared
   handles and imported with `CU_EXTERNAL_MEMORY_DEDICATED`. CUDA accesses
   them through `CUsurfObject`, which handles the D3D12 tiling layout.
4. Synchronisation uses a shared 64-bit fence imported as a CUDA external
   semaphore; no `cudaDeviceSynchronize()` in the loop.
5. NVENC registers the P010 `CUdeviceptr` directly
   (`NV_ENC_INPUT_RESOURCE_TYPE_CUDADEVICEPTR`).
6. Only compressed packets and metadata cross the bus.

### 9.1 Stage-1 implementation (NVDEC + NVENC)

The stage-1 path is implemented in `src/shim/loader/nv_decoder.cpp` and
`src/shim/loader/nv_encoder.cpp`; `src/core/hw_transcode.cpp` drives the loop.

Decode:

1. `rtx_shim_decoder_create` resolves `cuvidCreateVideoParser`,
   `cuvidParseVideoData`, `cuvidCreateDecoder`, `cuvidDecodePicture`,
   `cuvidMapVideoFrame64`, and `cuvidUnmapVideoFrame64` from `nvcuvid.dll`
   (never linked at build time).
2. `cuvidGetDecoderCaps` is queried first. A codec the GPU cannot decode fails
   with `RTX_ERR_ARCH_UNSUPPORTED` at create time instead of inside the first
   sequence callback.
3. The parser's sequence callback allocates a pool of `cuMemAllocPitch`
   surfaces at the **display** geometry (1920x1080, not the 1920x1088 coded
   size) and creates the decoder with `ulTargetWidth/Height` set to that
   geometry, so the container never declares macroblock padding.
4. Each `cuvidParseVideoData` call is one Annex-B access unit. The display
   callback maps the surface, issues two `cuMemcpy2DAsync` copies (luma and
   chroma) into a free pool slot, synchronises, and unmaps immediately. The
   decoder's DPB is never held while the encoder runs.
5. `rtx_shim_decoder_receive` pops ready frames; the caller returns each slot
   with `rtx_shim_decoder_release_frame` after NVENC consumed it.

Encode:

1. `NvEncodeAPICreateInstance` yields the function table; the session is opened
   with `nvEncOpenEncodeSessionEx` on the CUDA primary context.
2. `nvEncRegisterResource` registers the pool slot as
   `NV_ENC_INPUT_RESOURCE_TYPE_CUDADEVICEPTR` with
   `NV_ENC_BUFFER_FORMAT_NV12` or `NV_ENC_BUFFER_FORMAT_YUV420_10BIT`. The
   registered resource is cached per device pointer.
3. Per frame: `nvEncMapInputResource` → `nvEncEncodePicture` →
   `nvEncUnmapInputResource` → `nvEncLockBitstream`. Only the compressed
   bitstream is read back.
4. HEVC sessions set `pixelBitDepthMinus8` semantics through
   `inputBitDepth`/`outputBitDepth`, the VUI colour description, and the
   mastering-display / MaxCLL SEI pointers.

Accounting: `RtxHwStats` counts device-to-device bytes, host-to-device pixel
bytes, device-to-host pixel bytes, and host pixel copies separately. The
container path asserts `zero_copy_main_loop` only when both host counters are
zero. The raw artefact path uses `rtx_shim_encoder_encode_host` and
`rtx_shim_decoder_download` deliberately, and reports its traffic instead of
claiming zero-copy.

## 10. Failure model

All fallible operations return `Result<T>` with a stable `ErrorCode` and a
human-readable message plus optional detail. The NVIDIA path adds specific
codes for a missing DLL, an old driver, an unsupported architecture, a lost
device, OOM, NGX failure, and a TensorRT version mismatch. The CLI maps these
to the exit codes documented in the README and never aborts with an uncaught
exception.

`ResolutionGuard` detects mid-stream geometry changes. NGX contexts are
size-specific, so a change either resets the bridge (up to `max_resets`) or
fails with a diagnostic; it never continues with a stale context.
