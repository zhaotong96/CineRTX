# Gate 3 — Phase 3 acceptance gates (v0.2.0)

**Document id**: `SPEC-PHASE3-GATE3-20261026`
**Status**: normative. A build that fails any gate below is not a CineRTX
v0.2.0 release candidate.

Gate 3 is split into six independently executable gates. **Gate 3.0 is a
prerequisite**: it must be 100 % green before any W7–W9 business code is
accepted, because every later gate measures throughput and quality on a binary
whose media layer is already statically self-contained.

| Gate | Subject | Automation | Pass criteria |
| --- | --- | --- | --- |
| 3.0 | Static self-containment / single EXE | `tools/audit_imports.ps1`, `git grep`, CTest | see §1 |
| 3.1 | Throughput | `CineRTX --bench` on the reference host | see §2 |
| 3.2 | Reconstruction quality | PSNR / VMAF harness | see §3 |
| 3.3 | Viewport interference | `--viewport-self-test` + 60 Hz soak | see §4 |
| 3.4 | Long-run A/V synchronisation | 2 h soak | see §5 |
| 3.5 | Full-stack compliance | `check_spdx.ps1`, `audit_imports.ps1`, CTest | see §6 |

---

## 1. Gate 3.0 — Static self-containment (blocking)

### 1.1 No conditional media layer

```
git grep -nE "RTX_HAVE_FFMPEG|FfmpegNotCompiled|FfmpegMissing|rtx_copy_ffmpeg_runtime" \
    -- src include tests tools CMakeLists.txt cmake
git grep -nE "RTX_WITH_FFMPEG|RTX_FFMPEG_DLL_DIR" \
    -- src include tests tools CMakeLists.txt cmake
```

Match count **must be strictly 0** in both invocations. The pattern is
historical: each token names a mechanism that let the engine ship without a
working container layer.

`docs/spec/architecture.md` and this document necessarily quote the tokens (they
define the rule), so the search is scoped to code and build logic rather than to
the whole tracked tree.

### 1.2 Import table

```
pwsh -File tools/audit_imports.ps1 -BuildDir build/RelWithDebInfo
```

* Zone A: **0** static imports of `avcodec*`, `avformat*`, `avutil*`,
  `swresample*`, `swscale*`, `avfilter*`, `avdevice*`, `postproc*`.
* Zone B/C: **0** static imports of `nvcuda`, `nvcuvid`, `nvEncodeAPI64`,
  `nvsdk_ngx_d`, `_nvngx`, `nvngx_vsr`, `nvngx_truehdr`, `NvOFFRUC`,
  `nvinfer`, `nvonnxparser` — in `CineRTX.exe`, which embeds the `rtx_shim`
  and `rtx_fruc_bridge` static archives; the proprietary `rtx_ngx_bridge.dll`
  is a separately loaded Independent Module mirrored to
  `<exe>/plugins/nvidia_rtx` and audited separately.
* Output: `result: PASS`.

### 1.3 Output directory

No `av*.dll` and no `sw*.dll` may exist beside the binaries. The audit fails on
the first match; a folder-shaped FFmpeg distribution is not a CineRTX artifact.

### 1.4 Single-EXE envelope

```
1.2 MB <= CineRTX.exe <= 12.0 MB
```

The `1.2 MB` floor rejects an empty shell (the historical ~338 KB stub); the
`12.0 MB` ceiling rejects an unstripped build that dragged in debug info or an
untrimmed FFmpeg.

The golden band below the ceiling is **toolchain-dependent**, because the two
supported link configurations differ by roughly a factor of two:

| Path | Link flags | Golden band | Typical |
| --- | --- | --- | --- |
| MSVC | `/O1 /Gy /OPT:REF /OPT:ICF`, COFF symbol table retained | 8.0–12.0 MB | ~8.45 MB |
| GCC / Clang / Zig | `-Os -ffunction-sections -fdata-sections -Wl,--gc-sections -Wl,--strip-all` | 3.5–12.0 MB | ~4.10 MB |

`tools/audit_imports.ps1` selects the band from the artifact itself: a PE whose
COFF `NumberOfSymbols` is `0` has been stripped and is measured against
`3.5 MB`, otherwise against `8.0 MB`. Nothing in the build system is consulted,
and non-PE images (the Linux ELF) always take the `8.0 MB` band.

A build inside the hard gate but below its band is reported as a warning because
it usually means a decoder or muxer was trimmed away by mistake. Confirm against
the FFmpeg build tree rather than by guessing: every component named in the
`--enable-*` allow-lists of `tools/build_ffmpeg_static_*.sh` must appear as
`#define CONFIG_<NAME>_<KIND> 1` in `build/external/ffmpeg-build/config_components.h`.

### 1.5 Pure-directory isolation

```
mkdir C:\CineRTX_Clean_Isolated_Run
copy build\RelWithDebInfo\CineRTX.exe C:\CineRTX_Clean_Isolated_Run\
:: strip every FFmpeg entry from PATH, then:
CineRTX.exe --input sample_1080p.mp4 --output out_4k_hdr10.mp4
```

Pass requires a valid 4K HDR10 MP4 with no missing-DLL dialog and no PATH
dependency.

### 1.6 Test suite

```
ctest --test-dir build --output-on-failure
```

113 pre-existing unit tests plus 24 phase-3 additions (**137 total**) must be
100 % green. `rtx_unit_tests` must not touch a video file on disk: the container
layer is exercised through `rtx::io::MemoryAvioContext`.

---

## 2. Gate 3.1 — Throughput

| Metric | Threshold |
| --- | --- |
| NvOFFRUC steady-state end-to-end | >= 90 FPS |
| TensorRT RIFE steady-state | >= 45 FPS |
| `p010_pack` per-frame P99 | <= 0.8 ms |
| `rtx_nv12_pair_to_rife_nchw_fp16` per launch | < 0.15 ms |

---

## 3. Gate 3.2 — Reconstruction quality

| Metric | Threshold |
| --- | --- |
| PSNR vs. reference sequence | >= +1.5 dB over the phase-2 baseline |
| VMAF | >= +5.0 |
| Hard-cut tearing artefacts | exactly 0 |

The hard-cut count is produced by the scene arbiter: every frame flagged
`is_scene_cut` must be served by the duplicate bypass
(`cuMemcpyDtoDAsync`), never by the optical-flow path.

---

## 4. Gate 3.3 — Viewport interference

| Metric | Threshold |
| --- | --- |
| Background throughput loss with the 60 Hz viewport active | <= 2.0 % |
| PCIe pixel copies caused by the viewport | exactly 0 |
| Compute-shader tone map per frame | < 0.15 ms |

`CineRTX --viewport-self-test` covers the scheduling contract headlessly
(60 Hz throttle, occlusion bypass, split-screen clamp); the percentages require
the live GPU soak.

---

## 5. Gate 3.4 — Long-run synchronisation

Two-hour continuous soak at full load:

| Metric | Threshold |
| --- | --- |
| A/V alignment error |Δ_AV| | <= 1.0 ms |
| VRAM net leak | exactly 0 |
| Flicks timeline drift over 24 h (analytic) | < 1.42 ns |

The drift bound is analytic: `test_flicks_drift_24h` asserts that the
remainder-carrying rescale introduces **zero** accumulated error, so the 1.42 ns
figure is an upper bound on the residual, not an empirical average.

---

## 6. Gate 3.5 — Full-stack compliance

| Check | Command | Pass |
| --- | --- | --- |
| SPDX headers and license boundary | `pwsh -File tools/check_spdx.ps1` | `result : PASS`, `models : 0` |
| PE import table | `pwsh -File tools/audit_imports.ps1` | `result: PASS` |
| Unit tests | `ctest -R rtx_unit_tests` | 137/137 |
| CTest gates | `ctest` | 6/6 (`rtx_unit_tests`, `cli_self_test`, `cli_simulate_json`, `cli_rife_command`, `rtx_fruc_abi_contract_test`, `cli_gui_viewport_smoke_test`) |
| AI weights committed | `check_spdx.ps1` model scan | 0 |

---

## 7. Failing-gate policy

1. Gate 3.0 is blocking: no W7–W9 work is merged while it is red.
2. A gate that cannot be executed on the current host (no RTX GPU, no TensorRT
   SDK) is reported as `SKIP` with the missing prerequisite, never as `PASS`.
3. Any gate that regresses requires an entry in `docs/spec/testing.md` naming
   the measurement, the host and the delta.
