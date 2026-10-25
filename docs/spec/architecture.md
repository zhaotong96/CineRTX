# Architecture

## 1. Scope

The engine remasters 720p/1080p SDR sources into 4K HDR10 HEVC Main10 or AV1
Main10. Its distinguishing constraint is that uncompressed frames must never
cross the PCIe bus on the NVIDIA path. That constraint drives the stage order,
the memory plan, and the plugin boundary.

This document describes the architecture as implemented. Stage 1 (NVDEC
decode, NVENC encode, and the zero-copy VRAM hand-off) is implemented and
executes on Maxwell (sm_52) and newer. The RTX-only mechanisms — VSR, TrueHDR,
NvOFFRUC, and TensorRT — are marked as **gated**: the interface and error
contracts exist, but executing them requires the proprietary SDK and a Turing
or newer GPU.

## 2. Stage order

| Stage | Input | Output | Reference implementation | NVIDIA implementation |
| --- | --- | --- | --- | --- |
| 0. Demux | Container / raw | NV12 frames | Raw reader or libavformat | libavformat + `*_mp4toannexb` bitstream filter |
| 1. Decode | Compressed packets | NV12/P010 | Raw NV12 reader | NVDEC + `cuvidMapVideoFrame64` |
| 2. Scene detect | Consecutive luma planes | Cut flag | Subsampled SAD | CUDA SAD reduction kernel |
| 3. FRUC | Two 1080p frames | 1080p interpolated frames | Linear blend + duplicate on cut | NvOFFRUC (path A, gated) or TensorRT RIFE (path B, gated) |
| 4. Enhance | 1080p SDR | 4K HDR10 RGBA16F | BT.709 → BT.2020 + PQ + upscale | NGX VSR + TrueHDR on D3D12 (gated) |
| 5. Pack | RGBA16F | P010 | BT.2020 NCL matrix + 10-bit quantisation | Fused CUDA kernel writing `CUsurfObject` |
| 6. Encode | P010 | Packet | Deterministic packet metadata | NVENC `CUDADEVICEPTR` input |
| 7. Mux | Packets + side data | MP4/MKV | P010 writer + JSON sidecar | libavformat + container side data |

The order is intentional. Interpolating at 1080p keeps RIFE activations inside
the 8 GB budget; upscaling first would quadruple the pixel count before the
most memory-hungry stage. TrueHDR runs after VSR so that the inverse tone
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

### Path A: NvOFFRUC (gated)

NvOFFRUC consumes NV12 `CUdeviceptr` surfaces and maintains the previous frame
internally. The state machine forbids calling `NvOFFRUCProcess` twice with the
same input frame. The shim exposes the function table; execution requires the
Optical Flow SDK and an NVOFA-capable GPU.

### Path B: TensorRT RIFE (gated)

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
2. FRUC consumes and produces device pointers; no host staging buffer exists.
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
