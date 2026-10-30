# Gate 1 verification record

> **Historical record — stage-1 codec gate.** The product CLI no longer exposes
> a container passthrough: `--input` always runs the NVDEC → VSR + TrueHDR →
> NVENC remaster. The measurements below were taken with the earlier
> `runNvidiaContainerTranscode` harness and are kept as the zero-copy codec
> baseline. `tools/verify_gate1.ps1` now drives the product remaster for its
> container check (which also asserts `zero_copy_main_loop`); the script and
> section 4 remain the repeatable procedure.

Stage 1 exits only when the bare transcode loop moves no pixel through host
memory. This document records what was measured, on which hardware, and how to
repeat the measurement on the RTX 50-series acceptance host.

## 1. Scope

| Item | Requirement | Status |
| --- | --- | --- |
| NVDEC decode | `cuvidCreateDecoder` / `cuvidDecodePicture` / `cuvidMapVideoFrame64` resolved and executed | Implemented |
| NVENC encode | `nvEncRegisterResource` with `NV_ENC_INPUT_RESOURCE_TYPE_CUDADEVICEPTR` | Implemented |
| Container I/O | libavformat demux + Annex-B conversion + MP4 mux | Implemented |
| Zero-copy main loop | `decoder_device_to_host_bytes == 0` and `encoder_host_to_device_bytes == 0` | Measured 0 |
| Product binary | single `CineRTX.exe` with GUI/CLI; no `-base` prototype | Implemented |
| Unit suite | `rtx_tests.exe` green with no environment overrides | 113/113 at the time; now 137/137 |

## 2. Environments used for this record

### 2.1 Acceptance host (primary)

| Item | Value |
| --- | --- |
| Host | `[REDACTED_HOST]` (`192.168.***.***`) |
| GPU | NVIDIA GeForce RTX 5060 Laptop GPU (Blackwell, sm_120, 8 GiB) |
| Driver | 617.14, CUDA 13.4 |
| NVENC | `nvEncodeAPI64.dll`, version 209 |
| NVDEC | H.264 and HEVC Main/Main10 |
| OS | Windows 11 26200 |
| Toolchain | Zig 0.13 (Clang 18.1.6), CMake 3.29.6, Ninja |
| FFmpeg | 8.1 shared build, `RTX_FFMPEG_ROOT=C:/[REDACTED_DIR]/ffmpeg-n8.1-latest-win64-gpl-shared-8.1`（历史环境；`RTX_FFMPEG_ROOT` 已于阶段三废除，现由 CMake 从 `third_party/ffmpeg` 静态编译） |

### 2.2 Development host (secondary)

| Item | Value |
| --- | --- |
| GPU | NVIDIA GeForce GTX 970M (Maxwell, sm_52, 3 GiB) |
| Driver | CUDA 13.0 |
| NVENC | `nvEncodeAPI64.dll`, version 208 |
| NVDEC | H.264 supported; HEVC not supported on GM204 |
| OS | Windows 11 x64 |
| Toolchain | Zig 0.13 (Clang 18.1.6), CMake 3.29.6, Ninja |
| FFmpeg | 8.1 shared build |

Maxwell is deliberately kept as the low-water mark: if the stage-1 path runs
there it is not relying on any Blackwell-only feature. The RTX 5060 adds 10-bit
HEVC decode/encode, which exercises the P010 path.

## 3. Acceptance-host results (RTX 5060, sm_120)

### 3.1 Unit suite without environment overrides

```text
C:\rtx\repo\build\zig> rtx_tests.exe
[==========] 101 test(s) run, 0 failed (319 ms)

C:\rtx\repo\build\zig-ffmpeg> rtx_tests.exe
[==========] 101 test(s) run, 0 failed (236 ms)

C:\rtx\repo> ctest --test-dir build\zig-ffmpeg
100% tests passed, 0 tests failed out of 4
```

The previously failing `shim_probe_diagnostics` (which asserted only
`ARCH_UNSUPPORTED` / `NOT_IMPLEMENTED` / `OK`) passes, as does
`shim_stage1_backend_gate`.

### 3.2 8-bit container transcode (zero-copy)

```text
> rtx-transcode --input C:\rtx\sample.mp4 --output C:\rtx\out_hevc.mp4 ^
                --backend nvidia --codec hevc --bit-depth 8
backend: nvidia (NVIDIA GeForce RTX 5060 Laptop GPU)
path: container
input: h264 720x1280 (mp4)
output: hevc 720x1280 (8-bit, mp4)
frames: 241 decoded, 241 encoded; packets: 241 read, 241 written
bitstream bytes: 2695874
zero-copy main loop: yes
host pixel copies: decoder=0, encoder=0; D2H=0 bytes, H2D=0 bytes
device-to-device bytes: 333158400
elapsed: 453.1 ms
```

`ffprobe` on the output: `hevc`, `yuv420p`, 720x1280, 10.041667 s.

### 3.3 10-bit Main10 container transcode (zero-copy)

Source generated with `ffmpeg -c:v hevc_nvenc -pix_fmt p010le`
(`yuv420p10le`, 720x1280, 4 s):

```text
> rtx-transcode --input C:\rtx\sample10.mp4 --output C:\rtx\out10.mp4 ^
                --backend nvidia --codec hevc --bit-depth 10
backend: nvidia (NVIDIA GeForce RTX 5060 Laptop GPU)
path: container
input: hevc 720x1280 (mp4)
output: hevc 720x1280 (10-bit, mp4)
frames: 96 decoded, 96 encoded; packets: 96 read, 96 written
bitstream bytes: 1094990
zero-copy main loop: yes
host pixel copies: decoder=0, encoder=0; D2H=0 bytes, H2D=0 bytes
device-to-device bytes: 265420800
elapsed: 272.0 ms
```

`ffprobe`: `hevc`, `yuv420p10le`, 720x1280, 4.000000 s.
`265420800 = 720 * 1280 * 3 * 96`, i.e. one P010 pool copy per frame.

### 3.4 Gate-1 script

```text
> C:\rtx\gate1.cmd        (8-bit, 241 frames)
Gate 1 re-acceptance
  [PASS] unit-tests - [==========] 101 test(s) run, 0 failed (230 ms)
  [PASS] probe - NVIDIA GeForce RTX 5060 Laptop GPU; caps=nvdec
  [PASS] raw-nv12-to-p010 - exit=0, 7372800 bytes (expected 7372800)
  [PASS] container-zero-copy - frames=241/241, D2H=0, H2D=0, zero_copy=True
4/4 checks passed

> C:\rtx\gate1_10.cmd     (10-bit, 96 frames)
  [PASS] container-zero-copy - frames=96/96, D2H=0, H2D=0, zero_copy=True
4/4 checks passed
```

## 4. Development-host results (GTX 970M, sm_52)

### 4.1 8-bit container transcode (zero-copy)

```text
> rtx-transcode --input sample.mp4 --output out.mp4 --backend nvidia --codec hevc --bit-depth 8
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

The same input with `--codec h264` produces the same accounting
(241/241 frames, `zero-copy main loop: yes`, 0 host bytes, 912.9 ms).

Output validation (independent MP4 box parse):

```text
size 2721019, brand isom, sample entry hev1, timescale 1000, duration 10042 (10.042 s),
track 720x1280
```

`333158400` device-to-device bytes over 241 frames is
`720 * 1280 * 1.5 * 241 = 333,158,400`, i.e. exactly one D2D copy per decoded
frame into the pool. No host traffic is involved.

### 4.2 Raw NV12 → P010 artefact path

```text
> rtx-transcode --input-raw gate1.nv12 --width 640 --height 480 ^
                --output-raw gate1.p010 --backend nvidia --frames 8
warning: HEVC is unavailable on this GPU; the raw artefact path fell back to h264
backend: nvidia (NVIDIA GeForce GTX 970M)
path: raw-artifact
output: h264 640x480 (8-bit, raw-p010)
frames: 8 decoded, 8 encoded; packets: 0 read, 0 written
bitstream bytes: 1757
device-to-device bytes: 3686400
elapsed: 617.3 ms
```

`gate1.p010` is exactly `640 * 480 * 3 * 8 = 7,372,800` bytes. Luma occupies the
high bits (values 0–252 with the low six bits zero) and chroma is neutral 128,
which proves NVENC produced a decodable stream and NVDEC reconstructed it.

This path is explicitly **not** the zero-copy claim: it uploads the source and
downloads the verification output, and reports those bytes. The container path
above is the Gate-1 evidence.

## 5. Reproduction procedure

```powershell
# 1. Build (the container layer is statically linked, unconditionally)
cmake --preset windows-msvc
cmake --build --preset windows-msvc

# 2. Static and functional gate (raw artefact + product container remaster)
pwsh tools/verify_gate1.ps1 -BuildDir build/cmake/windows-msvc `
     -Container sample.mp4

# 3. A 10-bit source can be substituted; the product remaster always emits
#    HEVC Main10 regardless of the source depth.
pwsh tools/verify_gate1.ps1 -BuildDir build/cmake/windows-msvc `
     -Container sample10.mp4
```

`verify_gate1.ps1` asserts:

1. `rtx_tests.exe` exits 0 with `0 failed` and no `RTX_TEST_*` environment
   variables set.
2. The probe reports a device and the NVDEC capability bit.
3. The raw artefact output has exactly `width * height * 3 * frames` bytes.
4. The container report has `zero_copy_main_loop: true` with
   `decoder_device_to_host_bytes == 0` and `encoder_host_to_device_bytes == 0`.

### Nsight Systems confirmation

The counter assertion above is computed inside the process. To confirm it
independently on the wire:

```powershell
nsys profile --trace=cuda,nvtx,osrt --cuda-memory-usage=true `
     --output=gate1 --force-overwrite=true `
     CineRTX.exe --input sample.mp4 --output out.mp4
nsys stats --report cuda_gpu_mem_time_sum gate1.nsys-rep
```

Expected: the timeline contains `cuMemcpy2DAsync` device-to-device rows and the
NVENC/NVDEC kernels, and **no** `[CUDA memcpy HtoD]` or `[CUDA memcpy DtoH]`
rows for pixel buffers. The only host transfers are the compressed bitstream
read from `nvEncLockBitstream` and the demuxer's `AVPacket` payload, neither of
which is a frame.

For a stage-level view without Nsight, `--trace <file.json>` emits Chrome
tracing events from `TraceRecorder`.

## 6. Known limits of this record

- The Nsight capture was not produced on either machine. The in-process
  counters (D2H = H2D = 0 over 241 frames) are the primary evidence; the Nsight
  procedure above is the independent confirmation step and remains open.
- AV1 encode requires Ada/Blackwell and is out of scope for stage 1.
- 10-bit was verified on the RTX 5060 only. GM204 has no HEVC NVDEC engine, so
  the development host cannot exercise Main10 at all.
- The measurements are single-run, not a soak. A two-hour VRAM/TDR soak remains
  a hardware qualification step.
