# Gate 2 construction record (W4–W6)

Status of the phase-2 task book (`phase2_construction_specification.md`) against
the code and the hardware that were actually available. Nothing here is
projected: every "verified" row was executed on `[REDACTED_HOST]` (RTX 5060 Laptop,
sm_120, driver 617.14, CUDA 13.4) or on the Maxwell development host.

## 1. Action items (ACT-01 … ACT-07)

| Item | Priority | Status | Evidence |
| --- | --- | --- | --- |
| ACT-01 `p010_pack.cu` kernel + PTX embedding + driver JIT | P0 | **Done (load verified)** | PTX generated from the `.cu` with NVRTC, assembled by `ptxas` for sm_75 and sm_120; `rtx_ngx_pack_available()` returns 0 on the RTX 5060, i.e. `cuModuleLoadData` accepted the PTX and the kernel `rtx_rgba16f_to_p010_bt2020nc` was found |
| ACT-02 muxer mdcv/clli + unified HDR defaults | P0 | **Done, verified on hardware** | Output MP4 now contains `mdcv`, `clli`, `colr`; `ffprobe` reports `red_x=34000/50000`, `white_point=15635/16450`, `max_luminance=10000000/10000`, `min_luminance=1/10000`, `max_content=1000`, `max_average=300` (DCI-P3 D65, MaxFALL 300 from `SeiSynthesizer`) |
| ACT-03 NVENC VUI top-left chroma siting | P0 | **Done, verified on hardware** | `ffprobe` reports `chroma_location=topleft` on the transcoded output |
| ACT-04 remove `cuStreamSynchronize` from the decode callback | P1 | **Done, verified on hardware** | `CUevent` recorded after the export copy, surface unmapped immediately, consumer waits on the event; container transcode still 96/96 frames with `D2H=0, H2D=0` |
| ACT-05 script coverage | P1 | **Done** | `package.ps1` ships `rtx-transcode-base.exe`; `audit_imports.ps1` audits `rtx-transcode-base.exe` and `rtx_tests.exe`; `check_spdx.ps1` scans `third_party/` (MIT-only) and rejects empty sources |
| ACT-06 repository hygiene and docs | P2 | **Done** | stray `-` removed; `.gitignore` blocks it and `*.nsys-rep`; `docs/abi.md` says ABI 2; `docs/compliance.md` describes the vendored ffnvcodec headers and the FFmpeg build; `docs/gate1.md` uses `-Container` |
| ACT-07 Nsight trace on the RTX 5060 | P1 | **Blocked** | `nsys` is not installed on either host |

## 2. W4 — headless D3D12 ↔ CUDA interop

`src/shim/ngx_bridge/d3d12_interop.{h,cpp}` implements:

1. `D3D12CreateDevice` on the adapter whose `DXGI_ADAPTER_DESC1::AdapterLuid`
   matches the 8-byte LUID from `cuDeviceGetLuid` — no window, no swap chain, no
   implicit adapter 0.
2. Committed shared textures (`R10G10B10A2_UNORM` 1080p input,
   `R16G16B16A16_FLOAT` 4K output) with `D3D12_HEAP_FLAG_SHARED` and
   `D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS`, exported with
   `CreateSharedHandle`.
3. CUDA import with `cuImportExternalMemory` + `CUDA_EXTERNAL_MEMORY_DEDICATED`,
   `cuExternalMemoryGetMappedMipmappedArray`, `cuMipmappedArrayGetLevel` and
   `cuSurfObjectCreate` so the tiled layout is resolved by the surface unit.
4. Upload/readback helpers and a `ID3D12Fence`-based queue flush.

Measured on the RTX 5060 with `RTX_BRIDGE_TRACE=1`:

```text
[rtx_ngx] create: device ready
[rtx_ngx] create: cuda context bound
[rtx_ngx] create: input texture ready
[rtx_ngx] create: output texture ready
[rtx_ngx] selftest: upload
[rtx_ngx] selftest: d3d12->cuda copy
interop_selftest -> 13 (D3D12->CUDA copy failed: CUDA_ERROR_INVALID_CONTEXT)
```

So device creation, texture allocation, handle export, CUDA import and surface
binding all succeed; the D3D12 upload also succeeds. **The host↔array copy still
fails with `CUDA_ERROR_INVALID_CONTEXT`**, which is why the W4 gate ("CUDA write
→ D3D12 read without striping") is **not yet signed off**. The working
hypothesis is context ownership between the pack-module load and the D3D12
interop path; the next step is to isolate the copy in a standalone program that
does not load the pack module.

## 3. W5 — RTX Video SDK (VSR + TrueHDR)

**Blocked by the missing proprietary SDK.** `nvngx_vsr.dll`,
`nvngx_truehdr.dll` and `nvsdk_ngx_d.dll` are absent on both hosts, and the SDK
headers are not part of this repository. The bridge therefore reports
`RTX_ERR_NGX` from `rtx_ngx_bridge_create` / `process` and never pretends a
texture exists. `rtx_ngx_bridge_query_caps` reports `vsr_available=0`,
`truehdr_available=0` and a diagnostic naming the missing DLLs.

What is already in place for W5:

- the isolated `rtx_ngx_bridge.dll` with the v2 ABI and the shared-texture
  handles it must hand to NGX;
- independent texture/parameter plumbing points (the ABI keeps VSR and TrueHDR
  settings separate, so the Blackwell "two handles, two parameter blocks" rule
  can be implemented without an ABI change);
- the `RTX_WITH_NGX_SDK` CMake switch that expects
  `src/shim/ngx_bridge/ngx_d3d12_bridge.cpp`.

## 4. W6 — pack kernel, HDR10 dual injection, pipeline merge

| Deliverable | Status |
| --- | --- |
| `p010_pack.cu` with SPDX header, BT.2020 NCL + 2×2 chroma averaging | Done |
| PTX embedding without `cudart64_*.dll` (driver `cuModuleLoadData`) | Done, load verified on sm_120 |
| `rtx_ngx_pack_launch` entry point | Done (compiled, not yet exercised end to end because of the W4 copy issue) |
| HDR10 SEI/VUI + container dual injection | Done (SEI already existed; container boxes verified in ACT-02) |
| Pipeline merged into `PipelineScheduler` / backpressure | **Not done** — depends on W5 |

## 5. Gate 2 assessment

| Gate | Status | Reason |
| --- | --- | --- |
| 2.1 HDR10 badge on a physical display | **Not met** | needs VSR + TrueHDR output (W5 blocked); the container/bitstream metadata path itself is verified |
| 2.2 zero host traffic + Nsight | **Partially met** | stage-1 counters remain `D2H=0, H2D=0`; the `.nsys-rep` cannot be produced without Nsight; the D3D12 interop copy is still failing |
| 2.3 ≥ 60 fps at 1080p24 → 4K60 HDR10 | **Not met** | requires the NGX evaluation stage |
| 2.4 ≤ 5.8 GB peak, 2 h soak | **Not met** | same dependency |

Gate 2 is **not approved**. The phase-1 gates remain green (101/101 tests, 4/4
CTest, Gate-1 zero-copy transcode on sm_120 and sm_52).

## 6. What is needed to finish

1. Install the RTX Video SDK (headers + `nvngx_vsr.dll`, `nvngx_truehdr.dll`,
   `nvsdk_ngx_d.dll`) on the RTX 5060 host and rebuild with
   `RTX_WITH_NGX_SDK=ON`.
2. Resolve the interop copy failure (isolate without the pack module; verify
   whether `D3D12CreateDevice` invalidates the retained primary context).
3. Install Nsight Systems and capture 1000 frames (`--trace=cuda,nvtx,dx12`).
4. Run the Gate 2.1/2.3/2.4 measurements on the same host.

---

## 7. Rework outcome (RW2-01 … RW2-05, 2026-10-24)

The phase-2 audit (`rework_order_phase2.md`) rejected this record and ordered a
five-item rework. The rework is complete and re-verified; the full evidence log
is in [`rework_phase2_verification.md`](rework_phase2_verification.md).

| Rework item | Outcome |
| --- | --- |
| RW2-01 interop descriptors and `_v2` symbols | **Fixed** — `d3d12_interop.cpp` maps `R10G10B10A2` to `UNSIGNED_INT32`/1 channel and `RGB32` to `UNSIGNED_INT8`/4 channels from a single table, loads `cuMemcpy2D_v2`/`cuMemcpy2DAsync_v2` (bare-name fallback), re-binds the primary context before every copy, retries D3D12 feature levels 12_1→12_0→11_0, and synchronises the CUDA stream before the D3D12 readback |
| RW2-02 NVDEC output-surface pool | **Fixed** — `ulNumOutputSurfaces = max(slots.size(), 8)` (was 2); Gate 1 back to 4/4, no `cuvidMapVideoFrame64 -> 205` |
| RW2-03 interop selftest in CTest | **Done** — new `RTX_TEST(shim_ngx_d3d12_interop_selftest)`; suite is now 102/102, CTest 4/4 |
| RW2-04 W4 round trip on real hardware | **Verified** — `rtx_ngx_bridge_selftest(256, 256)` returns `RTX_OK` with `round trip OK: 256x256 RGBA16F, D3D12 -> CUDA -> D3D12` on the RTX 5060 acceptance host (sm_120, also at 1920x1080) and on the Maxwell development host; the round trip is now a permanent CTest case |
| RW2-05 W5/W6 honesty | **Done** — W5 stays suspended with `RTX_ERR_NGX` plus a diagnostic naming the missing DLLs; `query_caps` now reports interop availability independently of the SDK; no end-to-end upscale is claimed |

Gate 1 was re-verified on the acceptance host: 4/4 PASS for both the 8-bit and
the 10-bit Main10 container paths (241/241 frames, `D2H=0, H2D=0`), with the
unit suite at 102/102 and CTest at 4/4.

Still open and honestly blocked: the RTX Video SDK (W5, Gates 2.1/2.3/2.4),
the W6 scheduler merge (depends on W5), and the Nsight capture (ACT-07).

---

## 8. P0-1 remaster production pipeline and Gate 2.1/2.2/2.3 (2026-10-25)

The 2026-10-24 "SDK blocked" conclusion above was overturned by the lead
architect's audit (`phase2_w5_w6_and_gui_unified_construction_plan.md` §1.2):
`RTX_Video_SDK_v1.1.0` ships the static stub `nvsdk_ngx_s.lib`, which the
driver-side `_nvngx.dll` resolves at `NVSDK_NGX_D3D12_Init`. The W5 bridge was
verified on the RTX 5060 on 2026-10-24 (`rtx_w5\eval.log`: `vsr=1 truehdr=1`,
1920×1080 R10G10B10A2 → 3840×2160 RGBA16F, `RESULT: PASS`).

### 8.1 What was built (P0-1)

* **ABI v2 append** (`include/shim/ngx_bridge_abi.h`): `RtxNgxRemasterInput/
  Output/Stats`, `rtx_ngx_bridge_remaster`, `rtx_ngx_bridge_get_remaster_stats`,
  `rtx_ngx_bridge_abi_version` (returns 2). Nothing existing changed.
* **Kernels** (`p010_pack.cu`, PTX regenerated via NVRTC, compute_75):
  `rtx_yuv_to_rgb10` (NV12/P010 → R10G10B10A2, BT.709/2020/601, limited/full)
  and `rtx_rgba16f_to_p010_bt2020nc_v2` (adds TrueHDR scRGB interpretation:
  linear BT.709, 1.0 = 80 nit, BT.709→BT.2020 matrix, then the unchanged
  BT.2020-NCL PQ limited quantisation with IGN dither and top-left 3×3 chroma).
  The v1 kernel is untouched; `pack_verify.exe` still reports
  `mismatches: Y=0 UV=0` on the RTX 5060.
* **Bridge orchestration** (`bridge_exports.cpp`): per-frame
  preprocess → fence signal V → NGX evaluate → fence wait V+1 → pack v2 →
  stream sync, timed by four CUDA events; bridge-owned two-buffer P010 ring
  (`cuMemAllocPitch`), stats accumulation, `RTX_BRIDGE_TRACE` diagnostics.
* **Core** (`src/core/ngx_remaster.cpp` + `runNvidiaRemasterTranscode`):
  runtime loader with ABI gate, demux → NVDEC → remaster → NVENC → muxer,
  frame limit + progress + cancel, HDR10 metadata from `SeiSynthesizer`,
  and a producer/consumer encode pipeline (NVENC overlaps the next frame's
  NGX evaluation through the two-buffer P010 ring; ring-depth contract:
  frame *i* is issued only after frame *i−2* finished encoding).
* **CLI**: `--remaster`, `--frames` now honoured by the container path,
  `--target-width/--target-height/--vsr-quality` wired into the hardware path.
* **Bug fixed en route**: `tuningForPreset()` returned
  `NV_ENC_TUNING_INFO_UNDEFINED` for presets 3/4, which made those presets
  fail `rtx_shim_encoder_create` on driver 617.14; presets 3/4 now map to the
  latency tuning like 1/2.

### 8.2 Gate 2.1 — 1000-frame 1080p SDR → 4K HDR10 throughput — **PASS**

Command (RTX 5060, driver 617.14, 2026-10-25, `C:\Users\[REDACTED_USER]\rtx_w6`):

```bat
rtx-transcode.exe --input in_1080p_sdr_1000f.mp4 --output gate21_v2.mp4 ^
  --backend nvidia --codec hevc --bit-depth 10 --remaster ^
  --target-width 3840 --target-height 2160 --vsr-quality 1 ^
  --preset 3 --rate-control qp --qp 24 --frames 1000 --json
```

Input: 1000-frame 1920×1080 H.264 SDR built from the real 10-second IMAX
clip (`crop=720:608, scale=1920:1080, -stream_loop 4, -frames:v 1000`);
command and source construction are recorded here because the gate demands a
real-content input, and the synthetic `testsrc2` route was explicitly
disallowed for the HDR behaviour argument.

| Metric | Value | Red line |
| --- | --- | --- |
| frames decoded / encoded | 1000 / 1000 | 1000 |
| main loop (excl. init) | 7769.19 ms → **128.7 FPS** | ≥ 60 FPS |
| host pixel copies (D2H/H2D/dec/copies) | **0 / 0 / 0 / 0** (`zero_copy_main_loop: true`) | 0 |
| process VRAM peak (peak − pre-run baseline) | **1245 MiB** (6363 − 5118 MiB whole-card) | ≤ 5.8 GB (process criterion) |
| pack kernel per frame | avg **1.526 ms** (max 2.424 ms) | ≤ 1.8 ms (steady average) |
| NGX stage per frame | avg 5.49 ms | — |

Note on the VRAM criterion: the whole-card reading includes the remote
desktop baseline (handover §1.2: ~4748 MiB idle; 5118 MiB measured after
warm-up). `nvidia-smi --query-compute-apps=used_memory` returns `[N/A]` on
driver 617.14 and GPU perf counters are unavailable in the SSH session, so
the process number is reported as peak-minus-baseline of the whole-card
counter — both raw numbers are recorded above.

The first serial implementation measured 55.0 FPS on the same 1000-frame run
(VRAM-sampling noise excluded); the encode pipeline (§8.1) lifted it to
128.7 FPS by overlapping NVENC with NGX on the independent engines.

### 8.3 Gate 2.2 — zero-copy bus compliance — **PASS (metrics), nsys evidence missing**

* All four host pixel counters are zero in `gate21_v2.json` (see §8.2).
* Pack kernel timing comes from CUDA events on the pack stream; steady
  average 1.526 ms ≤ 1.8 ms, worst frame 2.424 ms recorded.
* `nsys` is not installed on the acceptance host (checked 2026-10-25:
  `where nsys` empty). Per the handover §P0-3 this gate is therefore signed
  as "metrics met, Nsight trace evidence missing" rather than a full pass.

### 8.4 Gate 2.3 — HDR10 metadata (MediaInfo) — **PASS (file level)**

`MediaInfo.exe gate21_v2.mp4` (remote `rtx_w6\gate21_v2_mediainfo.txt`):

```text
Format profile      : Main 10@L5@High
HDR format          : SMPTE ST 2086, HDR10 compatible
Color primaries     : BT.2020
Transfer            : PQ
Matrix coefficients : BT.2020 non-constant
Chroma subsampling  : 4:2:0 (Type 2 = top-left)
Mastering display   : Display P3, min 0.0001 cd/m2, max 1000 cd/m2
MaxCLL / MaxFALL    : 1000 / 300 cd/m2
```

`ffprobe` agrees (bt2020 / smpte2084 / bt2020nc + stream mdcv/cll side data).
Frame content sanity: `signalstats YAVG ≈ 452–499` on sampled frames — real
picture, not black or clipped. **The physical HDR10 badge on a display cannot
be verified from an SSH session** (no interactive desktop); per handover
§P0-4 this item requires on-site confirmation and stays open.

### 8.5 Binary evidence

| Artefact | SHA-256 |
| --- | --- |
| `rtx_ngx_bridge.dll` (build/zig-ngx, SDK ON) | `06755c7666b4f0c783062cd192698d4b01b149b4f9af2f115d0857a4cab38267` |
| `rtx-transcode.exe` (build/gcc-gui) | `fb60efee38620b2876bb6c15c59ae63b36bd7ba2b3e62129599ff74b1a613f2a` |
| `rtx_shim.dll` (build/gcc-gui) | `1e4c78b495b987b2cf1697b1230d14b184a9d50cde1d6cf4d282fedf211ba409` |
| `rtx-transcode-gui.exe` (build/gcc-gui) | `f596208b317ff07e21529225c5ae78fe131c88951edc52cee749e5cc10d989f8` |

Regression after the changes: `pack_verify.exe` → `mismatches: Y=0 UV=0` +
`TLS_REBIND_MATCH`; `ngx_eval_verify.exe` → `RESULT: PASS`; local suite
**113/113**, CTest **4/4**, SPDX and import-table audits **PASS**.

---

## 9. Gate 2.4 — sustained-load soak (2026-10-25, three windows)

Full evidence in
[`phase2_w5_w6_final_acceptance_report.md`](phase2_w5_w6_final_acceptance_report.md) §5.

* Three windows (56 / 65 / 53 min): **1,085 iterations of the 1000-frame
  remaster job, 1,079 completed** — 1,079,000 sustained full-load frames.
* **0 TDR, 0 BSOD, 0 device-lost, VRAM flat** (e.g. window 3: first-20
  samples 3850 MiB avg → last-20 3835 MiB avg, peak 4096; returns to the
  ~5 GB baseline between windows).  All six failures were host-memory
  starvation at resource acquisition (NVENC session OOM, TrueHDR eval
  `0xBAD00002`, one muxer packet alloc) under 1.2–1.8 GB system-available
  memory, each a clean structured error.
* The continuous-2-hour window is blocked by the shared acceptance
  workstation's desktop applications cycling through the 16 GB of RAM every
  ~50–60 min (available memory recovers to ~8.5 GB between windows while the
  engine's per-iteration footprint is constant — outputs byte-identical).
* System event log access is denied for the SSH account; the zero-TDR claim
  rests on the circumstantial evidence above (any nvlddmkm reset would fail
  an iteration with `RTX_ERR_DEVICE_LOST`; none did across 1,079 successful
  runs).

**Verdict: GPU stability PASS (174 min cumulative full load, no TDR / no
BSOD / no VRAM growth); the uninterrupted 2-hour criterion is recorded as
environment-blocked, not engine-blocked.**
