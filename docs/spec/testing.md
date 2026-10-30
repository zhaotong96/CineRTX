# Testing

## 1. Strategy

The test suite has to answer three different questions:

1. Does the portable engine behave correctly? — deterministic CPU tests.
2. Does the plugin boundary fail honestly? — tests that load the real shim and
   assert structured diagnostics instead of success.
3. Does the hardware path run? — the product container remaster
   (`--input ... --output ...`) and the raw NV12 → P010 verification path
   (`--input-raw ... --backend nvidia`) are exercised manually on a machine
   with an NVIDIA driver; see section 6. The CPU reference and passthrough
   entries are verification harnesses, never product alternatives.

No test fakes a GPU. The NVIDIA execution tests assert the exact error the
current hardware must produce, so a machine that suddenly reports RTX support
would fail the suite rather than silently pass.

## 2. Running

```powershell
cmake --preset zig-windows
cmake --build --preset zig-windows
ctest --preset zig-windows --output-on-failure
```

Or directly:

```powershell
build/cmake/zig-windows/rtx_tests.exe
```

Expected:

```text
[==========] 137 test(s) run, 0 failed (612 ms)
```

The shim, NGX bridge and FRUC bridge archives are linked directly into
`rtx_tests`, so there are no DLL paths to embed and no test environment
variables to set.

CI runs this suite on Linux GCC, Windows MSVC, and Zig 0.13 for Windows. Each
job also runs `tools/check_spdx.ps1` through the shared compliance job and
`tools/audit_imports.ps1` against its own binaries. The same jobs capture
`--simulate --json` and `--probe --json` and parse both with Python, so a
regression in JSON escaping or in the UTF-8 truncation of localized notes fails
CI rather than only showing up in a manual probe. Tagged builds package the
Zig binaries with `tools/package.ps1`.

Every CI job runs CTest in flake-hunting mode:

```bash
ctest --preset zig-windows --output-on-failure --repeat until-fail:10
```

The concurrency cases are cheap, so repeating them ten times keeps a
scheduling-dependent failure inside CI instead of on a user machine. To hunt a
flaky case locally, use the same flag, or run the binary directly:

```powershell
ctest --preset zig-windows --repeat until-fail:50
build/cmake/zig-windows/rtx_tests.exe --filter spsc
```

Each CTest entry declares an explicit timeout (`300 s` for the unit suite,
`60 s` for the CLI entries), so a hung spin loop or a blocked `LoadLibraryExW`
fails with the test name in the log instead of consuming the whole job.

Always finish the build before testing. A test that runs while the linker is
still rewriting `rtx_tests` fails with an unrelated error (the observed symptom
is a single failing case in an otherwise green run), which is a build/test
ordering artefact rather than a product defect. `ninja -n` must report
`no work to do` before a run is trusted.

Probe JSON is deliberately pure ASCII: non-ASCII diagnostics are emitted as
`\uXXXX` escapes, and invalid UTF-8 bytes become `\ufffd`. A capture therefore
stays valid even when a shell decodes native stdout with a legacy code page
before writing it. The regression is covered by
`shim_probe_json_is_ascii_safe`.

## 3. Harness

`tests/test_framework.h` provides:

- `RTX_TEST(name)` — self-registering test case;
- `RTX_CHECK`, `RTX_CHECK_EQ`, `RTX_CHECK_NEAR` — non-fatal assertions;
- `RTX_REQUIRE` — fatal assertion that aborts the current case;
- a `main` that runs every registered case, reports failures with file/line,
  and returns non-zero on any failure.

The harness has no external dependency, so it builds on Windows, Linux, and
macOS with the same include path.

## 4. Coverage map

| Module | Cases | What is verified |
| --- | ---: | --- |
| `test_color.cpp` | 11 | PQ encode/decode, BT.709/BT.2020 round trips, half-float conversion, RGBA16F → P010, bilinear NV12 scaling, NV12 blending, PSNR |
| `test_hdr.cpp` | 9 | TrueHDR parameter ranges, mastering presets, VUI field values, SEI 137/144 payload layout, emulation prevention, prefix NAL, AV1 OBU, sidecar and JSON round trip |
| `test_rational.cpp` | 9 | Normalisation, parsing/formatting, arithmetic, continued-fraction conversion, rounding modes, drift compensation, 24→60 and other frame mappers, invalid configuration |
| `test_pipeline.cpp` | 7 | 24→60 frame counts, no-interpolation mode, scene-cut bypass, P010 + sidecar + trace output, HDR-disabled mode, invalid geometry, raw NV12 input |
| `test_backend.cpp` | 4 | Device info, capability reporting, lifecycle, CPU interpolation, enhance/pack/encode |
| `test_concurrency.cpp` | 5 | SPSC capacity/rounding/wraparound and a concurrent producer/consumer, monotonic fence timeline |
| `test_scheduler.cpp` | 18 | Stage order, stage-reported vs measured durations, failure propagation, invalid config, backpressure drain/errors, in-flight limit, queue-depth gate/errors, single-frame submission, identity DAG fill, VRAM watermark tiers, trace events, JSON escaping, zero-copy audit, metrics JSON |
| `test_scene.cpp` | 7 | Cut detection basics, threshold/stride, histogram helper, resolution guard, luma fraction-above stats, TrueHDR adaptive law and EMA smoothing |
| `test_vram.cpp` | 11 | Pool spec bytes, 8 GB budget fit, encode depth covering B-frames, budget validation, pool lifecycle, lease move semantics, hysteresis, TDR guard, TDR queue-depth cap, device-backed pool, 65% watermarks |
| `test_io.cpp` | 10 | Synthetic source, cut pixel changes, raw NV12 reader, P010 writer, sidecar round trip, FFmpeg availability diagnostics, zero-disk memory AVIO read/seek/write, memory demux parse, memory container round trip |
| `test_shim.cpp` | 9 | Shim C ABI binding and symbol resolution (the shim archive is linked into `rtx_tests`, not loaded as a sidecar DLL), ABI version and result names, probe diagnostics, probe JSON ASCII/UTF-8 escaping, NGX bridge ABI (the bridge archive is likewise linked in and called directly), headless D3D12↔CUDA interop round trip (`rtx_ngx_bridge_selftest`, 256×256, asserts `RTX_OK` on CUDA+D3D12 hosts and the structured `RTX_ERR_DLL_MISSING` elsewhere) |
| `test_rife.cpp` | 7 | 32-pixel alignment, TensorRT 10.13.3.9 gate, config validation, `trtexec` command, input packing, output unpacking, round trip |
| `test_status.cpp` | 5 | Success/failure construction, stable error names, `Result<T>` and `Result<void>`, `fromBool` |
| `test_types.cpp` | 7 | Format metadata, alignment/sizes, `FrameBuffer` alignment, NV12/planar/RGBA allocation, metadata clone, device residency |
| `test_flicks_timebase.cpp` | 4 | Flicks 705.6 MHz timeline: exact division, 24 h remainder-carrying rescale with zero accumulated drift, negative/zero inputs, monotonic guard |
| `test_i18n.cpp` | 2 | Strongly-typed i18n table bounds and switch latency |
| `test_offruc_bridge.cpp` | 4 | FRUC bridge C ABI called directly from the linked archive: ABI version, scene-cut duplicate bypass, static-frame bypass, config rejection |
| `test_pingpong_pipeline.cpp` | 4 | Even/odd ping-pong scheduling: slot parity, event disable timing, scene-cut duplicate, no host wait |
| `test_audio_mixer.cpp` | 4 | AVX2 audio mixer matches the scalar reference, gain and clip, SPSC ring slice, PLL convergence |

Total: **137 cases**.

## 5. CTest entries

| Test | Command | Assertion |
| --- | --- | --- |
| `rtx_unit_tests` | `rtx_tests` | All 137 cases pass |
| `cli_self_test` | `CineRTX --self-test` | 4 → 8 frames, one scene cut, non-zero packets |
| `cli_simulate_json` | 64×64 → 128×128, 4 frames, JSON | Output contains `"interpolated"` and valid accounting |
| `cli_rife_command` | `--build-rife --onnx ... --engine ...` | Emits the 10.13.3.9 version lock on both the in-process and the `trtexec` path. The fixture ONNX is a text placeholder, so the in-process build is not exercised; the gap is tracked in [`TODO/rife_cli_test_fixture.md`](../TODO/rife_cli_test_fixture.md) |
| `rtx_fruc_abi_contract_test` | `rtx_tests --filter=fruc_abi_*` | Gate 5: pure C ABI isolation and the hard-cut duplicate bypass |
| `cli_gui_viewport_smoke_test` | `CineRTX --viewport-self-test` | Gate 6: DirectComposition viewport throttle, occlusion bypass and split clamp |

Each entry sets `TIMEOUT` explicitly, so a stall is reported as a named failure
instead of running until CTest's 1500 s default expires.

`--probe` is intentionally **not** a pass/fail CTest entry. Probe success means
"the shim answered", not "this machine can transcode"; making it a gate would
fail every CI runner without an NVIDIA GPU. Use the probe output in a separate
diagnostic job, or assert `--backend nvidia` exits non-zero where appropriate.

## 6. Verified environment

| Item | Value |
| --- | --- |
| OS | Windows 10/11 x64 |
| Compiler | Zig 0.13 `zig c++` (Clang 18.1.6) |
| Build system | CMake 3.29.6, Ninja |
| GPU | NVIDIA GeForce GTX 970M, Maxwell sm_52, 3 GiB |
| CUDA driver | 13.0 |
| NVENC | present (`nvEncodeAPI64.dll`, version 208) |
| NVDEC | H.264 present; HEVC absent on GM204 (`cuvidGetDecoderCaps` = 0) |
| NvOFFRUC / TensorRT / NGX | absent |
| Result | 102/102 tests, 4/4 CTest |

The shim probe on this machine reports:

```text
NVIDIA GeForce GTX 970M sm_52, CUDA 13.0, 3071 MiB;
below Turing 7.5 baseline (RTX Video disabled)
```

`--backend nvidia` with an upscaling request therefore exits `3` with the hint
to use `--backend cpu`. That is the intended behaviour for the RTX-only stages,
not a skipped test: the stage-1 codec path is exercised separately.

### 6.1 Stage-1 hardware run (sm_52)

```text
> CineRTX --input-raw test.nv12 --width 640 --height 480 ^
          --output-raw out.p010 --backend nvidia --frames 8
warning: HEVC is unavailable on this GPU; the raw artefact path fell back to h264
backend: nvidia (NVIDIA GeForce GTX 970M)
path: raw-artifact
output: h264 640x480 (8-bit, raw-p010)
frames: 8 decoded, 8 encoded; packets: 0 read, 0 written
bitstream bytes: 1757
device-to-device bytes: 3686400
elapsed: 617.3 ms
```

`out.p010` is `640 * 480 * 3 * 8 = 7,372,800` bytes, luma spans 0–252 with the
low six bits zero (8-bit content left-aligned in 10-bit words) and chroma is
neutral 128, which confirms the NVENC → NVDEC → P010 chain end to end.

### 6.2 Historical stage-1 container record (superseded)

The product container path now always runs the VSR + TrueHDR remaster. The
record below documents the earlier NVDEC → NVENC passthrough that was reachable
from the CLI before the single-product unification; it is kept as the codec
zero-copy baseline, not as a product command.

```text
> CineRTX --input sample.mp4 --output out.mp4 --backend nvidia --codec hevc --bit-depth 8
backend: nvidia (NVIDIA GeForce GTX 970M)
path: container
input: h264 720x1280 (mp4)
output: hevc 720x1280 (8-bit, mp4)
frames: 241 decoded, 241 encoded; packets: 241 read, 241 written
bitstream bytes: 2718613
zero-copy main loop: yes
host pixel copies: decoder=0, encoder=0; D2H=0 bytes, H2D=0 bytes
device-to-device bytes: 333158400
elapsed: 1224.7 ms
```

The device-to-device total is exactly `720 * 1280 * 1.5 * 241`, i.e. one
pool copy per decoded frame. The output MP4 parses as `hev1`, 720x1280,
10.042 s. `--codec h264` gave the same accounting in 912.9 ms. Today
`runNvidiaContainerTranscode` remains compiled as a verification entry point,
but neither the GUI nor the CLI selects it.

`pwsh tools/verify_gate1.ps1` automates all four checks; see
[`gate1.md`](gate1.md) for the full record.

### 6.3 Acceptance host (RTX 5060, sm_120)

`[REDACTED_HOST]`, GeForce RTX 5060 Laptop GPU, driver 617.14 / CUDA 13.4, NVENC
v209, Windows 11 26200. Built from the same sources with Zig 0.13 and the
FFmpeg 8.1 shared package.

```text
rtx_tests.exe                 102 test(s) run, 0 failed   (both builds, no env vars)
ctest                         4/4 passed
verify_gate1.ps1 (8-bit)      4/4 passed, 241/241 frames, D2H=0, H2D=0
verify_gate1.ps1 (10-bit)     4/4 passed,  96/96  frames, D2H=0, H2D=0
```

10-bit path (`hevc` 720x1280 `yuv420p10le` source → `yuv420p10le` output):

```text
output: hevc 720x1280 (10-bit, mp4)
frames: 96 decoded, 96 encoded
zero-copy main loop: yes
host pixel copies: decoder=0, encoder=0; D2H=0 bytes, H2D=0 bytes
device-to-device bytes: 265420800   (= 720*1280*3*96)
elapsed: 272.0 ms
```

`ffprobe` confirms the output is `hevc / yuv420p10le / 720x1280 / 4.000000 s`.
This closes the two defects the first acceptance round found: the path/NGX
assertions (`shim_probe_diagnostics` now passes on Blackwell without any
`RTX_TEST_*` override) and the missing hardware execution.

`RTX_SHIM_TRACE=1` prints every NVDEC/NVENC entry point. It is the supported way
to diagnose a stall on a new GPU generation, and it is how the missing HEVC
decoder on GM204 was identified (`cuvidCreateDecoder` → `CUDA_ERROR_NOT_SUPPORTED`,
now surfaced as `RTX_ERR_ARCH_UNSUPPORTED` at create time).

## 7. What is not covered

- NvOFFRUC/RIFE frame interpolation execution. The bridge ABI, duplicate
  bypass and TensorRT engine builder are tested, but the vendor interpolation
  and inference dispatch are not implemented; the product loop does not
  synthesise frames. NGX VSR + TrueHDR execution is covered on hardware.
- Container transcode on a machine without the surgical static archives. The
  container layer is compiled into every build (Gate 3.0); what a hermetic
  machine cannot do is *link*, because `cmake/RtxFfmpegStatic.cmake` fails at
  configure time when the archives are absent. The container code itself is
  exercised without a GPU, without an FFmpeg installation and without a disk
  file through `rtx::io::MemoryAvioContext`.
- Nsight Systems traces. `TraceRecorder` emits Chrome tracing JSON, but the
  PCIe zero-copy gate needs Nsight on qualified hardware.
- Two-hour VRAM soak and TDR testing. The pool and TDR guard are unit-tested;
  the long-duration gate is a hardware qualification step.
- UHD Blu-ray/streaming conformance playback. The metadata payloads are
  byte-checked, but the HDR10 badge must be validated on physical displays.

## 8. Adding a test

1. Create or extend a `tests/test_*.cpp`.
2. Include `test_framework.h` and the header under test.
3. Write `RTX_TEST(unique_name) { ... }`.
4. Add the file to `RTX_TEST_SOURCES` in `CMakeLists.txt`.
5. Rebuild and run `ctest`.

Keep tests deterministic: no wall-clock thresholds, no network, no GPU
assumptions. For timing-sensitive code, assert invariants (ordering, counts,
monotonicity) rather than absolute milliseconds.

