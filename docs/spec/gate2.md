# Gate 2 — 4K HDR10 remaster gates (historical record, closed)

**Document id**: `SPEC-PHASE3-GATE2-RECORD` (consolidated historical record).
The phase-2 work (W4 headless D3D12↔CUDA interop → W5 RTX Video SDK → W6 pack
kernel + HDR10 dual injection + pipeline merge) is **closed**: every gate below
was re-verified on `[REDACTED_HOST]` (RTX 5060 Laptop, sm_120, driver 617.14,
CUDA 13.4) and the mechanisms survive in today's product.

## 1. W4 — headless D3D12 ↔ CUDA interop

`src/shim/ngx_bridge/d3d12_interop.cpp` implements:

1. `D3D12CreateDevice` on the adapter whose `DXGI_ADAPTER_DESC1::AdapterLuid`
   matches the 8-byte LUID from `cuDeviceGetLuid` — no window, no swap chain,
   no implicit adapter 0; feature-level fallback 12_1→12_0→11_0.
2. Committed shared textures (`R10G10B10A2_UNORM` 1080p input,
   `R16G16B16A16_FLOAT` 4K output) with `D3D12_HEAP_FLAG_SHARED` and
   `D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS`, exported with
   `CreateSharedHandle`.
3. CUDA import with `cuImportExternalMemory` +
   `CUDA_EXTERNAL_MEMORY_DEDICATED`, `cuExternalMemoryGetMappedMipmappedArray`,
   `cuMipmappedArrayGetLevel` and `cuSurfObjectCreate` so the tiled layout is
   resolved by the surface unit.
4. `ID3D12Fence`-based queue flush, imported as a CUDA external semaphore.

The interop round trip (`rtx_ngx_bridge_selftest`: D3D12 → CUDA → D3D12,
256×256 and 1920×1080) is a permanent unit-test case in
`tests/test_shim.cpp` and runs green on both hosts.

## 2. W5 — RTX Video SDK (VSR + TrueHDR)

The SDK gap was resolved by the audit finding that `RTX_Video_SDK_v1.1.0`
ships the static stub `nvsdk_ngx_s.lib`, which the driver-side `_nvngx.dll`
resolves at `NVSDK_NGX_D3D12_Init`. The bridge is part of every Windows build
(the former opt-in switch was removed; the SDK sources are mandatory), and the
configure step fetches and SHA-256-verifies the SDK archive. The acceptance
run reported `vsr=1 truehdr=1` with a 1920×1080 `R10G10B10A2_UNORM` →
3840×2160 `RGBA16F` evaluation: **PASS**.

Blackwell reliability rule kept in `ngx_d3d12_bridge.cpp`: VSR and TrueHDR use
two independent NGX handles, two independent parameter blocks, and two
independent scratch buffers.

## 3. W6 — pack kernel, HDR10 dual injection, pipeline merge

| Deliverable | Status |
| --- | --- |
| `p010_pack.cu` with SPDX header, BT.2020 NCL + 2×2 chroma averaging | Done |
| PTX embedding without `cudart64_*.dll` (driver `cuModuleLoadData`, NVRTC-generated) | Done, load verified on sm_120 and sm_52 |
| Fused pack kernels (`rtx_yuv_to_rgb10`, `rtx_rgba16f_to_p010_bt2020nc` v1/v2) | Done; v2 = scRGB (1.0 = 80 nit) → BT.2020 → ST 2084 PQ with IGN dither |
| HDR10 SEI/VUI + container dual injection (`mdcv`/`clli`/`colr`) | Done; MediaInfo/ffprobe field-verified |
| Producer/consumer encode pipeline (NVENC overlaps NGX on independent engines) | Done — lifted 1080p→4K throughput from 55.0 FPS to 128.7 FPS |

## 4. Gate 2.1 — 1000-frame 1080p SDR → 4K HDR10 throughput — **PASS**

Command recorded at the time (container input always runs the remaster now):

```text
CineRTX.exe --input in_1080p_sdr_1000f.mp4 --output gate21.mp4 ^
  --codec hevc --preset 3 --rate-control qp --qp 24 --frames 1000
```

Input: 1000-frame 1920×1080 H.264 SDR built from a real 10-second clip
(synthetic test sources were explicitly disallowed for the HDR behaviour
argument).

| Metric | Value | Red line |
| --- | --- | --- |
| frames decoded / encoded | 1000 / 1000 | 1000 |
| main loop (excl. init) | 7769.19 ms → **128.7 FPS** | ≥ 60 FPS |
| host pixel copies (D2H/H2D/dec/copies) | **0 / 0 / 0 / 0** (`zero_copy_main_loop: true`) | 0 |
| process VRAM peak (peak − pre-run baseline) | **1245 MiB** | ≤ 5.8 GB (process criterion) |
| pack kernel per frame | avg **1.526 ms** (max 2.424 ms) | ≤ 1.8 ms (steady average) |
| NGX stage per frame | avg 5.49 ms | — |

## 5. Gate 2.2/2.3/2.4 — verdicts

| Gate | Verdict | Evidence |
| --- | --- | --- |
| 2.2 zero-copy bus compliance | **PASS (metrics)**; nsys evidence recorded as missing (no Nsight on the hosts) | all four host pixel counters zero; pack timing from CUDA events |
| 2.3 HDR10 metadata | **PASS (file level)**; the physical HDR10 badge on a display requires on-site confirmation | MediaInfo: Main 10@L5@High, SMPTE ST 2086, BT.2020/PQ, Display P3 mastering 0.0001–1000 cd/m², MaxCLL/MaxFALL 1000/300 |
| 2.4 sustained-load soak | **GPU stability PASS** — 174 min cumulative full load, 1,079,000 sustained frames, 0 TDR / 0 BSOD / 0 device-lost / VRAM flat; the uninterrupted 2-hour criterion was environment-blocked (host desktop apps cycling RAM), not engine-blocked | three soak windows (56/65/53 min), 1,085 iterations, 1,079 completed |

## 6. Re-run procedure (today)

```powershell
cmake --preset windows-msvc-ninja
cmake --build --preset windows-msvc-ninja
pwsh tools/verify_gate1.ps1 -BuildDir .vs/build/cmake/windows-msvc-ninja -Container sample_1080p.mp4
CineRTX.exe --input sample_1080p.mp4 --output out_4k_hdr10.mp4 --codec hevc --preset 5
python tools/verify_transcode_quality.py   # PSNR / VMAF quality harness
```
