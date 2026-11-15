# Gate 3 — release acceptance gates (v0.2.0 record, kept normative)

**Document id**: `SPEC-PHASE3-GATE3-20261026`
**Status**: the static self-containment gate remains blocking for every
release candidate; the throughput/quality/interaction gates below are the
current repeatable equivalents of the historical phase-3 gates.

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
pwsh -File tools/audit_imports.ps1 -BuildDir .vs/build/cmake/windows-msvc-ninja
```

* Zone A: **0** static imports of `avcodec*`, `avformat*`, `avutil*`,
  `swresample*`, `swscale*`, `avfilter*`, `avdevice*`, `postproc*`.
* Zone B/C: **0** static imports of `nvcuda`, `nvcuvid`, `nvEncodeAPI64`,
  `nvsdk_ngx_d`, `_nvngx`, `nvngx_vsr`, `nvngx_truehdr`, `nvinfer`,
  `nvonnxparser` — in `CineRTX.exe`, which embeds the `rtx_shim` static
  archive; the proprietary `rtx_ngx_bridge.dll` is a separately loaded
  Independent Module mirrored to `<exe>/plugins/nvidia_rtx` and audited
  separately.
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
`#define CONFIG_<NAME>_<KIND> 1` in the FFmpeg build tree's
`config_components.h`.

### 1.5 Pure-directory isolation

```
mkdir CineRTX_Clean_Isolated_Run
copy .vs\build\cmake\windows-msvc-ninja\CineRTX.exe CineRTX_Clean_Isolated_Run\
:: strip every FFmpeg entry from PATH, then:
CineRTX.exe --input sample_1080p.mp4 --output out_4k_hdr10.mp4
```

Pass requires a valid 4K HDR10 MP4 with no missing-DLL dialog and no PATH
dependency.

### 1.6 Test suite

```
ctest --test-dir .vs/build/cmake/windows-msvc-ninja --output-on-failure
```

All 73 unit cases must be 100 % green. `rtx_unit_tests` must not touch a video
file on disk: the container layer is exercised through
`rtx::io::MemoryAvioContext`.

## 2. Gate 3.1 — Throughput

| Metric | Threshold |
| --- | --- |
| Default zero-config 4K 60fps remaster main loop on the acceptance host | ≥ 60 FPS at target-fps 60 |
| NVOFA hardware optical-flow interpolation | driver-engine execution, device-side only |
| TensorRT RIFE steady-state (Route B) | ≥ 45 FPS |
| `p010_pack` per-frame P99 | ≤ 0.8 ms |

The default path targets `--target-fps 60` out of the box (Phase 8 golden
transcode); the transcode report JSON carries the measured main-loop FPS.

## 3. Gate 3.2 — Reconstruction quality

| Metric | Threshold |
| --- | --- |
| PSNR vs. reference sequence | ≥ +1.5 dB over the passthrough baseline |
| VMAF | ≥ +5.0 |
| Hard-cut tearing artefacts | exactly 0 |

The hard-cut count is produced by the scene arbiter: every frame flagged
`is_scene_cut` must be served by the duplicate bypass
(`cuMemcpyDtoDAsync`), never by the interpolation path. The quality harness is
`tools/verify_transcode_quality.py`.

## 4. Gate 3.3 — Workstation / viewport interference

| Metric | Threshold |
| --- | --- |
| Desktop interaction contract | `python tools/verify_desktop_interaction.py` — 29/29 PASS |
| Preview viewport present latency | flip-model present, occlusion-aware skip |
| PCIe pixel copies caused by the preview | exactly 0 (preview renders from the device-side ring) |

The Win32 preview viewport uses a `DXGI_SWAP_EFFECT_FLIP_DISCARD` swap chain on
`R10G10B10A2_UNORM` with present-bypass on occlusion; live percentages require
the on-host soak.

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

## 6. Gate 3.5 — Full-stack compliance

| Check | Command | Pass |
| --- | --- | --- |
| SPDX headers and license boundary | `pwsh tools/check_spdx.ps1` | `result : PASS`, `models : 0` |
| PE import table | `pwsh tools/audit_imports.ps1` | `result: PASS` |
| Unit tests | `ctest -R rtx_unit_tests` | 73/73 |
| AI weights committed | `check_spdx.ps1` model scan | 0 |

## 7. Failing-gate policy

1. Gate 3.0 is blocking: no release candidate ships while it is red.
2. A gate that cannot be executed on the current host (no RTX GPU, no TensorRT
   runtime) is reported as `SKIP` with the missing prerequisite, never as `PASS`.
3. Any gate that regresses requires an entry in `docs/spec/testing.md` naming
   the measurement, the host and the delta.
