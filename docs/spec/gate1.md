# Gate 1 — zero-copy codec baseline (historical record, still repeatable)

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
| Product binary | single `CineRTX.exe` with workstation/CLI dual mode; no prototype binaries | Implemented |
| Unit suite | `rtx_tests.exe` green with no environment overrides | 73/73 |

## 2. Environments used for this record

### 2.1 Acceptance host (primary)

| Item | Value |
| --- | --- |
| Host | `[REDACTED_HOST]` |
| GPU | NVIDIA GeForce RTX 5060 Laptop GPU (Blackwell, sm_120, 8 GiB) |
| Driver | 617.14, CUDA 13.4 |
| NVENC | `nvEncodeAPI64.dll`, version 209 |
| NVDEC | H.264 and HEVC Main/Main10 |
| OS | Windows 11 26200 |
| Toolchain | Zig 0.13 (Clang 18.1.6) / MSVC 19.51, CMake 3.25+, Ninja |
| FFmpeg | surgical static archives built from `third_party/ffmpeg` by the CMake configure step |

### 2.2 Development host (secondary)

| Item | Value |
| --- | --- |
| GPU | NVIDIA GeForce GTX 970M (Maxwell, sm_52, 3 GiB) |
| Driver | CUDA 13.0 |
| NVENC | `nvEncodeAPI64.dll`, version 208 |
| NVDEC | H.264 supported; HEVC not supported on GM204 |
| OS | Windows 11 x64 |

Maxwell is deliberately kept as the low-water mark: if the codec path runs
there it is not relying on any Blackwell-only feature. The RTX 5060 adds 10-bit
HEVC decode/encode, which exercises the P010 path.

## 3. Acceptance-host results (RTX 5060, sm_120)

### 3.1 Unit suite without environment overrides

```text
> rtx_tests.exe
[==========] 73 test(s) run, 0 failed
```

### 3.2 10-bit Main10 container remaster (zero-copy)

```text
> CineRTX.exe --input sample10.mp4 --output out10.mp4 --codec hevc --preset 5
backend: nvidia (NVIDIA GeForce RTX 5060 Laptop GPU)
output: hevc (10-bit, mp4)
zero-copy main loop: yes
host pixel copies: decoder=0, encoder=0; D2H=0 bytes, H2D=0 bytes
device-to-device bytes: one P010 pool copy per frame
```

`ffprobe` on the output: `hevc`, `yuv420p10le`, BT.2020 / PQ with mdcv/clli
side data. `265420800 = 720 * 1280 * 3 * 96` for the historical 96-frame run,
i.e. one P010 pool copy per frame and nothing else.

## 4. Development-host results (GTX 970M, sm_52)

The same container remaster with `--codec hevc` produces the same accounting
on the Maxwell host: all frames decoded and encoded, `zero-copy main loop:
yes`, 0 host bytes, device-to-device bytes exactly one pool copy per decoded
frame. Output validation by independent MP4 box parse: brand `isom`, sample
entry `hev1`, correct duration and track geometry.

## 5. Reproduction procedure

```powershell
# 1. Build (the container layer is statically linked, unconditionally)
cmake --preset windows-msvc-ninja
cmake --build --preset windows-msvc-ninja

# 2. Static and functional gate (unit suite + product container remaster)
pwsh tools/verify_gate1.ps1 -BuildDir .vs/build/cmake/windows-msvc-ninja `
     -Container sample.mp4
```

`verify_gate1.ps1` asserts:

1. `rtx_tests.exe` exits 0 with `0 failed` and no `RTX_TEST_*` environment
   variables set.
2. The probe reports a device and the NVDEC capability bit.
3. The container report has `zero_copy_main_loop: true` with
   `decoder_device_to_host_bytes == 0` and `encoder_host_to_device_bytes == 0`.

### Nsight Systems confirmation

The counter assertion above is computed inside the process. To confirm it
independently on the wire:

```powershell
nsys profile -t cuda,nvtx,osrt --cuda-memory-usage=true `
     --output=gate1 --force-overwrite=true `
     CineRTX.exe --input sample.mp4 --output out.mp4
nsys stats --report cuda_gpu_mem_time_sum gate1.nsys-rep
```

Expected: the timeline contains `cuMemcpy2DAsync` device-to-device rows and the
NVENC/NVDEC kernels, and **no** `[CUDA memcpy HtoD]` or `[CUDA memcpy DtoH]`
rows for pixel buffers. The only host transfers are the compressed bitstream
read from `nvEncLockBitstream` and the demuxer's `AVPacket` payload, neither of
which is a frame.

## 6. Known limits of this record

- The Nsight capture was not produced on either machine. The in-process
  counters (D2H = H2D = 0 over hundreds of frames) are the primary evidence;
  the Nsight procedure above is the independent confirmation step and remains
  open.
- AV1 encode requires Ada/Blackwell and is out of scope for the Maxwell host.
- 10-bit was verified on the RTX 5060 only. GM204 has no HEVC NVDEC engine, so
  the development host cannot exercise Main10 at all.
- The measurements are single-run, not a soak. A two-hour VRAM/TDR soak remains
  a hardware qualification step (see gate2.md §5 for the sustained-load
  verdict).
