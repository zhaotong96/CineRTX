# Architecture

## 1. Scope

The engine remasters 720p/1080p SDR sources into 4K HDR10 AV1 Main10 (default on Ada/Blackwell sm_89+) or HEVC Main10 (automatic fallback on pre-Ada architectures), with optional dual-path true frame interpolation. Its distinguishing constraint is that uncompressed frames must never cross the PCIe bus on the NVIDIA path. That constraint drives the stage order, the memory plan, and the plugin boundary.

The single product container path is NVDEC decode → dual-path true interpolation (NVOFA optical flow or TensorRT RIFE) → RTX VSR + TrueHDR in device memory → P010 pack → NVENC AV1/HEVC 10-bit encode, shared identically by the Win32 native workstation, the WebUI, and the CLI. The zero-copy codec foundation executes on Maxwell (sm_52) and newer. VSR and TrueHDR are native built-in hardware paths requiring Turing (sm_75) or newer plus NVIDIA driver runtimes. True frame interpolation lives inside the single plugin `rtx_ngx_bridge.dll` (Phase 8 dual-path true interpolation): Route A is the driver-native NVOFA hardware optical flow, Route B is TensorRT 10.13.3.9 + RIFE v4.25 deep-learning inference.

### 1.2 Single-EXE absolute self-containment (first principle)

**【单 EXE 绝对自包含第一原则】** CineRTX 主程序（单一 `CineRTX.exe`：无参数时启动 Win32 原生 "3+1" 四态专业工作站，带参数时附着父控制台运行完整 CLI）必须构建为 100% 自包含的原生 Win32 单体可执行文件。媒体容器解封装、交织复用、码流过滤与音频编解码均为不可分割的第一公民核心组件，必须通过手术刀级裁剪的 FFmpeg 静态库（`.lib`）全量内联进主可执行文件内部。代码库中永久严禁出现任何将媒体容器层降级为可选特性的条件编译宏（如 `RTX_WITH_FFMPEG` / `RTX_HAVE_FFMPEG`）或向产物目录拷贝外部媒体 DLL 的构建逻辑。`rtx_shim` 为静态库由 `rtx_core` 直接链接；`rtx_ngx_bridge` 因 Plugin-Exception 保持独立加载的 DLL（规范落点为 `build/external/plugins/nvidia_rtx/`，构建时镜像到 `CineRTX.exe` 同级的 `plugins/nvidia_rtx/`，安装布局相同）。PDB 调试符号严格重定向隔离至 `${CMAKE_BINARY_DIR}/symbols/` 目录，禁止增量链接 `.ilk` 污染。

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
5. **100% Strict C++26 RAII**: Every resource and engine class
   (`NvidiaTranscodeSession`, `D3D12Interop`, `NgxD3D12Bridge`, `PreviewViewport`,
   `MemoryAvioContext`) enforces zero-lifecycle-leak RAII via static factory
   `create(...)` and automatic deterministic destruction. Two-step
   `initialize()`, `shutdown()`, or `destroy()` methods are permanently banned.

### 1.3 Occam's Razor & Zero-Switch Invariant (奥卡姆剃刀与禁绝冗余开关铁律)

**【奥卡姆剃刀与禁绝冗余开关铁律】** CineRTX 坚决奉行“如无必要，勿增实体”的极简主义设计哲学。
在代码库与系统架构演进中：
1. **加任何开关之前，必须先经过至少三次独立深入反思，确认是否有不可替代的绝对必然性**。
   此类开关包括但不限于：
   - 命令行参数选项（如 `--switch`、`--feature-flag`）
   - 环境变量开关（如 `RTX_ENABLE_XXX`、`RTX_DISABLE_XXX`）
   - CMake 条件编译宏（如 `RTX_WITH_XXX`、`RTX_HAVE_XXX`）
   - 运行时内部动态配置分支或降级旁路
2. **单一确定性产品路径**：引擎核心行为必须保持确定、无歧义、无分支。产品转码路径无条件执行，不搞多重模式切换；容灾诊断与环境引导按最佳实践内建闭环，严禁通过增加开关将认知负担与决策成本转嫁给用户或部署环境。
3. **不可替代性门禁**：若且仅若经过三次深入反思后，确有不可调和的外部物理冲突（如不兼容的硬件规范要求），且无法通过确定性自适应推导完成时，方可提请架构委员会专项审议，并附带详尽的不可替代性论证。否则，一律视为违背架构哲学的冗余过度设计，坚决予以否决。

## 2. Stage order

| Stage | Input | Output | NVIDIA implementation |
| --- | --- | --- | --- |
| 0. Demux | Container | Compressed packets + audio | libavformat static + *_mp4toannexb bitstream filter |
| 1. Decode | Compressed packets | NV12/P010 | NVDEC + cuvidMapVideoFrame64 (device memory) |
| 2. Interpolate (optional) | NV12 frames | Motion-smoothed NV12 | NVOFA hardware optical flow (`rtx_ngx_bridge_interpolate_nv12`) or TensorRT RIFE (`rtx_ngx_bridge_interpolate_rife`), device-side |
| 3. Enhance | SDR frame | 4K HDR10 RGBA16F | NGX VSR + TrueHDR on D3D12 (zero-copy interop) |
| 4. Pack | RGBA16F | P010 | Fused CUDA kernel writing CUsurfObject (LUT3D + HDR10+ fused) |
| 5. Encode | P010 | Bitstream | NVENC NV_ENC_CODEC_AV1_GUID (default) or HEVC |
| 6. Mux | Packets + audio | MP4/MKV container | libavformat static + av1C/HVCC container extradata + audio copy |

The shipped product executes the stages with zero host pixel copies. TrueHDR runs after VSR so that inverse tone mapping sees reconstructed detail rather than upscaled compression artefacts. Interpolation runs before enhancement so the optical-flow or RIFE output is what VSR sees.

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

The NLE timeline uses the 705,600,000 Hz Flicks timebase; every passthrough
stream is guarded by the monotonic stream timestamp guard
(`include/rtx/timestamp_guard.h`), which carries zero accumulated drift over a
24-hour virtual stream.

Scene cuts set `bypass_interpolation`; the interval is filled with duplicates
rather than blended frames. This avoids the classic cross-cut ghost frame.

## 4. Dual-path true frame interpolation (rtx_ngx_bridge.dll)

Both interpolation routes live inside the single plugin `rtx_ngx_bridge.dll`
and share one `D3D12Interop` CUDA context:

- **Route A — NVOFA hardware optical flow**: the bridge dynamically binds the
  driver-native `nvofapi64.dll` from `%SystemRoot%\System32`
  (`src/shim/ngx_bridge/nvof_engine.h`; no static import), executes the
  hardware bidirectional optical flow, and warps frames with the PTX kernel
  `rtx_nvof_warp_nv12` loaded through `cuModuleLoadData`
  (`src/shim/ngx_bridge/p010_pack_runtime.cpp`). Entry point:
  `rtx_ngx_bridge_interpolate_nv12`.
- **Route B — TensorRT RIFE deep-learning inference**: TensorRT 10.13.3.9
  executes the RIFE v4.25 master model on NV12→FP16 NCHW device buffers.
  Entry points: `rtx_ngx_bridge_init_rife`,
  `rtx_ngx_bridge_interpolate_rife`, `rtx_ngx_bridge_build_rife_engine`
  (builds an `.engine` from an ONNX model with `--build-rife-engine`).

Both routes consume and produce raw device pointers — no host staging buffer.
Scene-cut frames bypass interpolation with a device-side duplicate copy. The
GPL core resolves these exports through the pure C ABI firewall
(`include/shim/ngx_bridge_abi.h`, ABI version `4u`) and never links an NVIDIA
library.

## 5. Device memory plan

The NVIDIA path keeps every pixel in device memory with three fixed device
allocations, created once per session:

1. The NVDEC decode pool: `cuMemAllocPitch` surfaces at the **display**
   geometry (1920×1080, not the 1920×1088 coded size), sized by the parser's
   sequence callback.
2. The bridge-owned **three-slot P010 output ring**
   (`src/shim/ngx_bridge/bridge_exports.cpp`: "P010 ring from two to three
   buffers"; slot selection follows the frame counter, call *i* writes
   `ring[i % 3]`). The core honours the ring-depth contract
   `remasters_issued − encodes_completed < 3`
   (`src/core/hw_transcode.cpp`), so NVENC overlaps the next frame's NGX
   evaluation on independent hardware engines.
3. The NGX interop textures: D3D12 committed resources exported as shared
   handles and imported with `CU_EXTERNAL_MEMORY_DEDICATED` (input
   `R10G10B10A2_UNORM`, output `RGBA16F`).

Managed memory (`cudaMallocManaged`) is never used: silent oversubscription
into shared host memory would destroy the throughput guarantee. A session that
cannot fit the plan in driver-reported free memory is rejected before any
allocation.

## 6. Three-end SSOT architecture and logging

One parameter contract and one dictionary drive every frontend:

1. **`kParamSchema` — 19 professional parameters**
   (`include/rtx/param_schema.h`, `std::array<ParamMeta, 19>`): the single
   source of truth for the CLI parser, the Win32 workstation controls, and the
   WebUI form. `CineRTX.exe --schema-json` exports it with typed defaults
   (`defaults.codec == "av1"`) and the bilingual labels as JSON.
2. **`rtx::trW` — 113-entry bilingual dictionary**
   (`include/rtx/i18n.h`, `static_assert(kStrIdCount == 113)`): `Ctrl+L`
   hot-swaps every label in place, without restarting the process.
3. **Win32 native "3+1" four-state workstation** (`src/gui/`): NVIDIA
   Signature Green `#76B900` on native Common Controls v6 with dark theme —
   `[1] Encoder` transcode matrix, `[2] Player` review playback, `[3] NLE`
   four-track Flicks timeline, and the `+1` fourth form: file-association
   double-click enters borderless fullscreen dynamic VSR quick playback with
   viewport-adaptive scale (e.g. `2.00x`, portrait `1.6875x`).
4. **WebUI tri-state workstation** (`webui/`): `python webui/server.py
   --port 8787` serves a zero-dependency JSON bridge that drives the same
   backend via `--session-stdio` (JSON over stdio) and `--schema-json`.
5. **CLI**: the same schema drives argument parsing, validation, and
   `--help`.
6. **Logging**: all engine logs stream through `OutputDebugStringW` with the
   unified `[CineRTX]` prefix (`include/rtx/logger.h`) — attach Sysinternals
   DebugView for live bilingual diagnostics; no disk log files, no
   environment switches.

## 7. HDR10 / HDR10+ metadata

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

`--hdr10plus` additionally emits SMPTE ST 2094-40 dynamic metadata per frame
(ITU-T T.35 HEVC prefix SEI, AV1 metadata OBU). A `.cube` 3D LUT
(`--lut`) is fused into the pack kernel via a hardware-trilinear FP16 `tex3D`
sample, and the warp-reduction frame statistics (max/avg nits) feed adaptive
TrueHDR back through `rtx_ngx_bridge_set_frame_stats`.

The mastering-display maximum is stored in 0.0001 cd/m² units, so 1000 nits is
`10,000,000`. `TrueHdrParameters::validate()` enforces the 100-based ranges and
the relationship between the tone-mapping target and the metadata peak.

## 8. Zero-copy invariants (NVIDIA path)

The following are architectural requirements, enforced by review and audited
in the import table:

1. Decoded surfaces are copied device-to-device into the decode pool and the
   NVDEC surface is unmapped immediately, so the decoder's DPB is never
   blocked.
2. True interpolation consumes and produces device pointers with no host
   staging buffer; scene cuts are served by a device-side duplicate copy.
3. NGX input/output textures are D3D12 committed resources exported as shared
   handles and imported with `CU_EXTERNAL_MEMORY_DEDICATED`. CUDA accesses
   them through `CUsurfObject`, which handles the D3D12 tiling layout.
4. Synchronisation uses a shared 64-bit fence imported as a CUDA external
   semaphore; no `cudaDeviceSynchronize()` in the loop.
5. NVENC registers the P010 `CUdeviceptr` directly
   (`NV_ENC_INPUT_RESOURCE_TYPE_CUDADEVICEPTR`).
6. Only compressed packets and metadata cross the bus.

### 8.1 Stage-1 implementation (NVDEC + NVENC)

The codec path is implemented in `src/shim/loader/nv_decoder.cpp` and
`src/shim/loader/nv_encoder.cpp`; `src/core/hw_transcode.cpp` drives the loop
through `NvidiaTranscodeSession`.

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
zero.

## 9. Failure model

All fallible operations return `Result<T>` with a stable `ErrorCode` and a
human-readable message plus optional detail. The NVIDIA path adds specific
codes for a missing DLL, an old driver, an unsupported architecture, a lost
device, OOM, and NGX failure; error slot 11 is reserved
(`RTX_ERR_RESERVED_11 = 11` in `include/shim/rtx_shim_abi.h`). The CLI maps
these to documented exit codes and never aborts with an uncaught exception.

NGX contexts are size-specific, so a mid-stream geometry change resets the
bridge through `rtx_ngx_bridge_reset` (bounded retries) or fails with a
diagnostic; it never continues with a stale context. Device-lost recovery
recreates the bridge, decoder, and encoder while preserving the muxer and the
LUT upload.
