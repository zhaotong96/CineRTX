# Plugin ABI

## 1. Purpose

The ABI is the legal and technical boundary between the GPL-licensed core and
the proprietary NVIDIA runtime. It is deliberately small, pure C, and free of
any NVIDIA SDK type. The core never includes `cuda.h`, `nvEncodeAPI.h`,
`nvinfer.h`, or `nvsdk_ngx.h`, and never links an NVIDIA import library.

Headers:

- [`include/shim/rtx_shim_abi.h`](../include/shim/rtx_shim_abi.h) — device,
  diagnostics, frame, HDR, encoder, decoder, and backend functions
  (`RTX_SHIM_ABI_VERSION` = `2u`).
- [`include/shim/ngx_bridge_abi.h`](../include/shim/ngx_bridge_abi.h) —
  D3D12/NGX bridge functions including the Phase 8 dual-path true
  interpolation exports (`RTX_NGX_BRIDGE_ABI_VERSION` = `4u`).

Both public headers are licensed `MIT OR Apache-2.0`.

## 2. Binary contract

| Property | Rule |
| --- | --- |
| Language | C99-compatible declarations, `extern "C"` |
| Calling convention | `RTX_SHIM_CALL` (`__cdecl` on Windows, default elsewhere) |
| Integer types | Fixed-width `int32_t`, `uint32_t`, `uint64_t`; no `bool` |
| Enum size | `int` (ABI-fixed by the C standard on all supported targets) |
| Struct padding | Explicit `reserved[]` arrays; zero-initialise all inputs |
| Strings | Fixed-size, NUL-terminated, UTF-8 |
| Handles | Opaque pointers owned by the shim |
| ABI version | `RTX_SHIM_ABI_VERSION` = `2u` |

Compatibility policy:

- The major ABI version only changes for layout changes or removed functions.
- New capability bits may be added without a version bump; unknown bits must
  be ignored.
- New functions may be appended. Consumers must resolve them optionally.
- `reserved[]` fields must be zero on input and are ignored on output.

## 3. Result codes

| Value | Name | Typical cause |
| ---: | --- | --- |
| 0 | `RTX_OK` | Success |
| 1 | `RTX_ERR_INVALID_ARGUMENT` | Null pointer, zero geometry, bad quality |
| 2 | `RTX_ERR_NOT_FOUND` | Requested resource absent |
| 3 | `RTX_ERR_IO` | File or stream failure |
| 4 | `RTX_ERR_UNSUPPORTED` | Feature not available or geometry not upscalable |
| 5 | `RTX_ERR_DLL_MISSING` | CUDA driver or required DLL absent |
| 6 | `RTX_ERR_ARCH_UNSUPPORTED` | GPU older than Turing (sm_75) for NGX features |
| 7 | `RTX_ERR_DRIVER_TOO_OLD` | Driver below the minimum |
| 8 | `RTX_ERR_DEVICE_LOST` | TDR or device removal |
| 9 | `RTX_ERR_OUT_OF_MEMORY` | Device plan does not fit |
| 10 | `RTX_ERR_NGX` | NGX feature/context failure |
| 11 | `RTX_ERR_RESERVED_11` | Reserved slot (`RTX_ERR_RESERVED_11 = 11`) |
| 12 | `RTX_ERR_NOT_IMPLEMENTED` | Path not compiled in this build |
| 13 | `RTX_ERR_INTERNAL` | Defensive catch-all |

`rtx_shim_result_name()` returns a stable string for logging.

## 4. Functions

### 4.1 Probe

```c
RtxResult rtx_shim_probe(RtxDiagnostics* diagnostics);
```

Fills device identity, compute capability, VRAM, driver/CUDA versions,
capability bits, and one entry per component (CUDA driver, NVENC, NVDEC, NGX
bridge, and the optional interpolation runtimes). It does not create a CUDA
context and does not allocate surfaces.

The function returns `RTX_OK` when the probe ran, even if no GPU or SDK is
present. The answer is in `diagnostics`, not only in the return code. This is
what lets `--probe` behave as a diagnostic instead of a pass/fail gate. If the
shim itself cannot be loaded, the CLI exits `4`; if the probe function returns
an error, it exits `5`.

Component notes are truncated to the fixed field size **on a UTF-8 code-point
boundary**, so localized Windows error messages never produce invalid JSON.

### 4.2 Create and destroy

```c
RtxResult rtx_shim_create_backend(const RtxBackendConfig* config,
                                  RtxBackend** backend);
void rtx_shim_destroy_backend(RtxBackend* backend);
```

Validation order:

1. `backend` and `config` must be non-null.
2. Width/height/target geometry must be positive.
3. `vsr_quality` must be 1–4.
4. The target must not be smaller than the source.
5. The CUDA driver and a device must be visible, and the device must expose an
   NVDEC engine plus NVENC.
6. Upscaling (`target != source`) and TrueHDR require compute capability ≥ 7.5
   (Turing); NVDEC/NVENC alone do not, so Maxwell GPUs create a backend for
   the codec path.
7. If a requested RTX feature has no matching capability, return
   `RTX_ERR_NGX` (missing SDK) or `RTX_ERR_ARCH_UNSUPPORTED` (old GPU).
8. TensorRT RIFE interpolation reports a structured diagnostic when the
   TensorRT runtime is absent; it never fabricates frames.

On failure, `*backend` is always set to `NULL`. On success, the caller owns the
handle and must pass it to `rtx_shim_destroy_backend()`. Destroying `NULL` is
safe.

### 4.2.1 Codec handles

`rtx_shim_decoder_*` and `rtx_shim_encoder_*` (declared in
`include/shim/rtx_shim_abi.h`) are independent opaque handles. They do not
require `rtx_shim_create_backend()`; the container transcode loop opens a
decoder and an encoder directly.

| Function | Contract |
| --- | --- |
| `rtx_shim_decoder_create` | Resolves `nvcuvid.dll`, queries `cuvidGetDecoderCaps`, and returns `RTX_ERR_ARCH_UNSUPPORTED` when the GPU has no engine for the codec/bit depth. |
| `rtx_shim_decoder_submit` | One Annex-B access unit. May synchronously fill the ready queue. |
| `rtx_shim_decoder_receive` | `RTX_ERR_NOT_FOUND` while no frame is ready, `RTX_ERR_IO` after EOS is drained. |
| `rtx_shim_decoder_release_frame` | Returns a pool slot; required before the slot is reused. |
| `rtx_shim_encoder_create` | Opens an NVENC session on the CUDA primary context. |
| `rtx_shim_encoder_encode` | Consumes an `RtxFrameDesc` whose planes are `CUdeviceptr`; no host copy. |
| `rtx_shim_encoder_get_stats` | Reports the measured PCIe traffic counters. |

Both handles are safe to destroy with `NULL`.

### 4.3 Queries

```c
RtxResult rtx_shim_get_device_info(RtxBackend*, RtxDeviceInfo*);
RtxResult rtx_shim_get_capabilities(RtxBackend*, uint32_t*);
const char* rtx_shim_last_error(RtxBackend*);
uint32_t rtx_shim_abi_version(void);
const char* rtx_shim_result_name(RtxResult);
```

`rtx_shim_last_error()` returns a pointer owned by the backend. It stays valid
until the next call on that backend or until destroy. A null handle returns a
static string, never a null pointer.

### 4.4 Threading

- `rtx_shim_probe()` is reentrant and may be called concurrently. Each call
  uses its own loader state.
- A backend handle is **not** thread-safe. External synchronisation is
  required, or one handle per worker.
- Result-name and ABI-version functions are pure and thread-safe.

## 5. Structures

### `RtxDeviceInfo`

`luid` is the 8-byte adapter LUID used to match a CUDA device to a D3D12
adapter. `capabilities` is a bitmask of `RtxCapabilityBits`. `driver_version`
uses the Windows `MAKELONG`-style encoding reported by the driver;
`cuda_version` is the CUDA driver API version (for example `13000` = 13.0).

### `RtxDiagnostics`

```c
RtxDeviceInfo device;
uint32_t component_count;
RtxComponentInfo components[16];
char summary[512];
```

`summary` is a single human-readable line suitable for logs. Components beyond
`component_count` are zeroed.

### `RtxFrameDesc`

Three device pointers and pitches, geometry, format, bit depth, PTS, and the
fence value that guards the frame. Pointers are `uint64_t` so the header does
not need a CUDA type. A null pointer means "plane not present".

### `RtxHdrMetadata`

Chromaticities are in 0.00002 units; luminances are in 0.0001 cd/m². The
values map directly onto HEVC SEI 137; `target_nits` retains the authoring
intent for TrueHDR and the sidecar.

### `RtxBackendConfig`

`vram_budget_bytes` defaults to 7 GiB in the core. `truehdr` uses the 100-based
scale: contrast 125, saturation 75, middle grey 44, peak 1000.

## 6. NGX bridge ABI (v4)

On Windows the bridge is a separate shared library (`rtx_ngx_bridge.dll`)
because only this component needs D3D12, DXGI, CUDA external memory, and NGX.
On non-Windows hosts the same C ABI is compiled into a static archive and
linked into the image — no `librtx_ngx_bridge.so` is produced. It remains
MIT/Apache-2.0.

`RTX_NGX_BRIDGE_ABI_VERSION` is `4u`
(`include/shim/ngx_bridge_abi.h`). `rtx_ngx_bridge_abi_version()` returns it;
callers must resolve it **before** using the newer exports. Version 4 covers,
under the append-only policy:

- the remaster contract: `rtx_ngx_bridge_remaster(bridge, in, out, stats)` —
  one decoded NV12/P010 device frame in, one bridge-owned 4K P010 device frame
  out (preprocess → fence signal `V` → VSR/TrueHDR → fence wait `V + 1` →
  pack → stream sync), with `rtx_ngx_bridge_get_remaster_stats` for
  cumulative counters and a **three-buffer P010 ring** (slot `i % 3`; the
  caller owns the ring-depth contract, frame *i* may only be submitted after
  frame *i−2* completed encoding);
- Route A interpolation: `rtx_ngx_bridge_get_interp_buffer` (device NV12
  scratch) and `rtx_ngx_bridge_interpolate_nv12` (NVOFA hardware optical flow
  + `rtx_nvof_warp_nv12` PTX motion compensation);
- Route B interpolation: `rtx_ngx_bridge_init_rife` (load a prebuilt
  `.engine`), `rtx_ngx_bridge_interpolate_rife` (prev/current NV12 device
  pointers + time phase `alpha` in (0, 1) → interpolated NV12; `scene_cut`
  out-flag bypasses inference on hard cuts), `rtx_ngx_bridge_build_rife_engine`
  (ONNX → TensorRT `.engine`), `rtx_ngx_bridge_destroy_rife`;
- preview plumbing: `rtx_ngx_bridge_preview_buffer`,
  `rtx_ngx_bridge_download_preview`, `rtx_ngx_bridge_upload_frame`,
  `rtx_ngx_bridge_set_frame_stats`.

Lifecycle (enhancement stage):

1. `rtx_ngx_bridge_query_caps()` — reports whether VSR/TrueHDR, a D3D12 device,
   and CUDA interop are present.
2. `rtx_ngx_bridge_create()` — creates the headless D3D12 device, committed
   shared textures, two NGX handles, and two scratch buffers.
3. `rtx_ngx_bridge_get_textures()` — returns texture descriptors and Win32
   shared handles. The caller imports them with
   `cuImportExternalMemory` using `CU_EXTERNAL_MEMORY_DEDICATED`.
4. Per frame: the CUDA side writes the input surface, signals the shared fence
   at value `N`, then calls `rtx_ngx_bridge_process(bridge, N)`. The bridge
   waits, runs VSR then TrueHDR, and signals `N + 1`.
5. `rtx_ngx_bridge_reset()` re-creates size-dependent state after a mid-stream
   resolution change.
6. `rtx_ngx_bridge_flush()` drains queued work; `destroy()` releases
   everything.

On Windows `NgxRemasterBridge` loads `rtx_ngx_bridge.dll` from
`RTX_NGX_BRIDGE_PATH`, then `<exe_dir>/plugins/nvidia_rtx/`, then the
executable directory. Every Windows build stages the bridge there (the
canonical copy lives under `build/external/plugins/nvidia_rtx`), and
`test_shim.cpp` links the SDK-free stub archive to exercise the same exported
entry points in-process. The SDK-enabled NVIDIA backend is the intended
production caller.

The stub validates arguments, reports `RTX_ERR_NOT_IMPLEMENTED` for execution,
and never pretends a texture exists. Every Windows build compiles the real
implementation against the proprietary RTX Video SDK headers — there is no
opt-in switch — and the build fails with an explanatory message if the
implementation translation unit is absent. Non-Windows hosts, which have no
NGX/D3D12, compile the SDK-free stub instead.

The only production consumer of the remaster export is
`NvidiaTranscodeSession` (`src/core/hw_transcode.cpp`); the CLI, the Win32
workstation, and the WebUI session all drive it. Interpolation is resolved by
the same loader (`include/rtx/ngx_remaster.h` binds `interpolate_nv12_` /
`interpolate_rife_` through the C ABI).

Consumers must keep the destruction order: destroy the NVENC session
**before** the bridge (the ring buffers are freed in the bridge destructor
while the CUDA context binding is still alive).

## 7. Loader security

`DynLibrary` uses:

- `LoadLibraryExW(path, NULL, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR |
  LOAD_LIBRARY_SEARCH_SYSTEM32)` for explicit paths;
- a fully qualified `%SystemRoot%\System32\<name>` path for bare names;
- `dlopen(path, RTLD_NOW | RTLD_LOCAL)` on POSIX.

It never calls `LoadLibraryW(name)` with a bare name, never searches the
current directory, and never uses `PATH` for SDK components. A failure includes
the attempted path and the OS error.

The NGX bridge (`rtx_ngx_bridge.dll`), the NVOFA engine (`nvofapi64.dll`,
System32 only), and the TensorRT runtime (`nvinfer.dll` family) follow the
same audited order independently: `RTX_NVIDIA_DIR`, then the executable
directory, then the bare name (System32 only). The TensorRT and NGX runtime
DLLs are SDK redistributables, not drivers, so they have no practical System32
presence.

## 8. Minimal consumer example

```c
#include "shim/rtx_shim_abi.h"

RtxDiagnostics diag;
RtxResult rc = rtx_shim_probe(&diag);
if (rc != RTX_OK) {
    fprintf(stderr, "probe failed: %s\n", rtx_shim_result_name(rc));
    return 1;
}

RtxBackendConfig cfg = {0};
cfg.width = 1920;
cfg.height = 1080;
cfg.target_width = 3840;
cfg.target_height = 2160;
cfg.vsr_quality = 2;
cfg.enable_truehdr = 1;
cfg.truehdr.contrast = 125;
cfg.truehdr.saturation = 75;
cfg.truehdr.middle_gray = 44;
cfg.truehdr.max_luminance = 1000;

RtxBackend* backend = NULL;
rc = rtx_shim_create_backend(&cfg, &backend);
if (rc != RTX_OK) {
    fprintf(stderr, "create failed: %s (%s)\n", rtx_shim_result_name(rc), diag.summary);
    return 1;
}

/* ... encode ... */

rtx_shim_destroy_backend(backend);
```

Compile against the header only; load the library with the platform loader or
let the CLI do it. Do not add `rtx_shim.lib` to the link line.
