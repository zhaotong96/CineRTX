# Open NVIDIA codec headers (MIT)

This directory vendors the MIT-licensed `nv-codec-headers` project maintained
by the FFmpeg community:

- Upstream: https://git.ffmpeg.org/nv-codec-headers.git
- Tag: `n13.0.19.1`
- License: MIT (the full permission notice is retained verbatim at the top of
  every header)

Files:

| File | Purpose |
| --- | --- |
| `ffnvcodec/nvEncodeAPI.h` | NVENC encode API structures and constants (MIT). |
| `ffnvcodec/dynlink_nvEncodeAPI.h` | Thin loader wrapper for `nvEncodeAPI64.dll` (created for this project, MIT). |
| `ffnvcodec/dynlink_cuda.h` | CUDA driver API types used by the loader headers (MIT). |
| `ffnvcodec/dynlink_cuviddec.h` | NVDEC decoder structures and function typedefs (MIT). |
| `ffnvcodec/dynlink_nvcuvid.h` | NVDEC parser structures and function typedefs (MIT). |
| `ffnvcodec/dynlink_loader.h` | Convenience loader combining the three above (MIT). |

These headers are **not** the proprietary NVIDIA Video Codec SDK files.  No
header from `C:\Program Files\NVIDIA GPU Computing Toolkit` (or any other
CUDA Toolkit installation) is copied into this tree.  The project resolves
all entry points at runtime with `LoadLibraryExW`/`GetProcAddress`, so no
NVIDIA import library is linked into any binary.

`n13.0.19.1` is pinned deliberately: it targets the NVIDIA Video Codec SDK
13.0 ABI, which is supported by the R580 driver branch (the development host
runs 582.66).  Newer `nv-codec-headers` drops require driver 610 or newer.
